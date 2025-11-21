#include "audio_input.h"
#include "audio_common.h"
#include "spdlog/spdlog.h"
#include <chrono>
#include <cstdio>
#include <opus.h>
#include <opus_defines.h>
#include <stdexcept>
#include <vector>

void AudioInput::input_data_callback(ma_device *pDevice, void *pOutput,
                                     const void *pInput, ma_uint32 frameCount) {
  CallbackData *cb_data = static_cast<CallbackData *>(pDevice->pUserData);
  std::vector<unsigned char> buffer(ENCODED_SIZE);
  auto encoded_bytes = opus_encode_float(
      cb_data->encoder_state, static_cast<const float *>(pInput), frameCount,
      buffer.data(), ENCODED_SIZE);
  if (encoded_bytes == -1) {
    printf("Error encoding audio\n");
    return;
  }
  buffer.resize(encoded_bytes);

  spdlog::trace("frame count: {}", frameCount);

  cb_data->capture_data_handler(buffer);

  (void)pOutput;
}

AudioInput::AudioInput(OpusEncoder *_encoder_state, CallbackData *_cb_data,
                       ma_device_id device_id) {
  cb_data = _cb_data;
  encoder_state = _encoder_state;

  ma_device_config device_config =
      ma_device_config_init(ma_device_type_capture);
  device = new ma_device;

  device_config.capture.pDeviceID = &device_id;
  device_config.capture.format = ma_format_f32;
  device_config.capture.channels = 2;
  device_config.sampleRate = SAMPLE_RATE;
  device_config.periodSizeInFrames = FRAME_COUNT;
  device_config.dataCallback = input_data_callback;
  device_config.pUserData = _cb_data;

  if (ma_device_init(NULL, &device_config, device) != MA_SUCCESS)
    throw std::runtime_error("Error initializaing input device");
}

void AudioInput::start() { ma_device_start(device); }
void AudioInput::stop() {
  ma_device_stop(device);
  ma_device_uninit(device);
}