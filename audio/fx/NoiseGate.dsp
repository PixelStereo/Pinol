// NyxMap stereo audio FX, using GRAME Faust standard library DSP.
import("stdfaust.lib");
threshold = hslider("threshold_dB",-45,-96,0,.1);
attack = hslider("attack_ms",2,.1,500,.1)/1000;
hold = hslider("hold_ms",50,0,1000,1)/1000;
release = hslider("release_ms",100,1,3000,1)/1000;
process = ef.gate_stereo(threshold,attack,hold,release);
