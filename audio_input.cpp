#include "audio_input.h"
#include "spdlog/spdlog.h"
#include <chrono>
#include <cstdio>
#include <opus_defines.h>

void input_data_callback(ma_device *pDevice, void *pOutput, const void *pInput,
                         ma_uint32 frameCount) {
  CallbackData *cb_data = static_cast<CallbackData *>(pDevice->pUserData);
  if (cb_data->ring_buffer == NULL) {
    cb_data->ring_buffer = create_ring_buffer((ENCODED_SIZE + sizeof(short)) *
                                              (BITRATE / 8 / frameCount));
  }
  ma_rb *ringBuffer = cb_data->ring_buffer;
  void *write_ptr;
  size_t target_bytes_to_write = sizeof(short) + ENCODED_SIZE;
  size_t bytes_to_write = target_bytes_to_write;
  if (ma_rb_acquire_write(ringBuffer, &bytes_to_write, &write_ptr) !=
          MA_SUCCESS ||
      bytes_to_write != target_bytes_to_write) {
    return;
  }

  unsigned short *size = static_cast<unsigned short *>(write_ptr);
  unsigned char *buffer =
      static_cast<unsigned char *>(write_ptr) + sizeof(*size);
  auto encoded_bytes = opus_encode_float(
      cb_data->encoder_state, static_cast<const float *>(pInput), frameCount,
      buffer, target_bytes_to_write - sizeof(short));
  *size = encoded_bytes;
  ma_rb_commit_write(ringBuffer, bytes_to_write);

  (void)pOutput;
}

ma_device *create_input_device(CallbackData *cb_data) {
  ma_device_config input_device_config =
      ma_device_config_init(ma_device_type_capture);
  ma_device *input_device = new ma_device;

  input_device_config.capture.format = ma_format_f32;
  input_device_config.capture.channels = 2;
  input_device_config.sampleRate = SAMPLE_RATE;
  input_device_config.periodSizeInFrames = FRAME_COUNT;
  input_device_config.dataCallback = input_data_callback;
  input_device_config.pUserData = cb_data;

  if (ma_device_init(NULL, &input_device_config, input_device) != MA_SUCCESS) {
    printf("Error initializaing input device\n");
    return NULL;
  }

  return input_device;
}
