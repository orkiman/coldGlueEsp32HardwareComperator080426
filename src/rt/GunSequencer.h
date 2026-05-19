#pragma once
#include <Arduino.h>
#include <stdint.h>
#include "hw/Pins.h"

// =============================================================================
// Per-gun "Dot Sequence" state machine.
//
// Phase 1 (Peak):
//   - DAC[g] := pick threshold (e.g. 2.0V for 1.0A)
//   - IN1=HIGH, MUX_SELECT=HIGH (arm hardware loop)
//   - Enable peak interrupt for this gun
//
// Phase 2 (Hold):  triggered by LM339 -> peakIsr
//   - Disable own peak interrupt (mask the chopping signals)
//   - DAC[g] := hold threshold (e.g. 0.8V for 0.4A)
//   - Start gptimer for Thold (cfg::hold_time_ms; overridden by test_open)
//
// Phase 3 (Active Fast Decay):  triggered by hold timer
//   - DAC[g] := ~zero threshold (e.g. 0.1V)
//   - IN1=LOW   (MUX_SELECT stays HIGH so LM339 drives IN2 -> Reverse Drive)
//   - When current collapses past the near-zero threshold, LM339 drops IN2,
//     driver enters coast.  We then return to Idle.
// =============================================================================

namespace seq {

enum class Phase : uint8_t {
    Idle    = 0,
    Peak    = 1,
    Hold    = 2,
    Decay   = 3,
};

void init();                                                       // wires up peak ISRs + gptimers

// Fire a full dot sequence on gun g (0..3).
// holdMs == 0 => use cfg::hold_time_ms; otherwise override (test_open).
// Returns false if the gun is busy or system is faulted / inactive.
bool fire(uint8_t gunIdx, uint32_t holdMs = 0) IRAM_ATTR;

// Abort any in-flight sequence on gun g (immediate Phase 3).
// gun == 0xFF -> all guns.  IRAM-safe.
void abort(uint8_t gunIdx) IRAM_ATTR;
void abortAll() IRAM_ATTR;

Phase phaseOf(uint8_t gunIdx);                                     // diagnostics
bool  isBusy(uint8_t gunIdx);

// Called by Config when set_config publishes a new threshold table.
void onConfigApplied();

} // namespace seq
