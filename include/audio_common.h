#pragma once

#include "miniaudio.h"

#include <opus.h>

#define SAMPLE_RATE 48000
#define BITRATE 64000
#define FRAME_COUNT 480
#define JITTER_DELAY 0
#define MAX_DELAY 4
#define ENCODED_SIZE (BITRATE / 8 * FRAME_COUNT) / SAMPLE_RATE

struct CallbackData {
  OpusEncoder *encoder_state;
  OpusDecoder *decoder_state;
  ma_rb *ring_buffer;
};

ma_rb *create_ring_buffer(size_t bufferSizeInBytes);