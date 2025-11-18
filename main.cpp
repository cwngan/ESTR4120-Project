#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#include <miniaudio.h>

#include <opus.h>
#include <opus_defines.h>

#include "main.h"

#define SAMPLE_RATE 48000
#define BITRATE 64000

void input_data_callback(ma_device *pDevice, void *pOutput, const void *pInput,
                         ma_uint32 frameCount) {

  CallbackData *cb_data = static_cast<CallbackData *>(pDevice->pUserData);
  ma_rb *ringBuffer = cb_data->ring_buffer;
  void *write_ptr;
  size_t bytesToWrite = (BITRATE / 8) / (SAMPLE_RATE / frameCount);
  if (ma_rb_acquire_write(ringBuffer, &bytesToWrite, &write_ptr) !=
          MA_SUCCESS ||
      bytesToWrite == 0)
    return;

  unsigned char *buffer = static_cast<unsigned char *>(write_ptr);
  auto encoded_bytes = opus_encode_float(cb_data->encoder_state,
                                         static_cast<const float *>(pInput),
                                         frameCount, buffer, bytesToWrite);
  ma_rb_commit_write(ringBuffer, bytesToWrite);

  (void)pOutput;
}

void output_data_callback(ma_device *pDevice, void *pOutput, const void *pInput,
                          ma_uint32 frameCount) {
  CallbackData *cb_data = static_cast<CallbackData *>(pDevice->pUserData);
  ma_rb *ringBuffer = cb_data->ring_buffer;
  void *read_ptr;
  size_t bytesToRead = (BITRATE / 8) / (SAMPLE_RATE / frameCount);
  if (ma_rb_acquire_read(ringBuffer, &bytesToRead, &read_ptr) != MA_SUCCESS)
    return;

  unsigned char *buffer = static_cast<unsigned char *>(read_ptr);
  auto decoded_bytes =
      opus_decode_float(cb_data->decoder_state, buffer, bytesToRead,
                        static_cast<float *>(pOutput), frameCount, 0);
  ma_rb_commit_read(ringBuffer, bytesToRead);

  (void)pInput;
}

int loopback_audio(OpusEncoder *encdoer_state, OpusDecoder *decoder_state) {
  ma_device_config input_device_config =
      ma_device_config_init(ma_device_type_capture);
  ma_device input_device;
  ma_device_config output_device_config =
      ma_device_config_init(ma_device_type_playback);
  ma_device output_device;

  ma_rb *ringBuffer = new ma_rb;
  if (ma_rb_init(BITRATE / 8, NULL, NULL, ringBuffer) != MA_SUCCESS)
    return -1;

  CallbackData *cb_data = new CallbackData;
  cb_data->encoder_state = encdoer_state;
  cb_data->decoder_state = decoder_state;
  cb_data->ring_buffer = ringBuffer;

  input_device_config.capture.format = ma_format_f32;
  input_device_config.capture.channels = 2;
  input_device_config.sampleRate = SAMPLE_RATE;
  input_device_config.dataCallback = input_data_callback;
  input_device_config.pUserData = cb_data;

  if (ma_device_init(NULL, &input_device_config, &input_device) != MA_SUCCESS) {
    printf("Error initializaing input device\n");
    return -1;
  }

  output_device_config.playback.format = ma_format_f32;
  output_device_config.playback.channels = 2;
  output_device_config.sampleRate = SAMPLE_RATE;
  output_device_config.dataCallback = output_data_callback;
  output_device_config.pUserData = cb_data;

  if (ma_device_init(NULL, &output_device_config, &output_device) !=
      MA_SUCCESS) {
    printf("Error initializaing output device\n");
    return -1;
  }

  ma_device_start(&input_device);
  ma_device_start(&output_device);

  printf("Press any key to quit...\n");
  getchar();

  ma_device_stop(&output_device);
  ma_device_uninit(&output_device);
  ma_device_stop(&input_device);
  ma_device_uninit(&input_device);

  return 0;
}

int main() {
  OPUS_SET_BITRATE(BITRATE);
  int error;
  OpusEncoder *encoder_state =
      opus_encoder_create(48000, 2, OPUS_APPLICATION_VOIP, &error);
  OpusDecoder *decoder_state = opus_decoder_create(48000, 2, &error);

  loopback_audio(encoder_state, decoder_state);
}