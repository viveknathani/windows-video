#include <initguid.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <combaseapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mftransform.h>
#include <wmcodecdsp.h>

// NAL unit types
#define NAL_SPS 0x07
#define NAL_PPS 0x08

DEFINE_GUID(CLSID_CMSH264DecoderMFT, 0x62CE7E72, 0x4C71, 0x4d20, 0xB1, 0x5D,
            0x45, 0x28, 0x31, 0xA8, 0x7D, 0x9D);

DEFINE_GUID(CODECAPI_AVDecVideoAcceleration_H264, 0xC8DCD850, 0x20FF, 0x4F0F,
            0x84, 0x09, 0x37, 0x32, 0xB6, 0x74, 0xB5, 0xFB);

DEFINE_GUID(CodecAPI_AVLowLatencyMode, 0x9c27891a, 0xed7a, 0x40e1, 0x88, 0xe8,
            0xb2, 0x27, 0x27, 0xa0, 0x24, 0xee);

// Utility function to check if a given byte sequence is a NAL start code
bool isNalStartCode(uint8_t *data, size_t size) {
  if (size < 4) {
    return false;
  }
  return (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 &&
          data[3] == 0x01);
}

uint8_t *convert_to_length_prefixed(uint8_t *nal_unit, size_t nal_length,
                                    size_t *out_length) {
  uint32_t nal_length32 = htonl(nal_length - 4);
  uint8_t *buffer = (uint8_t *)malloc(nal_length + 4);
  if (!buffer) {
    return NULL;
  }
  memcpy(buffer, &nal_length32, 4);
  memcpy(buffer + 4, nal_unit + 4, nal_length - 4);
  *out_length = nal_length + 4;
  return buffer;
}

void check_hr(HRESULT hr, const char *message) {
  if (FAILED(hr)) {
    printf("%s\n", message);
    printf("code 0x%lx\n", hr);
    exit(1);
  }
}

int main() {
  // Define height and width of the video
  const int width = 1920;
  const int height = 1080;

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

  while (i < fileSize) {
    // Find the next NAL start code
    if (isNalStartCode(&buffer[i], fileSize - i)) {
      // Skip the start code (0x00000001)
      // Now, 'i' points to the start of the NAL unit
      // Find the length of the NAL unit
      i += 4;
      size_t nalStart = i;
      while (i < fileSize && !(isNalStartCode(&buffer[i], fileSize - i))) {
        i++;
      }

      size_t nalLength = i - nalStart;
      printf("Found NAL unit with size: %zu bytes\n", nalLength);

      // Get the NAL unit
      uint8_t *nalUnit = &buffer[nalStart];

      // Get SPS and PPS data
      if ((nalUnit[0] & 0x1F) == NAL_SPS) {
        spsData = nalUnit;
        spsSize = nalLength;
        printf("SPS NAL unit found, size: %zu bytes\n", spsSize);
      } else if ((nalUnit[0] & 0x1F) == NAL_PPS) {
        ppsData = nalUnit;
        ppsSize = nalLength;
        printf("PPS NAL unit found, size: %zu bytes\n", ppsSize);
      }

      // If both SPS and PPS are found, set up the decoder
      if (spsData != NULL && ppsData != NULL) {
        foundSpsPps = true;
      }
    } else {
      i++; // If not a start code, just move to the next byte
    }
  }

  if (!foundSpsPps) {
    printf("Failed to find SPS and PPS\n");
    free(buffer);
    fclose(file);
    return 0;
  }

  HRESULT hr = S_OK;

  check_hr(CoInitializeEx(NULL, COINIT_MULTITHREADED),
           "failed to initialize COM");

  check_hr(MFStartup(MF_VERSION, MFSTARTUP_FULL), "failed to start MF");

  // Initialize the H.264 decoder
  IMFTransform *decoder = NULL;
  check_hr(CoCreateInstance(&CLSID_CMSH264DecoderMFT, NULL,
                            CLSCTX_INPROC_SERVER, &IID_IMFTransform,
                            (void **)&decoder),
           "failed to create decoder");

  IMFAttributes *attributes = NULL;
  check_hr(decoder->lpVtbl->GetAttributes(decoder, &attributes),
           "failed to get attributes");

  // Enable hardware acceleration using CODECAPI_AVDecVideoAcceleration_H264
  check_hr(attributes->lpVtbl->SetUINT32(
               attributes, &CODECAPI_AVDecVideoAcceleration_H264, TRUE),
           "failed to set hardware acceleration");

  check_hr(
      attributes->lpVtbl->SetUINT32(attributes, &CodecAPI_AVLowLatencyMode, 1),
      "failed to set low latency mode");

  IMFMediaType *inputMediaType = NULL;

  check_hr(MFCreateMediaType(&inputMediaType), "failed to create media type");

  check_hr(inputMediaType->lpVtbl->SetGUID(inputMediaType, &MF_MT_MAJOR_TYPE,
                                           &MFMediaType_Video),
           "failed to set major type");

  check_hr(inputMediaType->lpVtbl->SetGUID(inputMediaType, &MF_MT_SUBTYPE,
                                           &MFVideoFormat_H264),
           "failed to set subtype");

  check_hr(inputMediaType->lpVtbl->SetUINT32(inputMediaType, &MF_MT_FRAME_SIZE,
                                             1920 << 16 | 1080),
           "failed to set frame size");

  check_hr(inputMediaType->lpVtbl->SetUINT32(inputMediaType, &MF_MT_FRAME_RATE,
                                             30 << 16 | 1),
           "failed to set frame rate");
  if (FAILED(hr)) {
    printf("Failed to set frame rate\n");
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  check_hr(inputMediaType->lpVtbl->SetUINT32(
               inputMediaType, &MF_MT_PIXEL_ASPECT_RATIO, 1 << 16 | 1),
           "failed to set pixel aspect ratio");

  check_hr(inputMediaType->lpVtbl->SetUINT32(inputMediaType,
                                             &MF_MT_INTERLACE_MODE, 2),
           "failed to set interlace mode");

  check_hr(decoder->lpVtbl->SetInputType(decoder, 0, inputMediaType, 0),
           "failed to set input type");

  IMFMediaType *outputMediaType = NULL;
  int index = 0;
  while (true) {
    check_hr(decoder->lpVtbl->GetOutputAvailableType(decoder, 0, index,
                                                     &outputMediaType),
             "failed to get output available type");
    GUID subtype;
    check_hr(outputMediaType->lpVtbl->GetGUID(outputMediaType, &MF_MT_SUBTYPE,
                                              &subtype),
             "failed to get subtype");

    if (IsEqualGUID(&subtype, &MFVideoFormat_NV12)) {
      decoder->lpVtbl->SetOutputType(decoder, 0, outputMediaType, 0);
      printf("Found NV12 output type\n");
      break;
    }

    outputMediaType->lpVtbl->Release(outputMediaType);
    index++;
  }

  check_hr(
      decoder->lpVtbl->ProcessMessage(decoder, MFT_MESSAGE_COMMAND_FLUSH, 0),
      "failed to process flush command");

  check_hr(decoder->lpVtbl->ProcessMessage(
               decoder, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0),
           "failed to process begin streaming command");

  check_hr(decoder->lpVtbl->ProcessMessage(
               decoder, MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0),
           "failed to process start of stream command");

  printf("Decoder is ready!\n");

  // Read the file again but this time, decode the frames
  i = 0;
  while (i < fileSize) {
    if (isNalStartCode(&buffer[i], fileSize - i)) {

      size_t nalStart = i;
      i += 4;
      while (i < fileSize && !(isNalStartCode(&buffer[i], fileSize - i))) {
        i++;
      }
      size_t nalLength = i - nalStart;
      uint8_t *nalUnit = &buffer[nalStart];
      size_t lengthPrefixedBufferLength = 0;
      uint8_t *lengthPrefixedBuffer = convert_to_length_prefixed(
          nalUnit, nalLength, &lengthPrefixedBufferLength);

      IMFMediaBuffer *inputMediaBuffer = NULL;
      check_hr(
          MFCreateMemoryBuffer(lengthPrefixedBufferLength, &inputMediaBuffer),
          "failed to create memory buffer");

      uint8_t *bufferThatWillGoIn = NULL;
      DWORD maxLength = 0;
      DWORD currentLength = 0;
      check_hr(inputMediaBuffer->lpVtbl->Lock(inputMediaBuffer,
                                              &bufferThatWillGoIn, &maxLength,
                                              &currentLength),
               "failed to lock input buffer");

      memcpy(bufferThatWillGoIn, lengthPrefixedBuffer,
             lengthPrefixedBufferLength);

      check_hr(inputMediaBuffer->lpVtbl->Unlock(inputMediaBuffer),
               "failed to unlock");

      check_hr(inputMediaBuffer->lpVtbl->SetCurrentLength(
                   inputMediaBuffer, lengthPrefixedBufferLength),
               "failed to set current length");

      IMFSample *inputSample = NULL;
      check_hr(MFCreateSample(&inputSample), "failed to create input sample");

      check_hr(inputSample->lpVtbl->AddBuffer(inputSample, inputMediaBuffer),
               "failed to add buffer");

      printf("nal length: %zu\n", nalLength);

      check_hr(decoder->lpVtbl->ProcessInput(decoder, 0, inputSample, 0),
               "failed to process input");

      check_hr(decoder->lpVtbl->ProcessMessage(decoder,
                                               MFT_MESSAGE_COMMAND_DRAIN, 0),
               "failed to process drain command");

      // Get the decoded output
      IMFSample *outputSample = NULL;
      DWORD pdwStatus = 0;
      hr = decoder->lpVtbl->ProcessOutput(decoder, 0, 1, &outputSample,
                                          &pdwStatus);

      if (FAILED(hr)) {
        printf("code: 0x%lx\n", hr);
        continue;
      }

      IMFMediaBuffer *outputBuffer = NULL;
      check_hr(outputSample->lpVtbl->ConvertToContiguousBuffer(outputSample,
                                                               &outputBuffer),
               "failed to convert to contiguous buffer");

      BYTE *outputData = NULL;
      DWORD outputMaxLength = 0;
      DWORD outputCurrentLength = 0;
      hr = outputBuffer->lpVtbl->Lock(outputBuffer, &outputData,
                                      &outputMaxLength, &outputCurrentLength);

      printf("Decoded frame with size: %lu bytes\n", outputCurrentLength);

      outputBuffer->lpVtbl->Unlock(outputBuffer);
      outputBuffer->lpVtbl->Release(outputBuffer);
      outputSample->lpVtbl->Release(outputSample);

    } else {
      i++;
    }
  }

  CoUninitialize();
  free(buffer);
  fclose(file);
  return 0;
}