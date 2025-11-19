#include "common.h"

void input_data_callback(ma_device *pDevice, void *pOutput, const void *pInput,
                         ma_uint32 frameCount);

ma_device *create_input_device(CallbackData *cb_data);