// NyxMap stereo audio FX, using GRAME Faust standard library DSP.
import("stdfaust.lib");
rate = hslider("rate_Hz",4,.05,30,.01):si.smoo;
depth = hslider("depth",.7,0,1,.001):si.smoo;
gain = 1-depth*(.5+.5*os.osc(rate));
process = *(gain),*(gain);
