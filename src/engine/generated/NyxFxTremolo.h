/* ------------------------------------------------------------
name: "Tremolo"
Code generated with Faust 2.79.3 (https://faust.grame.fr)
Compilation options: -lang cpp -ct 1 -cn NyxFxTremolo -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __NyxFxTremolo_H__
#define  __NyxFxTremolo_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

#ifndef FAUSTCLASS
#define FAUSTCLASS NyxFxTremolo
#endif

#ifdef __APPLE__
#define exp10f __exp10f
#define exp10 __exp10
#endif

#if defined(_WIN32)
#define RESTRICT __restrict
#else
#define RESTRICT __restrict__
#endif

class NyxFxTremoloSIG0 {

  private:

	int iVec0[2];
	int iRec0[2];

  public:

	int getNumInputsNyxFxTremoloSIG0() {
		return 0;
	}
	int getNumOutputsNyxFxTremoloSIG0() {
		return 1;
	}

	void instanceInitNyxFxTremoloSIG0(int sample_rate) {
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			iVec0[l0] = 0;
		}
		for (int l1 = 0; l1 < 2; l1 = l1 + 1) {
			iRec0[l1] = 0;
		}
	}

	void fillNyxFxTremoloSIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = i1 + 1) {
			iVec0[0] = 1;
			iRec0[0] = (iVec0[1] + iRec0[1]) % 65536;
			table[i1] = std::sin(9.58738e-05f * float(iRec0[0]));
			iVec0[1] = iVec0[0];
			iRec0[1] = iRec0[0];
		}
	}

};

static NyxFxTremoloSIG0* newNyxFxTremoloSIG0() { return (NyxFxTremoloSIG0*)new NyxFxTremoloSIG0(); }
static void deleteNyxFxTremoloSIG0(NyxFxTremoloSIG0* dsp) { delete dsp; }

static float ftbl0NyxFxTremoloSIG0[65536];

class NyxFxTremolo : public dsp {

 private:

	int iVec1[2];
	int fSampleRate;
	float fConst0;
	float fConst1;
	float fConst2;
	FAUSTFLOAT fHslider0;
	float fRec2[2];
	float fConst3;
	float fRec1[2];
	FAUSTFLOAT fHslider1;
	float fRec3[2];

 public:
	NyxFxTremolo() {
	}

	void metadata(Meta* m) {
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.21.0");
		m->declare("compile_options", "-lang cpp -ct 1 -cn NyxFxTremolo -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("filename", "Tremolo.dsp");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.8.1");
		m->declare("name", "Tremolo");
		m->declare("oscillators.lib/name", "Faust Oscillator Library");
		m->declare("oscillators.lib/version", "1.6.0");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("signals.lib/name", "Faust Signal Routing Library");
		m->declare("signals.lib/version", "1.6.0");
	}

	virtual int getNumInputs() {
		return 2;
	}
	virtual int getNumOutputs() {
		return 2;
	}

	static void classInit(int sample_rate) {
		NyxFxTremoloSIG0* sig0 = newNyxFxTremoloSIG0();
		sig0->instanceInitNyxFxTremoloSIG0(sample_rate);
		sig0->fillNyxFxTremoloSIG0(65536, ftbl0NyxFxTremoloSIG0);
		deleteNyxFxTremoloSIG0(sig0);
	}

	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, float(fSampleRate)));
		fConst1 = 44.1f / fConst0;
		fConst2 = 1.0f - fConst1;
		fConst3 = 1.0f / fConst0;
	}

	virtual void instanceResetUserInterface() {
		fHslider0 = FAUSTFLOAT(4.0f);
		fHslider1 = FAUSTFLOAT(0.7f);
	}

	virtual void instanceClear() {
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			iVec1[l2] = 0;
		}
		for (int l3 = 0; l3 < 2; l3 = l3 + 1) {
			fRec2[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 2; l4 = l4 + 1) {
			fRec1[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = l5 + 1) {
			fRec3[l5] = 0.0f;
		}
	}

	virtual void init(int sample_rate) {
		classInit(sample_rate);
		instanceInit(sample_rate);
	}

	virtual void instanceInit(int sample_rate) {
		instanceConstants(sample_rate);
		instanceResetUserInterface();
		instanceClear();
	}

	virtual NyxFxTremolo* clone() {
		return new NyxFxTremolo();
	}

	virtual int getSampleRate() {
		return fSampleRate;
	}

	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("Tremolo");
		ui_interface->addHorizontalSlider("depth", &fHslider1, FAUSTFLOAT(0.7f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("rate_Hz", &fHslider0, FAUSTFLOAT(4.0f), FAUSTFLOAT(0.05f), FAUSTFLOAT(3e+01f), FAUSTFLOAT(0.01f));
		ui_interface->closeBox();
	}

	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = fConst1 * float(fHslider0);
		float fSlow1 = fConst1 * float(fHslider1);
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			iVec1[0] = 1;
			fRec2[0] = fSlow0 + fConst2 * fRec2[1];
			float fTemp0 = ((1 - iVec1[1]) ? 0.0f : fRec1[1] + fConst3 * fRec2[0]);
			fRec1[0] = fTemp0 - std::floor(fTemp0);
			fRec3[0] = fSlow1 + fConst2 * fRec3[1];
			float fTemp1 = 1.0f - 0.5f * fRec3[0] * (ftbl0NyxFxTremoloSIG0[std::max<int>(0, std::min<int>(int(65536.0f * fRec1[0]), 65535))] + 1.0f);
			output0[i0] = FAUSTFLOAT(float(input0[i0]) * fTemp1);
			output1[i0] = FAUSTFLOAT(float(input1[i0]) * fTemp1);
			iVec1[1] = iVec1[0];
			fRec2[1] = fRec2[0];
			fRec1[1] = fRec1[0];
			fRec3[1] = fRec3[0];
		}
	}

};

#endif
