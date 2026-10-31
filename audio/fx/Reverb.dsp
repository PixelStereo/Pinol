// NyxMap stereo audio FX, using GRAME Faust standard library DSP.
import("stdfaust.lib");
room = hslider("room",.6,0,.98,.001):si.smoo;
damp = hslider("damping",.5,0,1,.001):si.smoo;
process = re.stereo_freeverb(.7+room*.28,.5,damp,23):*(.15),*(.15);
