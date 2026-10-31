/* ------------------------------------------------------------
name: "Flanger"
Code generated with Faust 2.79.3 (https://faust.grame.fr)
Compilation options: -lang cpp -ct 1 -cn NyxFxFlanger -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __NyxFxFlanger_H__
#define  __NyxFxFlanger_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

#ifndef FAUSTCLASS
#define FAUSTCLASS NyxFxFlanger
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

class NyxFxFlangerSIG0 {

  private:

	int iVec2[2];
	int iRec2[2];

  public:

	int getNumInputsNyxFxFlangerSIG0() {
		return 0;
	}
	int getNumOutputsNyxFxFlangerSIG0() {
		return 1;
	}

	void instanceInitNyxFxFlangerSIG0(int sample_rate) {
		for (int l3 = 0; l3 < 2; l3 = l3 + 1) {
			iVec2[l3] = 0;
		}
		for (int l4 = 0; l4 < 2; l4 = l4 + 1) {
			iRec2[l4] = 0;
		}
	}

	void fillNyxFxFlangerSIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = i1 + 1) {
			iVec2[0] = 1;
			iRec2[0] = (iVec2[1] + iRec2[1]) % 65536;
			table[i1] = std::sin(9.58738e-05f * float(iRec2[0]));
			iVec2[1] = iVec2[0];
			iRec2[1] = iRec2[0];
		}
	}

};

static NyxFxFlangerSIG0* newNyxFxFlangerSIG0() { return (NyxFxFlangerSIG0*)new NyxFxFlangerSIG0(); }
static void deleteNyxFxFlangerSIG0(NyxFxFlangerSIG0* dsp) { delete dsp; }

static float ftbl0NyxFxFlangerSIG0[65536];

class NyxFxFlanger : public dsp {

 private:

	int iVec0[2];
	int fSampleRate;
	float fConst0;
	float fConst1;
	float fConst2;
	FAUSTFLOAT fHslider0;
	float fRec1[2];
	int IOTA0;
	float fVec1[16384];
	FAUSTFLOAT fHslider1;
	float fRec4[2];
	float fConst3;
	float fRec3[2];
	FAUSTFLOAT fHslider2;
	float fRec5[2];
	float fConst4;
	float fRec0[2];
	float fVec3[16384];
	float fConst5;
	float fRec7[2];
	float fRec6[2];

 public:
	NyxFxFlanger() {
	}

	void metadata(Meta* m) {
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.21.0");
		m->declare("compile_options", "-lang cpp -ct 1 -cn NyxFxFlanger -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("delays.lib/name", "Faust Delay Library");
		m->declare("delays.lib/version", "1.1.0");
		m->declare("filename", "Flanger.dsp");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.8.1");
		m->declare("name", "Flanger");
		m->declare("oscillators.lib/name", "Faust Oscillator Library");
		m->declare("oscillators.lib/version", "1.6.0");
		m->declare("phaflangers.lib/name", "Faust Phaser and Flanger Library");
		m->declare("phaflangers.lib/version", "1.1.0");
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
		NyxFxFlangerSIG0* sig0 = newNyxFxFlangerSIG0();
		sig0->instanceInitNyxFxFlangerSIG0(sample_rate);
		sig0->fillNyxFxFlangerSIG0(65536, ftbl0NyxFxFlangerSIG0);
		deleteNyxFxFlangerSIG0(sig0);
	}

	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, float(fSampleRate)));
		fConst1 = 44.1f / fConst0;
		fConst2 = 1.0f - fConst1;
		fConst3 = 1.0f / fConst0;
		fConst4 = 0.001f * fConst0;
		fConst5 = 1.03f / fConst0;
	}

	virtual void instanceResetUserInterface() {
		fHslider0 = FAUSTFLOAT(0.2f);
		fHslider1 = FAUSTFLOAT(0.25f);
		fHslider2 = FAUSTFLOAT(2.0f);
	}

	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			iVec0[l0] = 0;
		}
		for (int l1 = 0; l1 < 2; l1 = l1 + 1) {
			fRec1[l1] = 0.0f;
		}
		IOTA0 = 0;
		for (int l2 = 0; l2 < 16384; l2 = l2 + 1) {
			fVec1[l2] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = l5 + 1) {
			fRec4[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = l6 + 1) {
			fRec3[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = l7 + 1) {
			fRec5[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = l8 + 1) {
			fRec0[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 16384; l9 = l9 + 1) {
			fVec3[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 2; l10 = l10 + 1) {
			fRec7[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 2; l11 = l11 + 1) {
			fRec6[l11] = 0.0f;
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

	virtual NyxFxFlanger* clone() {
		return new NyxFxFlanger();
	}

	virtual int getSampleRate() {
		return fSampleRate;
	}

	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("Flanger");
		ui_interface->addHorizontalSlider("depth_ms", &fHslider2, FAUSTFLOAT(2.0f), FAUSTFLOAT(0.1f), FAUSTFLOAT(5.0f), FAUSTFLOAT(0.1f));
		ui_interface->addHorizontalSlider("feedback", &fHslider0, FAUSTFLOAT(0.2f), FAUSTFLOAT(0.0f), FAUSTFLOAT(0.9f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("rate_Hz", &fHslider1, FAUSTFLOAT(0.25f), FAUSTFLOAT(0.05f), FAUSTFLOAT(5.0f), FAUSTFLOAT(0.01f));
		ui_interface->closeBox();
	}

	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = fConst1 * float(fHslider0);
		float fSlow1 = fConst1 * float(fHslider1);
		float fSlow2 = fConst1 * float(fHslider2);
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			float fTemp0 = float(input0[i0]);
			iVec0[0] = 1;
			fRec1[0] = fSlow0 + fConst2 * fRec1[1];
			float fTemp1 = fRec1[0] * fRec0[1] - fTemp0;
			fVec1[IOTA0 & 16383] = fTemp1;
			int iTemp2 = 1 - iVec0[1];
			fRec4[0] = fSlow1 + fConst2 * fRec4[1];
			float fTemp3 = ((iTemp2) ? 0.0f : fRec3[1] + fConst3 * fRec4[0]);
			fRec3[0] = fTemp3 - std::floor(fTemp3);
			fRec5[0] = fSlow2 + fConst2 * fRec5[1];
			float fTemp4 = fConst4 * (fRec5[0] * ftbl0NyxFxFlangerSIG0[std::max<int>(0, std::min<int>(int(65536.0f * fRec3[0]), 65535))] + 6.0f);
			int iTemp5 = int(fTemp4);
			float fTemp6 = std::floor(fTemp4);
			fRec0[0] = fVec1[(IOTA0 - std::min<int>(8193, std::max<int>(0, iTemp5))) & 16383] * (fTemp6 + (1.0f - fTemp4)) + (fTemp4 - fTemp6) * fVec1[(IOTA0 - std::min<int>(8193, std::max<int>(0, iTemp5 + 1))) & 16383];
			output0[i0] = FAUSTFLOAT(0.5f * (fTemp0 + fRec0[0]));
			float fTemp7 = float(input1[i0]);
			float fTemp8 = fRec1[0] * fRec6[1] - fTemp7;
			fVec3[IOTA0 & 16383] = fTemp8;
			float fTemp9 = ((iTemp2) ? 0.0f : fRec7[1] + fConst5 * fRec4[0]);
			fRec7[0] = fTemp9 - std::floor(fTemp9);
			float fTemp10 = fConst4 * (fRec5[0] * ftbl0NyxFxFlangerSIG0[std::max<int>(0, std::min<int>(int(65536.0f * fRec7[0]), 65535))] + 6.0f);
			int iTemp11 = int(fTemp10);
			float fTemp12 = std::floor(fTemp10);
			fRec6[0] = fVec3[(IOTA0 - std::min<int>(8193, std::max<int>(0, iTemp11))) & 16383] * (fTemp12 + (1.0f - fTemp10)) + (fTemp10 - fTemp12) * fVec3[(IOTA0 - std::min<int>(8193, std::max<int>(0, iTemp11 + 1))) & 16383];
			output1[i0] = FAUSTFLOAT(0.5f * (fTemp7 + fRec6[0]));
			iVec0[1] = iVec0[0];
			fRec1[1] = fRec1[0];
			IOTA0 = IOTA0 + 1;
			fRec4[1] = fRec4[0];
			fRec3[1] = fRec3[0];
			fRec5[1] = fRec5[0];
			fRec0[1] = fRec0[0];
			fRec7[1] = fRec7[0];
			fRec6[1] = fRec6[0];
		}
	}

};

#endif
