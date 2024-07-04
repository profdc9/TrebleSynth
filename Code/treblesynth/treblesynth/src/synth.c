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

volatile uint32_t synth_note_active[MAX_POLYPHONY];
volatile uint32_t synth_note_stopping[MAX_POLYPHONY];
volatile uint32_t synth_note_stopping_fast[MAX_POLYPHONY];
uint8_t synth_note_number[MAX_POLYPHONY];
uint8_t synth_note_velocity[MAX_POLYPHONY];
uint32_t synth_note_stopping_counter[MAX_POLYPHONY];
uint32_t synth_note_count[MAX_POLYPHONY];
uint32_t synth_current_note_count;
int32_t synth_pitch_bend_value;

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

/**************************** SYNTH_TYPE_NONE **************************************************/

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_none)(synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_none(synth_parm *sp, synth_unit *su, int note)
#endif
{
    return (*su->stn.sample_ptr);
}

void synth_note_start_none(synth_parm *sp, synth_unit *su, uint32_t vco, uint32_t velocity, int note)
{
    su->stn.sample_ptr = &synth_unit_result[note][sp->stn.source_unit-1];
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
static int32_t __no_inline_not_in_flash_func(synth_type_process_vco)(synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_vco(synth_parm *sp, synth_unit *su, int note)
#endif
{
    su->stvco.counter += (su->stvco.counter_inc + ((su->stvco.counter_semitone_control_gain*(*su->stvco.control_ptr / 64) + 
                                                    su->stvco.counter_semitone_pitch_bend_gain * (synth_pitch_bend_value /64))/(QUANTIZATION_MAX/64)));
    int32_t sample = su->stvco.wave[(su->stvco.counter / SYNTH_OSCILLATOR_PRECISION) & (WAVETABLES_LENGTH-1)];
    sample = (sample * ((int32_t)sp->stvco.amplitude)) / 256;
    return sample;
}

const int32_t harmonic_addition[7] = { 0, (int32_t) (12.0f*(QUANTIZATION_MAX/MIDI_NOTES)),
                                       (int32_t) (19.01955f*(QUANTIZATION_MAX/MIDI_NOTES)),
                                       (int32_t) (24.0f*(QUANTIZATION_MAX/MIDI_NOTES)),
                                       (int32_t) (27.863137f*(QUANTIZATION_MAX/MIDI_NOTES)),
                                       (int32_t) (31.01955f*(QUANTIZATION_MAX/MIDI_NOTES)),
                                       (int32_t) (33.688259f*(QUANTIZATION_MAX/MIDI_NOTES)) };

void synth_note_start_vco(synth_parm *sp, synth_unit *su, uint32_t vco, uint32_t velocity, int note)
{
    if (sp->stvco.control_amplitude != 0)
        sp->stvco.amplitude = read_potentiometer_value(sp->stvco.control_amplitude)/(POT_MAX_VALUE/256);
    if (sp->stvco.control_control_gain != 0)
        sp->stvco.control_gain = read_potentiometer_value(sp->stvco.control_control_gain)/(POT_MAX_VALUE/64);

    if (sp->stvco.harmonic != 1)
    {
        int32_t adjvco = (int32_t) vco + harmonic_addition[sp->stvco.harmonic-1];
        while (adjvco > (QUANTIZATION_MAX-1)) adjvco -= ((QUANTIZATION_MAX/MIDI_NOTES)*12);
        vco = adjvco;
    }
    float counter_inc_float = counter_fraction_from_vco(vco);
    su->stvco.counter_inc = counter_inc_float;
    float f = counter_inc_float * SEMITONE_LOG_STEP;
    su->stvco.counter_semitone_control_gain = f * sp->stvco.control_gain;
    su->stvco.counter_semitone_pitch_bend_gain = f * sp->stvco.pitch_bend_gain;
    su->stvco.wave = wavetables[sp->stvco.osc_type-1];
    su->stvco.control_ptr = &synth_unit_result[note][sp->stvco.control_unit-1];
}

const synth_parm_configuration_entry synth_parm_configuration_entry_vco[] = 
{
    { "SourceUnit",  offsetof(synth_parm_vco,source_unit),           4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit", offsetof(synth_parm_vco,control_unit),          4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "OscType",     offsetof(synth_parm_vco,osc_type),              4, 2, 1, WAVETABLES_NUMBER, NULL },
    { "Amplitude",   offsetof(synth_parm_vco,amplitude),             4, 3, 0, 256, NULL },        
    { "Harmonic",    offsetof(synth_parm_vco,harmonic),              4, 1, 1, sizeof(harmonic_addition)/sizeof(int32_t), NULL },        
    { "ControlGain", offsetof(synth_parm_vco,control_gain),          4, 2, 0, 63, NULL },
    { "BendGain",    offsetof(synth_parm_vco,pitch_bend_gain),       4, 2, 0, 63, NULL },
    { "AmplCtrl",    offsetof(synth_parm_vco,control_amplitude),     4, 2, 0, POTENTIOMETER_MAX, "VCOAmpli" },
    { "GainCtrl",    offsetof(synth_parm_vco,control_control_gain),  4, 2, 0, POTENTIOMETER_MAX, "VCOGain" },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_vco synth_parm_vco_default = { 0, 0, 0, 1, 1, 256, 1, 0, 0, 0 };

/**************************** SYNTH_TYPE_ADSR **************************************************/

#define ADSR_SLOPE_SCALING 256

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_adsr)(synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_adsr(synth_parm *sp, synth_unit *su, int note)
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
                 {
                     if (sp->stadsr.output_type == 0) 
                         synth_note_active[note] = false;
                     else
                         su->stadsr.phase = 4;
                 }
                 break;
    }
    if (sp->stadsr.output_type == 0)
        return (((*su->stadsr.sample_ptr) * ((int32_t)amplitude)) / QUANTIZATION_MAX);
    return sp->stadsr.output_type == 1 ? ((int32_t)amplitude) : -((int32_t)amplitude);
}

void synth_note_start_adsr(synth_parm *sp, synth_unit *su, uint32_t adsr, uint32_t velocity, int note)
{
    su->stadsr.max_amp_level = (velocity * QUANTIZATION_MAX) / 128;
    if (sp->stadsr.control_sustain != 0)
        sp->stadsr.sustain_level = read_potentiometer_value(sp->stadsr.control_sustain)/64;
    su->stadsr.sustain_amp_level = (su->stadsr.max_amp_level * sp->stadsr.sustain_level) / 256;
    if (sp->stadsr.control_attack != 0)
        sp->stadsr.attack = read_potentiometer_value(sp->stadsr.control_attack)+512;
    su->stadsr.rise_slope = (ADSR_SLOPE_SCALING * su->stadsr.max_amp_level) / sp->stadsr.attack;
    if (sp->stadsr.control_decay != 0)
        sp->stadsr.decay = read_potentiometer_value(sp->stadsr.control_decay)+512;
    su->stadsr.decay_slope = (ADSR_SLOPE_SCALING * (su->stadsr.max_amp_level - su->stadsr.sustain_amp_level)) / sp->stadsr.decay;
    if (sp->stadsr.control_release != 0)
        sp->stadsr.release = read_potentiometer_value(sp->stadsr.control_release)+512;
    su->stadsr.release_slope = (ADSR_SLOPE_SCALING * su->stadsr.sustain_amp_level) / sp->stadsr.release;
    su->stadsr.sample_ptr = &synth_unit_result[note][sp->stadsr.source_unit-1];
}

const synth_parm_configuration_entry synth_parm_configuration_entry_adsr[] = 
{
    { "SourceUnit",  offsetof(synth_parm_adsr,source_unit),           4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit", offsetof(synth_parm_adsr,control_unit),          4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "Attack",      offsetof(synth_parm_adsr,attack),                4, 5, 512, DSP_SAMPLERATE, NULL },
    { "Decay",       offsetof(synth_parm_adsr,decay),                 4, 5, 512, DSP_SAMPLERATE, NULL },
    { "SustainLvl",  offsetof(synth_parm_adsr,sustain_level),         4, 3, 0, 255, NULL },
    { "Release",     offsetof(synth_parm_adsr,release),               4, 5, 512, DSP_SAMPLERATE, NULL },
    { "OutputType",  offsetof(synth_parm_adsr,output_type),           4, 1, 0, 2, NULL },
    { "AttackCtl",   offsetof(synth_parm_adsr,control_attack),        4, 2, 0, POTENTIOMETER_MAX, "AttackCtl" },
    { "DecayCtl",    offsetof(synth_parm_adsr,control_decay),         4, 2, 0, POTENTIOMETER_MAX, "DecayCtl" },
    { "SustainCtl",  offsetof(synth_parm_adsr,control_sustain),       4, 2, 0, POTENTIOMETER_MAX, "SustainCtl" },
    { "ReleaseCtl",  offsetof(synth_parm_adsr,control_release),       4, 2, 0, POTENTIOMETER_MAX, "ReleaseCtl" },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_adsr synth_parm_adsr_default = { 0, 0, 0,  2000, 4000, 128, 2000, 0, 0, 0, 0, 0 };

/**************************** SYNTH_TYPE_LOWPASS **************************************************/

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_lowpass)(synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_lowpass(synth_parm *sp, synth_unit *su, int note)
#endif
{
    int32_t dalpha = su->stlp.dalpha + (((int32_t)(*su->stlp.control_ptr))*((int32_t)sp->stlp.control_gain))/256;
    if (dalpha > (QUANTIZATION_MAX-1)) dalpha = QUANTIZATION_MAX-1;
    int32_t alpha = (QUANTIZATION_MAX-1) - dalpha;
    int32_t sample = (*su->stlp.sample_ptr);
    for (uint n=0;n<sp->stlp.stages;n++)
    {
        int32_t sy = su->stlp.stage_y[n];
        sy = (dalpha*sample + alpha*sy) / QUANTIZATION_MAX;
        su->stlp.stage_y[n] = sample = sy;
    }
    return sample;
}

void synth_note_start_lowpass(synth_parm *sp, synth_unit *su, uint32_t vco, uint32_t velocity, int note)
{
    if (sp->stlp.control_kneefreq != 0)
        sp->stlp.kneefreq = read_potentiometer_value(sp->stlp.control_kneefreq)/(POT_MAX_VALUE/256);

    float b = ((float)sp->stlp.kneefreq) * (1.0f/256.0f);
    float c0 = cosf(sp->stlp.frequency == 0 ? frequency_omega_from_vco(vco) : ((float)sp->stlp.frequency)*((float)(MATH_PI_F*2.0f/((float)DSP_SAMPLERATE))));
    float lbw = 1.0f-b*c0;
    float lb = 1.0f-b;
    float a = (lbw-sqrtf(lbw*lbw-lb*lb)) / lb;
    su->stlp.dalpha = (QUANTIZATION_MAX-1) -  ((int32_t) (a * QUANTIZATION_MAX));  
    su->stlp.sample_ptr = &synth_unit_result[note][sp->stlp.source_unit-1];
    su->stlp.control_ptr = &synth_unit_result[note][sp->stlp.control_unit-1];
}

const synth_parm_configuration_entry synth_parm_configuration_entry_lowpass[] = 
{
    { "SourceUnit",  offsetof(synth_parm_lowpass,source_unit),        4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit", offsetof(synth_parm_lowpass,control_unit),       4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "LPFreq",      offsetof(synth_parm_lowpass,kneefreq),           2, 3, 0, 255, NULL },
    { "Stages",      offsetof(synth_parm_lowpass,stages),             4, 1, 0, 4, NULL },
    { "Frequency",   offsetof(synth_parm_lowpass,frequency),          4, 4, 0, 4000, NULL },
    { "ControlGain", offsetof(synth_parm_lowpass,control_gain),       4, 3, 0, 255, NULL },
    { "LPFreqCtrl",  offsetof(synth_parm_lowpass,control_kneefreq),   4, 2, 0, POTENTIOMETER_MAX, "LPFreq" },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_lowpass synth_parm_lowpass_default = { 0, 0, 0, 192, 4, 0, 0, 255 };

/**************************** SYNTH_TYPE_OSC **************************************************/

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_osc)(synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_osc(synth_parm *sp, synth_unit *su, int note)
#endif
{
    int32_t sample;
    su->stosc.counter += (su->stosc.counter_inc + (su->stosc.counter_semitone_control_gain*( (*su->stosc.control_ptr) /64))/(QUANTIZATION_MAX/64) );
    if (sp->stosc.control_bend != 0)
    {
        int32_t pot_value = read_potentiometer_value(sp->stosc.control_bend)/64;
        su->stosc.counter += (su->stosc.counter_semitone_bend_gain*pot_value)/(POT_MAX_VALUE/64);
    }
    sample = su->stosc.wave[(su->stosc.counter / SYNTH_OSCILLATOR_PRECISION) & (WAVETABLES_LENGTH-1)];
    sample = (sample * ((int32_t)sp->stosc.amplitude)) / 256;
    return sample;
}

#define OSC_MINFREQ 1.0f
#define OSC_MAXFREQ 100.0f

const float osc_scaling = logf( OSC_MAXFREQ / OSC_MINFREQ ) / ((float)POT_MAX_VALUE);

void synth_note_start_osc(synth_parm *sp, synth_unit *su, uint32_t vco, uint32_t velocity, int note)
{
    if (sp->stosc.control_amplitude != 0)
        sp->stosc.amplitude = read_potentiometer_value(sp->stosc.control_amplitude)/(POT_MAX_VALUE/256);

    if (sp->stosc.control_frequency != 0)
    {
        int32_t pot_value = read_potentiometer_value(sp->stosc.control_frequency);
        sp->stosc.frequency = expf(osc_scaling * ((float)pot_value))*OSC_MINFREQ;
    }
    float counter_inc_float = ((float)sp->stosc.frequency)*((((float)SYNTH_OSCILLATOR_PRECISION)*((float)WAVETABLES_LENGTH))/((float)DSP_SAMPLERATE));
    su->stosc.counter_inc = counter_inc_float;
    float f = (counter_inc_float * SEMITONE_LOG_STEP);
    su->stosc.counter_semitone_control_gain = f * sp->stosc.control_gain;
    su->stosc.counter_semitone_bend_gain = f * sp->stosc.bend_gain;
    su->stosc.wave = wavetables[sp->stosc.osc_type-1];
    su->stosc.control_ptr = &synth_unit_result[note][sp->stosc.control_unit-1];
}

const synth_parm_configuration_entry synth_parm_configuration_entry_osc[] = 
{
    { "SourceUnit",  offsetof(synth_parm_osc,source_unit),        4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit", offsetof(synth_parm_osc,control_unit),       4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "Frequency",   offsetof(synth_parm_osc,frequency),          4, 4, OSC_MINFREQ, OSC_MAXFREQ, NULL },
    { "Amplitude",   offsetof(synth_parm_osc,amplitude),          4, 3, 0, 256,  NULL },
    { "OscType",     offsetof(synth_parm_osc,osc_type),           4, 2, 1, 8, NULL },
    { "ControlGain", offsetof(synth_parm_osc,control_gain),       4, 2, 0, 15, NULL },
    { "BendGain",    offsetof(synth_parm_osc,bend_gain),          4, 2, 0, 15, NULL },
    { "BendCtrl",    offsetof(synth_parm_osc,control_bend),       4, 2, 0, POTENTIOMETER_MAX, "LFOFMGain" },
    { "FreqCtrl",    offsetof(synth_parm_osc,control_frequency),  4, 2, 0, POTENTIOMETER_MAX, "LFOFreq" },
    { "AmplCtrl",    offsetof(synth_parm_osc,control_amplitude),  4, 2, 0, POTENTIOMETER_MAX, "LFOAmpli" },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_osc synth_parm_osc_default = { 0, 0, 0, 1, 1, 6, 256, 0, 1, 0, 0 };

/**************************** SYNTH_TYPE_VCA **************************************************/

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_vca)(synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_vca(synth_parm *sp, synth_unit *su, int note)
#endif
{
    int32_t control = ((*su->stvca.control_ptr) * ((int32_t)sp->stvca.control_gain)) / 256;
    int32_t sample = ((*su->stvca.sample_ptr) * ((control + QUANTIZATION_MAX)/2)) / QUANTIZATION_MAX;
    sample = (sample * ((int32_t)sp->stvca.amplitude)) / 64;
    if (sample > (QUANTIZATION_MAX-1)) sample = QUANTIZATION_MAX-1;
    if (sample < (-QUANTIZATION_MAX)) sample = -QUANTIZATION_MAX;
    return sample;
}

void synth_note_start_vca(synth_parm *sp, synth_unit *su, uint32_t vco, uint32_t velocity, int note)
{
    if (sp->stvca.control_amplitude != 0)
        sp->stvca.amplitude = read_potentiometer_value(sp->stvca.control_amplitude)/(POT_MAX_VALUE/256);
    su->stvca.sample_ptr = &synth_unit_result[note][sp->stvca.source_unit-1];
    su->stvca.control_ptr = &synth_unit_result[note][sp->stvca.control_unit-1];
}

const synth_parm_configuration_entry synth_parm_configuration_entry_vca[] = 
{
    { "SourceUnit",  offsetof(synth_parm_vca,source_unit),        4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit", offsetof(synth_parm_vca,control_unit),       4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlGain", offsetof(synth_parm_vca,control_gain),       4, 3, 0, 256, NULL },
    { "Amplitude",   offsetof(synth_parm_vca,amplitude),          4, 3, 0, 256, NULL },        
    { "AmplCtrl",    offsetof(synth_parm_vca,control_amplitude),  4, 2, 0, POTENTIOMETER_MAX, "VCAAmpli" },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_vca synth_parm_vca_default = { 0, 0, 0, 256, 64, 0 };

/**************************** SYNTH_TYPE_MIXER **************************************************/

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_mixer)(synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_mixer(synth_parm *sp, synth_unit *su, int note)
#endif
{
    int32_t control = ((*su->stmixer.control_ptr) * ((int32_t)sp->stmixer.control_gain)) / 256;
    int32_t mixval =  (control/(QUANTIZATION_MAX/256)) + sp->stmixer.mixval;
    if (mixval > 255) mixval = 255;
    else if (mixval < 0) mixval = 0;
    int32_t sample = (((*su->stmixer.sample_ptr) * mixval) + ((*su->stmixer.sample2_ptr) * (255-mixval))) / 256;
    sample = (sample * ((int32_t) sp->stmixer.amplitude)) / 256;
    return sample;
}

void synth_note_start_mixer(synth_parm *sp, synth_unit *su, uint32_t vco, uint32_t velocity, int note)
{
    if (sp->stmixer.control_mixval != 0)
        sp->stmixer.mixval = read_potentiometer_value(sp->stmixer.control_mixval)/(POT_MAX_VALUE/256);
    if (sp->stmixer.control_amplitude != 0)
        sp->stmixer.amplitude = read_potentiometer_value(sp->stmixer.control_amplitude)/(POT_MAX_VALUE/256);
    su->stmixer.sample_ptr = &synth_unit_result[note][sp->stmixer.source_unit-1];
    su->stmixer.sample2_ptr = &synth_unit_result[note][sp->stmixer.source2_unit-1];
    su->stmixer.control_ptr = &synth_unit_result[note][sp->stmixer.control_unit-1];
}

const synth_parm_configuration_entry synth_parm_configuration_entry_mixer[] = 
{
    { "SourceUnit",   offsetof(synth_parm_mixer,source_unit),        4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit",  offsetof(synth_parm_mixer,control_unit),       4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "Source2Unit",  offsetof(synth_parm_mixer,source2_unit) ,      4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "Mixval",       offsetof(synth_parm_mixer,mixval),             4, 3, 0, 256, NULL },        
    { "Amplitude",    offsetof(synth_parm_mixer,amplitude),          4, 3, 0, 256, NULL },        
    { "ControlGain",  offsetof(synth_parm_mixer,control_gain),       4, 3, 0, 256, NULL },
    { "MixCtrl",      offsetof(synth_parm_mixer,control_mixval),     4, 2, 0, POTENTIOMETER_MAX, "MixerBal" },
    { "AmplCtrl",     offsetof(synth_parm_mixer,control_amplitude),  4, 2, 0, POTENTIOMETER_MAX, "MixerAmpli" },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_mixer synth_parm_mixer_default = { 0, 0, 0, 1, 128, 256, 0, 0, 255};

/**************************** SYNTH_TYPE_RING **************************************************/

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_ring)(synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_ring(synth_parm *sp, synth_unit *su, int note)
#endif
{
    int32_t sample = ((*su->string.sample_ptr) * (*su->string.control_ptr)) / QUANTIZATION_MAX;
    sample = (sample * ((int32_t)sp->string.amplitude)) / 256;
    return sample;
}

void synth_note_start_ring(synth_parm *sp, synth_unit *su, uint32_t vco, uint32_t velocity, int note)
{
    if (sp->string.control_amplitude != 0)
        sp->string.amplitude = read_potentiometer_value(sp->string.control_amplitude)/(POT_MAX_VALUE/256);
    su->string.sample_ptr = &synth_unit_result[note][sp->string.source_unit-1];
    su->string.control_ptr = &synth_unit_result[note][sp->string.control_unit-1];
}

const synth_parm_configuration_entry synth_parm_configuration_entry_ring[] = 
{
    { "SourceUnit",  offsetof(synth_parm_ring,source_unit),        4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit", offsetof(synth_parm_ring,control_unit),       4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "Amplitude",   offsetof(synth_parm_ring,amplitude),          4, 3, 0, 256, NULL },        
    { "AmplCtrl",    offsetof(synth_parm_ring,control_amplitude),  4, 2, 0, POTENTIOMETER_MAX, "RingAmpli" },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_ring synth_parm_ring_default = { 0, 0, 0, 256, 0 };

/**************************** SYNTH_TYPE_NOISE **************************************************/

#ifdef PLACE_IN_RAM
static int32_t __no_inline_not_in_flash_func(synth_type_process_noise)(synth_parm *sp, synth_unit *su, int note)
#else
int32_t synth_type_process_noise(synth_parm *sp, synth_unit *su, int note)
#endif
{
    su->stnoise.counter += (su->stnoise.counter_inc + ((su->stnoise.counter_semitone_control_gain*(*su->stnoise.control_ptr / 64) + 
                                                        su->stnoise.counter_semitone_pitch_bend_gain * (synth_pitch_bend_value /64))/(QUANTIZATION_MAX/64)));
    if ( ((su->stnoise.counter ^ su->stnoise.last_counter) & ~(WAVETABLES_LENGTH*SYNTH_OSCILLATOR_PRECISION-1)) != 0)
    {
        su->stnoise.last_counter = su->stnoise.counter;
        su->stnoise.congruential_generator = (su->stnoise.congruential_generator * 1664525u + 1013904223u);
        su->stnoise.sample = (int32_t)((su->stnoise.congruential_generator >> 16) & (2*QUANTIZATION_MAX - 1)) - QUANTIZATION_MAX;
    }
    return (su->stnoise.sample * ((int32_t)sp->stnoise.amplitude)) / 256;
}

void synth_note_start_noise(synth_parm *sp, synth_unit *su, uint32_t vco, uint32_t velocity, int note)
{
    if (sp->stnoise.control_amplitude != 0)
        sp->stnoise.amplitude = read_potentiometer_value(sp->stnoise.control_amplitude)/(POT_MAX_VALUE/256);
    if (sp->stnoise.control_control_gain != 0)
        sp->stnoise.control_gain = read_potentiometer_value(sp->stnoise.control_control_gain)/(POT_MAX_VALUE/64);

    if (sp->stnoise.shiftup != 1)
    {
        vco += ((QUANTIZATION_MAX/MIDI_NOTES)*6)*sp->stnoise.shiftup;
        if (vco >= QUANTIZATION_MAX) vco = QUANTIZATION_MAX-1;
    }
    float counter_inc_float = counter_fraction_from_vco(vco);
    su->stnoise.counter_inc = counter_inc_float;
    float f = counter_inc_float * SEMITONE_LOG_STEP;
    su->stnoise.counter_semitone_control_gain = f * sp->stnoise.control_gain;
    su->stnoise.counter_semitone_pitch_bend_gain = f * sp->stnoise.pitch_bend_gain;
    su->stnoise.control_ptr = &synth_unit_result[note][sp->stnoise.control_unit-1];
}

const synth_parm_configuration_entry synth_parm_configuration_entry_noise[] = 
{
    { "SourceUnit",  offsetof(synth_parm_noise,source_unit),           4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "ControlUnit", offsetof(synth_parm_noise,control_unit),          4, 2, 1, MAX_SYNTH_UNITS, NULL },
    { "Amplitude",   offsetof(synth_parm_noise,amplitude),             4, 3, 0, 256, NULL },        
    { "ShiftUp",     offsetof(synth_parm_noise,shiftup),               4, 2, 1, 20, NULL },        
    { "ControlGain", offsetof(synth_parm_noise,control_gain),          4, 2, 0, 63, NULL },
    { "BendGain",    offsetof(synth_parm_noise,pitch_bend_gain),       4, 2, 0, 63, NULL },
    { "AmplCtrl",    offsetof(synth_parm_noise,control_amplitude),     4, 2, 0, POTENTIOMETER_MAX, "VCOAmpli" },
    { "GainCtrl",    offsetof(synth_parm_noise,control_control_gain),  4, 2, 0, POTENTIOMETER_MAX, "VCOGain" },
    { NULL, 0, 4, 0, 0,   1, NULL    }
};

const synth_parm_noise synth_parm_noise_default = { 0, 0, 0, 1, 256, 20, 0, 0, 0 };

/************STRUCTURES FOR ALL SYNTH TYPES *****************************/

const char * const stnames[] = 
{
    "None",
    "VCO",
    "ADSR",
    "VCF",
    "LFO",
    "VCA",
    "Mixer",
    "Ring",
    "Noise",
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
    synth_parm_configuration_entry_noise,
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
    synth_type_process_noise,
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
    synth_note_start_noise,
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
    (void *) &synth_parm_noise_default,
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

static inline int32_t synth_process(synth_parm *sp, synth_unit *su, int note)
{
    return stp[(int)sp->stn.sut](sp, su, note);
}

void synth_start_note_val(uint8_t note_no, uint8_t velocity, int note)
{
    uint32_t vco = ((uint32_t)note_no)*(QUANTIZATION_MAX/MIDI_NOTES);
    synth_unit *su = synth_unit_entry(note, 0);
    memset(su,'\000',sizeof(synth_unit)*MAX_SYNTH_UNITS);
    synth_parm *sp = synth_parm_entry(0);
    for (int unit_no=0;unit_no<MAX_SYNTH_UNITS;unit_no++)
    {
        sns[(int)sp->stn.sut](sp, su, vco, velocity, note);
        su++;
        sp++;
    }
    synth_note_number[note] = note_no;
    synth_note_velocity[note] = velocity;
    synth_note_stopping[note] = false;
    synth_note_stopping_fast[note] = false;
    synth_note_stopping_counter[note] = 0;
    synth_note_count[note] = ++synth_current_note_count;
    DMB();
    mutex_enter_blocking(&note_mutexes[note]);
    synth_note_active[note] = true;
    DMB();
    mutex_exit(&note_mutexes[note]);
}

void synth_stop_note_now(int note)
{
    mutex_enter_blocking(&note_mutexes[note]);
    synth_note_stopping_counter[note] = SYNTH_STOPPING_COUNTER;
    DMB();
    synth_note_stopping_fast[note] = true;
    DMB();
    mutex_exit(&note_mutexes[note]);
    while (synth_note_active[note]) {};
}

void synth_start_note(uint8_t note_no, uint8_t velocity)
{
    for (int note=0;note<MAX_POLYPHONY;note++)
    {
        if ((synth_note_active[note]) && (synth_note_number[note] == note_no))
            synth_stop_note_now(note);
    }
    for (int note=0;note<MAX_POLYPHONY;note++)
    {
        if (!synth_note_active[note])
        {
            synth_start_note_val(note_no, velocity, note);
            return;
        }
    }
    int oldest_note = 0;
    uint32_t oldest_note_count = synth_note_count[0];
    for (int note=1;note<MAX_POLYPHONY;note++)
    {
        if (synth_note_count[note] < oldest_note_count)
        {
            oldest_note_count = synth_note_count[note];
            oldest_note = note;
        }
    }
    synth_stop_note_now(oldest_note);
    synth_start_note_val(note_no, velocity, oldest_note);
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

void synth_panic(void)
{
    synth_pitch_bend_value = 0;
    for (int note=0;note<MAX_POLYPHONY;note++)
        if (synth_note_active[note])
        {
            synth_note_stopping_counter[note] = pc.fail_delay;
            DMB();
            mutex_enter_blocking(&note_mutexes[note]);
            synth_note_stopping[note] = true;
            DMB();
            mutex_exit(&note_mutexes[note]);
        }
}

void synth_initialize(void)
{
    static bool is_mutex_initialized = false;
    
    if (!is_mutex_initialized) mutex_init(&synth_mutex);
    synth_current_note_count = 0;
    synth_pitch_bend_value = 0;
    for (int note=0;note<MAX_POLYPHONY;note++)
    {
        synth_note_active[note] = false;
        synth_note_stopping[note] = false;
        synth_note_stopping_fast[note] = false;
        synth_note_stopping_counter[note] = 0;
        synth_note_number[note] = 0;
        synth_note_velocity[note] = 0;
        synth_note_count[note] = 0;
        if (!is_mutex_initialized) mutex_init(&note_mutexes[note]);
    }
    for (int unit_number=0;unit_number<MAX_SYNTH_UNITS;unit_number++) 
        synth_unit_initialize(unit_number, SYNTH_TYPE_NONE);
    is_mutex_initialized = true;
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
            synth_parm *sp = synth_parms;
            synth_unit *su = synth_units[note];
            for (int unit_no=0;unit_no<MAX_SYNTH_UNITS;unit_no++)
            {
                sur[unit_no+1] = (sp->stn.sut == 0) ? sur[sp->stn.source_unit-1] : synth_process(sp, su, note);
                sp++;
                su++;
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

void synth_set_pitch_bend_value(uint32_t pitch_bend_value)
{
    synth_pitch_bend_value = ((int32_t)pitch_bend_value)*((QUANTIZATION_MAX*2)/16384)-QUANTIZATION_MAX;
}
