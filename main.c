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

// Utility function to check if a given byte sequence is a NAL start code
bool isNalStartCode(uint8_t *data, size_t size) {
  if (size < 4) {
    return false;
  }
  return (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 &&
          data[3] == 0x01);
}

int main() {
  // define height and width of the video
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

  HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  if (FAILED(hr)) {
    printf("Failed to initialize COM\n");
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
  if (FAILED(hr)) {
    printf("Failed to start Media Foundation\n");
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  // Initialize the H.264 decoder
  IMFTransform *decoder = NULL;
  hr = CoCreateInstance(&CLSID_CMSH264DecoderMFT, NULL, CLSCTX_INPROC_SERVER,
                        &IID_IMFTransform, (void **)&decoder);
  if (FAILED(hr)) {
    printf("Failed to create H.264 decoder\n");
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  IMFAttributes *attributes = NULL;
  hr = decoder->lpVtbl->GetAttributes(decoder, &attributes);
  if (FAILED(hr)) {
    printf("Failed to get decoder attributes\n");
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  // enable hardware acceleration using CODECAPI_AVDecVideoAcceleration_H264
  hr = attributes->lpVtbl->SetUINT32(attributes,
                                     &CODECAPI_AVDecVideoAcceleration_H264, 1);
  if (FAILED(hr)) {
    printf("Failed to set hardware acceleration\n");
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  // Set the input media type
  IMFMediaType *inputMediaType = NULL;
  hr = MFCreateMediaType(&inputMediaType);
  if (FAILED(hr)) {
    printf("Failed to create input media type\n");
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = inputMediaType->lpVtbl->SetGUID(inputMediaType, &MF_MT_MAJOR_TYPE,
                                       &MFMediaType_Video);
  if (FAILED(hr)) {
    printf("Failed to set major type\n");
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = inputMediaType->lpVtbl->SetGUID(inputMediaType, &MF_MT_SUBTYPE,
                                       &MFVideoFormat_H264);
  if (FAILED(hr)) {
    printf("Failed to set subtype\n");
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = inputMediaType->lpVtbl->SetUINT32(inputMediaType, &MF_MT_FRAME_SIZE,
                                         1920 << 16 | 1080);
  if (FAILED(hr)) {
    printf("Failed to set frame size\n");
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = decoder->lpVtbl->SetInputType(decoder, 0, inputMediaType, 0);
  if (FAILED(hr)) {
    printf("Failed to set input type\n");
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  IMFMediaType *outputMediaType = NULL;
  hr = MFCreateMediaType(&outputMediaType);
  if (FAILED(hr)) {
    printf("Failed to create output media type\n");
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = outputMediaType->lpVtbl->SetGUID(outputMediaType, &MF_MT_MAJOR_TYPE,
                                        &MFMediaType_Video);
  if (FAILED(hr)) {
    printf("Failed to set major type\n");
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = outputMediaType->lpVtbl->SetGUID(outputMediaType, &MF_MT_SUBTYPE,
                                        &MFVideoFormat_NV12);
  if (FAILED(hr)) {
    printf("Failed to set subtype\n");
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = outputMediaType->lpVtbl->SetUINT32(outputMediaType, &MF_MT_FRAME_SIZE,
                                          1920 << 16 | 1080);
  if (FAILED(hr)) {
    printf("Failed to set frame size\n");
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = decoder->lpVtbl->SetOutputType(decoder, 0, outputMediaType, 0);
  if (FAILED(hr)) {
    printf("Failed to set output type\n");
    printf("code 0x%lx\n", hr);
    outputMediaType->lpVtbl->Release(outputMediaType);
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  printf("Successfully initialized COM and started MF and created decoder\n");
  CoUninitialize();

  free(buffer);
  fclose(file);
  return 0;
}