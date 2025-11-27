#pragma once

#include "miniaudio.h"

#include <fstream>
#include <functional>
#include <opus.h>
#include <unordered_map>
#include <vector>

#define SAMPLE_RATE 48000
#define BITRATE 128000
#define FRAME_COUNT 960
#define JITTER_DELAY 960
#define MAX_DELAY 4800
#define CHANNELS 2
#define ENCODED_SIZE (BITRATE / 8 * FRAME_COUNT) / SAMPLE_RATE

struct ClientStreamDecoder {
  int seq_number;
  OpusDecoder *decoder_state;
};

struct CallbackData {
  OpusEncoder *encoder_state;
  std::unordered_map<unsigned int, ClientStreamDecoder> decoder_states;
  int connected_clients = 0;
  std::unordered_map<unsigned int, ma_pcm_rb *> ring_buffers;

  std::function<void(std::vector<unsigned char> &data)> capture_data_handler;
};

ma_pcm_rb *create_ring_buffer(ma_uint32 bufferSizeInFrames);