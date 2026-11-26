#pragma once

#include "driver/gpio.h"
#include "hal/adc_types.h"

namespace DevicePins {
constexpr adc_channel_t THERMISTOR = ADC_CHANNEL_6;  // GPIO34, ADC1
constexpr gpio_num_t MPU_SDA = GPIO_NUM_21;
constexpr gpio_num_t MPU_SCL = GPIO_NUM_22;

constexpr gpio_num_t DISPLAY_SCLK = GPIO_NUM_13;
constexpr gpio_num_t DISPLAY_MOSI = GPIO_NUM_14;
constexpr gpio_num_t DISPLAY_DC = GPIO_NUM_25;
constexpr gpio_num_t DISPLAY_CS = GPIO_NUM_26;
constexpr gpio_num_t DISPLAY_RST = GPIO_NUM_27;

constexpr gpio_num_t RESET_MAX_NAKLONU = GPIO_NUM_32;
}
