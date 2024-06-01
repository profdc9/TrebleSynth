/* synth.h

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

#ifndef __SYNTH_H
#define __SYNTH_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "dsp.h"

#define MAX_POLYPHONY 8
#define MAX_SYNTH_UNITS 16


typedef enum 
{
    SYNTH_TYPE_NONE = 0,
    SYNTH_TYPE_SINE_SYNTH,
    SYNTH_TYPE_MAX_ENTRY
} synth_unit_type;

typedef struct
{
    synth_unit_type  sut;
    uint32_t source_unit;
    uint32_t control_unit;
} synth_parm_none;

typedef struct
{
    uint32_t notused;
} synth_type_none;

typedef struct
{
    synth_unit_type  sut;
    uint32_t source_unit;
    uint32_t control_unit;
    uint32_t mixval;
    uint32_t frequency[3];
    uint32_t amplitude[3];
    uint32_t control_number1;
    uint32_t control_number2;
} synth_parm_sine_synth;

typedef struct
{
    uint32_t last_frequency[3];
    uint32_t sine_counter[3];
    uint32_t sine_counter_inc[3];
    uint32_t pot_value1;
    uint32_t pot_value2;
} synth_type_sine_synth;

typedef union 
{
    synth_type_none         stn;
    synth_type_sine_synth   stss;
} synth_unit;

typedef union 
{
    synth_parm_none         stn;
    synth_parm_sine_synth   stss;
} synth_parm;

typedef bool    (synth_type_initialize)(void *initialization_data, synth_unit *su);
typedef int32_t (synth_type_process)(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su);

int32_t synth_process_all_units(void);
void synth_unit_struct_zero(synth_unit *su);
void synth_unit_initialize(int synth_unit_number, synth_unit_type dut);

extern const void * const synth_parm_struct_defaults[];
extern synth_parm synth_parms[MAX_DSP_UNITS];
extern synth_unit synth_units[MAX_POLYPHONY][MAX_DSP_UNITS];

inline synth_unit *synth_unit_entry(uint m, uint e)
{
    return &synth_units[m][e];
}

inline synth_parm *synth_parm_entry(uint e)
{
    return &synth_parms[e];
}

typedef struct
{
    const char *desc;
    uint32_t   offset;
    uint8_t    size;
    uint8_t    digits;
    uint32_t   minval;
    uint32_t   maxval;
    const char *controldesc;
} synth_parm_configuration_entry;

bool synth_unit_set_value(uint synth_unit_number, const char *desc, uint32_t value);
bool synth_unit_get_value(uint synth_unit_number, const char *desc, uint32_t *value);
synth_unit_type synth_unit_get_type(uint synth_unit_number);
const synth_parm_configuration_entry *synth_unit_get_configuration_entry(uint synth_unit_number, uint num);

extern const synth_parm_configuration_entry * const spce[];
extern const char * const stnames[];

uint32_t synth_read_value_prec(void *v, int prec);
void synth_set_value_prec(void *v, int prec, uint32_t val);

void synth_unit_reset_unitno(int synth_unit_number);
void synth_unit_reset_all(void);
void synth_initialize(void);

#ifdef __cplusplus
}
#endif

#endif /* __SYNTH_H */