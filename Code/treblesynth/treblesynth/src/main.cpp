/* main.cpp

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

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "main.h"
#include "treblesynth.h"
#include "ssd1306_i2c.h"
#include "buttons.h"
#include "dsp.h"
#include "pitch.h"
#include "ui.h"
#include "tinycl.h"

#include "usbmain.h"

#define NUMBER_OF_CONTROLS 24

uint16_t current_samples[NUMBER_OF_CONTROLS];
uint16_t samples[NUMBER_OF_CONTROLS];
uint16_t last_samples[NUMBER_OF_CONTROLS];
uint control_direction[NUMBER_OF_CONTROLS];
uint control_enabled[NUMBER_OF_CONTROLS];

uint current_input;
uint control_sample_no;

const int potentiometer_mapping[23] = { 1,2,3,4,6,7,5,10,9,8,11,12,14,15,13,18,17,16,19,20,22,23,21 };

uint32_t mag_avg;

uint     button_state[8*4];
const uint button_ind[32] = { 29,25,30,28,31,27,26,  2,1,0,3,4,6,7,5,  10,9,8,11,12,14,15,13, 18,17,16,19,20,22,23,21,24   };

void initialize_video(void);
void halt_video(void);

#define UNCLAIMED_ALARM 0xFFFFFFFF

uint dac_pwm_b3_slice_num, dac_pwm_b2_slice_num, dac_pwm_b1_slice_num, dac_pwm_b0_slice_num;
uint dac_pwm_a3_slice_num, dac_pwm_a2_slice_num, dac_pwm_a1_slice_num, dac_pwm_a0_slice_num;
uint claimed_alarm_num = UNCLAIMED_ALARM;

volatile uint32_t counter = 0;

void keyboard_poll(void)
{
    uint8_t button_no;
    
    while ((button_no=button_get_state_changed()) != BUTTONS_NO_CHANGE)
    {
        uint8_t button_on_off = (button_no & 0x80);
        button_no &= 0x7F;
        if (button_no > 6)
            midi_button_event(button_no-7, button_on_off);
    }
}

#define UART_FIFO_SIZE 256

typedef struct _uart_fifo
{
    uint head;
    uint tail;
    critical_section cs;
    uint8_t buf[UART_FIFO_SIZE];
} uart_fifo;

uart_fifo uart_fifo_input;
uart_fifo uart_fifo_output;

void initialize_uart_fifo(uart_fifo *uf)
{
    critical_section_init(&uf->cs);
    uf->head = uf->tail = 0;
}

void insert_into_uart_fifo(uart_fifo *uf, uint8_t ch)
{
    critical_section_enter_blocking(&uf->cs);
    uint nexthead = uf->head >= (UART_FIFO_SIZE-1) ? 0 : (uf->head+1);
    if (nexthead != uf->tail)
    {
        uf->buf[uf->head] = ch;
        uf->head = nexthead;
    }
    critical_section_exit(&uf->cs);
}

int remove_from_uart_fifo(uart_fifo *uf)
{
    int ret;
    critical_section_enter_blocking(&uf->cs);
    if (uf->tail == uf->head) 
        ret = -1;
    else 
    {
        ret = uf->buf[uf->tail];
        uf->tail = uf->tail >= (UART_FIFO_SIZE-1) ? 0 : (uf->tail+1);
    }
    critical_section_exit(&uf->cs);
    return ret;
}

int uart0_input(void)
{
    return remove_from_uart_fifo(&uart_fifo_input);
}

void uart0_output(const uint8_t *data, int num)
{
    for (int i=0;i<num;i++)
        insert_into_uart_fifo(&uart_fifo_output, data[i]);
    uart_set_irq_enables(uart0, true, true);
    irq_set_pending(UART0_IRQ);
}

void uart0_irq_handler(void)
{
    while (!(((uart_hw_t *)uart0)->fr & UART_UARTFR_RXFE_BITS))
    {
        insert_into_uart_fifo(&uart_fifo_input,((uart_hw_t *)uart0)->dr & 0xFF);
    }
    while (!(((uart_hw_t *)uart0)->fr & UART_UARTFR_TXFF_BITS))
    {
        int ch;
        if ((ch = remove_from_uart_fifo(&uart_fifo_output)) < 0)
        {
           uart_set_irq_enables(uart0, true, false);
           break;
        }
        ((uart_hw_t *)uart0)->dr = ch;
    } 
}

void initialize_uart(void)
{
    irq_set_enabled(UART0_IRQ, false);
    initialize_uart_fifo(&uart_fifo_input);
    initialize_uart_fifo(&uart_fifo_output);

    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    
    uart_init(uart0, 31250);
    uart_set_hw_flow(uart0, false, false);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart0, true);
    uart_set_translate_crlf(uart0, false);
    uart_set_irq_enables(uart0, true, false);
    irq_set_exclusive_handler(UART0_IRQ, uart0_irq_handler);
    irq_set_enabled(UART0_IRQ, true);
}

void idle_task(void)
{
    buttons_poll();
    midi_uart_poll();
    keyboard_poll();
    usb_task();
}

void initialize_pwm(void)
{
    gpio_set_function(DAC_PWM_B3, GPIO_FUNC_PWM);
    gpio_set_function(DAC_PWM_B2, GPIO_FUNC_PWM);
    gpio_set_function(DAC_PWM_B1, GPIO_FUNC_PWM);
    gpio_set_function(DAC_PWM_B0, GPIO_FUNC_PWM);

    dac_pwm_b3_slice_num = pwm_gpio_to_slice_num(DAC_PWM_B3);
    dac_pwm_b2_slice_num = pwm_gpio_to_slice_num(DAC_PWM_B2);
    dac_pwm_b1_slice_num = pwm_gpio_to_slice_num(DAC_PWM_B1);
    dac_pwm_b0_slice_num = pwm_gpio_to_slice_num(DAC_PWM_B0);
    
    pwm_set_clkdiv_int_frac(dac_pwm_b3_slice_num, 1, 0);
    pwm_set_clkdiv_int_frac(dac_pwm_b2_slice_num, 1, 0);
    pwm_set_clkdiv_int_frac(dac_pwm_b1_slice_num, 1, 0);
    pwm_set_clkdiv_int_frac(dac_pwm_b0_slice_num, 1, 0);
    
    pwm_set_clkdiv_mode(dac_pwm_b3_slice_num, PWM_DIV_FREE_RUNNING);
    pwm_set_clkdiv_mode(dac_pwm_b2_slice_num, PWM_DIV_FREE_RUNNING);
    pwm_set_clkdiv_mode(dac_pwm_b1_slice_num, PWM_DIV_FREE_RUNNING);
    pwm_set_clkdiv_mode(dac_pwm_b0_slice_num, PWM_DIV_FREE_RUNNING);

    pwm_set_phase_correct(dac_pwm_b3_slice_num, false);
    pwm_set_phase_correct(dac_pwm_b2_slice_num, false);
    pwm_set_phase_correct(dac_pwm_b1_slice_num, false);
    pwm_set_phase_correct(dac_pwm_b0_slice_num, false);
    
    pwm_set_wrap(dac_pwm_b3_slice_num, DAC_PWM_WRAP_VALUE-1);
    pwm_set_wrap(dac_pwm_b2_slice_num, DAC_PWM_WRAP_VALUE-1);
    pwm_set_wrap(dac_pwm_b1_slice_num, DAC_PWM_WRAP_VALUE-1);
    pwm_set_wrap(dac_pwm_b0_slice_num, DAC_PWM_WRAP_VALUE-1);
    
    pwm_set_output_polarity(dac_pwm_b3_slice_num, false, false);
    pwm_set_output_polarity(dac_pwm_b2_slice_num, false, false);
    pwm_set_output_polarity(dac_pwm_b1_slice_num, false, false);
    pwm_set_output_polarity(dac_pwm_b0_slice_num, false, false);
    
    pwm_set_enabled(dac_pwm_b3_slice_num, true);
    pwm_set_enabled(dac_pwm_b2_slice_num, true);
    pwm_set_enabled(dac_pwm_b1_slice_num, true);
    pwm_set_enabled(dac_pwm_b0_slice_num, true);

    gpio_set_function(DAC_PWM_A3, GPIO_FUNC_PWM);
    gpio_set_function(DAC_PWM_A2, GPIO_FUNC_PWM);
    gpio_set_function(DAC_PWM_A1, GPIO_FUNC_PWM);
    gpio_set_function(DAC_PWM_A0, GPIO_FUNC_PWM);

    dac_pwm_a3_slice_num = pwm_gpio_to_slice_num(DAC_PWM_A3);
    dac_pwm_a2_slice_num = pwm_gpio_to_slice_num(DAC_PWM_A2);
    dac_pwm_a1_slice_num = pwm_gpio_to_slice_num(DAC_PWM_A1);
    dac_pwm_a0_slice_num = pwm_gpio_to_slice_num(DAC_PWM_A0);
    
    pwm_set_clkdiv_int_frac(dac_pwm_a3_slice_num, 1, 0);
    pwm_set_clkdiv_int_frac(dac_pwm_a2_slice_num, 1, 0);
    pwm_set_clkdiv_int_frac(dac_pwm_a1_slice_num, 1, 0);
    pwm_set_clkdiv_int_frac(dac_pwm_a0_slice_num, 1, 0);
    
    pwm_set_clkdiv_mode(dac_pwm_a3_slice_num, PWM_DIV_FREE_RUNNING);
    pwm_set_clkdiv_mode(dac_pwm_a2_slice_num, PWM_DIV_FREE_RUNNING);
    pwm_set_clkdiv_mode(dac_pwm_a1_slice_num, PWM_DIV_FREE_RUNNING);
    pwm_set_clkdiv_mode(dac_pwm_a0_slice_num, PWM_DIV_FREE_RUNNING);

    pwm_set_phase_correct(dac_pwm_a3_slice_num, false);
    pwm_set_phase_correct(dac_pwm_a2_slice_num, false);
    pwm_set_phase_correct(dac_pwm_a1_slice_num, false);
    pwm_set_phase_correct(dac_pwm_a0_slice_num, false);
    
    pwm_set_wrap(dac_pwm_a3_slice_num, DAC_PWM_WRAP_VALUE-1);
    pwm_set_wrap(dac_pwm_a2_slice_num, DAC_PWM_WRAP_VALUE-1);
    pwm_set_wrap(dac_pwm_a1_slice_num, DAC_PWM_WRAP_VALUE-1);
    pwm_set_wrap(dac_pwm_a0_slice_num, DAC_PWM_WRAP_VALUE-1);
    
    pwm_set_output_polarity(dac_pwm_a3_slice_num, false, false);
    pwm_set_output_polarity(dac_pwm_a2_slice_num, false, false);
    pwm_set_output_polarity(dac_pwm_a1_slice_num, false, false);
    pwm_set_output_polarity(dac_pwm_a0_slice_num, false, false);
    
    pwm_set_enabled(dac_pwm_a3_slice_num, true);
    pwm_set_enabled(dac_pwm_a2_slice_num, true);
    pwm_set_enabled(dac_pwm_a1_slice_num, true);
    pwm_set_enabled(dac_pwm_a0_slice_num, true);
}

uint16_t read_potentiometer_value(uint v)
{
    if ((v == 0) || (v > (sizeof(potentiometer_mapping)/sizeof(potentiometer_mapping[0]))))
        return 0;
    return (uint16_t)(samples[potentiometer_mapping[v-1]]*(POT_MAX_VALUE/ADC_MAX_VALUE));
}

volatile uint16_t next_sample = 0;

absolute_time_t last_time;

static inline absolute_time_t update_next_timeout(const absolute_time_t last_time, uint32_t us, uint32_t min_us)
{
    absolute_time_t next_time = delayed_by_us(last_time, us);
    absolute_time_t next_time_sooner = make_timeout_time_us(min_us);
    return (absolute_time_diff_us(next_time, next_time_sooner) < 0) ? next_time : next_time_sooner;
}

uint8_t get_scan_button(uint8_t b)
{
    if (b >= (sizeof(button_ind)/sizeof(button_ind[0])))
        return 0;
    return button_state[button_ind[b]];
}

void select_control(uint ci, uint csn)
{
    gpio_put(GPIO_ADC_SEL0, (ci & 0x01) == 0);
    gpio_put(GPIO_ADC_SEL1, (ci & 0x02) == 0);
    gpio_put(GPIO_ADC_SEL2, (ci & 0x04) == 0);
    adc_select_input(csn);
}

/* only do when poll controls is turned off! */
void manually_poll_analog_controls(void)
{
   for (uint ci=0;ci<8;ci++)
   {
       for (uint csn=0;csn<3;csn++)
       {
            current_samples[ci+csn*8] = adc_hw->result;
            select_control(ci,csn);
            sleep_us(1000000 / DSP_SAMPLERATE);
            current_samples[ci+csn*8] = adc_hw->result;
       }
   }
}

void reset_control_samples(uint16_t *reset_samples)
{
    if (reset_samples == NULL) 
    {
        for (uint n=0;n<NUMBER_OF_CONTROLS;n++)
            control_enabled[n] = 1;
        return;
    }        
    memcpy(last_samples, reset_samples, sizeof(last_samples));
    manually_poll_analog_controls();
    for (uint n=0;n<NUMBER_OF_CONTROLS;n++)
    {
        control_enabled[n] = 0;
        control_direction[n] = current_samples[n] < last_samples[n];
        samples[n] = last_samples[n];
    }
}

inline void poll_controls(void)
{
   uint current_sample_no = current_input+control_sample_no*8;
   uint16_t sample = adc_hw->result;
   
   if (control_enabled[current_sample_no])
       samples[current_sample_no] = sample;
   else
   {
       if ( ((control_direction[current_sample_no]) && (sample >= last_samples[current_sample_no])) ||
            ((!control_direction[current_sample_no]) && (sample <= last_samples[current_sample_no])) )
            {
                samples[current_sample_no] = sample;
                control_enabled[current_sample_no] = 1;
            }
   }
   
   button_state[current_input] = gpio_get(GPIO_BUTTON1);
   button_state[current_input+8] = gpio_get(GPIO_BUTTON2);
   button_state[current_input+16] = gpio_get(GPIO_BUTTON3);
   button_state[current_input+24] = gpio_get(GPIO_BUTTON4);
   
   current_input = (current_input >= 7) ? 0 : (current_input+1);
   gpio_put(GPIO_ADC_SEL0, (current_input & 0x01) == 0);
   gpio_put(GPIO_ADC_SEL1, (current_input & 0x02) == 0);
   gpio_put(GPIO_ADC_SEL2, (current_input & 0x04) == 0);
   if (current_input == 0)
   {
     if ((++control_sample_no) >= 3)
         control_sample_no = 0;
     adc_select_input(control_sample_no);
   }   
}

static void __no_inline_not_in_flash_func(alarm_func)(uint alarm_num)
{
    static uint ticktock = 0;
    absolute_time_t next_alarm_time;

    if (ticktock)
    {
        pwm_set_both_levels(dac_pwm_b3_slice_num, next_sample, next_sample);
        pwm_set_both_levels(dac_pwm_b1_slice_num, next_sample, next_sample);
        pwm_set_both_levels(dac_pwm_a3_slice_num, next_sample, next_sample);
        pwm_set_both_levels(dac_pwm_a1_slice_num, next_sample, next_sample);
        poll_controls();
        ticktock = 0;
        last_time = delayed_by_us(last_time, 10);
        do
        {
            next_alarm_time = update_next_timeout(last_time, 0, 8);
        } while (hardware_alarm_set_target(claimed_alarm_num, next_alarm_time));
        return;
    } 
    next_sample = 0;
    ticktock = 1;
    last_time = delayed_by_us(last_time, 30);
    do
    {
        next_alarm_time = update_next_timeout(last_time, 0, 8);
    } while (hardware_alarm_set_target(claimed_alarm_num, next_alarm_time));
    counter++;
}

void reset_periodic_alarm(uint16_t *reset_samples)
{
    if (claimed_alarm_num == UNCLAIMED_ALARM) return;
    
    hardware_alarm_cancel(claimed_alarm_num);
    control_sample_no = 0;
    current_input = 0;
    select_control(0,0);
    reset_control_samples(reset_samples);
    hardware_alarm_set_callback(claimed_alarm_num, alarm_func);
    last_time = make_timeout_time_us(1000);
    absolute_time_t next_alarm_time = update_next_timeout(last_time, 0, 8);
    //absolute_time_t next_alarm_time = make_timeout_time_us(1000);
    hardware_alarm_set_target(claimed_alarm_num, next_alarm_time);
}


void initialize_periodic_alarm(uint16_t *reset_samples)
{
    if (claimed_alarm_num != UNCLAIMED_ALARM)
    {
        reset_periodic_alarm(reset_samples);
        return;
    }
    claimed_alarm_num = hardware_alarm_claim_unused(true);
    reset_periodic_alarm(reset_samples);
}

void initialize_adc(void)
{
    
    adc_init();
    adc_gpio_init(ADC_A1);
    adc_gpio_init(ADC_A2);
    adc_gpio_init(ADC_A3);
    adc_set_clkdiv(100);
    adc_set_round_robin(0);
    adc_run(false);
    adc_select_input(0);
    current_input = 0;
    adc_run(true);
}

void initialize_gpio(void)
{
    // initialize debug LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    gpio_init(GPIO_ADC_SEL0);
    gpio_set_dir(GPIO_ADC_SEL0, GPIO_OUT);
    gpio_init(GPIO_ADC_SEL1);
    gpio_set_dir(GPIO_ADC_SEL1, GPIO_OUT);
    gpio_init(GPIO_ADC_SEL2);
    gpio_set_dir(GPIO_ADC_SEL2, GPIO_OUT);

    gpio_set_dir(GPIO_BUTTON1, GPIO_IN);
    gpio_set_dir(GPIO_BUTTON2, GPIO_IN);
    gpio_set_dir(GPIO_BUTTON3, GPIO_IN);
    gpio_set_dir(GPIO_BUTTON4, GPIO_IN);

    gpio_init(23);
    gpio_set_dir(23, GPIO_OUT);
    gpio_put(23, 1);
}

char buttonpressed(uint8_t b)
{
    return button_readbutton(b) ? '1' : '0';
}

void adjust_parms(uint8_t unit_no)
{
    int sel = 0, redraw = 1;
    button_clear();
    for (;;)
    {
        const dsp_parm_configuration_entry *d = dpce[dsp_parms[unit_no].dtn.dut];
        idle_task();
        if (redraw)
        {
            if (sel == 0)
            {
                write_str_with_spaces(0,2,"Type",16);
                write_str_with_spaces(0,3,dtnames[dsp_parms[unit_no].dtn.dut],16);
            } else
            {
                char s[20];
                write_str_with_spaces(0,2,d[sel-1].desc,16);
                char *c = number_str(s, 
                    dsp_read_value_prec((void *)(((uint8_t *)&dsp_parms[unit_no]) + d[sel-1].offset), d[sel-1].size), 
                    d[sel-1].digits, 0);
                write_str_with_spaces(0,3,c,16);
            }
            display_refresh();
            redraw = 0;            
        }
        if (button_left())
        {
            button_clear();
            break;
        }
        else if (button_down() && (sel > 0))
        {
            sel--;
            redraw = 1;
        } else if (button_up())
        {
            if (d[sel].desc != NULL)
            {
                sel++;
                redraw = 1;
            }
        } else if (button_right() || button_enter())
        {
            if (sel == 0)
            {
                write_str_with_spaces(0,2,"Type select",16);
                menu_str mst = { dtnames,0,3,10,0,0 };
                mst.item = mst.itemesc = dsp_parms[unit_no].dtn.dut;
                int res;
                do_show_menu_item(&mst);
                do
                {
                    idle_task();
                    res = do_menu(&mst);
                } while (res == 0);
                if (res == 3)
                {
                    if (mst.item != dsp_parms[unit_no].dtn.dut)
                    {
                        dsp_unit_initialize(unit_no, (dsp_unit_type)mst.item);
                        dsp_unit_reset_all();
                    }
                }
            } else
            {
                scroll_number_dat snd = { 0, 3, 
                                          d[sel-1].digits,
                                          0,
                                          d[sel-1].minval,
                                          d[sel-1].maxval,
                                          0,
                                          dsp_read_value_prec((void *)(((uint8_t *)&dsp_parms[unit_no]) + d[sel-1].offset), d[sel-1].size),
                                          0, 0 };
                scroll_number_start(&snd);
                do
                {
                    idle_task();
                    scroll_number_key(&snd);
                } while (!snd.entered);
                if (snd.changed)
                   dsp_set_value_prec((void *)(((uint8_t *)&dsp_parms[unit_no]) + d[sel-1].offset), d[sel-1].size, snd.n);
            }
            redraw = 1;
        }
    }
    
    
}

void adjust_dsp_params(void)
{
    int unit_no = 0, redraw = 1;
    char s[20];

    button_clear();
    for (;;)
    {
        idle_task();
        if (redraw)
        {
            clear_display();
            write_str(0,0,"DSP Adj");
            sprintf(s,"Unit #%d", unit_no+1);
            write_str_with_spaces(0,1,s,16);
            write_str_with_spaces(0,2,dtnames[dsp_parms[unit_no].dtn.dut],16);
            display_refresh();
            redraw = 0;
        }
        if (button_left())
        {
            button_clear();
            break;
        }
        else if (button_down() && (unit_no > 0))
        {
            unit_no--;
            redraw = 1;
        } else if (button_up() && (unit_no < (MAX_DSP_UNITS-1)))
        {
            unit_no++;
            redraw = 1;
        } else if (button_right() || button_enter())
        {
           sprintf(s,"Unit #%d select", unit_no+1);
           write_str_with_spaces(0,1,s,16);
           adjust_parms(unit_no);
           redraw = 1;
        }
    }
}

int main2();

#define FLASH_BANKS 10
#define FLASH_PAGE_BYTES 4096u
#define FLASH_OFFSET_STORED (2*1024*1024)
#define FLASH_BASE_ADR 0x10000000
#define FLASH_MAGIC_NUMBER 0xFEE1FEDE

#define FLASH_PAGES(x) ((((x)+(FLASH_PAGE_BYTES-1))/FLASH_PAGE_BYTES)*FLASH_PAGE_BYTES)

uint32_t last_gen_no;
uint8_t  desc[16];
uint32_t pedal_onoff = 0;
uint8_t  pedal_control[4] = { 1, 2, 3, 4 };

typedef struct _flash_layout_data
{
    uint32_t magic_number;
    uint32_t gen_no;
    uint8_t  desc[16];
    dsp_parm dsp_parms[MAX_DSP_UNITS];
} flash_layout_data;

typedef union _flash_layout
{ 
    flash_layout_data fld;
    uint8_t      space[FLASH_PAGE_BYTES];
} flash_layout;

inline static uint32_t flash_offset_bank(uint bankno)
{
    return (FLASH_OFFSET_STORED - FLASH_PAGES(sizeof(flash_layout)) * (bankno+1));
}

inline static const uint8_t *flash_offset_address_bank(uint bankno)
{
    const uint8_t *const flashadr = (const uint8_t *const) FLASH_BASE_ADR;
    return &flashadr[flash_offset_bank(bankno)];
}

void message_to_display(const char *msg)
{
    write_str_with_spaces(0,5,msg,16);
    display_refresh();

    buttons_clear();
    for (;;)
    {
       idle_task();
       if (button_enter()) break;
     }
}

int write_data_to_flash(uint32_t flash_offset, const uint8_t *data, uint core, uint32_t length)
{
    length = FLASH_PAGES(length);                // pad up to next 4096 byte boundary
    flash_offset = FLASH_PAGES(flash_offset);
    bool multicore = multicore_lockout_victim_is_initialized(core);
    if (multicore) multicore_lockout_start_blocking();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_offset, length);
    flash_range_program(flash_offset, data, length);
    restore_interrupts(ints);
    if (multicore) multicore_lockout_end_blocking();
    reset_periodic_alarm(NULL);
    return 0;
}

uint select_bankno(uint bankno)
{
    bool notend = true;
    
    while (notend)
    {
        char s[20];
        sprintf(s,"Sel Bank: %02d", bankno);          
        write_str_with_spaces(0,4,s,16);
        flash_layout *fl = (flash_layout *) flash_offset_address_bank(bankno-1);
        write_str_with_spaces(0,5,fl->fld.magic_number == FLASH_MAGIC_NUMBER ? (char *)fl->fld.desc : "No Description",15);
        display_refresh();
        buttons_clear();
        for (;;)
        {
            idle_task();
            if (button_enter())
            {
                notend = false;
                break;
            }
            if (button_left()) return 0;
            if ((button_up()) && (bankno < (FLASH_BANKS))) 
            {
                bankno++;
                break;
            }
            if ((button_down()) && (bankno > 1)) 
            {
                bankno--;
                break;
            }
        }
    }
    return bankno;
}

const char * const pedalmenu[] = { "Turn Pedal Off", "Turn Pedal On", NULL };

menu_str pedalmenu_str = { pedalmenu, 0, 2, 15, 0, 0 };

void pedal_control_cmd()
{
  uint val;
  pedalmenu_str.item  = pedal_onoff;
  do_show_menu_item(&pedalmenu_str);
  buttons_clear();
  for (;;)
  {
      idle_task();
      val = do_menu(&pedalmenu_str);
      if (val != 0) break;
  } 
  if (val == 1) return;
  pedal_onoff = pedalmenu_str.item;
  if (pedal_onoff == 0) return;

  for (uint i=0;i<(sizeof(pedal_control)/sizeof(pedal_control[0]));i++)
  {
     char s[20];
     sprintf(s,"Pedal Button %u",i+1);
     write_str_with_spaces(0,2,s,16);
     pedal_control[i] = select_bankno(pedal_control[i]);
  }
}

#define PEDAL_SWITCH_INPUT 6

static uint pedal_current_state = 0;
static uint pedal_wait_state = 0;
static uint pedal_current_count = 0;

void pedal_display_state(void)
{
    if (!pedal_onoff) return;
    write_str(0,7,pedal_current_state == 1 ? "\001\001\001" : "\002\002\002" );
    write_str(4,7,pedal_current_state == 2 ? "\001\001\001" : "\002\002\002" );
    write_str(8,7,pedal_current_state == 3 ? "\001\001\001" : "\002\002\002" );
    write_str(12,7,pedal_current_state == 4 ? "\001\001\001" : "\002\002\002" );
}

int flash_load_bank(uint bankno)
{
    flash_layout *fl = (flash_layout *) flash_offset_address_bank(bankno);

    if (bankno >= FLASH_BANKS) return -1;
    if (fl->fld.magic_number == FLASH_MAGIC_NUMBER)
    {
        if (fl->fld.gen_no > last_gen_no)
            last_gen_no = fl->fld.gen_no;
        memcpy(desc, fl->fld.desc, sizeof(desc));
        memcpy((void *)dsp_parms, (void *) &fl->fld.dsp_parms, sizeof(dsp_parms));
        dsp_unit_reset_all();
    } else return -1;
    return 0;
}

void pedal_switch(void)
{
    static uint32_t last_us;
    uint32_t current_us = time_us_32();

    if ((current_us - last_us) < 5000) return;
    last_us = current_us;

    if (!pedal_onoff) return;

    uint val = read_potentiometer_value(PEDAL_SWITCH_INPUT);
    uint state = 0;

    if ((val>=(POT_MAX_VALUE*2/16)) && (val<(POT_MAX_VALUE*4/16))) state = 1;
    if ((val>=(POT_MAX_VALUE*4/16)) && (val<(POT_MAX_VALUE*6/16))) state = 2;
    if ((val>=(POT_MAX_VALUE*7/16)) && (val<(POT_MAX_VALUE*9/16))) state = 3;
    if ((val>=(POT_MAX_VALUE*10/16)) && (val<(POT_MAX_VALUE*12/16))) state = 4;

    if (pedal_wait_state != state)
    {
        pedal_wait_state = state;
        pedal_current_count = 0;
        return;
    } 
    if (pedal_current_count == 11)
        return;
    if (pedal_current_count == 10)
    {
        pedal_current_state = pedal_wait_state;
        pedal_display_state();
        if (pedal_current_state > 0)
        {
            if (flash_load_bank(pedal_control[pedal_current_state-1]-1) == 0)
            {
                char s[20];
                sprintf(s,"Bank loaded %02u",pedal_control[pedal_current_state-1]);
                write_str_with_spaces(0,5,s,16);
            } else
                write_str_with_spaces(0,5,"Bank NOT loaded",16);
        } 
        set_cursor(15,5);
        display_refresh();
    }
    pedal_current_count++;
}

int flash_load(void)
{
    uint bankno;
    if ((bankno = select_bankno(1)) == 0) return -1;
    message_to_display(flash_load_bank(bankno-1) ? "Not Loaded" : "Loaded");
    return 0;
}

void flash_load_most_recent(void)
{
    uint32_t newest_gen_no = 0;
    uint load_bankno = FLASH_BANKS;

    last_gen_no = 0;
    memset(desc,'\000',sizeof(desc));

    for (uint bankno = 0;bankno < FLASH_BANKS; bankno++)
    {
        flash_layout *fl = (flash_layout *) flash_offset_address_bank(bankno);
        if (fl->fld.magic_number == FLASH_MAGIC_NUMBER)
        {
            if (fl->fld.gen_no >= newest_gen_no)
            {
                newest_gen_no = fl->fld.gen_no;
                load_bankno = bankno;
            }
        }
    }
    if (load_bankno < FLASH_BANKS) flash_load_bank(load_bankno);
}

 const uint8_t validchars[] = { ' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 
                                'U', 'V', 'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4', '5', '6', '7' ,'8', '9', '-', '/', '.', '!', '?' };

int flash_save_bank(uint bankno)
{
    flash_layout *fl;

    if (bankno >= FLASH_BANKS) return -1;
    if ((fl = (flash_layout *)malloc(sizeof(flash_layout))) == NULL) return -1;
    memset((void *)fl,'\000',sizeof(flash_layout));
    fl->fld.magic_number = FLASH_MAGIC_NUMBER;
    fl->fld.gen_no = (++last_gen_no);
    memcpy(fl->fld.desc, desc, sizeof(fl->fld.desc));
    memcpy((void *)&fl->fld.dsp_parms, (void *)dsp_parms, sizeof(fl->fld.dsp_parms));
    int ret = write_data_to_flash(flash_offset_bank(bankno), (uint8_t *) fl, 1, sizeof(flash_layout));
    free(fl);
    return ret;
}

void flash_save(void)
{
    uint bankno;
    scroll_alpha_dat sad = { 0, 5, sizeof(desc)-1, sizeof(desc)-1, desc, validchars, sizeof(validchars), 0, 0, 0, 0, 0 };

    if ((bankno = select_bankno(1)) == 0) return;
    write_str_with_spaces(0,4,"Description:",15);
    scroll_alpha_start(&sad);
    for (;;)
    {
        idle_task();
        scroll_alpha_key(&sad);
        if (sad.exited) return;
        if (sad.entered) break;
    }    
    write_str_with_spaces(0,4,"",15);
    message_to_display(flash_save_bank(bankno-1) ? "Save Failed" : "Save Succeeded");      
}

bool pitch_poll(bool do_play_note, uint32_t *hz, int32_t *note_no)
{
    static uint32_t mag_note_thr = 0;
    static bool playing_note = false;
    static bool another_note = true;
    static int32_t min_offset = PITCH_MIN_OFFSET;
   
    uint32_t cur_mag_avg = mag_avg;
    bool note_event = false;
    
    if (pitch_current_entry >= NUM_PITCH_EDGES)
    {
        pitch_edge_autocorrelation();
        pitch_buffer_reset();
        int32_t entry = pitch_autocorrelation_max(min_offset);
        if (entry >= 0)
        {
            uint32_t hzn = pitch_estimate_peak_hz(entry);
            if (hzn > 0)
            {
                min_offset = pitch_autocor[entry].offset / 2;
                int32_t note_no_n = pitch_find_note(hzn);
                if ((note_no_n >= 0) && (another_note))
                {
                    *note_no = note_no_n;
                    mag_note_thr = cur_mag_avg*3/4;
                    *hz = hzn;
                    note_event = true;
                    if (do_play_note)
                    {
                        gpio_put(LED_PIN, 1);
                        uint32_t velocity = cur_mag_avg / (16 * ADC_PREC_VALUE / 128);
                        if (velocity > 127) velocity = 127;
                        midi_send_note(note_no_n+24, velocity);
                    }
                    playing_note = true;
                    another_note = false;
                }
            }
        }
    }
    if (cur_mag_avg < mag_note_thr) 
        another_note = true;
    if (cur_mag_avg < (512*40))
    {
        min_offset = PITCH_MIN_OFFSET;
        //pitch_buffer_reset();
        if (playing_note)
        {
            note_event = true;
            if (do_play_note) 
            {
               gpio_put(LED_PIN, 0);
               midi_send_note(0,0);
            }
            playing_note = false;
            another_note = true;
            mag_note_thr = 0;
        }
    }    
    return note_event;
}

void pitch_measure(void)
{

    char str[80];
    bool endloop = false;
    uint32_t last_time = 0, hz = 0;
    int32_t note_no = -1;
  
    clear_display();
    pitch_buffer_reset();
    buttons_clear();
    
    while (!endloop)
    {
        idle_task();
        if (button_enter() || button_left()) 
        {
            endloop = true;
            break;
        }
        pitch_poll(true, &hz, &note_no);
        if ((time_us_32() - last_time) > 250000)
        {
            last_time = time_us_32();
            sprintf(str,"Hz: %u",hz);
            write_str_with_spaces(0,0,str,16);
            sprintf(str,"Nt: %s %u", note_no < 0 ? "None" : notes[note_no].note, note_no < 0 ? 0 : notes[note_no].frequency_hz);
            write_str_with_spaces(0,1,str,16);
            display_refresh();
        }
    }
    midi_send_note(0,0);
    gpio_put(LED_PIN, 0);
}


void debugstuff(void)
{
    char str[40];
    bool endloop = false;
    
    while (!endloop)
    {
        uint32_t last_time = time_us_32();
        while ((time_us_32() - last_time) < 100000)
        {
            idle_task();
            if (button_enter() || button_left()) 
            {
                endloop = true;
                break;
            }
        }
        ssd1306_Clear_Buffer();
        sprintf(str,"%u %c%c%c%c%c",counter,buttonpressed(0),buttonpressed(1),buttonpressed(2),buttonpressed(3),buttonpressed(4));
        ssd1306_set_cursor(0,3);
        ssd1306_printstring(str);
        sprintf(str,"buf: %d",sample_circ_buf_value(0));
        ssd1306_set_cursor(0,4);
        ssd1306_printstring(str);
         sprintf(str,"rate %u",(uint32_t)((((uint64_t)counter)*1000000)/time_us_32()));
        ssd1306_set_cursor(0,5);
        ssd1306_printstring(str);
        ssd1306_render();
        uart0_output((uint8_t *)&counter, 2);
    }
}

const char * const mainmenu[] = { "Adjust", "Pitch", "Debug", "Pedal", "Load", "Save", NULL };

menu_str mainmenu_str = { mainmenu, 0, 2, 15, 0, 0 };

int test_cmd(int args, tinycl_parameter* tp, void *v)
{
  char s[20];
  tinycl_put_string("TEST=");
  sprintf(s,"%d\r\n",counter,counter);
  tinycl_put_string(s);
  return 1;
}

void type_cmd_write(uint unit_no, dsp_unit_type dut)
{
  char s[40];
  if (dut >= DSP_TYPE_MAX_ENTRY) return;
  sprintf(s,"INIT %u %u ",unit_no+1, (uint)dut);
  tinycl_put_string(s);
  tinycl_put_string(dtnames[(uint)dut]);
  tinycl_put_string("\r\n");    
}

int type_cmd(int args, tinycl_parameter* tp, void *v)
{
  uint unit_no=tp[0].ti.i;
  dsp_unit_type dut = DSP_TYPE_NONE;
  
  if (unit_no == 0)
  {
      do
      {
          dut = dsp_unit_get_type(unit_no);
          type_cmd_write(unit_no, dut);
          unit_no++;
      } while (dut < DSP_TYPE_MAX_ENTRY);
      return 1;
  }
  unit_no--;
  dut = dsp_unit_get_type(unit_no);
  if (dut < DSP_TYPE_MAX_ENTRY)
      type_cmd_write(unit_no, dut);
  return 1;
}

int init_cmd(int args, tinycl_parameter* tp, void *v)
{
  uint unit_no=tp[0].ti.i;
  uint type_no=tp[1].ti.i;  
  
  if (unit_no > 0)
  {
    dsp_unit_initialize(unit_no-1, (dsp_unit_type) type_no);
    dsp_unit_reset_all();
  }
  return 1;
}

void conf_entry_print(uint unit_no, const dsp_parm_configuration_entry *dpce)
{
    uint32_t value;
    char s[60];
    sprintf(s,"SET %u ",unit_no+1);
    tinycl_put_string(s);    
    tinycl_put_string(dpce->desc);
    if (!dsp_unit_get_value(unit_no, dpce->desc, &value)) value = 0;
    sprintf(s," %u %u %u\r\n",value,dpce->minval,dpce->maxval);
    tinycl_put_string(s);
}

 const dsp_parm_configuration_entry *conf_entry_lookup_print(uint unit_no, uint entry_no)
 {
    const dsp_parm_configuration_entry *dpce = dsp_unit_get_configuration_entry(unit_no, entry_no);
    if (dpce == NULL) return NULL;
    conf_entry_print(unit_no, dpce);
    return dpce;
 }

int conf_cmd(int args, tinycl_parameter* tp, void *v)
{
  uint unit_no=tp[0].ti.i;
  uint entry_no=tp[1].ti.i;
 
  if (unit_no > 0)
  {
    unit_no--;
    if (entry_no == 0)
    {
        dsp_unit_type dut = dsp_unit_get_type(unit_no);
        type_cmd_write(unit_no, dut);    
        while (conf_entry_lookup_print(unit_no, entry_no) != NULL) entry_no++;
    } else
        conf_entry_lookup_print(unit_no, entry_no-1);
  } else
  {
    for (;;)
    {
       dsp_unit_type dut = dsp_unit_get_type(unit_no);
       if (dut >= DSP_TYPE_MAX_ENTRY) break;
       type_cmd_write(unit_no, dut);    
       entry_no = 0;
       while (conf_entry_lookup_print(unit_no, entry_no) != NULL) entry_no++;
       unit_no++;
    }
  }
  tinycl_put_string("END 0 END\r\n");
  return 1;
}

int set_cmd(int args, tinycl_parameter* tp, void *v)
{
  uint unit_no=tp[0].ti.i;
  const char *parm = tp[1].ts.str;
  uint value=tp[2].ti.i;

  tinycl_put_string((unit_no > 0) && dsp_unit_set_value(unit_no-1, parm, value) ? "Set\r\n" : "Error\r\n");
  return 1;
}

int get_cmd(int args, tinycl_parameter* tp, void *v)
{
  uint unit_no=tp[0].ti.i;
  const char *parm = tp[1].ts.str;
  uint32_t value;
    
  if ((unit_no > 0) && dsp_unit_get_value(unit_no-1, parm, &value))
  {
      char s[40];
      sprintf(s,"%u\r\n",value);
      tinycl_put_string(s);
  } else
      tinycl_put_string("Error\r\n");
  return 1;
}

int save_cmd(int args, tinycl_parameter* tp, void *v)
{
  uint bankno=tp[0].ti.i;
 
  tinycl_put_string((bankno == 0) || flash_save_bank(bankno-1) ? "Not Saved\r\n" : "Saved\r\n");
  return 1;
}

int load_cmd(int args, tinycl_parameter* tp, void *v)
{
  uint bankno=tp[0].ti.i;
 
  tinycl_put_string((bankno == 0) || flash_load_bank(bankno-1) ? "Not Loaded\r\n" : "Loaded\r\n");
  return 1;
}

void copyspaces(char *c, const char *d, int n)
{
    while ((n>0) && (*d != '\000'))
    {
        for (uint i=0;i<(sizeof(validchars)/sizeof(validchars[0]));i++)
            if (((uint8_t) *d) == validchars[i])
            {
                *c++ = *d;
                n--;
                break;
            }
        d++; 
    }
    while (n>0)
    {
        *c++ = ' ';
        n--;
    }
    *c = '\000';
}

int setdesc_cmd(int args, tinycl_parameter* tp, void *v)
{
  const char *parm = tp[0].ts.str;
  
  copyspaces((char *)desc,parm,sizeof(desc)-1);
 
  tinycl_put_string("Description: '");
  tinycl_put_string((const char *)desc);
  tinycl_put_string("'\r\n");
  return 1;
}

int getdesc_cmd(int args, tinycl_parameter* tp, void *v)
{
  tinycl_put_string("Description: '");
  tinycl_put_string((const char *)desc);
  tinycl_put_string("'\r\n");
  return 1;
}

int a_cmd(int args, tinycl_parameter* tp, void *v)
{
    char s[256];
    absolute_time_t at = get_absolute_time();
    uint32_t l = to_us_since_boot(at);
    pitch_edge_autocorrelation();
    at = get_absolute_time();
    uint32_t m = to_us_since_boot(at);
    for (uint i=0;i<pitch_current_entry;i++)
    {
        sprintf(s,"%u edge: neg: %u sample: %d  counter: %d\r\n", i, pitch_edges[i].negative, pitch_edges[i].sample, pitch_edges[i].counter-pitch_edges[0].counter);
        tinycl_put_string(s);
    }
    for (uint i=pitch_autocor_size;i>0;)
    {
        i--;
        sprintf(s,"%u offset: %u autocor: %d", i, pitch_autocor[i].offset, pitch_autocor[i].autocor);
        tinycl_put_string(s);
        sprintf(s,"     %d %d %d\r\n",pitch_autocorrelation_sample(pitch_autocor[i].offset-1),
                                 pitch_autocorrelation_sample(pitch_autocor[i].offset),
                                 pitch_autocorrelation_sample(pitch_autocor[i].offset+1));
        tinycl_put_string(s);
    }
    sprintf(s,"total us: %u\r\n", (uint32_t)(m-l));
    tinycl_put_string(s);
    pitch_buffer_reset();
    return 1;
}

int help_cmd(int args, tinycl_parameter *tp, void *v);

const tinycl_command tcmds[] =
{
  { "GETDESC", "Get description", getdesc_cmd, TINYCL_PARM_END },
  { "SETDESC", "Set description", setdesc_cmd, TINYCL_PARM_STR, TINYCL_PARM_END },
  { "LOAD", "Load configuration", load_cmd, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "SAVE", "Save configuration", save_cmd, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "GET",  "Get configuration entry", get_cmd, TINYCL_PARM_INT, TINYCL_PARM_STR, TINYCL_PARM_END },
  { "SET",  "Set configuration entry", set_cmd, TINYCL_PARM_INT, TINYCL_PARM_STR, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "CONF", "Get configuration list", conf_cmd, TINYCL_PARM_INT, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "INIT", "Set type of effect", init_cmd, TINYCL_PARM_INT, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "TYPE", "Get type of effect", type_cmd, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "A", "Test autocorrelation", a_cmd, TINYCL_PARM_END },
  { "TEST", "Test", test_cmd, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "HELP", "Display This Help", help_cmd, {TINYCL_PARM_END } }
};

int help_cmd(int args, tinycl_parameter *tp, void *v)
{
  tinycl_print_commands(sizeof(tcmds) / sizeof(tinycl_command), tcmds);
  return 1;
}

void test_analog_controls(void)
{
   char s[100];
   for (;;)
   {
    manually_poll_analog_controls();
    clear_display();
    for (int r=0;r<7;r++)
    {
        sprintf(s,"%04d %04d %04d",current_samples[r*3+1],current_samples[r*3+2],current_samples[r*3+3]);
        write_str(0,r,s);
    }
    sprintf(s,"%d",counter);
    write_str(0,7,s);
    display_refresh();    
    idle_task();
   }
}

void test_analog_controls_sticky(void)
{
   char s[100];
   for (;;)
   {
    clear_display();
    for (int r=0;r<7;r++)
    {
        sprintf(s,"%05d%05d%05d",read_potentiometer_value(r*3+4),read_potentiometer_value(r*3+5),read_potentiometer_value(r*3+6));
        write_str(0,r,s);
    }
    sprintf(s,"%d",counter);
    write_str(0,7,s);
    display_refresh();    
    idle_task();
   }
}

void test_controls(void)
{
   char s[20];
   for (;;)
   {
    clear_display();
    for (int a=0;a<32;a++)
        write_str(a & 7, a / 8, button_state[a] ? "1" : "0");
    for (int a=0;a<8;a++)
        write_str(a & 7, 4, get_scan_button(a) ? "1" : "0");
    sprintf(s,"%d",counter);
    write_str(0,7,s);
    display_refresh();    
    idle_task();
   }
}

int main()
{
    initialize_uart();
    usb_init();    
    //stdio_init_all();
    initialize_dsp();
    initialize_pitch();
    initialize_gpio();
    buttons_initialize();
    initialize_pwm();
    ssd1306_Initialize();
    initialize_adc();
    initialize_periodic_alarm(NULL);
    flash_load_most_recent();
    
    //test_analog_controls();
    //test_analog_controls_sticky();
    for (;;)
    {
        clear_display();
        write_str(0,0,"TrebleSynth");
        write_str_with_spaces(0,1,(char *)desc,15);
        pedal_display_state();
        do_show_menu_item(&mainmenu_str);
        buttons_clear();
        for (;;)
        {
            if (tinycl_task(sizeof(tcmds) / sizeof(tinycl_command), tcmds, NULL))
            {
                tinycl_do_echo = 1;
                tinycl_put_string("> ");
            }
            pedal_switch();
            idle_task();
            uint val = do_menu(&mainmenu_str);
            if ((val == 2) || (val == 3)) break;
        }
        switch (mainmenu_str.item)
        {
            case 0:  adjust_dsp_params();
                     break;
            case 1:  pitch_measure();
                     break;
            case 2:  debugstuff();
                     break;
            case 3:  pedal_control_cmd();
                     break;
            case 4:  flash_load();
                     break;
            case 5:  flash_save();
                     break;
        }
    }
}

