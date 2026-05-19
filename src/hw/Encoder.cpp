#include "Encoder.h"
#include "Pins.h"
#include "config/Config.h"

#include <driver/pcnt.h>
#include <driver/gpio.h>
#include <esp_timer.h>

// Forward decls from pattern scheduler (Core 1).
namespace pattern {
    void onPhotocellEdge       (uint32_t pulseCountAtEdge) IRAM_ATTR;
    void onPhotocellFallingEdge(uint32_t pulseCountAtEdge) IRAM_ATTR;
}

namespace encoder {

static constexpr pcnt_unit_t PCNT_UNIT = PCNT_UNIT_0;
static constexpr int16_t     PCNT_LIMIT = 30000;          // wrap window

// 32-bit accumulator extending PCNT's 16-bit counter.
static volatile uint32_t s_pulseAccum = 0;
static volatile int16_t  s_lastSnap   = 0;

// Debounce state for photocell.
static volatile int64_t s_lastEdgeUs = 0;

static void IRAM_ATTR pcntOverflowIsr(void* /*arg*/) {
    uint32_t status = 0;
    pcnt_get_event_status(PCNT_UNIT, &status);
    if (status & PCNT_EVT_H_LIM) {
        s_pulseAccum += (uint32_t)PCNT_LIMIT;
        pcnt_counter_clear(PCNT_UNIT);
        s_lastSnap = 0;
    }
}

uint32_t IRAM_ATTR pulseCount() {
    int16_t cnt = 0;
    pcnt_get_counter_value(PCNT_UNIT, &cnt);
    return s_pulseAccum + (uint32_t)(uint16_t)cnt;
}

static void IRAM_ATTR photocellIsr(void* /*arg*/) {
    int64_t  now        = esp_timer_get_time();
    uint32_t debounceUs = cfg::Config::active()->debounce_ms * 1000u;
    if ((uint64_t)(now - s_lastEdgeUs) < debounceUs) return;
    s_lastEdgeUs = now;

    uint32_t p     = pulseCount();
    int      level = gpio_get_level((gpio_num_t)pins::PHOTOCELL);
    if (level) pattern::onPhotocellEdge(p);
    else       pattern::onPhotocellFallingEdge(p);
}

static void initPcnt() {
    pcnt_config_t c = {};
    c.pulse_gpio_num = pins::ENCODER;
    c.ctrl_gpio_num  = PCNT_PIN_NOT_USED;
    c.lctrl_mode     = PCNT_MODE_KEEP;
    c.hctrl_mode     = PCNT_MODE_KEEP;
    c.pos_mode       = PCNT_COUNT_INC;        // count rising edges
    c.neg_mode       = PCNT_COUNT_DIS;
    c.counter_h_lim  = PCNT_LIMIT;
    c.counter_l_lim  = -1;
    c.unit           = PCNT_UNIT;
    c.channel        = PCNT_CHANNEL_0;
    pcnt_unit_config(&c);

    // ~80 MHz APB / 1023 ticks ~= 12.8 us min pulse width filter.  Plenty for
    // an industrial encoder; tightens if needed via cfg later.
    pcnt_set_filter_value(PCNT_UNIT, 100);
    pcnt_filter_enable(PCNT_UNIT);

    pcnt_event_enable(PCNT_UNIT, PCNT_EVT_H_LIM);
    pcnt_counter_pause(PCNT_UNIT);
    pcnt_counter_clear(PCNT_UNIT);

    pcnt_isr_service_install(0);
    pcnt_isr_handler_add(PCNT_UNIT, pcntOverflowIsr, nullptr);
    pcnt_counter_resume(PCNT_UNIT);
}

static void initPhotocell() {
    gpio_config_t g = {};
    g.pin_bit_mask = (1ULL << pins::PHOTOCELL);
    g.mode         = GPIO_MODE_INPUT;
    g.pull_up_en   = GPIO_PULLUP_DISABLE;     // opto provides defined level
    g.pull_down_en = GPIO_PULLDOWN_DISABLE;
    g.intr_type    = GPIO_INTR_ANYEDGE;       // both edges; level decides
    gpio_config(&g);

    // gpio_install_isr_service is idempotent-ish: ESP_ERR_INVALID_STATE if
    // already installed (e.g. by Arduino attachInterrupt).  Ignore that.
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    gpio_isr_handler_add((gpio_num_t)pins::PHOTOCELL, photocellIsr, nullptr);
}

void init() {
    initPcnt();
    initPhotocell();
}

void injectSwTrigger() {
    pattern::onPhotocellEdge(pulseCount());
}

} // namespace encoder
