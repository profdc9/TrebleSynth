
#include <stdint.h>
#include "main.h"
#include "treblesynth.h"
#include "buttons.h"
#include "hardware/timer.h"

static bool _button_pressed[NUM_BUTTONS];
static bool _state_changed[NUM_BUTTONS];
static uint8_t _state_count[NUM_BUTTONS];
static uint8_t _last_state[NUM_BUTTONS];
static uint8_t _repeated_state[NUM_BUTTONS];

const uint8_t repeat_states_init_ctr = 40;
const uint8_t repeat_states_next_ctr = 20;

void buttons_initialize(void)
{
}

void buttons_clear()
{
  for (uint8_t i=0; i<NUM_BUTTONS; i++)
  {
    _button_pressed[i] = false;
    _state_changed[i] = false;
  }
}

void buttons_poll()
{
  static uint32_t last_us;
  uint32_t current_us = time_us_32();

  if ((current_us - last_us) < 5000) return;
  last_us = current_us;
   
  for (uint8_t i=0; i<NUM_BUTTONS; i++)
  {
     uint8_t state = get_scan_button(i) != 0;
     if (state == _last_state[i])
     {
        _state_count[i] = 0;
        if (!state)
        {
          if ((--_repeated_state[i]) == 0)
          {
            _repeated_state[i] = repeat_states_next_ctr;
            _button_pressed[i] = true;
          }
        }
     } else 
     {  if (_state_count[i] > 10)
        {
           _state_count[i] = 0;
           _state_changed[i] = true;
           _repeated_state[i] = repeat_states_init_ctr;
           _last_state[i] = state;
           if ((!state) && (!_button_pressed[i]))
              _button_pressed[i] = true;
        } else _state_count[i]++;
     }
  }
}

bool button_getpressed(uint8_t b)
{
  bool state = _button_pressed[b];
  if (state) _button_pressed[b] = false;
  return state;
}

bool button_waitpressed(uint8_t b)
{
  bool state = button_getpressed(b);
  if (!state)
  {
    while (!(_last_state[b]))
      buttons_poll();
  }
  return state;
}

uint8_t button_readunbounced(uint8_t b)
{
  return (get_scan_button(b) == 0);
}

uint8_t button_readbutton(uint8_t b)
{
  return !(_last_state[b]);
}

uint8_t button_get_state_changed(void)
{
  for (uint8_t i=0; i<NUM_BUTTONS; i++)
  {
     if (_state_changed[i])
     {
         _state_changed[i] = false;
         return _last_state[i] ? i : (i | 0x80);
     }
  }
  return BUTTONS_NO_CHANGE;
}