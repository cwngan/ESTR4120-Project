#include "audio_output.h"
#include "audio_common.h"
#include "spdlog/spdlog.h"
#include <chrono>
#include <cstdio>
#include <opus.h>

void AudioOutput::output_data_callback(ma_device *pDevice, void *pOutput,
                                       const void *pInput,
                                       ma_uint32 frameCount) {
  CallbackData *cb_data = static_cast<CallbackData *>(pDevice->pUserData);
  if (cb_data->ring_buffer_count == 0)
    return;

  int ring_buffer_count = cb_data->ring_buffer_count;
  spdlog::trace("{} ring buffer to read from", ring_buffer_count);
  for (auto entry : cb_data->ring_buffers) {
    auto id = entry.first;
    auto ring_buffer = entry.second;
    ma_uint32 available_read = ma_rb_available_read(ring_buffer);
    if (available_read < JITTER_DELAY * (ENCODED_SIZE + sizeof(short)))
      continue;

    if (available_read > MAX_DELAY * (ENCODED_SIZE + sizeof(short))) {
      ma_uint32 skip = available_read - (MAX_DELAY - JITTER_DELAY) *
                                            (ENCODED_SIZE + sizeof(short));
      spdlog::warn("{} bytes behind, skipping {} bytes", available_read, skip);
      ma_rb_seek_read(ring_buffer, skip);
    }

    void *read_ptr;
    size_t target_bytes_to_read = sizeof(short) + ENCODED_SIZE;
    size_t bytes_to_read = target_bytes_to_read;
    if (ma_rb_acquire_read(ring_buffer, &bytes_to_read, &read_ptr) !=
            MA_SUCCESS ||
        bytes_to_read != target_bytes_to_read)
      continue;

    unsigned short *size = static_cast<unsigned short *>(read_ptr);
    unsigned char *buffer =
        static_cast<unsigned char *>(read_ptr) + sizeof(*size);

    auto decoded_bytes =
        opus_decode_float(cb_data->decoder_states[id], buffer, *size,
                          cb_data->decoded_data.data(), frameCount, 0);
    spdlog::trace("decoding with parameters: frameCount={}, size={}",
                  frameCount, *size);

    for (int i = 0; i < frameCount * pDevice->playback.channels; i++)
      static_cast<float *>(pOutput)[i] += cb_data->decoded_data[i];

    ma_rb_commit_read(ring_buffer, bytes_to_read);
    spdlog::trace("read {} bytes at {}, decoded to {} bytes", *size, read_ptr,
                  decoded_bytes);
  }

  (void)pInput;
}

AudioOutput::AudioOutput(CallbackData *_cb_data, ma_device_id device_id) {
  cb_data = _cb_data;

  ma_device_config device_config =
      ma_device_config_init(ma_device_type_playback);
  device = new ma_device;

  device_config.playback.pDeviceID = &device_id;
  device_config.playback.format = ma_format_f32;
  device_config.playback.channels = CHANNELS;
  device_config.sampleRate = SAMPLE_RATE;
  device_config.dataCallback = output_data_callback;
  device_config.periodSizeInFrames = FRAME_COUNT;
  device_config.pUserData = cb_data;

  if (ma_device_init(NULL, &device_config, device) != MA_SUCCESS)
    throw std::runtime_error("Error initializaing output device");
}

void AudioOutput::start() { ma_device_start(device); }
void AudioOutput::pause() { ma_device_stop(device); }
void AudioOutput::stop() {
  ma_device_stop(device);
  ma_device_uninit(device);
}