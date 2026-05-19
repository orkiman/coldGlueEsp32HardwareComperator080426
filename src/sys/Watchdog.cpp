#include "Watchdog.h"
#include "hw/Driver.h"
#include "rt/GunSequencer.h"
#include "rt/PatternScheduler.h"
#include "config/Config.h"
#include "comms/Events.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace watchdog {

static bool s_tripped = false;

static void wdTask(void*) {
    cfg::g_sys.lastCmdMs.store(millis(), std::memory_order_release);
    for (;;) {
        uint32_t now = millis();
        uint32_t last = cfg::g_sys.lastCmdMs.load(std::memory_order_acquire);
        bool armed = cfg::g_sys.active.load(std::memory_order_acquire);

        if (armed && (now - last) > TIMEOUT_MS && !s_tripped) {
            s_tripped = true;
            drv::killAll();
            seq::abortAll();
            pattern::abortAll();
            cfg::g_sys.active.store(false, std::memory_order_release);
            evt::postWatchdogTimeout();
        } else if ((now - last) <= TIMEOUT_MS) {
            s_tripped = false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void init() {
    xTaskCreatePinnedToCore(wdTask, "wdog", 4096, nullptr, 3, nullptr, 0);
}

} // namespace watchdog
