// NyxMap instrument, implemented with the GRAME Faust standard libraries.
// The DSP backend, noise algorithms, ADSR, oscillators and granular delay pitch
// shifting come from Faust. Generated C++ is checked in for portable builds.
import("stdfaust.lib");
freq = hslider("frequency",220,20,8000,.01):si.smoo;
ratio = hslider("ratio",2,.01,16,.001):si.smoo;
depth = hslider("index",2,0,20,.01):si.smoo;
gain = hslider("gain",.2,0,1,.001):si.smoo;
size = hslider("grainMs",80,5,500,.01):si.smoo;
density = hslider("density",15,1,100,.01):si.smoo;
pitch = hslider("pitch",1,.125,8,.001):si.smoo;
attack = hslider("attack",.01,.001,10,.001);
decay = hslider("decay",.1,.001,10,.001);
sustain = hslider("sustain",.8,0,1,.001);
release = hslider("release",.1,.001,10,.001);
gate = checkbox("gate");
kind = nentry("_kind",0,0,4,1);
speed = hslider("_speed",1,0,8,.001):si.smoo;
fm(f,d) = os.osc(min(20000,f*speed) + os.osc(min(20000,f*ratio*speed))*d*min(20000,f*ratio*speed));
phase = os.phasor(1,density*speed);
window = (phase < min(1,size*density/1000)) * (0.5-0.5*cos(2*ma.PI*min(1,phase/max(.001,min(1,size*density/1000)))));
hasSample = checkbox("_hasSample");
grain(x) = (x + (1-hasSample)*os.osc(freq*speed)) : ef.transpose(min(30000,size*ma.SR/1000),max(1,min(15000,size*ma.SR/2000)),12*log(max(.001,pitch))/log(2)) : *(window);
voice(x,r,g,b) = ba.selectn(5,kind,fm(freq,depth),grain(x),
    fm(freq*pow(2,2*(r-b)),depth*g)*(.2126*r+.7152*g+.0722*b),no.noise,no.pink_noise);
process(x,r,g,b) = voice(x,r:si.smoo,g:si.smoo,b:si.smoo)*gain*en.adsr(attack,decay,sustain,release,gate);
