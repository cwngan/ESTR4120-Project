#include "audio_input.h"
#include "audio_output.h"

#include <opus.h>
#include <opus_defines.h>

#include <cstdio>

int loopback_audio(OpusEncoder *encoder_state, OpusDecoder *decoder_state) {

  CallbackData *cb_data = new CallbackData;
  cb_data->encoder_state = encoder_state;
  cb_data->decoder_state = decoder_state;
  cb_data->ring_buffer = NULL;

  ma_device *input_device = create_input_device(cb_data);
  ma_device *output_device = create_output_device(cb_data);

  opus_encoder_ctl(encoder_state, OPUS_SET_BITRATE(BITRATE));
  opus_decoder_ctl(decoder_state, OPUS_SET_BITRATE(BITRATE));

  ma_device_start(output_device);
  ma_device_start(input_device);

  printf("Press <enter> to quit...\n");
  getchar();

  ma_device_stop(output_device);
  ma_device_uninit(output_device);
  ma_device_stop(input_device);
  ma_device_uninit(input_device);

  ma_rb_uninit(cb_data->ring_buffer);
  delete cb_data;

  return 0;
}

int main() {
  int error;
  OpusEncoder *encoder_state =
      opus_encoder_create(SAMPLE_RATE, 2, OPUS_APPLICATION_VOIP, &error);
  OpusDecoder *decoder_state = opus_decoder_create(SAMPLE_RATE, 2, &error);

  loopback_audio(encoder_state, decoder_state);
}