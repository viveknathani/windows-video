#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_dxva2.h>
#include <libavutil/pixdesc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// NAL unit types
#define NAL_SPS 0x07
#define NAL_PPS 0x08

// Utility function to check if a given byte sequence is a NAL start code
bool isNalStartCode(uint8_t *data, size_t size) {
  if (size < 4) {
    return false;
  }
  return (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 &&
          data[3] == 0x01);
}

// implementation of get_hw_format callback
enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
                                 const enum AVPixelFormat *pix_fmts) {
  const enum AVPixelFormat *p;
  for (p = pix_fmts; *p != -1; p++) {
    printf("format: %s\n", av_get_pix_fmt_name(*p));
    if (*p == AV_PIX_FMT_GBRP) {
      return *p;
    }
  }
  fprintf(stderr, "Failed to get HW surface format.\n");
  return AV_PIX_FMT_NONE;
}

int main() {
  // Define height and width of the video
  const int width = 1920;
  const int height = 1080;

  av_log_set_level(AV_LOG_DEBUG);

  AVCodec *decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!decoder) {
    printf("Failed to find decoder\n");
    return 1;
  }

  AVCodecContext *decoderContext = avcodec_alloc_context3(decoder);
  if (!decoderContext) {
    printf("Failed to allocate decoder context\n");
    return 1;
  }

  AVBufferRef *hwDeviceCtx = NULL;
  if (av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_QSV, NULL, NULL,
                             0)) {
    printf("Failed to create CUDA device\n");
    return 1;
  }

  decoderContext->hw_device_ctx = av_buffer_ref(hwDeviceCtx);
  if (!decoderContext->hw_device_ctx) {
    printf("Failed to set CUDA device\n");
    return 1;
  }

  decoderContext->pix_fmt = AV_PIX_FMT_NV12;
  decoderContext->get_format = get_hw_format;
  if (!decoderContext->pix_fmt) {
    printf("Failed to set pixel format\n");
    return 1;
  }

  decoderContext->width = width;
  decoderContext->height = height;

  if (avcodec_open2(decoderContext, decoder, NULL) < 0) {
    printf("Failed to open codec\n");
    return 1;
  }

  printf("decoder pixel format: %s\n",
         av_get_pix_fmt_name(decoderContext->pix_fmt));

  // Open the file
  const char *filename = "video.h264";
  FILE *file = fopen(filename, "rb");
  if (!file) {
    printf("Failed to open file: %s\n", filename);
    return 0;
  }

  // Read the entire file into a buffer
  fseek(file, 0, SEEK_END);
  long fileSize = ftell(file);
  fseek(file, 0, SEEK_SET);
  uint8_t *buffer = (uint8_t *)malloc(fileSize);
  if (!buffer) {
    printf("Failed to allocate memory\n");
    fclose(file);
    return 0;
  }
  fread(buffer, 1, fileSize, file);

  size_t i = 0;
  uint8_t *spsData = NULL;
  uint8_t *ppsData = NULL;
  size_t spsSize = 0;
  size_t ppsSize = 0;
  bool foundSpsPps = false;
  FILE *out = fopen("output.raw", "wb");

  while (i < fileSize) {
    // Find the next NAL start code
    if (isNalStartCode(&buffer[i], fileSize - i)) {
      // Now, 'i' points to the start of the NAL unit
      // Find the length of the NAL unit
      size_t nalStart = i;
      i += 4;
      while (i < fileSize && !(isNalStartCode(&buffer[i], fileSize - i))) {
        i++;
      }

      size_t nalLength = i - nalStart;

      // Get the NAL unit
      uint8_t *nalUnit = &buffer[nalStart];

      // Get SPS and PPS data
      if ((nalUnit[4] & 0x1F) == NAL_SPS) {
        spsData = nalUnit;
        spsSize = nalLength;
        printf("SPS NAL unit found, size: %zu bytes\n", spsSize);

        // Send SPS data to decoder
        AVPacket spsPacket;
        spsPacket.data = spsData;
        spsPacket.size = spsSize;
        avcodec_send_packet(decoderContext, &spsPacket);
        continue;
      } else if ((nalUnit[4] & 0x1F) == NAL_PPS) {
        ppsData = nalUnit;
        ppsSize = nalLength;
        printf("PPS NAL unit found, size: %zu bytes\n", ppsSize);

        // Send PPS data to decoder
        AVPacket ppsPacket;
        ppsPacket.data = ppsData;
        ppsPacket.size = ppsSize;
        avcodec_send_packet(decoderContext, &ppsPacket);
        continue;
      }

      if (spsData != NULL && ppsData != NULL) {
        foundSpsPps = true;
      }

      if (!foundSpsPps) {
        printf("cannot proceed with decoding\n");
        continue;
      }

      // Send the NAL unit to the decoder
      AVPacket packet;
      packet.data = nalUnit;
      packet.size = nalLength;
      int r = avcodec_send_packet(decoderContext, &packet);
      if (r != 0) {
        continue;
      }

      printf("Sent packet with size: %zu bytes\n", nalLength);

      // Get the decoded frame
      AVFrame *frame = av_frame_alloc();
      if (!frame) {
        printf("Failed to allocate frame\n");
        return 1;
      }

      if (avcodec_receive_frame(decoderContext, frame) != 0) {
        printf("Failed to receive frame\n");
        continue;
      }

      printf("Decoded frame: %dx%d and format: %s\n", frame->width,
             frame->height, av_get_pix_fmt_name(frame->format));

      uint8_t *green = frame->data[0];
      uint8_t *red = frame->data[1];
      uint8_t *blue = frame->data[2];
      uint8_t *rgbaFrame = malloc(width * height * 4);
      for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
          rgbaFrame[i * width * 4 + j * 4] = red[i * frame->linesize[1] + j];
          rgbaFrame[i * width * 4 + j * 4 + 1] =
              green[i * frame->linesize[0] + j];
          rgbaFrame[i * width * 4 + j * 4 + 2] =
              blue[i * frame->linesize[2] + j];
          rgbaFrame[i * width * 4 + j * 4 + 3] = 255;
        }
      }

      if (file != NULL) {
        fwrite(rgbaFrame, sizeof(uint8_t), height * width * 4, out);
        fclose(file);
      }

    } else {
      i++; // If not a start code, just move to the next byte
    }
  }
  return 0;
}