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
#include "synth.h"
#include "ui.h"
#include "tinycl.h"

#include "usbmain.h"

#define NUMBER_OF_CONTROLS 24
#define CONTROL_VALUE_LENGTH 16

uint16_t current_samples[NUMBER_OF_CONTROLS];
uint16_t samples[NUMBER_OF_CONTROLS];
uint16_t changed_samples[NUMBER_OF_CONTROLS];
uint16_t last_samples[NUMBER_OF_CONTROLS];
bool sample_changed[NUMBER_OF_CONTROLS];
bool control_direction[NUMBER_OF_CONTROLS];
bool control_enabled[NUMBER_OF_CONTROLS];

char control_value[NUMBER_OF_CONTROLS][CONTROL_VALUE_LENGTH];

uint current_input;
uint control_sample_no;

const int potentiometer_mapping[POTENTIOMETER_MAX] = { 1,2,3,4,6,7,5,10,9,8,11,12,14,15,13,18,17,16,19,20,22,23,21 };

uint32_t mag_avg;

uint button_state[NUM_BUTTONS];
const uint button_ind[NUM_BUTTONS] = { 29,25,30,28,31,27,26,  2,1,0,3,4,6,7,5,  10,9,8,11,12,14,15,13, 18,17,16,19,20,22,23,21,24   };
void initialize_video(void);
void halt_video(void);

#define UNCLAIMED_ALARM 0xFFFFFFFF

uint dac_pwm_b3_slice_num, dac_pwm_b2_slice_num, dac_pwm_b1_slice_num, dac_pwm_b0_slice_num;
uint dac_pwm_a3_slice_num, dac_pwm_a2_slice_num, dac_pwm_a1_slice_num, dac_pwm_a0_slice_num;
uint claimed_alarm_num = UNCLAIMED_ALARM;

volatile uint32_t counter = 0;

bool pause_poll_keyboard = false;
bool pause_poll_controls = false;

void update_control_values(void);

project_configuration pc;

const project_configuration pc_default =
{
  PROJECT_MAGIC_NUMBER,  /* magic_number */
  48,                    /* transpose value */
  15000,                 /* fail delay */
};

void initialize_project_configuration(void)
{
    if (pc.magic_number != PROJECT_MAGIC_NUMBER)
        memcpy((void *)&pc, (void *)&pc_default, sizeof(pc));
}

void keyboard_poll(void)
{
    uint8_t button_no;
    
    if (pause_poll_keyboard) return;
    while ((button_no=button_get_state_changed()) != BUTTONS_NO_CHANGE)
    {
        uint8_t button_on_off = (button_no & 0x80);
        button_no &= 0x7F;
        if (button_no > 6)
            midi_button_event(button_no-7, button_on_off);
    }
}

void controls_poll(void)
{
    int val, pos;
    if (pause_poll_controls) return;
    for (uint c=1;c<((sizeof(potentiometer_mapping)/sizeof(potentiometer_mapping[0])));c++)
    {
        int m = potentiometer_mapping[c-1];
        if ((control_value[c][0] != '\000') && (control_value[c][0] != ' ') && (sample_changed[m]))
        {
            write_str_with_spaces(0,7,control_enabled[m] ? "*" : (control_direction[m] ? "<" : ">"),1);
            write_str_with_spaces(1,7,control_value[c],14);

            pos = 3;
            val = current_samples[m];
            val = val+val/2;
            while (val >= (ADC_MAX_VALUE/8))
            {
                ssd1306_writecharXOR(pos++,7,0x8);
                val -= (ADC_MAX_VALUE/8);
            }
            ssd1306_writecharXOR(pos++,7,(val*8)/(ADC_MAX_VALUE/8));
            sample_changed[m] = false;
            display_refresh();
            return;
        }
    }
}

#define UART_FIFO_SIZE 256

typedef struct _uart_fifo
{
    uint head;
    uint tail;
    uint8_t buf[UART_FIFO_SIZE];
} uart_fifo;

uart_fifo uart_fifo_input;
uart_fifo uart_fifo_output;
critical_section uart_cs;

void initialize_uart_fifo(uart_fifo *uf)
{
    critical_section_enter_blocking(&uart_cs);
    uf->head = uf->tail = 0;
    critical_section_exit(&uart_cs);
}

void insert_into_uart_fifo(uart_fifo *uf, uint8_t ch)
{
    critical_section_enter_blocking(&uart_cs);
    uint nexthead = uf->head >= (UART_FIFO_SIZE-1) ? 0 : (uf->head+1);
    if (nexthead != uf->tail)
    {
        uf->buf[uf->head] = ch;
        uf->head = nexthead;
    }
    critical_section_exit(&uart_cs);
}

int remove_from_uart_fifo(uart_fifo *uf)
{
    int ret;
    critical_section_enter_blocking(&uart_cs);
    if (uf->tail == uf->head) 
        ret = -1;
    else 
    {
        ret = uf->buf[uf->tail];
        uf->tail = uf->tail >= (UART_FIFO_SIZE-1) ? 0 : (uf->tail+1);
    }
    critical_section_exit(&uart_cs);
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
    critical_section_init(&uart_cs);
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
    usb_task();
    midi_uart_poll();
    buttons_poll();
    keyboard_poll();
    controls_poll();
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
   select_control(0,0);
}

void reset_control_samples(uint16_t *reset_samples)
{
    memset(sample_changed,'\000',sizeof(sample_changed));
    if (reset_samples == NULL) 
    {
        for (uint n=0;n<NUMBER_OF_CONTROLS;n++)
            control_enabled[n] = true;
        return;
    }        
    memcpy(last_samples, reset_samples, sizeof(last_samples));
    manually_poll_analog_controls();
    for (uint n=0;n<NUMBER_OF_CONTROLS;n++)
    {
        control_enabled[n] = false;
        control_direction[n] = current_samples[n] < last_samples[n];
        samples[n] = last_samples[n];
    }
}

inline void poll_controls(void)
{
   uint current_sample_no = current_input+control_sample_no*8;
   uint16_t sample = adc_hw->result;

   if ( (sample > (current_samples[current_sample_no] + POTENTIOMETER_VALUE_SENSITIVITY)) || 
        (current_samples[current_sample_no] > (sample + POTENTIOMETER_VALUE_SENSITIVITY)) )
   {
       sample_changed[current_sample_no] = true;
       current_samples[current_sample_no] = sample;
   }

   if (control_enabled[current_sample_no])
       samples[current_sample_no] = sample;
   else
   {
       if ( ((control_direction[current_sample_no]) && (sample >= last_samples[current_sample_no])) ||
            ((!control_direction[current_sample_no]) && (sample <= last_samples[current_sample_no])) )
            {
                samples[current_sample_no] = sample;
                control_enabled[current_sample_no] = true;
            }
   }
   
   button_state[current_input] = gpio_get(GPIO_BUTTON1);
   button_state[current_input+8] = gpio_get(GPIO_BUTTON2);
   button_state[current_input+16] = gpio_get(GPIO_BUTTON3);
   button_state[current_input+24] = gpio_get(GPIO_BUTTON4);
   
   current_input = (current_input + 1) & 0x07;
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
    int32_t s = synth_process_all_units() / (MAX_POLYPHONY/2);
    if (s < (-QUANTIZATION_MAX)) s = -QUANTIZATION_MAX;
    if (s > (QUANTIZATION_MAX-1)) s = QUANTIZATION_MAX-1;
    next_sample = (s + QUANTIZATION_MAX) / ((QUANTIZATION_MAX*2) / DAC_PWM_WRAP_VALUE);
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
    reset_control_samples(reset_samples);
    hardware_alarm_set_callback(claimed_alarm_num, alarm_func);
    last_time = make_timeout_time_us(1000);
    absolute_time_t next_alarm_time = update_next_timeout(last_time, 0, 8);
    //absolute_time_t next_alarm_time = make_timeout_time_us(1000);
    hardware_alarm_set_target(claimed_alarm_num, next_alarm_time);
}

void reset_load_controls(void)
{
    update_control_values();
    reset_periodic_alarm(samples);
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

void adjust_dsp_parms_unit(uint8_t unit_no)
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
           adjust_dsp_parms_unit(unit_no);
           redraw = 1;
        }
    }
}

void adjust_synth_parms_unit(uint8_t unit_no)
{
    int sel = 0, redraw = 1;
    button_clear();
    for (;;)
    {
        const synth_parm_configuration_entry *sp = spce[synth_parms[unit_no].stn.sut];
        idle_task();
        if (redraw)
        {
            if (sel == 0)
            {
                write_str_with_spaces(0,2,"Type",16);
                write_str_with_spaces(0,3,stnames[synth_parms[unit_no].stn.sut],16);
            } else
            {
                char s[20];
                write_str_with_spaces(0,2,sp[sel-1].desc,16);
                char *c = number_str(s, 
                    synth_read_value_prec((void *)(((uint8_t *)&synth_parms[unit_no]) + sp[sel-1].offset), sp[sel-1].size), 
                    sp[sel-1].digits, 0);
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
            if (sp[sel].desc != NULL)
            {
                sel++;
                redraw = 1;
            }
        } else if (button_right() || button_enter())
        {
            if (sel == 0)
            {
                write_str_with_spaces(0,2,"Type select",16);
                menu_str mst = { stnames,0,3,10,0,0 };
                mst.item = mst.itemesc = synth_parms[unit_no].stn.sut;
                int res;
                do_show_menu_item(&mst);
                do
                {
                    idle_task();
                    res = do_menu(&mst);
                } while (res == 0);
                if (res == 3)
                {
                    if (mst.item != synth_parms[unit_no].stn.sut)
                    {
                        synth_unit_initialize(unit_no, (synth_unit_type)mst.item);
                        synth_unit_reset_all();
                    }
                }
            } else
            {
                uint8_t control = sp[sel-1].controldesc != NULL;
                scroll_number_dat snd = { 0, 3, 
                                          sp[sel-1].digits,
                                          0,
                                          sp[sel-1].minval,
                                          sp[sel-1].maxval,
                                          control,
                                          0,
                                          synth_read_value_prec((void *)(((uint8_t *)&synth_parms[unit_no]) + sp[sel-1].offset), sp[sel-1].size),
                                          0, 0 };
                scroll_number_start(&snd);
                if (control) pause_poll_keyboard = true;
                buttons_clear();
                do
                {
                    idle_task();
                    scroll_number_key(&snd);
                } while (!snd.entered);
                pause_poll_keyboard = false;
                buttons_clear();
                if (snd.changed)
                {
                   synth_set_value_prec((void *)(((uint8_t *)&synth_parms[unit_no]) + sp[sel-1].offset), sp[sel-1].size, snd.n);
                   update_control_values();
                }
            }
            redraw = 1;
        }
    }
}

void adjust_synth_params(void)
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
            write_str(0,0,"Synth Adj");
            sprintf(s,"Unit #%d", unit_no+1);
            write_str_with_spaces(0,1,s,16);
            write_str_with_spaces(0,2,stnames[synth_parms[unit_no].stn.sut],16);
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
        } else if (button_up() && (unit_no < (MAX_SYNTH_UNITS-1)))
        {
            unit_no++;
            redraw = 1;
        } else if (button_right() || button_enter())
        {
           sprintf(s,"Unit #%d select", unit_no+1);
           write_str_with_spaces(0,1,s,16);
           adjust_synth_parms_unit(unit_no);
           redraw = 1;
        }
    }
}

#define FLASH_BANKS 10
#define FLASH_PAGE_BYTES 4096u
#define FLASH_OFFSET_STORED (2*1024*1024)
#define FLASH_BASE_ADR 0x10000000
#define FLASH_MAGIC_NUMBER 0xFEE1FED4

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
    project_configuration pc;
    uint16_t samples[NUMBER_OF_CONTROLS];
    dsp_parm dsp_parms[MAX_DSP_UNITS];
    synth_parm synth_parms[MAX_SYNTH_UNITS];
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
        memcpy((void *)&pc, (void *) &fl->fld.pc, sizeof(pc));
        memcpy((void *)samples, (void *) &fl->fld.samples, sizeof(samples));
        memcpy((void *)dsp_parms, (void *) &fl->fld.dsp_parms, sizeof(dsp_parms));
        memcpy((void *)synth_parms, (void *) &fl->fld.synth_parms, sizeof(synth_parms));
        initialize_project_configuration();
        dsp_unit_reset_all();
        synth_unit_reset_all();
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
    if (flash_load_bank(bankno-1))
    {
        message_to_display("Not Loaded");
    } else
    {
        reset_load_controls();
        message_to_display("Loaded");
    }
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
    if (load_bankno < FLASH_BANKS) 
    {
        if (!flash_load_bank(load_bankno))
            reset_load_controls();
    }
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
    memcpy(fl->fld.samples, samples, sizeof(fl->fld.samples));
    memcpy((void *)&fl->fld.pc, (void *)&pc, sizeof(fl->fld.pc));
    memcpy((void *)&fl->fld.dsp_parms, (void *)dsp_parms, sizeof(fl->fld.dsp_parms));
    memcpy((void *)&fl->fld.synth_parms, (void *)synth_parms, sizeof(fl->fld.synth_parms));
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

static int32_t dv1=0, dv2=0, dv3=0;

void set_debug_vals(int32_t v1, int32_t v2, int32_t v3)
{
    dv1=v1;dv2=v2;dv3=v3;
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
        sprintf(str,"v1=%d",dv1);
        ssd1306_set_cursor(0,0);
        ssd1306_printstring(str);
        sprintf(str,"v2=%d",dv2);
        ssd1306_set_cursor(0,1);
        ssd1306_printstring(str);
        sprintf(str,"v3=%d",dv3);
        ssd1306_set_cursor(0,2);
        ssd1306_printstring(str);
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
    }
}

const char * const mainmenu[] = { "SynthAdj", "FiltAdjust", "Debug", "Pedal", "Load", "Save", "Conf", NULL };

menu_str mainmenu_str = { mainmenu, 0, 2, 15, 0, 0 };

int test_cmd(int args, tinycl_parameter* tp, void *v)
{
  char s[20];
  tinycl_put_string("TEST=");
  sprintf(s,"%d\r\n",counter,counter);
  tinycl_put_string(s);
  return 1;
}

void e_type_cmd_write(uint unit_no, dsp_unit_type dut)
{
  char s[40];
  if (dut >= DSP_TYPE_MAX_ENTRY) return;
  sprintf(s,"EINIT %u %u ",unit_no+1, (uint)dut);
  tinycl_put_string(s);
  tinycl_put_string(dtnames[(uint)dut]);
  tinycl_put_string("\r\n");    
}

int etype_cmd(int args, tinycl_parameter* tp, void *v)
{
  uint unit_no=tp[0].ti.i;
  dsp_unit_type dut = DSP_TYPE_NONE;
  
  if (unit_no == 0)
  {
      do
      {
          dut = dsp_unit_get_type(unit_no);
          e_type_cmd_write(unit_no, dut);
          unit_no++;
      } while (dut < DSP_TYPE_MAX_ENTRY);
      return 1;
  }
  unit_no--;
  dut = dsp_unit_get_type(unit_no);
  if (dut < DSP_TYPE_MAX_ENTRY)
      e_type_cmd_write(unit_no, dut);
  return 1;
}

int einit_cmd(int args, tinycl_parameter* tp, void *v)
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

void e_conf_entry_print(uint unit_no, const dsp_parm_configuration_entry *dpce)
{
    uint32_t value;
    char s[60];
    sprintf(s,"ESET %u ",unit_no+1);
    tinycl_put_string(s);    
    tinycl_put_string(dpce->desc);
    if (!dsp_unit_get_value(unit_no, dpce->desc, &value)) value = 0;
    sprintf(s," %u %u %u\r\n",value,dpce->minval,dpce->maxval);
    tinycl_put_string(s);
}

 const dsp_parm_configuration_entry *e_conf_entry_lookup_print(uint unit_no, uint entry_no)
 {
    const dsp_parm_configuration_entry *dpce = dsp_unit_get_configuration_entry(unit_no, entry_no);
    if (dpce == NULL) return NULL;
    e_conf_entry_print(unit_no, dpce);
    return dpce;
 }

int econf_cmd(int args, tinycl_parameter* tp, void *v)
{
  uint unit_no=tp[0].ti.i;
  uint entry_no=tp[1].ti.i;
 
  if (unit_no > 0)
  {
    unit_no--;
    if (entry_no == 0)
    {
        dsp_unit_type dut = dsp_unit_get_type(unit_no);
        e_type_cmd_write(unit_no, dut);    
        while (e_conf_entry_lookup_print(unit_no, entry_no) != NULL) entry_no++;
    } else
        e_conf_entry_lookup_print(unit_no, entry_no-1);
  } else
  {
    for (;;)
    {
       dsp_unit_type dut = dsp_unit_get_type(unit_no);
       if (dut >= DSP_TYPE_MAX_ENTRY) break;
       e_type_cmd_write(unit_no, dut);    
       entry_no = 0;
       while (e_conf_entry_lookup_print(unit_no, entry_no) != NULL) entry_no++;
       unit_no++;
    }
  }
  tinycl_put_string("END 0 END\r\n");
  return 1;
}

int eset_cmd(int args, tinycl_parameter* tp, void *v)
{
  uint unit_no=tp[0].ti.i;
  const char *parm = tp[1].ts.str;
  uint value=tp[2].ti.i;

  tinycl_put_string((unit_no > 0) && dsp_unit_set_value(unit_no-1, parm, value) ? "Set\r\n" : "Error\r\n");
  return 1;
}

int eget_cmd(int args, tinycl_parameter* tp, void *v)
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

void s_type_cmd_write(uint unit_no, synth_unit_type sut)
{
  char s[40];
  if (sut >= SYNTH_TYPE_MAX_ENTRY) return;
  sprintf(s,"SINIT %u %u ",unit_no+1, (uint)sut);
  tinycl_put_string(s);
  tinycl_put_string(stnames[(uint)sut]);
  tinycl_put_string("\r\n");    
}

int stype_cmd(int args, tinycl_parameter* tp, void *v)
{
  uint unit_no=tp[0].ti.i;
  synth_unit_type sut = SYNTH_TYPE_NONE;
  
  if (unit_no == 0)
  {
      do
      {
          sut = synth_unit_get_type(unit_no);
          s_type_cmd_write(unit_no, sut);
          unit_no++;
      } while (sut < SYNTH_TYPE_MAX_ENTRY);
      return 1;
  }
  unit_no--;
  sut = synth_unit_get_type(unit_no);
  if (sut < SYNTH_TYPE_MAX_ENTRY)
      s_type_cmd_write(unit_no, sut);
  return 1;
}

int sinit_cmd(int args, tinycl_parameter* tp, void *v)
{
  uint unit_no=tp[0].ti.i;
  uint type_no=tp[1].ti.i;  
  
  if (unit_no > 0)
  {
    synth_unit_initialize(unit_no-1, (synth_unit_type) type_no);
    synth_unit_reset_all();
  }
  return 1;
}

void s_conf_entry_print(uint unit_no, const synth_parm_configuration_entry *spce)
{
    uint32_t value;
    char s[60];
    sprintf(s,"SSET %u ",unit_no+1);
    tinycl_put_string(s);    
    tinycl_put_string(spce->desc);
    if (!synth_unit_get_value(unit_no, spce->desc, &value)) value = 0;
    sprintf(s," %u %u %u\r\n",value,spce->minval,spce->maxval);
    tinycl_put_string(s);
}

const synth_parm_configuration_entry *s_conf_entry_lookup_print(uint unit_no, uint entry_no)
 {
    const synth_parm_configuration_entry *spce = synth_unit_get_configuration_entry(unit_no, entry_no);
    if (spce == NULL) return NULL;
    s_conf_entry_print(unit_no, spce);
    return spce;
 }

void update_control_values(void)
{
    int unit_no = 0, entry_no;
    uint32_t value;
    
    memset(control_value,'\000',sizeof(control_value));
    for (;;)
    {
        synth_unit_type sut = synth_unit_get_type(unit_no);
        if (sut >= SYNTH_TYPE_MAX_ENTRY) break;
        entry_no = 0;
        for (;;)
        {
          const synth_parm_configuration_entry *spce = synth_unit_get_configuration_entry(unit_no, entry_no);
          if (spce == NULL) break;
          if (spce->controldesc != NULL)
          {
            if (synth_unit_get_value(unit_no, spce->desc, &value))
            {
                if ((value >= 1) && (value <= ((sizeof(potentiometer_mapping)/sizeof(potentiometer_mapping[0])))))
                {
                   char s[CONTROL_VALUE_LENGTH+1];
                   snprintf(s, CONTROL_VALUE_LENGTH, "%02d %s", unit_no+1, spce->controldesc);
                   memcpy(control_value[value], s, CONTROL_VALUE_LENGTH);
                }
            }
          }
          entry_no++;
        }
        unit_no++;
    }
}

int sconf_cmd(int args, tinycl_parameter* tp, void *v)
{
  uint unit_no=tp[0].ti.i;
  uint entry_no=tp[1].ti.i;
 
  if (unit_no > 0)
  {
    unit_no--;
    if (entry_no == 0)
    {
        synth_unit_type sut = synth_unit_get_type(unit_no);
        s_type_cmd_write(unit_no, sut);    
        while (s_conf_entry_lookup_print(unit_no, entry_no) != NULL) entry_no++;
    } else
        s_conf_entry_lookup_print(unit_no, entry_no-1);
  } else
  {
    for (;;)
    {
       synth_unit_type sut = synth_unit_get_type(unit_no);
       if (sut >= SYNTH_TYPE_MAX_ENTRY) break;
       s_type_cmd_write(unit_no, sut);    
       entry_no = 0;
       while (s_conf_entry_lookup_print(unit_no, entry_no) != NULL) entry_no++;
       unit_no++;
    }
  }
  tinycl_put_string("END 0 END\r\n");
  return 1;
}

int sset_cmd(int args, tinycl_parameter* tp, void *v)
{
  uint unit_no=tp[0].ti.i;
  const char *parm = tp[1].ts.str;
  uint value=tp[2].ti.i;

  bool isnoterr = (unit_no > 0) && synth_unit_set_value(unit_no-1, parm, value);
  tinycl_put_string(isnoterr ? "Set\r\n" : "Error\r\n");
  if (isnoterr) update_control_values();
  return 1;
}

int sget_cmd(int args, tinycl_parameter* tp, void *v)
{
  uint unit_no=tp[0].ti.i;
  const char *parm = tp[1].ts.str;
  uint32_t value;
    
  if ((unit_no > 0) && synth_unit_get_value(unit_no-1, parm, &value))
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

int help_cmd(int args, tinycl_parameter *tp, void *v);

const tinycl_command tcmds[] =
{
  { "GETDESC", "Get description", getdesc_cmd, TINYCL_PARM_END },
  { "SETDESC", "Set description", setdesc_cmd, TINYCL_PARM_STR, TINYCL_PARM_END },
  { "LOAD", "Load configuration", load_cmd, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "SAVE", "Save configuration", save_cmd, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "EGET",  "Get effect configuration entry", eget_cmd, TINYCL_PARM_INT, TINYCL_PARM_STR, TINYCL_PARM_END },
  { "ESET",  "Set effect configuration entry", eset_cmd, TINYCL_PARM_INT, TINYCL_PARM_STR, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "ECONF", "Get effect configuration list", econf_cmd, TINYCL_PARM_INT, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "EINIT", "Set effect type", einit_cmd, TINYCL_PARM_INT, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "ETYPE", "Get effect type", etype_cmd, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "SGET",  "Get synth configuration entry", sget_cmd, TINYCL_PARM_INT, TINYCL_PARM_STR, TINYCL_PARM_END },
  { "SSET",  "Set synth configuration entry", sset_cmd, TINYCL_PARM_INT, TINYCL_PARM_STR, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "SCONF", "Get synth configuration list", sconf_cmd, TINYCL_PARM_INT, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "SINIT", "Set synth type", sinit_cmd, TINYCL_PARM_INT, TINYCL_PARM_INT, TINYCL_PARM_END },
  { "STYPE", "Get synth type", stype_cmd, TINYCL_PARM_INT, TINYCL_PARM_END },
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

const char *const confmenu[] = {"Quit", "Transpose", "FailDelay", NULL };

typedef struct _configuration_entry
{
  void     *entry;
  uint8_t   bytes;
  uint8_t   digits;
  uint32_t  min_value;
  uint32_t  max_value;
} configuration_entry;

const configuration_entry configuration_entries[] = 
{
  { &pc.note_transpose,             1, 2, 0, 95 },    /* NOTE TRANSPOSE */
  { &pc.fail_delay,                 4, 5, 1, 99999 }  /* FAIL DELAY */
};

void configuration(void)
{
  clear_display();
  uint8_t selected;

  write_str(0,2,"Configuration");
  menu_str mn = { confmenu, 0, 3, 16, 0, 0 };
  do_show_menu_item(&mn);
  for (;;)
  {
    const configuration_entry *c = NULL;
    scroll_number_dat snd = { 0, 4, 0, 0, 0, 0, 0, 0, 0, 0 };
    int last_item = -1;
    do
    {
      idle_task();
      selected = do_menu(&mn);
      if (mn.item != last_item)
      {
          last_item = mn.item;
          if (mn.item == 0)
          {
             c = NULL;
             write_str_with_spaces(0, 4, "", 16);
          } else
          {
             char s[20];
             c = &configuration_entries[mn.item-1];
             switch (c->bytes)
             {
                case 1: snd.n = *((uint8_t *)c->entry);
                        break;
                case 2: snd.n = *((uint16_t *)c->entry);
                        break;
                case 4: snd.n = *((uint32_t *)c->entry);
                        break;
             } 
             char *d = number_str(s, snd.n, c->digits, 0);
             write_str_with_spaces(0, 4, d, 16);
          }
          display_refresh();
      }
    } while (!selected);
    if (mn.item == 0)  break;
    snd.digits = c->digits;
    snd.minimum_number = c->min_value;
    snd.maximum_number = c->max_value;
    scroll_number_start(&snd);
    while (!snd.entered)
    {
      idle_task();
      scroll_number_key(&snd);
    }
    if (snd.changed)
    {
      switch (c->bytes)
      {
        case 1: *((uint8_t *)c->entry) = snd.n;
                break;
        case 2: *((uint16_t *)c->entry) = snd.n;
                break;
        case 4: *((uint32_t *)c->entry) = snd.n;
                break;
      }
    } 
  }
}

int main()
{
    sleep_us(250000u);
    set_sys_clock_khz(250000u, true);
    initialize_uart();
    usb_init();    
    initialize_project_configuration();
    initialize_dsp();
    synth_initialize();
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
            case 0:  adjust_synth_params();
                     break;
            case 1:  adjust_dsp_params();
                     break;
            case 2:  debugstuff();
                     break;
            case 3:  pedal_control_cmd();
                     break;
            case 4:  flash_load();
                     break;
            case 5:  flash_save();
                     break;
            case 6:  configuration();
                     break;
        }
    }
}

