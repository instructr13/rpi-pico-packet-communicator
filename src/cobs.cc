#include "cobs.h"

size_t cobs::encode(const uint8_t *input, const size_t length,
                    uint8_t *output) {
  if (length == 0) {
    output[0] = 1;

    return 1;
  }

  const auto *end = input + length;
  auto dst = output;
  auto code_ptr = dst++;

  uint8_t code = 1;

  while (input < end) {
    if (*input == 0) {
      *code_ptr = code;
      code_ptr = dst++;

      code = 1;

      ++input;
    } else {
      *dst++ = *input++;
      ++code;

      if (code == 0xFF) {
        *code_ptr = code;
        code_ptr = dst++;

        code = 1;
      }
    }
  }

  *code_ptr = code;

  *dst++ = 0; // Delimiter

  return dst - output;
}

bool cobs::decode(const uint8_t *input, const size_t length, uint8_t *output,
                  size_t &output_length) {
  if (length == 0) {
    output_length = 0;

    return true;
  }

  auto ptr = input;
  const auto *end = input + length;
  auto dst = output;

  while (ptr < end) {
    uint8_t code = *ptr++;

    if (code == 0 || ptr + (code - 1) > end) {
      // Invalid COBS data
      return false;
    }

    for (uint8_t i = 1; i < code; ++i) {
      *dst++ = *ptr++;
    }

    if (code < 0xFF && ptr < end) {
      *dst++ = 0;
    }
  }

  output_length = dst - output;

  return true;
}
