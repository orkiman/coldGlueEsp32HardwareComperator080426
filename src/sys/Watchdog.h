#pragma once
#include <stdint.h>

// =============================================================================
// HMI link watchdog.  If no command (any cmd including ping) is received for
// TIMEOUT_MS, all outputs are killed and {"event":"watchdog_timeout"} fires.
// =============================================================================

namespace watchdog {
    constexpr uint32_t TIMEOUT_MS = 2000;
    void init();
}
