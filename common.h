#pragma once

#include <miniaudio.h>

#include <opus.h>

#define SAMPLE_RATE 48000
#define BITRATE 64000
#define JITTER_DELAY 0
#define ENCODED_SIZE(frameCount) (BITRATE / 8) / (SAMPLE_RATE / frameCount)

struct CallbackData {
  OpusEncoder *encoder_state;
  OpusDecoder *decoder_state;
  ma_rb *ring_buffer;
};

ma_rb *create_ring_buffer(size_t bufferSizeInBytes);