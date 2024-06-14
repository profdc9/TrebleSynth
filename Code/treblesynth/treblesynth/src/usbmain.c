/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "treblesynth.h"
#include "hardware/timer.h"
#include "tusb.h"
#include "usbmain.h"
#include "main.h"
#include "synth.h"

//------------- prototypes -------------//
void cdc_task(void);

/*------------- MAIN -------------*/
int usb_init(void)
{
  // init device stack on configured roothub port
  tud_init(BOARD_TUD_RHPORT);

  return 0;
}

static uint usb_did_write = 0;
static uint32_t usb_write_last_flush = 0;


void usb_write_char(uint8_t ch)
{
    while ((tud_cdc_n_connected(0)) && (!tud_cdc_n_write_available(0))) 
        usb_task();
    if (tud_cdc_n_connected(0))
    {
        
        tud_cdc_n_write_char(0, ch);
        if (tud_cdc_n_write_available(0) == 0)
        {
            tud_cdc_n_write_flush(0);
            usb_write_last_flush = time_us_32();
            usb_did_write = 0;
        } else usb_did_write = 1;
    }
}

int usb_read_character(void)
{
    if (!tud_cdc_n_connected(0)) return -1;
    return tud_cdc_n_read_char(0);
}

//--------------------------------------------------------------------+
// USB CDC
//--------------------------------------------------------------------+
void cdc_task(void)
{
    if (!tud_cdc_n_connected(0))
        return;
    if (usb_did_write)
    {
        uint32_t microtime = time_us_32();
        if ((microtime - usb_write_last_flush) > 1000u)
        {
            tud_cdc_n_write_flush(0);
            usb_write_last_flush = microtime;
            usb_did_write = 0;
        }
    }      
}

void midi_send_note(uint8_t note, uint8_t velocity)
{
    static uint8_t last_note = 0;

    uint8_t msg[3];

    if (note > 0)
    {
        msg[0] = 0x90;                    // Note On - Channel 1
        msg[1] = note;                    // Note Number
        msg[2] = velocity;                // Velocity
        tud_midi_n_stream_write(0, 0, msg, 3);
        uart0_output(msg, 3);
    }
    if (last_note > 0)
    {
        msg[0] = 0x80;                    // Note Off - Channel 1
        msg[1] = last_note;               // Note Number
        msg[2] = 0;                       // Velocity
        tud_midi_n_stream_write(0, 0, msg, 3);
        uart0_output(msg, 3);
    }
    last_note = note;
}

bool midi_perform_event(const uint8_t cmdbuf[], int num)
{
   bool ex = false;
   int cmdprefix = cmdbuf[0] & 0xF0;
   if (num > 2)
   {
       switch (cmdprefix)
       {
            case 0x90:  gpio_put(25,1);
                        synth_start_note(cmdbuf[1],cmdbuf[2]);
                        ex = true;
                        break;
            case 0x80:  gpio_put(25,0);
                        synth_stop_note(cmdbuf[1],cmdbuf[2]);
                        ex = true;
                        break;
            case 0xE0:  {
                           uint32_t pitch_bend = ((int32_t)(cmdbuf[1] & 0x7F)) + (((int32_t)(cmdbuf[2] & 0x7F)) * 128);
                           synth_set_pitch_bend_value(pitch_bend);
                        }
                        break;
       }
   }
   return ex;
}

void midi_button_event(uint8_t note, uint8_t on_off)
{
    uint8_t msg[3];

    if (on_off)
    {
        msg[0] = 0x90;                    // Note On - Channel 1
        msg[1] = note+pc.note_transpose;  // Note Number
        msg[2] = 127;                     // Velocity
        tud_midi_n_stream_write(0, 0, msg, 3);
        uart0_output(msg, 3);
        midi_perform_event(msg, 3);
    } else
    {
        msg[0] = 0x80;                    // Note Off - Channel 1
        msg[1] = note+pc.note_transpose;  // Note Number
        msg[2] = 127;                     // Velocity
        tud_midi_n_stream_write(0, 0, msg, 3);
        uart0_output(msg, 3);
        midi_perform_event(msg, 3);
    }
}

void midi_uart_poll(void)
{
    static int num = 0;
    static uint8_t cmdbuf[3];
    
    int ch = uart0_input();
    
    if (ch < 0) return;
    if (ch & 0x80) num = 0;
    if (num < (sizeof(cmdbuf)/sizeof(cmdbuf[0])))
        cmdbuf[num++] = ch;
    if ((num >= 2) && (midi_perform_event(cmdbuf, num))) num = 0;
}

void midi_task(void)
{
    uint8_t cmdbuf[4];
    while ( tud_midi_n_available(0,0) ) 
    {
        tud_midi_n_packet_read(0,cmdbuf);
        midi_perform_event(&cmdbuf[1],3);
    }
}

void usb_task(void)
{
    tud_task();
    cdc_task();
    midi_task();
}
