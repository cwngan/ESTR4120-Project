#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#include "audio_common.h"

ma_rb *create_ring_buffer(size_t bufferSizeInBytes) {
  ma_rb *rb = new ma_rb;
  if (ma_rb_init(bufferSizeInBytes, NULL, NULL, rb) != MA_SUCCESS)
    return NULL;
  return rb;
}