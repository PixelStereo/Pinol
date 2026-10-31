// NyxMap stereo audio FX, using GRAME Faust standard library DSP.
import("stdfaust.lib");
cutoff = hslider("cutoff",2000,20,20000,1):si.smoo;
q = hslider("resonance",0.707,0.1,10,0.001):si.smoo;
filter = fi.resonlp(min(cutoff,ma.SR*.45),q,1);
process = filter,filter;
