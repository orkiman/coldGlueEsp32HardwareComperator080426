#pragma once

// =============================================================================
// DRV8262 nFAULT supervisor (GPIO 39, active LOW).
// On falling edge: kill every output, latch fault flag, emit hardware_fault.
// Recovery is only possible via a fresh set_active:true from the HMI.
// =============================================================================

namespace fault {
    void init();
}
