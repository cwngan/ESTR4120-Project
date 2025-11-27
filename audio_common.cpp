#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#include "audio_common.h"

ma_pcm_rb *create_ring_buffer(ma_uint32 bufferSizeInFrames) {
  ma_pcm_rb *rb = new ma_pcm_rb;
  if (ma_pcm_rb_init(ma_format_f32, CHANNELS, bufferSizeInFrames, NULL, NULL,
                     rb) != MA_SUCCESS)
    return NULL;
  return rb;
}