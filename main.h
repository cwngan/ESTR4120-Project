#include "miniaudio.h"
#include "opus.h"

struct CallbackData {
  OpusEncoder *encoder_state;
  OpusDecoder *decoder_state;
  ma_rb *ring_buffer;
};