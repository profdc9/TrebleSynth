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
#include "waves.h"
#include "synth.h"
#include "treblesynth.h"

#define QUANTIZATION_BITS 15
#define QUANTIZATION_MAX (1<<QUANTIZATION_BITS)
#define QUANTIZATION_MAX_FLOAT ((float)(1<<QUANTIZATION_BITS))

uint32_t synth_note_frequency_table_counter[MIDI_NOTES];
float synth_note_frequency_table[MIDI_NOTES];

volatile uint32_t synth_note_active[MAX_POLYPHONY];
volatile uint32_t synth_note_stopping[MAX_POLYPHONY];
volatile uint32_t synth_note_stopping_fast[MAX_POLYPHONY];
uint8_t synth_note_number[MAX_POLYPHONY];
uint8_t synth_note_velocity[MAX_POLYPHONY];
uint32_t synth_note_stopping_counter[MAX_POLYPHONY];

synth_unit synth_units[MAX_POLYPHONY][MAX_SYNTH_UNITS];
synth_parm synth_parms[MAX_SYNTH_UNITS];
int32_t synth_unit_result[MAX_POLYPHONY][MAX_SYNTH_UNITS+1];

mutex_t synth_mutex;
mutex_t note_mutexes[MAX_POLYPHONY];

inline float frequency_omega_from_vco(uint32_t vco)
{
    return expf((SEMITONE_LOG_STEP*((float)MIDI_NOTES)/((float)QUANTIZATION_MAX))*((float)vco))*(MATH_PI_F*2.0f*MIDI_FREQUENCY_0/((float)DSP_SAMPLERATE));
}

inline float frequency_fraction_from_vco(uint32_t vco)
{
    return expf((SEMITONE_LOG_STEP*((float)MIDI_NOTES)/((float)QUANTIZATION_MAX))*((float)vco))*(MIDI_FREQUENCY_0/((float)DSP_SAMPLERATE));
}

inline float frequency_semitone_fraction_from_vco(uint32_t vco)
{
    return expf((SEMITONE_LOG_STEP*((float)MIDI_NOTES)/((float)QUANTIZATION_MAX))*((float)vco))*(SEMITONE_LOG_STEP*MIDI_FREQUENCY_0/((float)DSP_SAMPLERATE));
}

inline float counter_fraction_from_vco(uint32_t vco)
{
    return expf((SEMITONE_LOG_STEP*((float)MIDI_NOTES)/((float)QUANTIZATION_MAX))*((float)vco))*((MIDI_FREQUENCY_0/((float)DSP_SAMPLERATE))*((((float)SYNTH_OSCILLATOR_PRECISION)*WAVETABLES_LENGTH)));
}

void synth_note_frequency_table_initialize(void)
{
    for (int note_no=0;note_no<MIDI_NOTES;note_no++)
    {
        synth_note_frequency_table[note_no] = expf(SEMITONE_LOG_STEP*note_no)*(MIDI_FREQUENCY_0/DSP_SAMPLERATE);
        synth_note_frequency_table_counter[note_no] =  synth_note_frequency_table[note_no]*(((float)SYNTH_OSCILLATOR_PRECISION)*SINE_TABLE_ENTRIES);
    }
}

/**************************** SYNTH_TYPE_NONE **************************************************/

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_none)(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_none(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#endif
{
    return sample;
}

void synth_note_start_none(synth_parm *sp, synth_unit *su, uint32_t vco, uint32_t velocity)
{
}

const synth_parm_configuration_entry synth_parm_configuration_entry_none[] = 
{
    { "SourceUnit",  offsetof(synth_parm_vco,source_unit),        4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit", offsetof(synth_parm_vco,control_unit),       4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_none synth_parm_none_default = { 0, 0, 0 };

/**************************** SYNTH_TYPE_VCO **************************************************/

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_vco)(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_vco(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#endif
{
    su->stvco.counter += (su->stvco.counter_inc + (su->stvco.counter_semitone_control_gain*(control/64))/(QUANTIZATION_MAX/64) );
    sample = wavetables[sp->stvco.osc_type-1][(su->stvco.counter / SYNTH_OSCILLATOR_PRECISION) & (WAVETABLES_LENGTH-1)];
    sample = (sample * ((int32_t)sp->stvco.amplitude)) / 256;
    return sample;
}

void synth_note_start_vco(synth_parm *sp, synth_unit *su, uint32_t vco, uint32_t velocity)
{
    if (sp->stvco.control_amplitude != 0)
        sp->stvco.amplitude = read_potentiometer_value(sp->stvco.control_amplitude)/(POT_MAX_VALUE/256);

    if (sp->stvco.octave != 2)
    {
        int32_t adjvco = (int32_t) vco + (((int32_t)sp->stvco.octave) - 2) * ((QUANTIZATION_MAX/MIDI_NOTES)*12);
        while (adjvco < 0) adjvco += ((QUANTIZATION_MAX/MIDI_NOTES)*12);
        while (adjvco > (QUANTIZATION_MAX-1)) adjvco -= (QUANTIZATION_MAX-1);
        vco = adjvco;
    }
    float counter_inc_float = counter_fraction_from_vco(vco);
    su->stvco.counter_inc = counter_inc_float;
    su->stvco.counter_semitone_control_gain = (counter_inc_float * SEMITONE_LOG_STEP) * sp->stvco.control_gain;
}

const synth_parm_configuration_entry synth_parm_configuration_entry_vco[] = 
{
    { "SourceUnit",  offsetof(synth_parm_vco,source_unit),        4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit", offsetof(synth_parm_vco,control_unit),       4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "OscType",     offsetof(synth_parm_vco,osc_type),           4, 1, 1, WAVETABLES_NUMBER, NULL },
    { "ControlGain", offsetof(synth_parm_vco,control_gain),       4, 2, 0, 31, NULL },
    { "Amplitude",   offsetof(synth_parm_vco,amplitude),          4, 3, 0, 256, NULL },        
    { "Octave",      offsetof(synth_parm_vco,octave),             4, 1, 0, 4, NULL },        
    { "AmplCtrl",    offsetof(synth_parm_vco,control_amplitude),  4, 2, 0, POTENTIOMETER_MAX, "Amplitude" },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_vco synth_parm_vco_default = { 0, 0, 0, 1, 1, 256, 2, 0 };

/**************************** SYNTH_TYPE_ADSR **************************************************/

#define ADSR_SLOPE_SCALING 256

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_adsr)(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_adsr(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#endif
{
    uint32_t amplitude = 0;
    switch (su->stadsr.phase)
    {
        case 0:  amplitude = (su->stadsr.counter * su->stadsr.rise_slope) / ADSR_SLOPE_SCALING;
                 if ((++su->stadsr.counter) >= sp->stadsr.attack)
                 {
                     su->stadsr.counter = 0;
                     su->stadsr.phase = 1;
                 }
                 break;
        case 1:  amplitude = su->stadsr.max_amp_level - ((su->stadsr.counter * su->stadsr.decay_slope) / ADSR_SLOPE_SCALING);
                 if ((++su->stadsr.counter) >= sp->stadsr.decay)
                 {
                     su->stadsr.counter = 0;
                     su->stadsr.phase = 2;
                 }
                 break;
        case 2:  amplitude = su->stadsr.sustain_amp_level;
                 if (synth_note_stopping[note])
                 {
                     su->stadsr.counter = 0;
                     su->stadsr.phase = 3;
                 }
                 break;
        case 3:  amplitude = su->stadsr.sustain_amp_level - ((su->stadsr.counter * su->stadsr.release_slope) / ADSR_SLOPE_SCALING);
                 if ((++su->stadsr.counter) >= sp->stadsr.release)
                     synth_note_active[note] = false;
                 break;
    }
    sample = (sample * ((int32_t)amplitude)) / QUANTIZATION_MAX;
    return sample;
}

void synth_note_start_adsr(synth_parm *sp, synth_unit *su, uint32_t adsr, uint32_t velocity)
{
    su->stadsr.max_amp_level = (velocity * QUANTIZATION_MAX) / 128;
    if (sp->stadsr.control_sustain != 0)
        sp->stadsr.sustain_level = read_potentiometer_value(sp->stadsr.control_sustain)/64;
    su->stadsr.sustain_amp_level = (su->stadsr.max_amp_level * sp->stadsr.sustain_level) / 256;
    if (sp->stadsr.control_attack != 0)
        sp->stadsr.attack = read_potentiometer_value(sp->stadsr.control_attack)+1024;
    su->stadsr.rise_slope = (ADSR_SLOPE_SCALING * su->stadsr.max_amp_level) / sp->stadsr.attack;
    if (sp->stadsr.control_decay != 0)
        sp->stadsr.decay = read_potentiometer_value(sp->stadsr.control_decay)+1024;
    su->stadsr.decay_slope = (ADSR_SLOPE_SCALING * (su->stadsr.max_amp_level - su->stadsr.sustain_amp_level)) / sp->stadsr.decay;
    if (sp->stadsr.control_release != 0)
        sp->stadsr.release = read_potentiometer_value(sp->stadsr.control_release)+1024;
    su->stadsr.release_slope = (ADSR_SLOPE_SCALING * su->stadsr.sustain_amp_level) / sp->stadsr.release;
}

const synth_parm_configuration_entry synth_parm_configuration_entry_adsr[] = 
{
    { "SourceUnit",  offsetof(synth_parm_adsr,source_unit),           4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit", offsetof(synth_parm_adsr,control_unit),          4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "Attack",      offsetof(synth_parm_adsr,attack),                4, 5, 1024, DSP_SAMPLERATE, NULL },
    { "Decay",       offsetof(synth_parm_adsr,decay),                 4, 5, 1024, DSP_SAMPLERATE, NULL },
    { "SustainLvl",  offsetof(synth_parm_adsr,sustain_level),         4, 3, 0, 255, NULL },
    { "Release",     offsetof(synth_parm_adsr,release),               4, 5, 1024, DSP_SAMPLERATE, NULL },
    { "AttackCtl",   offsetof(synth_parm_adsr,control_attack),        4, 2, 0, POTENTIOMETER_MAX, "AttackCtl" },
    { "DecayCtl",    offsetof(synth_parm_adsr,control_decay),         4, 2, 0, POTENTIOMETER_MAX, "DecayCtl" },
    { "SustainCtl",  offsetof(synth_parm_adsr,control_sustain),       4, 2, 0, POTENTIOMETER_MAX, "SustainCtl" },
    { "ReleaseCtl",  offsetof(synth_parm_adsr,control_release),       4, 2, 0, POTENTIOMETER_MAX, "ReleaseCtl" },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_adsr synth_parm_adsr_default = { 0, 0, 0,  2000, 4000, 128, 2000, 0, 0, 0, 0 };

/**************************** SYNTH_TYPE_LOWPASS **************************************************/

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_lowpass)(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_lowpass(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#endif
{
    for (uint n=0;n<sp->stlp.stages;n++)
    {
        int32_t sy = su->stlp.stage_y[n];
        sy = sample = ((QUANTIZATION_MAX-su->stlp.alpha)*sample + su->stlp.alpha*sy) / QUANTIZATION_MAX;
        su->stlp.stage_y[n] = sample = sy;
    }
    return sample;    
}

void synth_note_start_lowpass(synth_parm *sp, synth_unit *su, uint32_t vco, uint32_t velocity)
{
    if (sp->stlp.control_kneefreq != 0)
        sp->stlp.kneefreq = read_potentiometer_value(sp->stlp.control_kneefreq)/(POT_MAX_VALUE/256);

    float b = ((float)sp->stlp.kneefreq) * (1.0f/256.0f);
    float c0 = cosf(frequency_omega_from_vco(vco));
    float lbw = 1.0f-b*c0;
    float lb = 1.0f-b;
    float a = (lbw-sqrtf(lbw*lbw-lb*lb)) / lb;
    su->stlp.alpha = (int32_t) (a * QUANTIZATION_MAX);  
    //set_debug_vals(su->stlp.alpha, (int32_t)(b*1000000.f), (int32_t)(c0*1000000.f));

}

const synth_parm_configuration_entry synth_parm_configuration_entry_lowpass[] = 
{
    { "SourceUnit",  offsetof(synth_parm_lowpass,source_unit),        4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit", offsetof(synth_parm_lowpass,control_unit),       4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "KneeFreq",    offsetof(synth_parm_lowpass,kneefreq),           2, 3, 0, 255, NULL },
    { "Stages",      offsetof(synth_parm_lowpass,stages),             4, 1, 0, 4, NULL },
    { "KneeCtrl",    offsetof(synth_parm_lowpass,control_kneefreq),   4, 2, 0, POTENTIOMETER_MAX, "Kneefreq" },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_lowpass synth_parm_lowpass_default = { 0, 0, 0, 192, 4, 0 };

/**************************** SYNTH_TYPE_OSC **************************************************/

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_osc)(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_osc(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#endif
{
    su->stosc.counter += (su->stosc.counter_inc + (su->stosc.counter_semitone_control_gain*(control/64))/(QUANTIZATION_MAX/64) );
    if (sp->stosc.control_bend != 0)
    {
        int32_t pot_value = read_potentiometer_value(sp->stosc.control_bend)/64;
        su->stosc.counter += (su->stosc.counter_semitone_bend_gain*pot_value)/(POT_MAX_VALUE/64);
    }
    sample = wavetables[sp->stosc.osc_type-1][(su->stosc.counter / SYNTH_OSCILLATOR_PRECISION) & (WAVETABLES_LENGTH-1)];

    sample = (sample * ((int32_t)sp->stosc.amplitude)) / 256;
    return sample;
}

#define OSC_MINFREQ 1.0f
#define OSC_MAXFREQ 4000.0f

const float osc_scaling = logf( OSC_MAXFREQ / OSC_MINFREQ ) / ((float)POT_MAX_VALUE);

void synth_note_start_osc(synth_parm *sp, synth_unit *su, uint32_t vco, uint32_t velocity)
{
    if (sp->stosc.control_amplitude != 0)
        sp->stosc.amplitude = read_potentiometer_value(sp->stosc.control_amplitude)/(POT_MAX_VALUE/256);

    if (sp->stosc.control_frequency != 0)
    {
        int32_t pot_value = read_potentiometer_value(sp->stosc.control_frequency);
        sp->stosc.frequency = expf(osc_scaling * ((float)pot_value))*OSC_MINFREQ;
    }
    float counter_inc_float = ((float)sp->stosc.frequency)*((((float)SYNTH_OSCILLATOR_PRECISION)*((float)WAVETABLES_LENGTH))/((float)DSP_SAMPLERATE));
    su->stvco.counter_inc = counter_inc_float;
    float f = (counter_inc_float * SEMITONE_LOG_STEP);
    su->stosc.counter_semitone_control_gain = f * sp->stosc.control_gain;
    su->stosc.counter_semitone_bend_gain = f * sp->stosc.bend_gain;
}

const synth_parm_configuration_entry synth_parm_configuration_entry_osc[] = 
{
    { "SourceUnit",  offsetof(synth_parm_osc,source_unit),        4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit", offsetof(synth_parm_osc,control_unit),       4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "Frequency",   offsetof(synth_parm_osc,frequency),          4, 4, OSC_MINFREQ, OSC_MAXFREQ, NULL },
    { "Amplitude",   offsetof(synth_parm_osc,amplitude),          4, 3, 0, 256,  NULL },
    { "OscType",     offsetof(synth_parm_osc,osc_type),           4, 1, 1, 8, NULL },
    { "ControlGain", offsetof(synth_parm_osc,control_gain),       4, 2, 0, 15, NULL },
    { "BendGain",    offsetof(synth_parm_osc,bend_gain),          4, 2, 0, 15, NULL },
    { "BendCtrl",    offsetof(synth_parm_osc,control_bend),       4, 2, 0, POTENTIOMETER_MAX, "BendGain" },
    { "FreqCtrl",    offsetof(synth_parm_osc,control_frequency),  4, 2, 0, POTENTIOMETER_MAX, "Frequency" },
    { "AmplCtrl",    offsetof(synth_parm_osc,control_amplitude),  4, 2, 0, POTENTIOMETER_MAX, "Amplitude" },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_osc synth_parm_osc_default = { 0, 0, 0, 1, 1, 262, 256, 0, 1, 0, 0 };

/**************************** SYNTH_TYPE_VCA **************************************************/

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_vca)(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_vca(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#endif
{
    control = (control * ((int32_t)sp->stvca.control_gain)) / 256;
    sample = (sample * ((control + QUANTIZATION_MAX)/2)) / QUANTIZATION_MAX;
    sample = (sample * ((int32_t)sp->stvca.amplitude)) / 64;
    if (sample > (QUANTIZATION_MAX-1)) sample = QUANTIZATION_MAX-1;
    if (sample < (-QUANTIZATION_MAX)) sample = -QUANTIZATION_MAX;
    return sample;
}

void synth_note_start_vca(synth_parm *sp, synth_unit *su, uint32_t vco, uint32_t velocity)
{
    if (sp->stvca.control_amplitude != 0)
        sp->stvca.amplitude = read_potentiometer_value(sp->stvca.control_amplitude)/(POT_MAX_VALUE/256);
}

const synth_parm_configuration_entry synth_parm_configuration_entry_vca[] = 
{
    { "SourceUnit",  offsetof(synth_parm_vca,source_unit),        4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit", offsetof(synth_parm_vca,control_unit),       4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlGain", offsetof(synth_parm_vca,control_gain),       4, 3, 0, 256, NULL },
    { "Amplitude",   offsetof(synth_parm_vca,amplitude),          4, 3, 0, 256, NULL },        
    { "AmplCtrl",    offsetof(synth_parm_vca,control_amplitude),  4, 2, 0, POTENTIOMETER_MAX, "Amplitude" },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_vca synth_parm_vca_default = { 0, 0, 0, 256, 64, 0 };

/**************************** SYNTH_TYPE_MIXER **************************************************/

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_mixer)(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_mixer(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#endif
{
    int32_t other_sample = synth_unit_result[note][sp->stmixer.source2_unit-1];
    sample = (sample * ((int32_t) sp->stmixer.mixval) + other_sample * ((int32_t) (256-sp->stmixer.mixval))) / 256;
    sample = (sample * ((int32_t) sp->stmixer.amplitude)) / 256;
    return sample;
}

void synth_note_start_mixer(synth_parm *sp, synth_unit *su, uint32_t vco, uint32_t velocity)
{
    if (sp->stmixer.control_mixval != 0)
        sp->stmixer.mixval = read_potentiometer_value(sp->stmixer.control_mixval)/(POT_MAX_VALUE/256);
    if (sp->stmixer.control_amplitude != 0)
        sp->stmixer.amplitude = read_potentiometer_value(sp->stmixer.control_amplitude)/(POT_MAX_VALUE/256);
}

const synth_parm_configuration_entry synth_parm_configuration_entry_mixer[] = 
{
    { "SourceUnit",   offsetof(synth_parm_mixer,source_unit),        4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit",  offsetof(synth_parm_mixer,control_unit),       4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "Source2Unit",  offsetof(synth_parm_mixer,source2_unit) ,      4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "Mixval",       offsetof(synth_parm_mixer,mixval),             4, 3, 0, 256, NULL },        
    { "Amplitude",    offsetof(synth_parm_mixer,amplitude),          4, 3, 0, 256, NULL },        
    { "MixCtrl",      offsetof(synth_parm_mixer,control_mixval),     4, 2, 0, POTENTIOMETER_MAX, "Mixer" },
    { "AmplCtrl",     offsetof(synth_parm_mixer,control_amplitude),  4, 2, 0, POTENTIOMETER_MAX, "Amplitude" },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_mixer synth_parm_mixer_default = { 0, 0, 0, 1, 128, 256, 0, 0 };

/**************************** SYNTH_TYPE_RING **************************************************/

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_ring)(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_ring(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
#endif
{
    sample = (sample * control) / QUANTIZATION_MAX;
    sample = (sample * ((int32_t)sp->string.amplitude)) / 256;
    return sample;
}

void synth_note_start_ring(synth_parm *sp, synth_unit *su, uint32_t vco, uint32_t velocity)
{
    if (sp->string.control_amplitude != 0)
        sp->string.amplitude = read_potentiometer_value(sp->string.control_amplitude)/(POT_MAX_VALUE/256);
}

const synth_parm_configuration_entry synth_parm_configuration_entry_ring[] = 
{
    { "SourceUnit",  offsetof(synth_parm_ring,source_unit),        4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit", offsetof(synth_parm_ring,control_unit),       4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "Amplitude",   offsetof(synth_parm_ring,amplitude),          4, 3, 0, 256, NULL },        
    { "AmplCtrl",    offsetof(synth_parm_ring,control_amplitude),  4, 2, 0, POTENTIOMETER_MAX, "Amplitude" },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_ring synth_parm_ring_default = { 0, 0, 0, 256, 0 };

/************STRUCTURES FOR ALL SYNTH TYPES *****************************/

const char * const stnames[] = 
{
    "None",
    "VCO",
    "ADSR",
    "Lowpass",
    "Osc",
    "VCA",
    "Mixer",
    "Ring",
    NULL
};

const synth_parm_configuration_entry * const spce[] = 
{
    synth_parm_configuration_entry_none,
    synth_parm_configuration_entry_vco,
    synth_parm_configuration_entry_adsr,
    synth_parm_configuration_entry_lowpass,
    synth_parm_configuration_entry_osc,
    synth_parm_configuration_entry_vca,
    synth_parm_configuration_entry_mixer,
    synth_parm_configuration_entry_ring,
    NULL
};

synth_type_process * const stp[] = {
    synth_type_process_none,
    synth_type_process_vco,
    synth_type_process_adsr,
    synth_type_process_lowpass,
    synth_type_process_osc,
    synth_type_process_vca,
    synth_type_process_mixer,
    synth_type_process_ring,
};

synth_note_start * const sns[] = 
{
    synth_note_start_none,
    synth_note_start_vco,
    synth_note_start_adsr,
    synth_note_start_lowpass,
    synth_note_start_osc,
    synth_note_start_vca,
    synth_note_start_mixer,
    synth_note_start_ring,
};

const void * const synth_parm_struct_defaults[] =
{
    (void *) &synth_parm_none_default,
    (void *) &synth_parm_vco_default,
    (void *) &synth_parm_adsr_default,
    (void *) &synth_parm_lowpass_default,
    (void *) &synth_parm_osc_default,
    (void *) &synth_parm_vca_default,
    (void *) &synth_parm_mixer_default,
    (void *) &synth_parm_ring_default,
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
    sp->stn.control_unit = 1;
    DMB();
    sp->stn.sut = sut;
    DMB();
}

static inline int32_t synth_process(int32_t sample, int32_t control, synth_parm *sp, synth_unit *su, int note)
{
    return stp[(int)sp->stn.sut](sample, control, sp, su, note);
}

void synth_start_note(uint8_t note_no, uint8_t velocity)
{
    uint32_t vco = ((uint32_t)note_no)*(QUANTIZATION_MAX/MIDI_NOTES);
    for (int note=0;note<MAX_POLYPHONY;note++)
    {
        if ((synth_note_active[note]) && (synth_note_number[note] == note_no))
        {
            mutex_enter_blocking(&note_mutexes[note]);
            synth_note_stopping_counter[note] = SYNTH_STOPPING_COUNTER;
            DMB();
            synth_note_stopping_fast[note] = true;
            DMB();
            mutex_exit(&note_mutexes[note]);
            while (synth_note_active[note]) {};
        }
    }
    for (int note=0;note<MAX_POLYPHONY;note++)
    {
        if (!synth_note_active[note])
        {
            mutex_enter_blocking(&note_mutexes[note]);
            for (int unit_no=0;unit_no<MAX_SYNTH_UNITS;unit_no++)
            {
                synth_unit *su = synth_unit_entry(note, unit_no);
                synth_parm *sp = synth_parm_entry(unit_no);
                memset(su,'\000',sizeof(synth_unit));
                sns[(int)sp->stn.sut](sp, su, vco, velocity);
            }
            synth_note_number[note] = note_no;
            synth_note_velocity[note] = velocity;
            synth_note_stopping[note] = false;
            synth_note_stopping_fast[note] = false;
            synth_note_stopping_counter[note] = 0;
            DMB();
            synth_note_active[note] = true;
            DMB();
            mutex_exit(&note_mutexes[note]);
            return;
        }
    }
}

void synth_stop_note(uint8_t note_no, uint8_t velocity)
{
    for (int note=0;note<MAX_POLYPHONY;note++)
        if ((synth_note_active[note]) && (synth_note_number[note] == note_no))
        {
            mutex_enter_blocking(&note_mutexes[note]);
            synth_note_stopping_counter[note] = pc.fail_delay;
            DMB();
            synth_note_stopping[note] = true;
            DMB();
            mutex_exit(&note_mutexes[note]);
            return;
        }
}

void synth_initialize(void)
{
    synth_note_frequency_table_initialize();
    mutex_init(&synth_mutex);
    for (int note=0;note<MAX_POLYPHONY;note++)
    {
        synth_note_active[note] = false;
        synth_note_stopping[note] = false;
        synth_note_stopping_fast[note] = false;
        synth_note_stopping_counter[note] = 0;
        synth_note_number[note] = 0;
        synth_note_velocity[note] = 0;
        mutex_init(&note_mutexes[note]);
    }
    for (int unit_number=0;unit_number<MAX_SYNTH_UNITS;unit_number++) 
        synth_unit_initialize(unit_number, SYNTH_TYPE_NONE);
}

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_local_process_all_units)(void)
#else
int32_t synth_local_process_all_units(void)
#endif
{
    int32_t total_sample = 0;
    for (int note=0;note<MAX_POLYPHONY;note++)
    {
        if ((synth_note_active[note]) && (mutex_try_enter(&note_mutexes[note],NULL)))
        {
            int32_t *sur = synth_unit_result[note];
            sur[0] = 0;
            for (int unit_no=0;unit_no<MAX_SYNTH_UNITS;unit_no++)
            {
                synth_parm *sp = synth_parm_entry(unit_no);
                if (sp->stn.sut == 0)
                {
                    sur[unit_no+1] = sur[sp->stn.source_unit-1];
                } else
                {
                    synth_unit *su = synth_unit_entry(note, unit_no);
                    int32_t source_prev = sur[sp->stn.source_unit-1];
                    int32_t control_prev = sur[sp->stn.control_unit-1];
                    sur[unit_no+1] = synth_process(source_prev, control_prev, sp, su, note);
                }
            }
            int32_t sample = sur[MAX_SYNTH_UNITS];
            if (synth_note_stopping_fast[note])
            {
               if (synth_note_stopping_counter[note] > 0)
               {
                    synth_note_stopping_counter[note]--;
                    total_sample += (sample * ((int32_t)synth_note_stopping_counter[note])) / SYNTH_STOPPING_COUNTER;
               } else
                    synth_note_active[note] = false;
            } else
            {
                total_sample += sample;
                if (synth_note_stopping[note])
                {
                    if (synth_note_stopping_counter[note] > 0)
                        synth_note_stopping_counter[note]--;
                    else
                        synth_note_active[note] = false;
                }
            }
            mutex_exit(&note_mutexes[note]);
        }
    }
    return (total_sample / DIVIDER_POLYPHONY);
}


int32_t synth_process_all_units(void)
{
    return synth_local_process_all_units();
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
