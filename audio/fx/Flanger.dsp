// NyxMap stereo audio FX, using GRAME Faust standard library DSP.
import("stdfaust.lib");
rate = hslider("rate_Hz",.25,.05,5,.01):si.smoo;
depth = hslider("depth_ms",2,.1,5,.1):si.smoo;
feedback = hslider("feedback",.2,0,.9,.001):si.smoo;
left = (6+depth*os.osc(rate))*ma.SR/1000;
right = (6+depth*os.osc(rate*1.03))*ma.SR/1000;
process = pf.flanger_stereo(8192,left,right,1,feedback,0);
