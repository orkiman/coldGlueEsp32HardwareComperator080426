#pragma once
#include <Arduino.h>
#include "Pins.h"

// =============================================================================
// DRV8262 + MUX low-level GPIO abstraction.
//
// DRV8262 truth table (per docs/initial prompt.md  Section 2):
//   IN1=1, IN2=0  Forward drive (+48V across coil)
//   IN1=1, IN2=1  Slow decay / brake
//   IN1=0, IN2=1  Reverse drive (-48V active fast decay)
//   IN1=0, IN2=0  Coast / fast decay (safe)
//
// MUX_SELECT (per gun):
//   LOW  -> ESP32-controlled IN2  (via MUX_IN2 pin)
//   HIGH -> LM339-controlled IN2  (hardware current loop)
// =============================================================================

namespace drv {

void init();                                                         // configure all pins, force safe state
void setIn1     (uint8_t gunIdx, bool high) IRAM_ATTR;
void setMuxIn2  (uint8_t gunIdx, bool high) IRAM_ATTR;               // only meaningful when MUX_SELECT=LOW
void setMuxSelect(uint8_t gunIdx, bool hwLoop) IRAM_ATTR;            // true = LM339 controls IN2

// Master kill: drive every gun into coast mode (IN1=0, IN2=0, MUX_SELECT=0).
// IRAM-safe; called from nFAULT ISR and emergency shutdown paths.
void killAll() IRAM_ATTR;

} // namespace drv
