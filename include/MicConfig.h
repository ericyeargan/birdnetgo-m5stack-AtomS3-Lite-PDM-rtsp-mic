#pragma once

#include <stdint.h>

// Microphone selection is a build-time choice (see platformio.ini):
//   MIC_SPH0645=0  Unit Mini PDM on the ESP32-S3 PDM RX path (default)
//   MIC_SPH0645=1  Adafruit SPH0645LM4H breakout on the standard I2S RX path
#ifndef MIC_SPH0645
#define MIC_SPH0645 0
#endif

#if MIC_SPH0645
// -- Pins (AtomS3 Lite + SPH0645LM4H). Tie SEL to GND so the mic drives the left slot.
#define I2S_BCLK_PIN    5   // BCLK (G5)
#define I2S_WS_PIN      6   // LRCL (G6)
#define I2S_DATA_IN_PIN 7   // DOUT (G7)
#define MIC_MODEL_NAME  "SPH0645LM4H"
#define MIC_PIN_HINT    "Firmware expects BCLK=G5, LRCL=G6, DOUT=G7, SEL=GND."
// Each 32-bit slot carries 18 data bits MSB-first (bits 31..14), 6 zero bits
// (13..8), then 8 bit-times where the mic tri-states DOUT (7..0).
typedef int32_t I2sSample;
#else
// -- Pins (AtomS3 Lite + Unit Mini PDM)
// Mic wired to the header pins (M5's Grove reference wiring is CLK=G1, DATA=G2).
#define I2S_CLK_PIN     39  // PDM CLK (G39)
#define I2S_DATA_IN_PIN 38  // PDM DATA (G38)
#define MIC_MODEL_NAME  "Unit Mini PDM"
#define MIC_PIN_HINT    "Firmware expects CLK=G39 and DATA=G38."
typedef int16_t I2sSample;
#endif

// Bit alignment of the SPH0645 data in its 32-bit slot, measured at I2S start.
enum I2sStdAlignment : uint8_t {
    I2S_STD_ALIGN_UNKNOWN = 0,
    I2S_STD_ALIGN_OK = 1,      // LSB (bit 14) toggles, pad bits 13..8 stay zero
    I2S_STD_ALIGN_LATE = 2,    // bit 14 never set: captured one bit late, sign lost
    I2S_STD_ALIGN_EARLY = 3,   // pad bits set: captured early, or junk on the line
    I2S_STD_ALIGN_SILENT = 4,  // all zero: wiring, SEL, or slot problem
};

inline const char* i2sStdAlignmentName(uint8_t alignment) {
    switch (alignment) {
        case I2S_STD_ALIGN_OK: return "aligned";
        case I2S_STD_ALIGN_LATE: return "late";
        case I2S_STD_ALIGN_EARLY: return "early";
        case I2S_STD_ALIGN_SILENT: return "silent";
        default: return "unknown";
    }
}
