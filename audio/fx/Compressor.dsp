// NyxMap stereo audio FX, using GRAME Faust standard library DSP.
import("stdfaust.lib");
ratio = hslider("ratio",4,1,20,.1);
threshold = hslider("threshold_dB",-18,-60,0,.1);
attack = hslider("attack_ms",10,.1,500,.1)/1000;
release = hslider("release_ms",100,1,3000,1)/1000;
makeup = hslider("makeup_dB",0,0,24,.1):si.smoo:ba.db2linear;
process = co.compressor_stereo(ratio,threshold,attack,release):*(makeup),*(makeup);
