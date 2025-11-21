#include "audio_common.h"
#include <opus.h>

struct AudioOutput {
  static void output_data_callback(ma_device *pDevice, void *pOutput,
                                   const void *pInput, ma_uint32 frameCount);

  ma_device *device;
  OpusDecoder *decoder_state;
  CallbackData *cb_data;

  AudioOutput(OpusDecoder *_decoder_state, CallbackData *_cb_data);

  void start();
  void stop();
};