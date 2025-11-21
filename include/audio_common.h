#pragma once

#include "miniaudio.h"

#include <fstream>
#include <functional>
#include <opus.h>
#include <vector>

#define SAMPLE_RATE 48000
#define BITRATE 256000
#define FRAME_COUNT 480
#define JITTER_DELAY 2
#define MAX_DELAY 10
#define ENCODED_SIZE (BITRATE / 8 * FRAME_COUNT) / SAMPLE_RATE

struct CallbackData {
  OpusEncoder *encoder_state;
  OpusDecoder *decoder_state;
  ma_rb *ring_buffer;

  std::function<void(std::vector<unsigned char> &data)> capture_data_handler;
};

ma_rb *create_ring_buffer(size_t bufferSizeInBytes);