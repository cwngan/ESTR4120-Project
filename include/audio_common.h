#pragma once

#include "miniaudio.h"

#include <fstream>
#include <functional>
#include <opus.h>
#include <unordered_map>
#include <vector>

#define SAMPLE_RATE 48000
#define BITRATE 64000
#define FRAME_COUNT 960
#define JITTER_DELAY 1
#define MAX_DELAY 5
#define CHANNELS 2
#define ENCODED_SIZE (BITRATE / 8 * FRAME_COUNT) / SAMPLE_RATE

struct CallbackData {
  OpusEncoder *encoder_state;
  std::unordered_map<unsigned int, OpusDecoder *> decoder_states;
  int ring_buffer_count = 0;
  std::unordered_map<unsigned int, ma_rb *> ring_buffers;
  std::vector<float> decoded_data;

  std::function<void(std::vector<unsigned char> &data)> capture_data_handler;
};

ma_rb *create_ring_buffer(size_t bufferSizeInBytes);