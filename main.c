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

  // Enable hardware acceleration using CODECAPI_AVDecVideoAcceleration_H264
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
                                       &MFVideoFormat_H264_ES);
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

  // Set frame rate for input media type
  hr = inputMediaType->lpVtbl->SetUINT32(inputMediaType, &MF_MT_FRAME_RATE,
                                         30 << 16 | 1);
  if (FAILED(hr)) {
    printf("Failed to set frame rate\n");
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  // Set aspect ratio for input media type
  hr = inputMediaType->lpVtbl->SetUINT32(
      inputMediaType, &MF_MT_PIXEL_ASPECT_RATIO, 1 << 16 | 1);
  if (FAILED(hr)) {
    printf("Failed to set pixel aspect ratio\n");
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = inputMediaType->lpVtbl->SetUINT32(inputMediaType, &MF_MT_INTERLACE_MODE,
                                         2);
  if (FAILED(hr)) {
    printf("Failed to set interlace mode\n");
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
  int index = 0;
  while (true) {
    hr = decoder->lpVtbl->GetOutputAvailableType(decoder, 0, index,
                                                 &outputMediaType);
    if (FAILED(hr)) {
      printf("Failed to get output available type\n");
      CoUninitialize();
      free(buffer);
      fclose(file);
      return 0;
    }
    GUID subtype;
    hr = outputMediaType->lpVtbl->GetGUID(outputMediaType, &MF_MT_SUBTYPE,
                                          &subtype);
    if (FAILED(hr)) {
      printf("Failed to get subtype\n");
      CoUninitialize();
      free(buffer);
      fclose(file);
      return 0;
    }

    if (IsEqualGUID(&subtype, &MFVideoFormat_NV12)) {
      decoder->lpVtbl->SetOutputType(decoder, 0, outputMediaType, 0);
      printf("Found NV12 output type\n");
      break;
    }

    outputMediaType->lpVtbl->Release(outputMediaType);
    index++;
  }

  hr = decoder->lpVtbl->SetOutputType(decoder, 0, outputMediaType, 0);
  if (FAILED(hr)) {
    printf("Failed to set output type\n");
    printf("Error code: %x\n", hr);
    outputMediaType->lpVtbl->Release(outputMediaType);
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = decoder->lpVtbl->ProcessMessage(decoder, MFT_MESSAGE_COMMAND_FLUSH, 0);
  if (FAILED(hr)) {
    printf("Failed to process flush command\n");
    outputMediaType->lpVtbl->Release(outputMediaType);
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = decoder->lpVtbl->ProcessMessage(decoder,
                                       MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
  if (FAILED(hr)) {
    printf("Failed to process begin streaming command\n");
    outputMediaType->lpVtbl->Release(outputMediaType);
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = decoder->lpVtbl->ProcessMessage(decoder,
                                       MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
  if (FAILED(hr)) {
    printf("Failed to process start of stream command\n");
    outputMediaType->lpVtbl->Release(outputMediaType);
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  printf("Decoder is ready!\n");

  // Pass SPS and PPS data to the decoder
  IMFMediaBuffer *spsBuffer = NULL;
  hr = MFCreateMemoryBuffer(spsSize, &spsBuffer);
  if (FAILED(hr)) {
    printf("Failed to create SPS buffer\n");
    outputMediaType->lpVtbl->Release(outputMediaType);
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  BYTE *spsBufferData = NULL;
  hr = spsBuffer->lpVtbl->Lock(spsBuffer, &spsBufferData, NULL, NULL);
  if (FAILED(hr)) {
    printf("Failed to lock SPS buffer\n");
    spsBuffer->lpVtbl->Release(spsBuffer);
    outputMediaType->lpVtbl->Release(outputMediaType);
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }
  memcpy(spsBufferData, spsData, spsSize);
  spsBuffer->lpVtbl->Unlock(spsBuffer);

  IMFSample *spsSample = NULL;
  hr = MFCreateSample(&spsSample);
  if (FAILED(hr)) {
    printf("Failed to create SPS sample\n");
    spsBuffer->lpVtbl->Release(spsBuffer);
    outputMediaType->lpVtbl->Release(outputMediaType);
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = spsSample->lpVtbl->AddBuffer(spsSample, spsBuffer);
  if (FAILED(hr)) {
    printf("Failed to add buffer to SPS sample\n");
    spsSample->lpVtbl->Release(spsSample);
    spsBuffer->lpVtbl->Release(spsBuffer);
    outputMediaType->lpVtbl->Release(outputMediaType);
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = decoder->lpVtbl->ProcessInput(decoder, 0, spsSample, 0);
  if (FAILED(hr)) {
    printf("Failed to process SPS input\n");
    spsSample->lpVtbl->Release(spsSample);
    spsBuffer->lpVtbl->Release(spsBuffer);
    outputMediaType->lpVtbl->Release(outputMediaType);
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  spsSample->lpVtbl->Release(spsSample);
  spsBuffer->lpVtbl->Release(spsBuffer);

  printf("SPS data sent to the decoder\n");

  IMFMediaBuffer *ppsBuffer = NULL;
  hr = MFCreateMemoryBuffer(ppsSize, &ppsBuffer);
  if (FAILED(hr)) {
    printf("Failed to create PPS buffer\n");
    outputMediaType->lpVtbl->Release(outputMediaType);
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  BYTE *ppsBufferData = NULL;
  hr = ppsBuffer->lpVtbl->Lock(ppsBuffer, &ppsBufferData, NULL, NULL);
  if (FAILED(hr)) {
    printf("Failed to lock PPS buffer\n");
    ppsBuffer->lpVtbl->Release(ppsBuffer);
    outputMediaType->lpVtbl->Release(outputMediaType);
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }
  memcpy(ppsBufferData, ppsData, ppsSize);

  ppsBuffer->lpVtbl->Unlock(ppsBuffer);

  IMFSample *ppsSample = NULL;
  hr = MFCreateSample(&ppsSample);
  if (FAILED(hr)) {
    printf("Failed to create PPS sample\n");
    ppsBuffer->lpVtbl->Release(ppsBuffer);
    outputMediaType->lpVtbl->Release(outputMediaType);
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = ppsSample->lpVtbl->AddBuffer(ppsSample, ppsBuffer);
  if (FAILED(hr)) {
    printf("Failed to add buffer to PPS sample\n");
    ppsSample->lpVtbl->Release(ppsSample);
    ppsBuffer->lpVtbl->Release(ppsBuffer);
    outputMediaType->lpVtbl->Release(outputMediaType);
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  hr = decoder->lpVtbl->ProcessInput(decoder, 0, ppsSample, 0);
  if (FAILED(hr)) {
    printf("Failed to process PPS input\n");
    ppsSample->lpVtbl->Release(ppsSample);
    ppsBuffer->lpVtbl->Release(ppsBuffer);
    outputMediaType->lpVtbl->Release(outputMediaType);
    inputMediaType->lpVtbl->Release(inputMediaType);
    attributes->lpVtbl->Release(attributes);
    decoder->lpVtbl->Release(decoder);
    CoUninitialize();
    free(buffer);
    fclose(file);
    return 0;
  }

  ppsSample->lpVtbl->Release(ppsSample);
  ppsBuffer->lpVtbl->Release(ppsBuffer);

  printf("PPS data sent to the decoder\n");

  // Read the file again but this time, decode the frames
  i = 0;
  while (i < fileSize) {
    if (isNalStartCode(&buffer[i], fileSize - i)) {
      i += 4;
      size_t nalStart = i;
      while (i < fileSize && !(isNalStartCode(&buffer[i], fileSize - i))) {
        i++;
      }

      size_t nalLength = i - nalStart;

      // Get the NAL unit
      uint8_t *nalUnit = &buffer[nalStart];

      // get input status
      DWORD inputStatus = 0;
      hr = decoder->lpVtbl->GetInputStatus(decoder, 0, &inputStatus);
      if (FAILED(hr)) {
        printf("Failed to get input status\n");
        printf("code 0x%lx\n", hr);
        continue;
      }
      printf("Input status: %lu\n", inputStatus);
      if (inputStatus != MFT_INPUT_STATUS_ACCEPT_DATA) {
        printf("Input status is not accept data\n");
        continue;
      }

      // Decode the NAL unit
      IMFMediaBuffer *inputBuffer = NULL;
      hr = MFCreateMemoryBuffer(nalLength, &inputBuffer);
      if (FAILED(hr)) {
        printf("Failed to create input buffer\n");
        outputMediaType->lpVtbl->Release(outputMediaType);
        inputMediaType->lpVtbl->Release(inputMediaType);
        attributes->lpVtbl->Release(attributes);
        decoder->lpVtbl->Release(decoder);
        CoUninitialize();
        free(buffer);
        fclose(file);
        return 0;
      }

      BYTE *inputData = NULL;
      DWORD inputMaxLength = 0;
      DWORD inputCurrentLength = 0;
      hr = inputBuffer->lpVtbl->Lock(inputBuffer, &inputData, &inputMaxLength,
                                     &inputCurrentLength);
      if (FAILED(hr)) {
        printf("Failed to lock input buffer\n");
        inputBuffer->lpVtbl->Release(inputBuffer);
        outputMediaType->lpVtbl->Release(outputMediaType);
        inputMediaType->lpVtbl->Release(inputMediaType);
        attributes->lpVtbl->Release(attributes);
        decoder->lpVtbl->Release(decoder);
        CoUninitialize();
        free(buffer);
        fclose(file);
        return 0;
      }

      memcpy(inputData, nalUnit, nalLength);

      hr = inputBuffer->lpVtbl->SetCurrentLength(inputBuffer, nalLength);
      if (FAILED(hr)) {
        printf("Failed to set current length\n");
        inputBuffer->lpVtbl->Unlock(inputBuffer);
        inputBuffer->lpVtbl->Release(inputBuffer);
        outputMediaType->lpVtbl->Release(outputMediaType);
        inputMediaType->lpVtbl->Release(inputMediaType);
        attributes->lpVtbl->Release(attributes);
        decoder->lpVtbl->Release(decoder);
        CoUninitialize();
        free(buffer);
        fclose(file);
        return 0;
      }

      inputBuffer->lpVtbl->Unlock(inputBuffer);

      IMFSample *inputSample = NULL;
      hr = MFCreateSample(&inputSample);
      if (FAILED(hr)) {
        printf("Failed to create input sample\n");
        inputBuffer->lpVtbl->Release(inputBuffer);
        outputMediaType->lpVtbl->Release(outputMediaType);
        inputMediaType->lpVtbl->Release(inputMediaType);
        attributes->lpVtbl->Release(attributes);
        decoder->lpVtbl->Release(decoder);
        CoUninitialize();
        free(buffer);
        fclose(file);
        return 0;
      }

      hr = inputSample->lpVtbl->AddBuffer(inputSample, inputBuffer);
      if (FAILED(hr)) {
        printf("Failed to add buffer to sample\n");
        inputSample->lpVtbl->Release(inputSample);
        inputBuffer->lpVtbl->Release(inputBuffer);
        outputMediaType->lpVtbl->Release(outputMediaType);
        inputMediaType->lpVtbl->Release(inputMediaType);
        attributes->lpVtbl->Release(attributes);
        decoder->lpVtbl->Release(decoder);
        CoUninitialize();
        free(buffer);
        fclose(file);
        return 0;
      }

      hr = decoder->lpVtbl->ProcessInput(decoder, 0, inputSample, 0);
      if (FAILED(hr)) {
        printf("Failed to process input\n");
        inputSample->lpVtbl->Release(inputSample);
        inputBuffer->lpVtbl->Release(inputBuffer);
        outputMediaType->lpVtbl->Release(outputMediaType);
        inputMediaType->lpVtbl->Release(inputMediaType);
        attributes->lpVtbl->Release(attributes);
        decoder->lpVtbl->Release(decoder);
        CoUninitialize();
        free(buffer);
        fclose(file);
        return 0;
      }

      inputSample->lpVtbl->Release(inputSample);
      inputBuffer->lpVtbl->Release(inputBuffer);

      // get output status
      DWORD status = 0;
      hr = decoder->lpVtbl->GetOutputStatus(decoder, &status);
      if (FAILED(hr)) {
        printf("Failed to get output status\n");
        printf("code 0x%lx\n", hr);
        continue;
      }

      if (status == MFT_OUTPUT_STATUS_SAMPLE_READY) {
        printf("Output sample is ready\n");
      } else {
        printf("Output sample is not ready: %lu\n", status);
        continue;
      }

      // Get the decoded output
      IMFSample *outputSample = NULL;
      DWORD pdwStatus = 0;
      hr = decoder->lpVtbl->ProcessOutput(decoder, 0, 1, &outputSample,
                                          &pdwStatus);
      if (FAILED(hr)) {
        printf("Failed to process output\n");
        printf("code 0x%lx\n", hr);
        continue;
      }

      // Get the buffer from the output sample
      IMFMediaBuffer *outputBuffer = NULL;
      hr = outputSample->lpVtbl->ConvertToContiguousBuffer(outputSample,
                                                           &outputBuffer);
      if (FAILED(hr)) {
        printf("Failed to convert to contiguous buffer\n");
        outputSample->lpVtbl->Release(outputSample);
        outputMediaType->lpVtbl->Release(outputMediaType);
        inputMediaType->lpVtbl->Release(inputMediaType);
        attributes->lpVtbl->Release(attributes);
        decoder->lpVtbl->Release(decoder);
        CoUninitialize();
        free(buffer);
        fclose(file);
        return 0;
      }

      BYTE *outputData = NULL;
      DWORD outputMaxLength = 0;
      DWORD outputCurrentLength = 0;
      hr = outputBuffer->lpVtbl->Lock(outputBuffer, &outputData,
                                      &outputMaxLength, &outputCurrentLength);
      if (FAILED(hr)) {
        printf("Failed to lock output buffer\n");
        outputBuffer->lpVtbl->Release(outputBuffer);
        outputSample->lpVtbl->Release(outputSample);
        outputMediaType->lpVtbl->Release(outputMediaType);
        inputMediaType->lpVtbl->Release(inputMediaType);
        attributes->lpVtbl->Release(attributes);
        decoder->lpVtbl->Release(decoder);
        CoUninitialize();
        free(buffer);
        fclose(file);
        return 0;
      }

      // Process the decoded frame here
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