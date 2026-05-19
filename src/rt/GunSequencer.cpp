#include "GunSequencer.h"
#include "hw/Driver.h"
#include "hw/Dac.h"
#include "config/Config.h"
#include "comms/Events.h"

#include <driver/gpio.h>
#include <esp_timer.h>
#include <atomic>

namespace seq {

// Tunable: the Phase-3 "near zero" threshold (volts).
// 0.1 V on the INA240 == 0.05 A coil current — small enough that the LM339
// drops IN2 the moment the coil current collapses past it, terminating the
// reverse drive before any negative current flows.
static constexpr float NEAR_ZERO_V = 0.10f;

// ---------- per-gun runtime state ----------
struct GunRt {
    std::atomic<Phase>  phase{Phase::Idle};
    // The on-timer counts the full Peak+Hold budget from the start of
    // seq::fire().  When it fires we drop into Phase 3 regardless of which
    // phase we're in -- which also guards against "stuck in Peak" when the
    // LM339 trip never arrives (open coil, broken sense, etc.).
    esp_timer_handle_t  onTimer = nullptr;
    // Cached thresholds (volts) refreshed from cfg on every config publish.
    float               vPick   = 2.0f;
    float               vHold   = 0.8f;
    float               vNearZ  = NEAR_ZERO_V;
};

static GunRt s_g[pins::NUM_GUNS];

// ---------- helpers ----------
static inline bool systemArmed() {
    return cfg::g_sys.active.load(std::memory_order_acquire) &&
          !cfg::g_sys.fault .load(std::memory_order_acquire);
}

// ---------- Phase 3 entry ----------
static void IRAM_ATTR enterPhase3(uint8_t g) {
    s_g[g].phase.store(Phase::Decay, std::memory_order_release);
    dac::requestThreshold(g, s_g[g].vNearZ);
    drv::setIn1(g, false);            // MUX_SELECT stays HIGH -> reverse drive
    // Hardware completes the active fast decay autonomously.  Mark idle.
    s_g[g].phase.store(Phase::Idle, std::memory_order_release);
}

// ---------- on-timer callback (esp_timer task ctx) ----------
// Fires when the per-gun on-timeout elapses, whether we are still in Peak
// (peak trip never came) or already in Hold (normal end-of-droplet).
static void onTimerCb(void* user) {
    uint8_t g = (uint8_t)(uintptr_t)user;
    // If a peak trip is still pending we must mask the IRQ; otherwise a
    // late LM339 edge could re-enter peakIsr after we already dropped to
    // Phase 3 below.
    gpio_intr_disable((gpio_num_t)pins::PEAK_IRQ[g]);
    enterPhase3(g);
}

// ---------- peak ISR (LM339 rising edge per gun) ----------
static void IRAM_ATTR peakIsr(void* arg) {
    uint8_t g = (uint8_t)(uintptr_t)arg;
    if (s_g[g].phase.load(std::memory_order_acquire) != Phase::Peak) return;

    // Mask own IRQ first thing -- avoid being flooded by chopping signals.
    gpio_intr_disable((gpio_num_t)pins::PEAK_IRQ[g]);

    s_g[g].phase.store(Phase::Hold, std::memory_order_release);

    // Update DAC to hold threshold; LM339 will autonomously chop from here.
    // No timer action here -- the on-timer was already armed by fire() and
    // continues to count the remaining on-time budget.
    dac::requestThreshold(g, s_g[g].vHold);
}

// ---------- init helpers ----------
static void initPeakPin(uint8_t g) {
    gpio_config_t c = {};
    c.pin_bit_mask = (1ULL << pins::PEAK_IRQ[g]);
    c.mode         = GPIO_MODE_INPUT;
    c.pull_up_en   = GPIO_PULLUP_DISABLE;     // ext 10k pull-up to 3V3
    c.pull_down_en = GPIO_PULLDOWN_DISABLE;
    c.intr_type    = GPIO_INTR_POSEDGE;
    gpio_config(&c);
    gpio_isr_handler_add((gpio_num_t)pins::PEAK_IRQ[g],
                         peakIsr, (void*)(uintptr_t)g);
    gpio_intr_disable((gpio_num_t)pins::PEAK_IRQ[g]);   // armed only during fire()
}

static void initOnTimer(uint8_t g) {
    esp_timer_create_args_t a = {};
    a.callback        = &onTimerCb;
    a.arg             = (void*)(uintptr_t)g;
    a.dispatch_method = ESP_TIMER_TASK;
    a.name            = "on";
    esp_timer_create(&a, &s_g[g].onTimer);
}

void init() {
    // GPIO ISR service was installed by encoder::init().  If not, install now.
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);

    for (uint8_t g = 0; g < pins::NUM_GUNS; ++g) {
        initPeakPin(g);
        initOnTimer(g);
    }
    onConfigApplied();
}

void onConfigApplied() {
    const cfg::RuntimeConfig* c = cfg::Config::active();
    float vPick = dac::ampsToVolts(c->pick_current_a);
    float vHold = dac::ampsToVolts(c->hold_current_a);
    for (uint8_t g = 0; g < pins::NUM_GUNS; ++g) {
        s_g[g].vPick  = vPick;
        s_g[g].vHold  = vHold;
        s_g[g].vNearZ = NEAR_ZERO_V;
    }
}

// ---------- public API ----------
bool IRAM_ATTR fire(uint8_t g, uint32_t onMs) {
    if (g >= pins::NUM_GUNS) return false;
    if (!systemArmed())      return false;

    // CAS to Peak; if not currently Idle the gun is busy.
    Phase expected = Phase::Idle;
    if (!s_g[g].phase.compare_exchange_strong(expected, Phase::Peak,
                                              std::memory_order_acq_rel)) {
        return false;
    }

    // On-timeout = total Peak+Hold budget, measured from RIGHT NOW.
    // onMs == 0 means "use the per-gun configured on_timeout_ms (Dots mode)";
    // any non-zero caller value (lines = long ceiling, tests) overrides.
    uint64_t onUs = (onMs == 0)
        ? (uint64_t)(cfg::Config::active()->pattern[g].on_timeout_ms * 1000.0f)
        : (uint64_t)onMs * 1000ull;
    if (onUs < 50)        onUs = 50;          // 50 us minimum sanity
    if (onUs > 5'000'000) onUs = 5'000'000;   // 5 s hard ceiling

    // Phase 1: arm DAC to pick, drive IN1, route LM339 to IN2, enable peak IRQ.
    dac::requestThreshold(g, s_g[g].vPick);
    drv::setMuxSelect(g, true);     // S=1 -> LM339 drives IN2
    drv::setIn1     (g, true);
    gpio_intr_enable((gpio_num_t)pins::PEAK_IRQ[g]);
    // Arm the full-cycle on-timer NOW so that "stuck in Peak" cannot last
    // longer than `onUs` even if the LM339 peak trip never arrives.
    esp_timer_start_once(s_g[g].onTimer, onUs);
    return true;
}

void IRAM_ATTR abort(uint8_t g) {
    if (g >= pins::NUM_GUNS) return;
    Phase p = s_g[g].phase.load(std::memory_order_acquire);
    if (p == Phase::Idle) return;

    gpio_intr_disable((gpio_num_t)pins::PEAK_IRQ[g]);
    esp_timer_stop(s_g[g].onTimer);
    enterPhase3(g);
}

void IRAM_ATTR abortAll() {
    for (uint8_t g = 0; g < pins::NUM_GUNS; ++g) abort(g);
}

Phase phaseOf(uint8_t g) {
    return (g < pins::NUM_GUNS) ? s_g[g].phase.load() : Phase::Idle;
}

bool isBusy(uint8_t g) {
    return phaseOf(g) != Phase::Idle;
}

} // namespace seq
