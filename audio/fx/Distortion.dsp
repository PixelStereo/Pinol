// NyxMap stereo audio FX, using GRAME Faust standard library DSP.
import("stdfaust.lib");
drive = hslider("drive",.5,0,1,.001):si.smoo;
tone = hslider("tone",6000,50,20000,1):si.smoo;
fx = ef.cubicnl_nodc(drive,0):fi.lowpass(2,min(tone,ma.SR*.45));
process = fx,fx;
