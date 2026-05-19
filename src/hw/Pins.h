#pragma once
#include <stdint.h>

// =============================================================================
// Cold Glue Controller - ESP32-S3 Pin Map
// Source of truth: docs/initial prompt.md  Section 4
// =============================================================================

namespace pins {

constexpr uint8_t NUM_GUNS = 4;

// Per-gun pin arrays, indexed 0..3 (Gun 1..Gun 4).
constexpr int8_t OPTO_IN[NUM_GUNS]    = { 1,  2,  4,  5  }; // 24V opto inputs (Gun1 = photocell)
constexpr int8_t DRV_IN1[NUM_GUNS]    = { 6,  7, 15, 16  }; // DRV8262 IN1/IN3 main drive
constexpr int8_t MUX_IN2[NUM_GUNS]    = { 8,  9, 17, 18  }; // MUX I0 input (manual ESP32 override)
constexpr int8_t MUX_SELECT[NUM_GUNS] = {10, 11, 14, 48  }; // S=1 LM339 control, S=0 ESP32 control
constexpr int8_t PEAK_IRQ[NUM_GUNS]   = {12, 13, 21, 38  }; // From LM339, ext 10k pull-up

// Photocell trigger is Opto Input #1 by spec ("e.g. Input_1_24v").
constexpr int8_t PHOTOCELL = OPTO_IN[0];

// --- Global pins ---
constexpr int8_t I2C_SDA  = 41;   // -> MCP4728
constexpr int8_t I2C_SCL  = 42;   // -> MCP4728
constexpr int8_t N_FAULT  = 39;   // DRV8262 nFAULT, active LOW, ext 10k pull-up
constexpr int8_t ENCODER  = 40;   // 6N137 single-channel pulse input

// --- UART0 (HMI link via CP2102 / pin header) ---
// Default Arduino Serial uses these on ESP32-S3 DevKitC-1.
constexpr int8_t UART_TX  = 43;
constexpr int8_t UART_RX  = 44;

} // namespace pins
