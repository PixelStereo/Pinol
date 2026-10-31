// NyxMap stereo audio FX, using GRAME Faust standard library DSP.
import("stdfaust.lib");
time = hslider("time_ms",250,1,2000,1);
feedback = hslider("feedback",.35,0,.9,.001):si.smoo;
echo = (+ : de.sdelay(384000,256,time*ma.SR/1000)) ~ *(feedback);
process = echo,echo;
