/* treblesynth.h

*/

/*
   Copyright (c) 2024 Daniel Marks

  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#ifndef _TREBLESYNTH_H
#define _TREBLESYNTH_H

#define PLACE_IN_RAM

#define POTENTIOMETER_MAX 23

#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/timer.h"
#include "hardware/sync.h"
#include "hardware/flash.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

#define DMB() __dmb()

#define UART_TX_PIN 0
#define UART_RX_PIN 1

#define DAC_PWM_A3 15
#define DAC_PWM_A2 14
#define DAC_PWM_A1 13
#define DAC_PWM_A0 12

#define DAC_PWM_B3 15
#define DAC_PWM_B2 14
#define DAC_PWM_B1 13
#define DAC_PWM_B0 12

#define DAC_PWM_WRAP_VALUE 0x1000

#define ADC_A1 26
#define ADC_A2 27
#define ADC_A3 28
#define ADC_MAX_VALUE 4096
#define ADC_PREC_VALUE 16384

#define GPIO_ADC_SEL0 16
#define GPIO_ADC_SEL1 17
#define GPIO_ADC_SEL2 18

#define GPIO_BUTTON1 19
#define GPIO_BUTTON2 20
#define GPIO_BUTTON3 21
#define GPIO_BUTTON4 22

#define POT_MAX_VALUE 16384u

#define FLASH_BANKS 10
#define FLASH_PAGE_BYTES 4096u
#define FLASH_OFFSET_STORED (2*1024*1024)
#define FLASH_BASE_ADR 0x10000000
#define FLASH_MAGIC_NUMBER 0xFEE1FED9

#ifndef LED_PIN
#define LED_PIN 25
#endif /* LED_PIN */

#ifdef __cplusplus
extern "C"
{
#endif

uint16_t read_potentiometer_value(uint v);

void set_debug_vals(int32_t v1, int32_t v2, int32_t v3);

#define POTENTIOMETER_VALUE_SENSITIVITY 20
#define PROJECT_MAGIC_NUMBER 0xF00BB00F

typedef struct _project_configuration
{ 
  uint32_t  magic_number;
  uint8_t   note_transpose;
  uint32_t  fail_delay;
} project_configuration;

extern project_configuration pc;

#ifdef __cplusplus
}
#endif


#endif /* _TREBLESYNTH_H */
