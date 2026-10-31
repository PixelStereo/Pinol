/* ------------------------------------------------------------
name: "Compressor"
Code generated with Faust 2.79.3 (https://faust.grame.fr)
Compilation options: -lang cpp -ct 1 -cn NyxFxCompressor -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
------------------------------------------------------------ */

#ifndef  __NyxFxCompressor_H__
#define  __NyxFxCompressor_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>

#ifndef FAUSTCLASS
#define FAUSTCLASS NyxFxCompressor
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


class NyxFxCompressor : public dsp {

 private:

	int fSampleRate;
	float fConst0;
	float fConst1;
	float fConst2;
	FAUSTFLOAT fHslider0;
	float fRec0[2];
	FAUSTFLOAT fHslider1;
	float fConst3;
	FAUSTFLOAT fHslider2;
	FAUSTFLOAT fHslider3;
	float fRec2[2];
	FAUSTFLOAT fHslider4;
	float fRec1[2];

 public:
	NyxFxCompressor() {
	}

	void metadata(Meta* m) {
		m->declare("analyzers.lib/amp_follower_ar:author", "Jonatan Liljedahl, revised by Romain Michon");
		m->declare("analyzers.lib/name", "Faust Analyzer Library");
		m->declare("analyzers.lib/version", "1.2.0");
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.21.0");
		m->declare("compile_options", "-lang cpp -ct 1 -cn NyxFxCompressor -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
		m->declare("compressors.lib/compression_gain_mono:author", "Julius O. Smith III");
		m->declare("compressors.lib/compression_gain_mono:copyright", "Copyright (C) 2014-2020 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("compressors.lib/compression_gain_mono:license", "MIT-style STK-4.3 license");
		m->declare("compressors.lib/compressor_stereo:author", "Julius O. Smith III");
		m->declare("compressors.lib/compressor_stereo:copyright", "Copyright (C) 2014-2020 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("compressors.lib/compressor_stereo:license", "MIT-style STK-4.3 license");
		m->declare("compressors.lib/name", "Faust Compressor Effect Library");
		m->declare("compressors.lib/version", "1.6.0");
		m->declare("filename", "Compressor.dsp");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LGPL with exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.8.1");
		m->declare("name", "Compressor");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("signals.lib/name", "Faust Signal Routing Library");
		m->declare("signals.lib/onePoleSwitching:author", "Jonatan Liljedahl, revised by Dario Sanfilippo");
		m->declare("signals.lib/onePoleSwitching:licence", "STK-4.3");
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
		fConst3 = 1.0f / fConst0;
	}

	virtual void instanceResetUserInterface() {
		fHslider0 = FAUSTFLOAT(0.0f);
		fHslider1 = FAUSTFLOAT(1e+01f);
		fHslider2 = FAUSTFLOAT(-18.0f);
		fHslider3 = FAUSTFLOAT(1e+02f);
		fHslider4 = FAUSTFLOAT(4.0f);
	}

	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			fRec0[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = l1 + 1) {
			fRec2[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = l2 + 1) {
			fRec1[l2] = 0.0f;
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

	virtual NyxFxCompressor* clone() {
		return new NyxFxCompressor();
	}

	virtual int getSampleRate() {
		return fSampleRate;
	}

	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("Compressor");
		ui_interface->addHorizontalSlider("attack_ms", &fHslider1, FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.1f), FAUSTFLOAT(5e+02f), FAUSTFLOAT(0.1f));
		ui_interface->addHorizontalSlider("makeup_dB", &fHslider0, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(24.0f), FAUSTFLOAT(0.1f));
		ui_interface->addHorizontalSlider("ratio", &fHslider4, FAUSTFLOAT(4.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(0.1f));
		ui_interface->addHorizontalSlider("release_ms", &fHslider3, FAUSTFLOAT(1e+02f), FAUSTFLOAT(1.0f), FAUSTFLOAT(3e+03f), FAUSTFLOAT(1.0f));
		ui_interface->addHorizontalSlider("threshold_dB", &fHslider2, FAUSTFLOAT(-18.0f), FAUSTFLOAT(-6e+01f), FAUSTFLOAT(0.0f), FAUSTFLOAT(0.1f));
		ui_interface->closeBox();
	}

	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = fConst1 * float(fHslider0);
		float fSlow1 = float(fHslider1);
		float fSlow2 = 0.0005f * fSlow1;
		int iSlow3 = std::fabs(fSlow2) < 1.1920929e-07f;
		float fSlow4 = ((iSlow3) ? 0.0f : std::exp(-(fConst3 / ((iSlow3) ? 1.0f : fSlow2))));
		float fSlow5 = 1.0f - fSlow4;
		float fSlow6 = float(fHslider2);
		float fSlow7 = 0.001f * float(fHslider3);
		int iSlow8 = std::fabs(fSlow7) < 1.1920929e-07f;
		float fSlow9 = ((iSlow8) ? 0.0f : std::exp(-(fConst3 / ((iSlow8) ? 1.0f : fSlow7))));
		float fSlow10 = 0.001f * fSlow1;
		int iSlow11 = std::fabs(fSlow10) < 1.1920929e-07f;
		float fSlow12 = ((iSlow11) ? 0.0f : std::exp(-(fConst3 / ((iSlow11) ? 1.0f : fSlow10))));
		float fSlow13 = 1.0f / std::max<float>(1.1920929e-07f, float(fHslider4)) + -1.0f;
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			fRec0[0] = fSlow0 + fConst2 * fRec0[1];
			float fTemp0 = std::pow(1e+01f, 0.05f * fRec0[0]);
			float fTemp1 = float(input0[i0]);
			float fTemp2 = float(input1[i0]);
			float fTemp3 = std::fabs(std::fabs(fTemp2) + std::fabs(fTemp1));
			float fTemp4 = ((fTemp3 > fRec2[1]) ? fSlow12 : fSlow9);
			fRec2[0] = fTemp3 * (1.0f - fTemp4) + fRec2[1] * fTemp4;
			fRec1[0] = fSlow13 * std::max<float>(2e+01f * std::log10(std::max<float>(1.1754944e-38f, fRec2[0])) - fSlow6, 0.0f) * fSlow5 + fSlow4 * fRec1[1];
			float fTemp5 = std::pow(1e+01f, 0.05f * fRec1[0]);
			output0[i0] = FAUSTFLOAT(fTemp1 * fTemp5 * fTemp0);
			output1[i0] = FAUSTFLOAT(fTemp2 * fTemp5 * fTemp0);
			fRec0[1] = fRec0[0];
			fRec2[1] = fRec2[0];
			fRec1[1] = fRec1[0];
		}
	}

};

#endif
