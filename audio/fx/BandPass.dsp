// NyxMap stereo audio FX, using GRAME Faust standard library DSP.
import("stdfaust.lib");
cutoff = hslider("cutoff",1000,20,20000,1):si.smoo;
q = hslider("resonance",1,0.1,10,0.001):si.smoo;
filter = fi.resonbp(min(cutoff,ma.SR*.45),q,1);
process = filter,filter;
