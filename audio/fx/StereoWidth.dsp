// NyxMap stereo audio FX, using GRAME Faust standard library DSP.
import("stdfaust.lib");
width = hslider("width",1,0,2,.001):si.smoo;
process(l,r) = m+width*s,m-width*s with { m=(l+r)*.5;s=(l-r)*.5; };
