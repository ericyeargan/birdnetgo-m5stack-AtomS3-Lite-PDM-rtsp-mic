#pragma once

#include <stdint.h>

struct Sph0645ProbeResult {
    bool valid = false;
    bool bclkInvert = false;
    bool msbJustified = false;  // MSB-justified framing (no Philips 1-bit delay)
    uint8_t alignment = 0;  // I2sStdAlignment
    uint32_t samples = 0;
    uint32_t lsbSetCount = 0;     // bit 14 (data LSB) set
    uint32_t bit15SetCount = 0;   // bit 15 set
    uint32_t padSetCount = 0;     // any of bits 13..8 (driven zero) set
    uint32_t nonzeroCount = 0;
    uint32_t everSetMask = 0;     // OR of all words
    uint32_t alwaysSetMask = 0;   // AND of all words
};
