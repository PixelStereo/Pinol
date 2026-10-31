// NyxMap stereo audio FX, using GRAME Faust standard library DSP.
import("stdfaust.lib");
bass = hslider("bass_dB",0,-18,18,.1):si.smoo;
mid = hslider("mid_dB",0,-18,18,.1):si.smoo;
treble = hslider("treble_dB",0,-18,18,.1):si.smoo;
eq = fi.low_shelf(bass,200):fi.peak_eq(mid,1000,1000):fi.high_shelf(treble,min(5000,ma.SR*.4));
process = eq,eq;
