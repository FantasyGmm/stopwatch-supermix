#pragma once

#include <cstddef>
#include <cstdint>


namespace ble_voice_probe {

size_t ima_adpcm_block_size(size_t sample_count);
size_t encode_ima_adpcm_block(const int16_t* samples, size_t sample_count,
                              uint8_t* output, size_t capacity);

}  // namespace ble_voice_probe
