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
    esp_timer_handle_t  holdTimer = nullptr;
    // Cached thresholds (volts) refreshed from cfg on every config publish.
    float               vPick   = 2.0f;
    float               vHold   = 0.8f;
    float               vNearZ  = NEAR_ZERO_V;
};

static GunRt    s_g[pins::NUM_GUNS];
static uint64_t s_holdUs[pins::NUM_GUNS] = {0, 0, 0, 0};

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

// ---------- hold timer callback (esp_timer task ctx) ----------
static void holdTimerCb(void* user) {
    uint8_t g = (uint8_t)(uintptr_t)user;
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
    dac::requestThreshold(g, s_g[g].vHold);

    // Arm esp_timer for Thold.  Duration was stored on s_holdUs[g] by fire().
    esp_timer_start_once(s_g[g].holdTimer, s_holdUs[g]);
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

static void initHoldTimer(uint8_t g) {
    esp_timer_create_args_t a = {};
    a.callback        = &holdTimerCb;
    a.arg             = (void*)(uintptr_t)g;
    a.dispatch_method = ESP_TIMER_TASK;
    a.name            = "hold";
    esp_timer_create(&a, &s_g[g].holdTimer);
}

void init() {
    // GPIO ISR service was installed by encoder::init().  If not, install now.
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);

    for (uint8_t g = 0; g < pins::NUM_GUNS; ++g) {
        initPeakPin(g);
        initHoldTimer(g);
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
bool IRAM_ATTR fire(uint8_t g, uint32_t holdMs) {
    if (g >= pins::NUM_GUNS) return false;
    if (!systemArmed())      return false;

    // CAS to Peak; if not currently Idle the gun is busy.
    Phase expected = Phase::Idle;
    if (!s_g[g].phase.compare_exchange_strong(expected, Phase::Peak,
                                              std::memory_order_acq_rel)) {
        return false;
    }

    // Stash hold duration; peakIsr arms the timer once the LM339 trips.
    uint64_t holdUs = (holdMs == 0)
        ? (uint64_t)(cfg::Config::active()->hold_time_ms * 1000.0f)
        : (uint64_t)holdMs * 1000ull;
    if (holdUs < 50)        holdUs = 50;          // 50 us minimum sanity
    if (holdUs > 5'000'000) holdUs = 5'000'000;   // 5 s hard cap
    s_holdUs[g] = holdUs;

    // Phase 1: arm DAC to pick, drive IN1, route LM339 to IN2, enable peak IRQ.
    dac::requestThreshold(g, s_g[g].vPick);
    drv::setMuxSelect(g, true);     // S=1 -> LM339 drives IN2
    drv::setIn1     (g, true);
    gpio_intr_enable((gpio_num_t)pins::PEAK_IRQ[g]);
    return true;
}

void IRAM_ATTR abort(uint8_t g) {
    if (g >= pins::NUM_GUNS) return;
    Phase p = s_g[g].phase.load(std::memory_order_acquire);
    if (p == Phase::Idle) return;

    gpio_intr_disable((gpio_num_t)pins::PEAK_IRQ[g]);
    esp_timer_stop(s_g[g].holdTimer);
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
