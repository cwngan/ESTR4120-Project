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
  if (cb_data->connected_clients == 0)
    return;

  int ring_buffer_count = cb_data->connected_clients;
  spdlog::trace("{} ring buffer to read from", ring_buffer_count);
  for (auto entry : cb_data->ring_buffers) {
    auto id = entry.first;
    auto ring_buffer = entry.second;
    ma_uint32 available_read = ma_pcm_rb_available_read(ring_buffer);

    if (available_read > MAX_DELAY) {
      ma_uint32 skip = available_read - (MAX_DELAY + JITTER_DELAY) / 2;
      spdlog::warn("{} frames behind, skipping {} frames", available_read,
                   skip);
      ma_pcm_rb_seek_read(ring_buffer, skip);
    }

    void *read_ptr;
    ma_uint32 frames_to_read = FRAME_COUNT;
    if (ma_pcm_rb_acquire_read(ring_buffer, &frames_to_read, &read_ptr) !=
            MA_SUCCESS ||
        frames_to_read != FRAME_COUNT) {

      spdlog::trace(
          "fail to acquire enough ptr to read enough data from buffer");
      continue;
    }

    float *pcm = static_cast<float *>(read_ptr);
    for (int i = 0; i < frames_to_read * pDevice->playback.channels; i++)
      static_cast<float *>(pOutput)[i] += pcm[i];

    ma_pcm_rb_commit_read(ring_buffer, frames_to_read);
    spdlog::trace("read {} frames at {}", frames_to_read, read_ptr);
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