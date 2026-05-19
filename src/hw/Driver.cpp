#include "Driver.h"
#include <driver/gpio.h>

namespace drv {

void init() {
    for (uint8_t g = 0; g < pins::NUM_GUNS; ++g) {
        gpio_num_t in1 = (gpio_num_t)pins::DRV_IN1[g];
        gpio_num_t in2 = (gpio_num_t)pins::MUX_IN2[g];
        gpio_num_t sel = (gpio_num_t)pins::MUX_SELECT[g];

        gpio_reset_pin(in1);
        gpio_reset_pin(in2);
        gpio_reset_pin(sel);

        gpio_set_direction(in1, GPIO_MODE_OUTPUT);
        gpio_set_direction(in2, GPIO_MODE_OUTPUT);
        gpio_set_direction(sel, GPIO_MODE_OUTPUT);

        gpio_set_level(in1, 0);
        gpio_set_level(in2, 0);
        gpio_set_level(sel, 0);   // ESP32-controlled IN2 by default -> coast
    }
}

void IRAM_ATTR setIn1(uint8_t g, bool high) {
    gpio_set_level((gpio_num_t)pins::DRV_IN1[g], high ? 1 : 0);
}

void IRAM_ATTR setMuxIn2(uint8_t g, bool high) {
    gpio_set_level((gpio_num_t)pins::MUX_IN2[g], high ? 1 : 0);
}

void IRAM_ATTR setMuxSelect(uint8_t g, bool hwLoop) {
    gpio_set_level((gpio_num_t)pins::MUX_SELECT[g], hwLoop ? 1 : 0);
}

void IRAM_ATTR killAll() {
    for (uint8_t g = 0; g < pins::NUM_GUNS; ++g) {
        gpio_set_level((gpio_num_t)pins::DRV_IN1[g],    0);
        gpio_set_level((gpio_num_t)pins::MUX_IN2[g],    0);
        gpio_set_level((gpio_num_t)pins::MUX_SELECT[g], 0);  // coast under ESP32 control
    }
}

} // namespace drv
