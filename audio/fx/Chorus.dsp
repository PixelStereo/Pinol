// NyxMap stereo audio FX, using GRAME Faust standard library DSP.
import("stdfaust.lib");
rate = hslider("rate_Hz",.8,.05,10,.01):si.smoo;
depth = hslider("depth_ms",4,.1,10,.1):si.smoo;
left = (12+depth*os.osc(rate))*ma.SR/1000;
right = (12+depth*os.osc(rate*1.07))*ma.SR/1000;
process = de.fdelay(8192,left),de.fdelay(8192,right);
