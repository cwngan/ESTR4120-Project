#include "audio_common.h"

void output_data_callback(ma_device *pDevice, void *pOutput, const void *pInput,
                          ma_uint32 frameCount);
ma_device *create_output_device(CallbackData *cb_data);