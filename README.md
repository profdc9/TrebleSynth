# Treble Synth

A video about the TrebleSynth:  [Video](https://www.youtube.com/watch?v=252Vte4tYh0)

The TrebleSynth is a "Modular Synth on a Chip" based on the Raspberry Pi Pico.  It is a through-hole design that is easy to assemble yourself and uses only inexpensive generic parts that are widely available to minimize costs.  One can create various modules on the chip and interconnect them by specifying which unit takes input from another input, assign the controls of these modules to potentiometers, and then adjust the potentiometers to achieve your desired sound.  It also has a fully capable effects module on the chip as well.  The modular synth supports six note polyphony.

The synth has a 25 key, two-octave keyboard (though external MIDI controllers are highly recommended, as the built-in synth keys are rudimentary and more suitable for testing).  The TrebleSynth accepts notes over serial MIDI input and outputs the synth keyboard presses over the serial MIDI output.  There is also a USB MIDI input/output, and notes can be played from a computer to the TrebleSynth over USB and similarly the synth keyboard presses are sent over the USB MIDI output.  The are two jacks available for expression and selection pedals.

When plugged into a USB port, the TrebleSynth enumerates both as a MIDI device and a COM port.  The COM port interface has a text-based interface that may be used to read and set configuration data from a PC.   Type "HELP" for a list of the commands.  For example, typing "ECONF 0 0" lists all of the current effects configuration data, and "SCONF 0 0" lists all of the current synth configuration data.  The data is output in the form of the commands used to reprogram the same state back into the device, so these may be directly copied into a text file and pasted back into a terminal to recreate the configuration.  At some point I (or some volunteer...) could write a graphical user interface that allows external configuration of the modules.

A peculiarity of the device is that, because it uses potentiometers, when loading a saved configuration, the potentiometers may not be at the same positions when the configuration was saved.  When adjusting a potentiometer, the unit number and type of control is displayed.  In addition, initially after the configuration is loaded, the control does not adjust its corresponding parameter until the control is turned back to the original position it was at when the configuration was saved.  A symbol "O" is shown when the control is adjusted to the point where the parameter is being adjusted again.  The symbols "->" and "<-" show that the control should be turned clockwise ("->") or counterclockwise ("<-") until a dot ("O") is displayed, and likewise if ">" is shown, the control should be turned down until "\*" is displayed.

Each module has a "SourceUnit" and "ControlUnit."  The source unit is the unit that the module receives audio input from, and the control unit is the unit that the module receives parameter data from (for example, to adjust pitch for the VCO or amplitude for the VCA).  The unit number of 1 corresponds a zero-valued source (zero voltage signal source).  A unit number of 2 or above corresponds to the input received from the previous module.  So for example, a SourceUnit for 4 would be the output of unit 3 (which normally is input to unit 4, unless unit 4's SourceUnit is otherwise changed).  For example, the mixer has two SourceUnits, one of which defaults to its own unit number (so that it receives the input from the previous unit), and another that can be set to another previous unit, so that the output of these units can be combined.

The code is all licensed under the zlib license (no warranty, yes commercial and non-commercial use).  The PCB is licensed under CC-BY-SA 4.0.  A new, highly efficient, 15-bit fixed-point precision synthesizer engine, specifically suited for Cortex M0+ processors with no floating point or 64-bit integer multiply or divide, was written for this project.  Because the samples are processed in real time, the lag due to processing is about 100 microseconds.  In order to achieve this, the Pi Pico is overclocked to 250 MHz with no seemingly bad effects.

The type of modules that one can create are:

1. VCO (Voltage Controlled Oscillator).  Eight waveforms are included: sine, half-sine, triangle, sawtooth, 1/2 duty cycle square wave, 1/4 duty cycle square wave, 1/8 duty cycle square wave.  The pitch can be modulated by other modules so that FM synthesis can be performed, or vibrato effects can be created.
2. ADSR (Attack Decay Sustain Release).  A envelope module that can be used to modulate the amplitude of a signal, or output the envelope directly so that, for example, an envelope may be used to modulate the pitch of the signal.
3. VCF (Voltage Controlled Filter).  This is a up to four-pole lowpass filter modeled loosely on the 24 dB/octave Moog ladder filter.  It has an adjustable fixed frequency cutoff, or the frequency cutoff is determined by the note being played (frequency=0).
4. LFO (Low Frequency Oscillator).  An oscillator with a frequency that does not depend on the note frequency.  This can be used, for example, to implement vibrato and tremolo effects when used to modulate other units.
5. VCA (Voltage Controlled Amplifier).  A voltage controlled amplifier that uses the output of another module to modulate the gain.  Additional gain may be applied with a VCA as well.
6. Mixer.  Can be used to combine the output signals of two modules.  For example, two separate VCO FM synthesis chains can be combined together.  The mixing fraction can be modulated (for example by LFO or ADSR).
7. Ring.  A ring modulator where the signal from one module four-quadrant multiplies another module.
8. VDO (Variable Duty-Cycle Oscillator)  A square or sawtooth wave oscillator with a duty cycle that can be modulated (for example by an ADSR or LFO) to change the timbre.  The pitch can be modulated as well (for vibrato or chirp effects).
9. Noise.  A noise source that can be used to create percussive effects.

Up to 10 modules can be used at a time.  Subtractive and FM synthesis is possible with various configurations of the modules. It also includes these effect taken from the GuitarPico ( https://www.github.com/profdc9/GuitarPico ) project:

1.  Noise Gate
2.  Delay 
3.  Room / Reverb (set up specific echoes at particular delays)
4.  Combine (mix together the signals from several effects)
5.  2nd order Bandpass filter
6.  2nd order Lowpass filter
7.  2nd order Highpass filter
8.  2nd order Allpass filter
9.  Tremolo (amplitude modulated by a low frequency oscillator)
10.  Vibrato (pitch modulated by a low frequency oscillator)
11.  Wah (bandpass filter with center frequency controlled by an external control like a pedal)
12.  AutoWah (bandpass filter with center frequency modulated by a low frequency oscillator)
13.  Envelope (bandpass filter with center frequency modulated by signal amplitude)
14.  Distortion (amplification resulting in saturation of the signal)
15.  Overdrive (selectable threshold for low signal / high signal amplitude gain)
16.  Compressor (amplifies weak signals to equal out overall amplitude of signal)
17.  Ring (ring modulator using low frequency oscillator)
18.  Flanger (pitch modulated by a low frequency oscillator, with feedback and combined with unmodulated signal)
19.  Chorus (pitch modulated by a low frequency oscillator, no feedback and combined with unmodulated signal)
20.  Phaser (signal run through multiple stages of all pass filters, combined with unmodulated signal)
21.  Backwards (plays the last samples backwards for weird swooping effect)
22.  PitchShift (allows shifting the pitch by variable amounts, useful for harmony-like effect)
23.  Whammy (pitch shift based on external control like a pedal)
24.  Octave (rectification and amplification of the signal with extreme distortion)
25.  Sinusoidal Oscillator (built in test signal source)

The effects may be cascaded, to up to 16 in a sequence.  Since the synthesizer has an audio input, at some point I will likely implement audio pass-through so the TrebleSynth can act as a guitar pedal as well like the GuitarPico.

![Picture](pics/TrebleSynth.jpg)

A list of parts is included below, with [LCSC](https://lcsc.com) part numbers (with noted exceptions), minimum quantity order, and prices as of the time of writing (2024-06-24).  For some of the resistors and capacitors, you may be better off buying an assortment kit rather than ordering large quantities.

1.  Raspberry Pi Pico X 1 ($4.00 from Sparkfun DEV-17829, $5.00 from Adafruit 4883)
2.  CD4051 ($0.152/pc, QTY 5 C507164)
3.  LM358 ($0.1167/pc, QTY 5, C434570)
4.  2k resistor 1/4 watt metal film ($0.0085/pc, QTY 100, C410663)
5.  1k resistor 1/4 watt metal film ($0.0060/pc, QTY 100, C713997)
6.  2N3904 ($0.0149/pc, QTY 200, C118538)
7.  16 pin DIP socket ($0.031/pc, QTY 30, C72115)
8.  10 ohm resistor 1/4 watt metal film ($0.0147/pc, QTY 50, C1365579)
9.  PC817 optocoupler ($0.0647/pc, QTY 50, C2840441)
10.  Female straight header ($0.28/pc, QTY 20, C343636)
11.  Male straight header ($0.078/pc, QTY 50, C429959)
12.  6 mm button ($0.011/pc, QTY 100, C5340135)
13.  RK09K 10k potentiometer ($0.69/pc, QTY 20, C209779) - note Aliexpress has much cheaper alternatives
14.  RK09Y 10k potentiometer ($0.42/pc, QTY 1, C470717) - note Aliexpress has much cheaper alternatives
15.  220 ohm resistor 1/4 watt metal film ($0.0094/pc, QTY 100, C127220)
16.  2k2 resistor 1/4 watt metal film ($0.0058/pc, QTY 100, C714002)
17.  4k7 resistor 1/4 watt metal film ($0.0109/pc, QTY 100, C119339)
18.  22k resistor 1/4 watt metal film ($0.0054/pc, QTY 100, C601722)
19.  1M resistor 1/4 watt metal film ($0.0106/pc, QTY 100, C119389)
20.  100k resistor 1/4 watt metal film ($0.0094/pc, QTY 100, C119369)
21.  L7805CV regulator ($0.19/pc, QTY 1, C111887)
22.  2 pin terminal connector ($0.0842/pc, QTY 5, C474881)
23.  MIDI DIN 5-pin female socket ($0.2556/pc, QTY 5, C2939344)
24.  TRS female jack ($0.1536/pc, QTY 5, C145814)
25.  Dc barrel jack power connector ($0.0510, QTY 10, C16214)
26.  SD Card connector ($0.156/pc, QTY 5, C91145)
27.  TL431L ($0.0361/pc, QTY 10, C5155530)
28.  10k resistor 1/4 watt metal film ($0.0102, QTY 50, C57436)
29.  100 pF 50+ V ceramic disc acpacitor ($0.0092/pc, QTY 50, C2761725)
30.  10 nF 50+ V ceramic disc capacitor ($0.0106/pc, QTY 50, C2761727)
31.  100 nF 50+ V ceramic disc capacitor ($0.0117/pc, QTY 50, C254094)
32.  2200 pF 50+ V ceramic disc capacitor ($0.0175/pc, QTY 50, C2976625)
33.  4700 pF 50+ V ceramic disc capacitor ($0.0209/pc, QTY 20, C2896052)
34.  22 nF 50+ V ceramic disc capacitor ($0.0044/pc, QTY 100, C377845)
35.  1000 pF 50+ V ceramic disc capacitor ($0.0092/pc, QTY 100, C2832504)
36.  10 uF 50V+ polarized electrolytic capacitor ($0.0134/pc, QTY 50, C43345)
37.  100 uF 25V+ polarized electrolytic capacitor ($0.0148/pc, QTY 50, C47873)
38.  220 uF 16V+ polarized electrolytic capacitor ($0.0201/pc, QTY 20, C43349)
39.  1N4148 diode ($0.0070/pc, QTY 100, C19190385)
40.  1N5819 Schottky diode ($0.031/pc, QTY 20, C5295404)
41.  470 ohm 1/4 watt metal film ($0.0091/pc, QTY 50, C129899)
42.  22 pF 50+ V ceramic disc capacitor ($0.0092/pc, QTY 50, C2832510)
43.  SSD1306 128x64 OLED display (4 pin I2C version) - available on Aliexpress or Amazon
44.  8 pin DIP socket ($0.0240/pc, QTY 20, C72124)
