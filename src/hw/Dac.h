#pragma once
#include <Arduino.h>
#include <atomic>
#include "Pins.h"

// =============================================================================
// MCP4728 12-bit quad DAC (I2C @ 400 kHz).
//
// Sets the dynamic threshold voltage for each gun's LM339 comparator:
//   Vthresh = I_amps * 2     (INA240 gain 50 V/V * 40 mOhm shunt)
//
// Channel mapping: DAC channel N -> Gun (N+1).
//
// Hot-path strategy:
//   * ISRs and RT tasks NEVER touch I2C. They call requestThreshold(g, V),
//     which atomically updates a shadow and pings the DacTask.
//   * DacTask runs on Core 1, drains pending requests, and pushes them to the
//     chip via Adafruit_MCP4728::fastWrite (single I2C transaction, ~225 us
//     at 400 kHz for all 4 channels).
//   * If multiple requests arrive while a write is in flight, only the LATEST
//     shadow value is sent (lossless from the hardware-correctness viewpoint:
//     the comparator only cares about the final threshold).
// =============================================================================

namespace dac {

bool init();                                                  // I2C + chip + DacTask

// Request a new threshold (in volts, 0..VDD) for one gun.  Returns immediately.
// Safe from any context including ISR.
void requestThreshold(uint8_t gunIdx, float volts) IRAM_ATTR;

// Convenience: convert amps -> volts using INA240 transfer (Vout = 2*I).
inline float ampsToVolts(float a) { return a * 2.0f; }

// Force-write everything to a single low value (used on emergencyShutdown).
// Blocks briefly on I2C; call from task context only.
void blockingZeroAll();

} // namespace dac
