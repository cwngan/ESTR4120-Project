#include "audio_common.h"
#include <functional>
#include <opus.h>
#include <vector>

struct AudioInput {
  static void input_data_callback(ma_device *pDevice, void *pOutput,
                                  const void *pInput, ma_uint32 frameCount);

  ma_device *device;
  OpusEncoder *encoder_state;
  CallbackData *cb_data;

  AudioInput(OpusEncoder *_encoder_state, CallbackData *_cb_data);

  void start();
  void stop();
};