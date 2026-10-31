/* ------------------------------------------------------------
name: "NyxSynth"
Code generated with Faust 2.79.3 (https://faust.grame.fr)
Compilation options: -lang cpp -ct 1 -cn NyxFaustSynth -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __NyxFaustSynth_H__
#define  __NyxFaustSynth_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

#ifndef FAUSTCLASS
#define FAUSTCLASS NyxFaustSynth
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

class NyxFaustSynthSIG0 {

  private:

	int iVec2[2];
	int iRec2[2];

  public:

	int getNumInputsNyxFaustSynthSIG0() {
		return 0;
	}
	int getNumOutputsNyxFaustSynthSIG0() {
		return 1;
	}

	void instanceInitNyxFaustSynthSIG0(int sample_rate) {
		for (int l4 = 0; l4 < 2; l4 = l4 + 1) {
			iVec2[l4] = 0;
		}
		for (int l5 = 0; l5 < 2; l5 = l5 + 1) {
			iRec2[l5] = 0;
		}
	}

	void fillNyxFaustSynthSIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = i1 + 1) {
			iVec2[0] = 1;
			iRec2[0] = (iVec2[1] + iRec2[1]) % 65536;
			table[i1] = std::sin(9.58738e-05f * float(iRec2[0]));
			iVec2[1] = iVec2[0];
			iRec2[1] = iRec2[0];
		}
	}

};

static NyxFaustSynthSIG0* newNyxFaustSynthSIG0() { return (NyxFaustSynthSIG0*)new NyxFaustSynthSIG0(); }
static void deleteNyxFaustSynthSIG0(NyxFaustSynthSIG0* dsp) { delete dsp; }

static float ftbl0NyxFaustSynthSIG0[65536];

class NyxFaustSynth : public dsp {

 private:

	int iVec0[2];
	FAUSTFLOAT fCheckbox0;
	float fVec1[2];
	int iRec0[2];
	FAUSTFLOAT fHslider0;
	int fSampleRate;
	float fConst0;
	float fRec1[2];
	FAUSTFLOAT fHslider1;
	FAUSTFLOAT fHslider2;
	FAUSTFLOAT fHslider3;
	FAUSTFLOAT fEntry0;
	float fConst1;
	float fConst2;
	FAUSTFLOAT fHslider4;
	float fRec5[2];
	FAUSTFLOAT fHslider5;
	float fRec6[2];
	FAUSTFLOAT fHslider6;
	float fRec7[2];
	float fConst3;
	float fRec4[2];
	FAUSTFLOAT fHslider7;
	float fRec8[2];
	float fRec3[2];
	FAUSTFLOAT fHslider8;
	float fRec9[2];
	FAUSTFLOAT fHslider9;
	float fRec10[2];
	float fRec11[2];
	float fConst4;
	FAUSTFLOAT fHslider10;
	float fRec13[2];
	float fConst5;
	float fRec12[2];
	float fRec14[2];
	FAUSTFLOAT fCheckbox1;
	int IOTA0;
	float fVec3[131072];
	float fRec15[2];
	float fRec16[2];
	float fRec17[2];
	float fRec19[2];
	float fRec18[2];
	int iRec20[2];
	float fRec21[4];
	FAUSTFLOAT fHslider11;
	float fRec22[2];

 public:
	NyxFaustSynth() {
	}

	void metadata(Meta* m) {
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.21.0");
		m->declare("compile_options", "-lang cpp -ct 1 -cn NyxFaustSynth -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("delays.lib/name", "Faust Delay Library");
		m->declare("delays.lib/version", "1.1.0");
		m->declare("envelopes.lib/adsr:author", "Yann Orlarey and Andrey Bundin");
		m->declare("envelopes.lib/author", "GRAME");
		m->declare("envelopes.lib/copyright", "GRAME");
		m->declare("envelopes.lib/license", "LGPL with exception");
		m->declare("envelopes.lib/name", "Faust Envelope Library");
		m->declare("envelopes.lib/version", "1.3.0");
		m->declare("filename", "NyxSynth.dsp");
		m->declare("filters.lib/fir:author", "Julius O. Smith III");
		m->declare("filters.lib/fir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/fir:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/iir:author", "Julius O. Smith III");
		m->declare("filters.lib/iir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/iir:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/lowpass0_highpass1", "MIT-style STK-4.3 license");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/version", "1.7.1");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.8.1");
		m->declare("misceffects.lib/name", "Misc Effects Library");
		m->declare("misceffects.lib/version", "2.5.1");
		m->declare("name", "NyxSynth");
		m->declare("noises.lib/name", "Faust Noise Generator Library");
		m->declare("noises.lib/version", "1.4.1");
		m->declare("oscillators.lib/name", "Faust Oscillator Library");
		m->declare("oscillators.lib/version", "1.6.0");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("signals.lib/name", "Faust Signal Routing Library");
		m->declare("signals.lib/version", "1.6.0");
	}

	virtual int getNumInputs() {
		return 4;
	}
	virtual int getNumOutputs() {
		return 1;
	}

	static void classInit(int sample_rate) {
		NyxFaustSynthSIG0* sig0 = newNyxFaustSynthSIG0();
		sig0->instanceInitNyxFaustSynthSIG0(sample_rate);
		sig0->fillNyxFaustSynthSIG0(65536, ftbl0NyxFaustSynthSIG0);
		deleteNyxFaustSynthSIG0(sig0);
	}

	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, float(fSampleRate)));
		fConst1 = 44.1f / fConst0;
		fConst2 = 1.0f - fConst1;
		fConst3 = 1.0f / fConst0;
		fConst4 = 0.0005f * fConst0;
		fConst5 = 0.001f * fConst0;
	}

	virtual void instanceResetUserInterface() {
		fCheckbox0 = FAUSTFLOAT(0.0f);
		fHslider0 = FAUSTFLOAT(0.1f);
		fHslider1 = FAUSTFLOAT(0.01f);
		fHslider2 = FAUSTFLOAT(0.1f);
		fHslider3 = FAUSTFLOAT(0.8f);
		fEntry0 = FAUSTFLOAT(0.0f);
		fHslider4 = FAUSTFLOAT(2.0f);
		fHslider5 = FAUSTFLOAT(1.0f);
		fHslider6 = FAUSTFLOAT(2.2e+02f);
		fHslider7 = FAUSTFLOAT(2.0f);
		fHslider8 = FAUSTFLOAT(15.0f);
		fHslider9 = FAUSTFLOAT(8e+01f);
		fHslider10 = FAUSTFLOAT(1.0f);
		fCheckbox1 = FAUSTFLOAT(0.0f);
		fHslider11 = FAUSTFLOAT(0.2f);
	}

	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			iVec0[l0] = 0;
		}
		for (int l1 = 0; l1 < 2; l1 = l1 + 1) {
			fVec1[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			iRec0[l2] = 0;
		}
		for (int l3 = 0; l3 < 2; l3 = l3 + 1) {
			fRec1[l3] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = l6 + 1) {
			fRec5[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = l7 + 1) {
			fRec6[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 2; l8 = l8 + 1) {
			fRec7[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = l9 + 1) {
			fRec4[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 2; l10 = l10 + 1) {
			fRec8[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 2; l11 = l11 + 1) {
			fRec3[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 2; l12 = l12 + 1) {
			fRec9[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = l13 + 1) {
			fRec10[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 2; l14 = l14 + 1) {
			fRec11[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 2; l15 = l15 + 1) {
			fRec13[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 2; l16 = l16 + 1) {
			fRec12[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 2; l17 = l17 + 1) {
			fRec14[l17] = 0.0f;
		}
		IOTA0 = 0;
		for (int l18 = 0; l18 < 131072; l18 = l18 + 1) {
			fVec3[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 2; l19 = l19 + 1) {
			fRec15[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 2; l20 = l20 + 1) {
			fRec16[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 2; l21 = l21 + 1) {
			fRec17[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 2; l22 = l22 + 1) {
			fRec19[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 2; l23 = l23 + 1) {
			fRec18[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 2; l24 = l24 + 1) {
			iRec20[l24] = 0;
		}
		for (int l25 = 0; l25 < 4; l25 = l25 + 1) {
			fRec21[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 2; l26 = l26 + 1) {
			fRec22[l26] = 0.0f;
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

	virtual NyxFaustSynth* clone() {
		return new NyxFaustSynth();
	}

	virtual int getSampleRate() {
		return fSampleRate;
	}

	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("NyxSynth");
		ui_interface->addCheckButton("_hasSample", &fCheckbox1);
		ui_interface->addNumEntry("_kind", &fEntry0, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(4.0f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("_speed", &fHslider5, FAUSTFLOAT(1.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(8.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("attack", &fHslider1, FAUSTFLOAT(0.01f), FAUSTFLOAT(0.001f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("decay", &fHslider2, FAUSTFLOAT(0.1f), FAUSTFLOAT(0.001f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("density", &fHslider8, FAUSTFLOAT(15.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(1e+02f), FAUSTFLOAT(0.01f));
		ui_interface->addHorizontalSlider("frequency", &fHslider6, FAUSTFLOAT(2.2e+02f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(8e+03f), FAUSTFLOAT(0.01f));
		ui_interface->addHorizontalSlider("gain", &fHslider11, FAUSTFLOAT(0.2f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addCheckButton("gate", &fCheckbox0);
		ui_interface->addHorizontalSlider("grainMs", &fHslider9, FAUSTFLOAT(8e+01f), FAUSTFLOAT(5.0f), FAUSTFLOAT(5e+02f), FAUSTFLOAT(0.01f));
		ui_interface->addHorizontalSlider("index", &fHslider7, FAUSTFLOAT(2.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(0.01f));
		ui_interface->addHorizontalSlider("pitch", &fHslider10, FAUSTFLOAT(1.0f), FAUSTFLOAT(0.125f), FAUSTFLOAT(8.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("ratio", &fHslider4, FAUSTFLOAT(2.0f), FAUSTFLOAT(0.01f), FAUSTFLOAT(16.0f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("release", &fHslider0, FAUSTFLOAT(0.1f), FAUSTFLOAT(0.001f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addHorizontalSlider("sustain", &fHslider3, FAUSTFLOAT(0.8f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->closeBox();
	}

	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* input2 = inputs[2];
		FAUSTFLOAT* input3 = inputs[3];
		FAUSTFLOAT* output0 = outputs[0];
		float fSlow0 = float(fCheckbox0);
		int iSlow1 = fSlow0 == 0.0f;
		float fSlow2 = 1.0f / std::max<float>(1.0f, fConst0 * float(fHslider0));
		float fSlow3 = std::max<float>(1.0f, fConst0 * float(fHslider1));
		float fSlow4 = 1.0f / fSlow3;
		float fSlow5 = float(fHslider3);
		float fSlow6 = (1.0f - fSlow5) / std::max<float>(1.0f, fConst0 * float(fHslider2));
		float fSlow7 = float(fEntry0);
		int iSlow8 = fSlow7 >= 3.0f;
		int iSlow9 = fSlow7 >= 2.0f;
		int iSlow10 = fSlow7 >= 1.0f;
		float fSlow11 = fConst1 * float(fHslider4);
		float fSlow12 = fConst1 * float(fHslider5);
		float fSlow13 = fConst1 * float(fHslider6);
		float fSlow14 = fConst1 * float(fHslider7);
		float fSlow15 = fConst1 * float(fHslider8);
		float fSlow16 = fConst1 * float(fHslider9);
		float fSlow17 = fConst1 * float(fHslider10);
		float fSlow18 = 1.0f - float(fCheckbox1);
		int iSlow19 = fSlow7 >= 4.0f;
		float fSlow20 = fConst1 * float(fHslider11);
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			iVec0[0] = 1;
			fVec1[0] = fSlow0;
			iRec0[0] = iSlow1 * (iRec0[1] + 1);
			fRec1[0] = fSlow0 + fRec1[1] * float(fVec1[1] >= fSlow0);
			int iTemp0 = 1 - iVec0[1];
			fRec5[0] = fSlow11 + fConst2 * fRec5[1];
			fRec6[0] = fSlow12 + fConst2 * fRec6[1];
			fRec7[0] = fSlow13 + fConst2 * fRec7[1];
			float fTemp1 = fRec7[0] * fRec6[0];
			float fTemp2 = fTemp1 * fRec5[0];
			float fTemp3 = std::min<float>(2e+04f, fTemp2);
			float fTemp4 = ((iTemp0) ? 0.0f : fRec4[1] + fConst3 * fTemp3);
			fRec4[0] = fTemp4 - std::floor(fTemp4);
			fRec8[0] = fSlow14 + fConst2 * fRec8[1];
			float fTemp5 = ((iTemp0) ? 0.0f : fRec3[1] + fConst3 * (std::min<float>(2e+04f, fTemp1) + fRec8[0] * fTemp3 * ftbl0NyxFaustSynthSIG0[std::max<int>(0, std::min<int>(int(65536.0f * fRec4[0]), 65535))]));
			fRec3[0] = fTemp5 - std::floor(fTemp5);
			fRec9[0] = fSlow15 + fConst2 * fRec9[1];
			fRec10[0] = fSlow16 + fConst2 * fRec10[1];
			float fTemp6 = std::min<float>(1.0f, 0.001f * fRec10[0] * fRec9[0]);
			float fTemp7 = ((iTemp0) ? 0.0f : fRec11[1] + fConst3 * fRec6[0] * fRec9[0]);
			fRec11[0] = fTemp7 - std::floor(fTemp7);
			fRec13[0] = fSlow17 + fConst2 * fRec13[1];
			float fTemp8 = std::min<float>(3e+04f, fConst5 * fRec10[0]);
			fRec12[0] = std::fmod(fRec12[1] + fTemp8 + (1.0f - std::pow(2.0f, 1.442695f * std::log(std::max<float>(0.001f, fRec13[0])))), fTemp8);
			float fTemp9 = std::min<float>(fRec12[0] / std::max<float>(1.0f, std::min<float>(1.5e+04f, fConst4 * fRec10[0])), 1.0f);
			float fTemp10 = ((iTemp0) ? 0.0f : fRec14[1] + fConst3 * fTemp1);
			fRec14[0] = fTemp10 - std::floor(fTemp10);
			float fTemp11 = float(input0[i0]) + fSlow18 * ftbl0NyxFaustSynthSIG0[std::max<int>(0, std::min<int>(int(65536.0f * fRec14[0]), 65535))];
			fVec3[IOTA0 & 131071] = fTemp11;
			float fTemp12 = fRec12[0] + fTemp8;
			int iTemp13 = int(fTemp12);
			float fTemp14 = std::floor(fTemp12);
			int iTemp15 = int(fRec12[0]);
			float fTemp16 = std::floor(fRec12[0]);
			fRec15[0] = fConst1 * float(input3[i0]) + fConst2 * fRec15[1];
			fRec16[0] = fConst1 * float(input1[i0]) + fConst2 * fRec16[1];
			fRec17[0] = fConst1 * float(input2[i0]) + fConst2 * fRec17[1];
			float fTemp17 = std::pow(2.0f, 2.0f * (fRec16[0] - fRec15[0]));
			float fTemp18 = std::min<float>(2e+04f, fTemp2 * fTemp17);
			float fTemp19 = ((iTemp0) ? 0.0f : fRec19[1] + fConst3 * fTemp18);
			fRec19[0] = fTemp19 - std::floor(fTemp19);
			float fTemp20 = ((iTemp0) ? 0.0f : fRec18[1] + fConst3 * (std::min<float>(2e+04f, fTemp1 * fTemp17) + fRec8[0] * fRec17[0] * fTemp18 * ftbl0NyxFaustSynthSIG0[std::max<int>(0, std::min<int>(int(65536.0f * fRec19[0]), 65535))]));
			fRec18[0] = fTemp20 - std::floor(fTemp20);
			iRec20[0] = 1103515245 * iRec20[1] + 12345;
			float fTemp21 = 4.656613e-10f * float(iRec20[0]);
			fRec21[0] = 0.5221894f * fRec21[3] + fTemp21 + 2.494956f * fRec21[1] - 2.0172658f * fRec21[2];
			fRec22[0] = fSlow20 + fConst2 * fRec22[1];
			output0[i0] = FAUSTFLOAT(fRec22[0] * ((iSlow8) ? ((iSlow19) ? 0.049922034f * fRec21[0] + 0.0506127f * fRec21[2] - (0.095993534f * fRec21[1] + 0.004408786f * fRec21[3]) : fTemp21) : ((iSlow9) ? ftbl0NyxFaustSynthSIG0[std::max<int>(0, std::min<int>(int(65536.0f * fRec18[0]), 65535))] * (0.7152f * fRec17[0] + 0.2126f * fRec16[0] + 0.0722f * fRec15[0]) : ((iSlow10) ? 0.5f * ((fVec3[(IOTA0 - std::min<int>(65537, std::max<int>(0, iTemp15))) & 131071] * (fTemp16 + (1.0f - fRec12[0])) + (fRec12[0] - fTemp16) * fVec3[(IOTA0 - std::min<int>(65537, std::max<int>(0, iTemp15 + 1))) & 131071]) * fTemp9 + (fVec3[(IOTA0 - std::min<int>(65537, std::max<int>(0, iTemp13))) & 131071] * (fTemp14 + (1.0f - fTemp12)) + (fTemp12 - fTemp14) * fVec3[(IOTA0 - std::min<int>(65537, std::max<int>(0, iTemp13 + 1))) & 131071]) * (1.0f - fTemp9)) * float(fRec11[0] < fTemp6) * (1.0f - std::cos(6.2831855f * std::min<float>(1.0f, fRec11[0] / std::max<float>(0.001f, fTemp6)))) : ftbl0NyxFaustSynthSIG0[std::max<int>(0, std::min<int>(int(65536.0f * fRec3[0]), 65535))]))) * std::max<float>(0.0f, std::min<float>(fSlow4 * fRec1[0], std::max<float>(fSlow6 * (fSlow3 - fRec1[0]) + 1.0f, fSlow5)) * (1.0f - fSlow2 * float(iRec0[0]))));
			iVec0[1] = iVec0[0];
			fVec1[1] = fVec1[0];
			iRec0[1] = iRec0[0];
			fRec1[1] = fRec1[0];
			fRec5[1] = fRec5[0];
			fRec6[1] = fRec6[0];
			fRec7[1] = fRec7[0];
			fRec4[1] = fRec4[0];
			fRec8[1] = fRec8[0];
			fRec3[1] = fRec3[0];
			fRec9[1] = fRec9[0];
			fRec10[1] = fRec10[0];
			fRec11[1] = fRec11[0];
			fRec13[1] = fRec13[0];
			fRec12[1] = fRec12[0];
			fRec14[1] = fRec14[0];
			IOTA0 = IOTA0 + 1;
			fRec15[1] = fRec15[0];
			fRec16[1] = fRec16[0];
			fRec17[1] = fRec17[0];
			fRec19[1] = fRec19[0];
			fRec18[1] = fRec18[0];
			iRec20[1] = iRec20[0];
			for (int j0 = 3; j0 > 0; j0 = j0 - 1) {
				fRec21[j0] = fRec21[j0 - 1];
			}
			fRec22[1] = fRec22[0];
		}
	}

};

#endif
