/* dsp.c

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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "synth.h"
#include "treblesynth.h"

#define QUANTIZATION_BITS 15
#define QUANTIZATION_MAX (1<<QUANTIZATION_BITS)
#define QUANTIZATION_MAX_FLOAT ((float)(1<<QUANTIZATION_BITS))

synth_unit synth_units[MAX_POLYPHONY][MAX_SYNTH_UNITS];
synth_parm synth_parms[MAX_SYNTH_UNITS];
int32_t synth_unit_result[MAX_POLYPHONY][MAX_SYNTH_UNITS+1];

mutex_t synth_mutex;
mutex_t note_mutexes[MAX_POLYPHONY];

/**************************** SYNTH_TYPE_NONE **************************************************/

int32_t synth_type_process_none(int32_t sample, int32_t control, synth_parm *dp, synth_unit *du)
{
    return sample;
}

const synth_parm_configuration_entry synth_parm_configuration_entry_none[] = 
{
    { "SourceUnit",  offsetof(synth_parm_none,source_unit),        4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_none synth_parm_none_default = { 0, 0, 0 };

/******************************SYNTH_TYPE_SINE_SYNTH*******************************************/

int32_t synth_type_process_sin_synth(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su)
{
    uint32_t new_input = read_potentiometer_value(sp->stss.control_number1);
    if (abs(new_input - su->stss.pot_value1) >= POTENTIOMETER_VALUE_SENSITIVITY)
    {
        su->stss.pot_value1 = new_input;
        sp->stss.frequency[0] = 40 + new_input/(POT_MAX_VALUE/2048);
    }
    new_input = read_potentiometer_value(sp->stss.control_number2);
    if (abs(new_input - su->stss.pot_value2) >= POTENTIOMETER_VALUE_SENSITIVITY)
    {
        su->stss.pot_value2 = new_input;
        sp->stss.amplitude[0] = new_input/(POT_MAX_VALUE/256);
    }
    if (sp->stss.frequency[0] != su->stss.last_frequency[0])
    {
        su->stss.last_frequency[0] = sp->stss.frequency[0];
        su->stss.sine_counter_inc[0] = (su->stss.last_frequency[0]*65536) / DSP_SAMPLERATE;
    }
    if (sp->stss.frequency[1] != su->stss.last_frequency[1])
    {
        su->stss.last_frequency[1] = sp->stss.frequency[1];
        su->stss.sine_counter_inc[1] = (su->stss.last_frequency[1]*65536) / DSP_SAMPLERATE;
    }
    if (sp->stss.frequency[2] != su->stss.last_frequency[2])
    {
        su->stss.last_frequency[2] = sp->stss.frequency[2];
        su->stss.sine_counter_inc[2] = (su->stss.last_frequency[2]*65536) / DSP_SAMPLERATE;
    }
    uint ct = 1;
    int32_t sine_val = sample * ((int32_t)sp->stss.mixval);
    for (uint i=0;i<(sizeof(su->stss.sine_counter)/sizeof(su->stss.sine_counter[0]));i++)
    {
        if (sp->stss.amplitude[i] != 0) 
        {
            su->stss.sine_counter[i] += su->stss.sine_counter_inc[i];
            int32_t val = sine_table_entry((su->stss.sine_counter[i] & 0xFFFF) / 256) / (QUANTIZATION_MAX / (ADC_PREC_VALUE/2));
            sine_val += val * ((int32_t)sp->stss.amplitude[i]);
            ct++;
        }
    }
    if (ct > 2)
        sine_val /= (256 * 4);
    else if (ct > 1)
        sine_val /= (256 * 2);
    else sine_val /= 256;
    return sine_val;
}


const synth_parm_configuration_entry synth_parm_configuration_entry_sin_synth[] = 
{
    { "Freq1",        offsetof(synth_parm_sine_synth,frequency[0]),     4, 4, 100, 4000, NULL },
    { "Amplitude1",   offsetof(synth_parm_sine_synth,amplitude[0]),     4, 3, 0,   255, NULL  },
    { "Freq2",        offsetof(synth_parm_sine_synth,frequency[1]),     4, 4, 100, 4000, NULL },
    { "Amplitude2",   offsetof(synth_parm_sine_synth,amplitude[1]),     4, 3, 0,   255, NULL },
    { "Freq3",        offsetof(synth_parm_sine_synth,frequency[2]),     4, 4, 100, 4000, NULL },
    { "Amplitude3",   offsetof(synth_parm_sine_synth,amplitude[2]),     4, 3, 0,   255, NULL },
    { "Mixval",       offsetof(synth_parm_sine_synth,mixval),           4, 4, 0,   255, NULL },
    { "FreqCtrl",     offsetof(synth_parm_sine_synth,control_number1),  4, 2, 0, POTENTIOMETER_MAX, "OscFreq" },
    { "AmpCtrl",      offsetof(synth_parm_sine_synth,control_number2),  4, 2, 0, POTENTIOMETER_MAX, "OscAmp" },
    { "SourceUnit",   offsetof(synth_parm_sine_synth,source_unit),      4, 2, 1, MAX_SYNTH_UNITS },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_sine_synth synth_parm_sine_synth_default = { 0, 0, 0, 255, { 300, 300, 300}, {255, 0, 0}, 0, 0 };

/************STRUCTURES FOR ALL SYNTH TYPES *****************************/

const char * const stnames[] = 
{
    "None",
    "Sin Synth",
    NULL
};

const synth_parm_configuration_entry * const spce[] = 
{
    synth_parm_configuration_entry_none,
    synth_parm_configuration_entry_sin_synth, 
    NULL
};

synth_type_process * const stp[] = {
    synth_type_process_none,
    synth_type_process_sin_synth,
};

const void * const synth_parm_struct_defaults[] =
{
    (void *) &synth_parm_none_default,
    (void *) &synth_parm_sine_synth_default
};

/********************* SYNTH PROCESS STRUCTURE *******************************************/

uint32_t synth_read_value_prec(void *v, int prec)
{
    if (prec == 1)
        return *((uint8_t *)v);
    if (prec == 2)
        return *((uint16_t *)v);
    return *((uint32_t *)v);
}

void synth_set_value_prec(void *v, int prec, uint32_t val)
{
    DMB();
    if (prec == 1)
    {
        *((uint8_t *)v) = val;
        return;
    } else if (prec == 2)
    {
        *((uint16_t *)v) = val;
        return;
    } else
    {
        *((uint32_t *)v) = val;
    }
    DMB();

}

void synth_lock(int note_no)
{
    mutex_enter_blocking(&note_mutexes[note_no]);
}

void synth_unlock(int note_no)
{
    mutex_exit(&note_mutexes[note_no]);
}

void synth_unit_struct_zero(synth_unit *su)
{
    memset((void *)su,'\000',sizeof(synth_unit));
}

void synth_unit_reset(int synth_note, int synth_unit_number)
{
    synth_unit *du = synth_unit_entry(synth_note, synth_unit_number);

    synth_unit_struct_zero(du);
}

void synth_unit_reset_unitno(int unitno)
{
    for (int j=0;j<MAX_POLYPHONY;j++)
    {
        synth_lock(j);
        synth_unit_reset(j, unitno);        
        synth_unlock(j);
    }
}

void synth_unit_reset_all(void)
{
    for (int i=0;i<MAX_SYNTH_UNITS;i++)
        synth_unit_reset_unitno(i);
}

void synth_unit_initialize(int synth_unit_number, synth_unit_type sut)
{
    synth_parm *sp;
    
    if (sut >= SYNTH_TYPE_MAX_ENTRY) return;
    
    sp = synth_parm_entry(synth_unit_number);
    
    synth_unit_reset_unitno(synth_unit_number);
    memcpy((void *)sp, synth_parm_struct_defaults[sut], sizeof(synth_parm));
    sp->stn.source_unit = synth_unit_number + 1;
    DMB();
    sp->stn.sut = sut;
    DMB();
}

static inline int32_t synth_process(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su)
{
    return stp[(int)sp->stn.sut](sample, control, sp, su);
}

void synth_initialize(void)
{
    mutex_init(&synth_mutex);
    for (int i=0;i<MAX_POLYPHONY;i++)
        mutex_init(&note_mutexes[i]);
    for (int unit_number=0;unit_number<MAX_SYNTH_UNITS;unit_number++) 
        synth_unit_initialize(unit_number, SYNTH_TYPE_NONE);
}

int32_t synth_process_all_units(void)
{
    int32_t total_sample = 0;
    for (int note=0;note<MAX_POLYPHONY;note++)
    {
        if (mutex_try_enter(&note_mutexes[note],NULL))
        {
            synth_unit_result[note][0] = 0;
            for (int unit_no=0;unit_no<MAX_SYNTH_UNITS;unit_no++)
            {
                synth_parm *sp = synth_parm_entry(unit_no);
                synth_unit *su = synth_unit_entry(note, unit_no);
                int32_t prev = synth_unit_result[note][sp->stn.source_unit-1];
                synth_unit_result[note][unit_no+1] = synth_process(prev, prev, sp, su);
            }
            total_sample += synth_unit_result[note][MAX_SYNTH_UNITS];
            mutex_exit(&note_mutexes[note]);
        }
    }
    return total_sample;
}

synth_unit_type synth_unit_get_type(uint synth_unit_number)
{
    if (synth_unit_number >= MAX_SYNTH_UNITS) return SYNTH_TYPE_MAX_ENTRY;
    synth_parm *sp = synth_parm_entry(synth_unit_number);
    return sp->stn.sut;
}

const synth_parm_configuration_entry *synth_unit_get_configuration_entry(uint synth_unit_number, uint num)
{
    if (synth_unit_number >= MAX_SYNTH_UNITS) return NULL;
    synth_parm *sp = synth_parm_entry(synth_unit_number);
    const synth_parm_configuration_entry *spce_l = spce[sp->stn.sut];
    while (spce_l->desc != NULL)
    {
        if (num == 0) return spce_l;
        num--;
        spce_l++;
    }
    return NULL;
}

bool synth_unit_set_value(uint synth_unit_number, const char *desc, uint32_t value)
{
    if (synth_unit_number >= MAX_SYNTH_UNITS) return NULL;
    synth_parm *sp = synth_parm_entry(synth_unit_number);
    const synth_parm_configuration_entry *spce_l = spce[sp->stn.sut];
    while (spce_l->desc != NULL)
    {
        if (!strcmp(spce_l->desc,desc))
        {
           if ((value >= spce_l->minval) && (value <= spce_l->maxval))
           {
                synth_set_value_prec((void *)(((uint8_t *)synth_parm_entry(synth_unit_number)) + spce_l->offset), spce_l->size, value); 
                return true;
           } else return false;
            
        }
        spce_l++;
    }
    return false;
}

bool synth_unit_get_value(uint synth_unit_number, const char *desc, uint32_t *value)
{
    if (synth_unit_number >= MAX_SYNTH_UNITS) return NULL;
    synth_parm *sp = synth_parm_entry(synth_unit_number);
    const synth_parm_configuration_entry *spce_l = spce[sp->stn.sut];
    while (spce_l->desc != NULL)
    {
        if (!strcmp(spce_l->desc,desc))
        {
           uint32_t v = synth_read_value_prec((void *)(((uint8_t *)synth_parm_entry(synth_unit_number)) + spce_l->offset), spce_l->size);
           *value = v;
           return true;
        }
        spce_l++;
    }
    return false;
}
