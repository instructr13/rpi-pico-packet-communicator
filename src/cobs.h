#pragma once

#include <cstddef>
#include <cstdint>

namespace pcomm::cobs {

size_t encode(const uint8_t *input, size_t length, uint8_t *output);

bool decode(const uint8_t *input, size_t length, uint8_t *output,
            size_t &output_length);

} // namespace cobs
