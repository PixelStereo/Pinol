/* ------------------------------------------------------------
name: "Equalizer"
Code generated with Faust 2.79.3 (https://faust.grame.fr)
Compilation options: -lang cpp -ct 1 -cn NyxFxEqualizer -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __NyxFxEqualizer_H__
#define  __NyxFxEqualizer_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

#ifndef FAUSTCLASS
#define FAUSTCLASS NyxFxEqualizer
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

static float NyxFxEqualizer_faustpower2_f(float value) {
	return value * value;
}

class NyxFxEqualizer : public dsp {

 private:

	int fSampleRate;
	float fConst0;
	float fConst1;
	float fConst2;
	FAUSTFLOAT fHslider0;
	float fRec0[2];
	float fConst3;
	float fConst4;
	float fConst5;
	float fConst6;
	float fConst7;
	float fConst8;
	FAUSTFLOAT fHslider1;
	float fRec3[2];
	float fConst9;
	float fConst10;
	float fConst11;
	float fConst12;
	float fConst13;
	float fConst14;
	float fConst15;
	float fConst16;
	float fConst17;
	float fConst18;
	float fVec0[2];
	float fConst19;
	float fConst20;
	float fRec6[2];
	float fRec5[3];
	FAUSTFLOAT fHslider2;
	float fRec7[2];
	float fRec9[2];
	float fRec8[3];
	float fRec4[3];
	float fVec1[2];
	float fConst21;
	float fConst22;
	float fRec2[2];
	float fRec1[3];
	float fRec11[2];
	float fRec10[3];
	float fVec2[2];
	float fRec16[2];
	float fRec15[3];
	float fRec18[2];
	float fRec17[3];
	float fRec14[3];
	float fVec3[2];
	float fRec13[2];
	float fRec12[3];
	float fRec20[2];
	float fRec19[3];

 public:
	NyxFxEqualizer() {
	}

	void metadata(Meta* m) {
		m->declare("analyzers.lib/name", "Faust Analyzer Library");
		m->declare("analyzers.lib/version", "1.2.0");
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.21.0");
		m->declare("compile_options", "-lang cpp -ct 1 -cn NyxFxEqualizer -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("filename", "Equalizer.dsp");
		m->declare("filters.lib/filterbank:author", "Julius O. Smith III");
		m->declare("filters.lib/filterbank:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/filterbank:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/fir:author", "Julius O. Smith III");
		m->declare("filters.lib/fir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/fir:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/highpass:author", "Julius O. Smith III");
		m->declare("filters.lib/highpass:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/highshelf:author", "Julius O. Smith III");
		m->declare("filters.lib/highshelf:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/highshelf:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/iir:author", "Julius O. Smith III");
		m->declare("filters.lib/iir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/iir:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/low_shelf:author", "Julius O. Smith III");
		m->declare("filters.lib/low_shelf:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/low_shelf:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/lowpass0_highpass1", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowpass0_highpass1:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowpass:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/lowshelf:author", "Julius O. Smith III");
		m->declare("filters.lib/lowshelf:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowshelf:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/peak_eq:author", "Julius O. Smith III");
		m->declare("filters.lib/peak_eq:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/peak_eq:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/tf1:author", "Julius O. Smith III");
		m->declare("filters.lib/tf1:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf1:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/tf1s:author", "Julius O. Smith III");
		m->declare("filters.lib/tf1s:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf1s:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/tf2:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/tf2s:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2s:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2s:license", "MIT-style STK-4.3 license");
		m->declare("filters.lib/version", "1.7.1");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.8.1");
		m->declare("name", "Equalizer");
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
	}

	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, float(fSampleRate)));
		fConst1 = 44.1f / fConst0;
		fConst2 = 1.0f - fConst1;
		fConst3 = std::tan(3.1415927f * (std::min<float>(5e+03f, 0.4f * fConst0) / fConst0));
		fConst4 = 1.0f / NyxFxEqualizer_faustpower2_f(fConst3);
		fConst5 = 2.0f * (1.0f - fConst4);
		fConst6 = 1.0f / fConst3;
		fConst7 = (fConst6 + -1.0f) / fConst3 + 1.0f;
		fConst8 = 1.0f / ((fConst6 + 1.0f) / fConst3 + 1.0f);
		fConst9 = 3141.5928f / (fConst0 * std::sin(6283.1855f / fConst0));
		fConst10 = std::tan(3141.5928f / fConst0);
		fConst11 = 1.0f / fConst10;
		fConst12 = 2.0f * (1.0f - 1.0f / NyxFxEqualizer_faustpower2_f(fConst10));
		fConst13 = std::tan(628.31854f / fConst0);
		fConst14 = 1.0f / NyxFxEqualizer_faustpower2_f(fConst13);
		fConst15 = 2.0f * (1.0f - fConst14);
		fConst16 = 1.0f / fConst13;
		fConst17 = (fConst16 + -1.0f) / fConst13 + 1.0f;
		fConst18 = 1.0f / ((fConst16 + 1.0f) / fConst13 + 1.0f);
		fConst19 = 1.0f - fConst16;
		fConst20 = 1.0f / (fConst16 + 1.0f);
		fConst21 = 1.0f - fConst6;
		fConst22 = 1.0f / (fConst6 + 1.0f);
	}

	virtual void instanceResetUserInterface() {
		fHslider0 = FAUSTFLOAT(0.0f);
		fHslider1 = FAUSTFLOAT(0.0f);
		fHslider2 = FAUSTFLOAT(0.0f);
	}

	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			fRec0[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = l1 + 1) {
			fRec3[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			fVec0[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = l3 + 1) {
			fRec6[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 3; l4 = l4 + 1) {
			fRec5[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = l5 + 1) {
			fRec7[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = l6 + 1) {
			fRec9[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 3; l7 = l7 + 1) {
			fRec8[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 3; l8 = l8 + 1) {
			fRec4[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = l9 + 1) {
			fVec1[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 2; l10 = l10 + 1) {
			fRec2[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 3; l11 = l11 + 1) {
			fRec1[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 2; l12 = l12 + 1) {
			fRec11[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 3; l13 = l13 + 1) {
			fRec10[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 2; l14 = l14 + 1) {
			fVec2[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 2; l15 = l15 + 1) {
			fRec16[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 3; l16 = l16 + 1) {
			fRec15[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 2; l17 = l17 + 1) {
			fRec18[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 3; l18 = l18 + 1) {
			fRec17[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 3; l19 = l19 + 1) {
			fRec14[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 2; l20 = l20 + 1) {
			fVec3[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 2; l21 = l21 + 1) {
			fRec13[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 3; l22 = l22 + 1) {
			fRec12[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 2; l23 = l23 + 1) {
			fRec20[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 3; l24 = l24 + 1) {
			fRec19[l24] = 0.0f;
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

	virtual NyxFxEqualizer* clone() {
		return new NyxFxEqualizer();
	}

	virtual int getSampleRate() {
		return fSampleRate;
	}

	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("Equalizer");
		ui_interface->addHorizontalSlider("bass_dB", &fHslider2, FAUSTFLOAT(0.0f), FAUSTFLOAT(-18.0f), FAUSTFLOAT(18.0f), FAUSTFLOAT(0.1f));
		ui_interface->addHorizontalSlider("mid_dB", &fHslider1, FAUSTFLOAT(0.0f), FAUSTFLOAT(-18.0f), FAUSTFLOAT(18.0f), FAUSTFLOAT(0.1f));
		ui_interface->addHorizontalSlider("treble_dB", &fHslider0, FAUSTFLOAT(0.0f), FAUSTFLOAT(-18.0f), FAUSTFLOAT(18.0f), FAUSTFLOAT(0.1f));
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
			fRec0[0] = fSlow0 + fConst2 * fRec0[1];
			float fTemp0 = std::pow(1e+01f, 0.05f * fRec0[0]);
			fRec3[0] = fSlow1 + fConst2 * fRec3[1];
			int iTemp1 = fRec3[0] > 0.0f;
			float fTemp2 = fConst9 * std::pow(1e+01f, 0.05f * std::fabs(fRec3[0]));
			float fTemp3 = ((iTemp1) ? fConst9 : fTemp2);
			float fTemp4 = fConst11 * (fConst11 + fTemp3) + 1.0f;
			float fTemp5 = ((iTemp1) ? fTemp2 : fConst9);
			float fTemp6 = fConst11 * (fConst11 - fTemp5) + 1.0f;
			float fTemp7 = fConst12 * fRec4[1];
			float fTemp8 = fConst11 * (fConst11 - fTemp3) + 1.0f;
			float fTemp9 = float(input0[i0]);
			fVec0[0] = fTemp9;
			fRec6[0] = -(fConst20 * (fConst19 * fRec6[1] - fConst16 * (fTemp9 - fVec0[1])));
			fRec5[0] = fRec6[0] - fConst18 * (fConst17 * fRec5[2] + fConst15 * fRec5[1]);
			fRec7[0] = fSlow2 + fConst2 * fRec7[1];
			float fTemp10 = std::pow(1e+01f, 0.05f * fRec7[0]);
			fRec9[0] = -(fConst20 * (fConst19 * fRec9[1] - (fTemp9 + fVec0[1])));
			fRec8[0] = fRec9[0] - fConst18 * (fConst17 * fRec8[2] + fConst15 * fRec8[1]);
			fRec4[0] = fConst18 * ((fRec8[2] + fRec8[0] + 2.0f * fRec8[1]) * fTemp10 + fConst14 * (fRec5[2] + (fRec5[0] - 2.0f * fRec5[1]))) - (fRec4[2] * fTemp8 + fTemp7) / fTemp4;
			float fTemp11 = fConst11 * (fConst11 + fTemp5) + 1.0f;
			float fTemp12 = (fTemp7 + fRec4[0] * fTemp11 + fRec4[2] * fTemp6) / fTemp4;
			fVec1[0] = fTemp12;
			fRec2[0] = -(fConst22 * (fConst21 * fRec2[1] - fConst6 * (fTemp12 - fVec1[1])));
			fRec1[0] = fRec2[0] - fConst8 * (fConst7 * fRec1[2] + fConst5 * fRec1[1]);
			fRec11[0] = -(fConst22 * (fConst21 * fRec11[1] - (fTemp12 + fVec1[1])));
			fRec10[0] = fRec11[0] - fConst8 * (fConst7 * fRec10[2] + fConst5 * fRec10[1]);
			output0[i0] = FAUSTFLOAT(fConst8 * (fRec10[2] + fRec10[0] + 2.0f * fRec10[1] + fConst4 * (fRec1[2] + (fRec1[0] - 2.0f * fRec1[1])) * fTemp0));
			float fTemp13 = fConst12 * fRec14[1];
			float fTemp14 = float(input1[i0]);
			fVec2[0] = fTemp14;
			fRec16[0] = -(fConst20 * (fConst19 * fRec16[1] - fConst16 * (fTemp14 - fVec2[1])));
			fRec15[0] = fRec16[0] - fConst18 * (fConst17 * fRec15[2] + fConst15 * fRec15[1]);
			fRec18[0] = -(fConst20 * (fConst19 * fRec18[1] - (fTemp14 + fVec2[1])));
			fRec17[0] = fRec18[0] - fConst18 * (fConst17 * fRec17[2] + fConst15 * fRec17[1]);
			fRec14[0] = fConst18 * (fTemp10 * (fRec17[2] + fRec17[0] + 2.0f * fRec17[1]) + fConst14 * (fRec15[2] + (fRec15[0] - 2.0f * fRec15[1]))) - (fTemp8 * fRec14[2] + fTemp13) / fTemp4;
			float fTemp15 = (fTemp13 + fRec14[0] * fTemp11 + fTemp6 * fRec14[2]) / fTemp4;
			fVec3[0] = fTemp15;
			fRec13[0] = -(fConst22 * (fConst21 * fRec13[1] - fConst6 * (fTemp15 - fVec3[1])));
			fRec12[0] = fRec13[0] - fConst8 * (fConst7 * fRec12[2] + fConst5 * fRec12[1]);
			fRec20[0] = -(fConst22 * (fConst21 * fRec20[1] - (fTemp15 + fVec3[1])));
			fRec19[0] = fRec20[0] - fConst8 * (fConst7 * fRec19[2] + fConst5 * fRec19[1]);
			output1[i0] = FAUSTFLOAT(fConst8 * (fRec19[2] + fRec19[0] + 2.0f * fRec19[1] + fConst4 * fTemp0 * (fRec12[2] + (fRec12[0] - 2.0f * fRec12[1]))));
			fRec0[1] = fRec0[0];
			fRec3[1] = fRec3[0];
			fVec0[1] = fVec0[0];
			fRec6[1] = fRec6[0];
			fRec5[2] = fRec5[1];
			fRec5[1] = fRec5[0];
			fRec7[1] = fRec7[0];
			fRec9[1] = fRec9[0];
			fRec8[2] = fRec8[1];
			fRec8[1] = fRec8[0];
			fRec4[2] = fRec4[1];
			fRec4[1] = fRec4[0];
			fVec1[1] = fVec1[0];
			fRec2[1] = fRec2[0];
			fRec1[2] = fRec1[1];
			fRec1[1] = fRec1[0];
			fRec11[1] = fRec11[0];
			fRec10[2] = fRec10[1];
			fRec10[1] = fRec10[0];
			fVec2[1] = fVec2[0];
			fRec16[1] = fRec16[0];
			fRec15[2] = fRec15[1];
			fRec15[1] = fRec15[0];
			fRec18[1] = fRec18[0];
			fRec17[2] = fRec17[1];
			fRec17[1] = fRec17[0];
			fRec14[2] = fRec14[1];
			fRec14[1] = fRec14[0];
			fVec3[1] = fVec3[0];
			fRec13[1] = fRec13[0];
			fRec12[2] = fRec12[1];
			fRec12[1] = fRec12[0];
			fRec20[1] = fRec20[0];
			fRec19[2] = fRec19[1];
			fRec19[1] = fRec19[0];
		}
	}

};

#endif
