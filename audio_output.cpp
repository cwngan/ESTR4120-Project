#include "audio_output.h"
#include "audio_common.h"
#include "spdlog/spdlog.h"
#include <chrono>
#include <cstdio>

void output_data_callback(ma_device *pDevice, void *pOutput, const void *pInput,
                          ma_uint32 frameCount) {
  CallbackData *cb_data = static_cast<CallbackData *>(pDevice->pUserData);
  if (cb_data->ring_buffer == NULL) {
    cb_data->ring_buffer = create_ring_buffer((ENCODED_SIZE + sizeof(short)) *
                                              (SAMPLE_RATE / frameCount));
  }
  ma_rb *ringBuffer = cb_data->ring_buffer;

  ma_uint32 available_read = ma_rb_available_read(ringBuffer);
  if (available_read < JITTER_DELAY * (ENCODED_SIZE + sizeof(short)))
    return;

  if (available_read > MAX_DELAY * (ENCODED_SIZE + sizeof(short))) {
    ma_uint32 skip =
        available_read - MAX_DELAY * (ENCODED_SIZE + sizeof(short));
    spdlog::info("skipping {} bytes", skip);
    ma_rb_commit_read(ringBuffer, skip);
  }

  void *read_ptr;
  size_t target_bytes_to_read = sizeof(short) + ENCODED_SIZE;
  size_t bytes_to_read = target_bytes_to_read;
  if (ma_rb_acquire_read(ringBuffer, &bytes_to_read, &read_ptr) != MA_SUCCESS ||
      bytes_to_read != target_bytes_to_read)
    return;

  unsigned short *size = static_cast<unsigned short *>(read_ptr);
  unsigned char *buffer =
      static_cast<unsigned char *>(read_ptr) + sizeof(*size);

  auto decoded_bytes =
      opus_decode_float(cb_data->decoder_state, buffer, *size,
                        static_cast<float *>(pOutput), frameCount, 0);
  ma_rb_commit_read(ringBuffer, bytes_to_read);

  spdlog::debug("{}: read {}, frame count: {}",
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count(),
                read_ptr, frameCount);

  (void)pInput;
}

ma_device *create_output_device(CallbackData *cb_data) {
  ma_device_config output_device_config =
      ma_device_config_init(ma_device_type_playback);
  ma_device *output_device = new ma_device;

  output_device_config.playback.format = ma_format_f32;
  output_device_config.playback.channels = 2;
  output_device_config.sampleRate = SAMPLE_RATE;
  output_device_config.dataCallback = output_data_callback;
  output_device_config.periodSizeInFrames = FRAME_COUNT;
  output_device_config.pUserData = cb_data;

  if (ma_device_init(NULL, &output_device_config, output_device) !=
      MA_SUCCESS) {
    printf("Error initializaing output device\n");
    return NULL;
  }

  return output_device;
}
