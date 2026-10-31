#pragma once
#include "OnsetAnalysis.h"
#include "Published.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <complex>
#include <cmath>
#include <cstdint>

// Audio-thread processing: fixed storage; no allocation, locks or callbacks.
// Readers see normalized, smoothed program-audio energy before the master fader.
class AudioAnalysis {
public:
    static constexpr int fftSize = 4096, binCount = fftSize / 2 + 1, hopSize = 1024;
    struct Spectrum {
        std::array<float, binCount> power{};
        int rate = 48000;
        // Integrated, stereo-safe RMS over the selected FFT bins. Fractional
        // edge weights avoid jumps as a band handle crosses a bin boundary.
        float band(float lo, float hi) const { return 1.f - std::exp(-4.f * float(std::sqrt(bandPower(lo, hi)))); }
        // The power (mean square) of the bins between lo and hi Hz
        double bandPower(float lo, float hi) const {
            if (rate <= 0 || !std::isfinite(lo) || !std::isfinite(hi) || hi <= lo) return 0;
            const float step = float(rate) / fftSize;
            // Only overlapping bins can contribute. Keep a bin of margin on
            // either side so rounding at fractional edges uses the same weight
            // calculation as the full scan. Clamp before converting to int:
            // callers can supply finite frequencies beyond the integer range.
            const float lastBin = float(binCount - 1);
            const int first = int(std::clamp(std::floor(lo / step) - 1.f, 1.f, lastBin));
            const int last = int(std::clamp(std::ceil(hi / step) + 1.f, 1.f, lastBin));
            double sum = 0;
            for (int k = first; k <= last; ++k) {
                const float left = (k - .5f) * step, right = (k + .5f) * step;
                const float weight = std::clamp((std::min(hi,right)-std::max(lo,left))/step,0.f,1.f);
                sum += power[size_t(k)] * weight;
            }
            return std::max(0., sum);
        }
    };
    AudioAnalysis() {
        constexpr double pi=3.14159265358979323846;
        for(int n=0;n<fftSize;++n)m_window[n]=float(.5-.5*std::cos(2*pi*n/fftSize));
        for(int n=0;n<fftSize/2;++n)m_twiddle[n]=std::polar(1.f,float(-2*pi*n/fftSize));

    }
    Spectrum spectrum() const { return m_spectra.read(); }
    // The flux of the last hops, per band: what hit detection (OnsetDetector) reads, per interaction
    OnsetFlux onsetFlux() const { return m_onsets.read(); }
    // The last waveSize frames heard, per channel (-1..1), republished every waveHop frames: what an ISF "audio"
    // input shows
    static constexpr int waveSize = 2048, waveHop = 256;
    struct Wave {
        std::array<std::array<float, waveSize>, 2> samples{};
    };
    Wave wave() const { return m_waves.read(); }

    // What an ISF "audio" input receives: `columns` samples per channel (0: 1024), a row per channel, centred on 0.5.
    // Fewer than 1024: each column holds the strongest sample of its stretch of the last 1024 (the envelope stays).
    static void waveColumns(const Wave &w, int columns, float *out /* 2 * columns */) {
        const int n = columns > 0 ? std::min(columns, int(waveSize)) : 1024;
        const int span = std::max(n, 1024);
        for (int c = 0; c < 2; ++c) {
            const float *x = w.samples[size_t(c)].data() + waveSize - span;
            for (int i = 0; i < n; ++i) {
                const int a = int(std::int64_t(i) * span / n), b = std::max(a + 1, int(std::int64_t(i + 1) * span / n));
                float v = 0;
                for (int k = a; k < b; ++k) if (std::abs(x[k]) > std::abs(v)) v = x[k];
                out[c * n + i] = std::clamp(.5f + .5f * v, 0.f, 1.f);
            }
        }
    }
    // What an ISF "audioFFT" input receives: `columns` bands (0: 512) from 20 Hz to 20 kHz (or Nyquist) on a log
    // scale, each its power in dB, 0 at -60 dB, 1 at 0 dB (a full-scale sine); two rows (the spectrum is stereo-safe:
    // the same in both). One column: the whole band, the overall loudness.
    static void spectrumColumns(const Spectrum &s, int columns, float *out /* 2 * columns */) {
        const int n = columns > 0 ? std::min(columns, 2048) : 512;
        const float lo = 20.f, hi = std::min(20000.f, s.rate * .5f);
        for (int i = 0; i < n; ++i) {
            const float a = lo * std::pow(hi / lo, float(i) / n), b = lo * std::pow(hi / lo, float(i + 1) / n);
            const double p = s.bandPower(a, b);
            const float v = std::clamp(1.f + float(10. * std::log10(std::max(p, 1e-12))) / 60.f, 0.f, 1.f);
            out[i] = out[n + i] = v;
        }
    }
    static int waveWidth(int columns) { return columns > 0 ? std::min(columns, int(waveSize)) : 1024; }
    static int spectrumWidth(int columns) { return columns > 0 ? std::min(columns, 2048) : 512; }
    struct Values { float level=0, bass=0, mid=0, high=0, onset=0; };
    Values values() const { return {m_level.load(),m_bass.load(),m_mid.load(),m_high.load(),m_onset.load()}; }
    void reset() {
        for(int c=0;c<2;++c){m_low[c]=m_upper[c]=0;}
        m_write=m_filled=m_hop=0;
        for(auto &channel:m_ring)channel.fill(0);
        m_waveHop=0;
        Spectrum cleared;
        cleared.rate = m_rate;
        m_spectra.publish(cleared);
        m_waves.publish(Wave{});
        m_slow=0; m_level=0; m_bass=0; m_mid=0; m_high=0; m_onset=0;
        m_onsets.reset();
    }
    void process(const float *stereo,int frames,int rate) {
        if(!stereo||frames<=0||rate<=0)return;
        constexpr double pi=3.14159265358979323846;
        const float low=1-std::exp(float(-2*pi*180/rate)), upper=1-std::exp(float(-2*pi*2500/rate));
        double energy[4]={};
        if(m_rate!=rate){m_rate=rate;reset();}
        m_onsets.process(stereo,frames,rate);
        for(int i=0;i<frames;++i) {
          for(int c=0;c<2;++c) {
            float x=stereo[2*i+c]; if(!std::isfinite(x))x=0;
            m_ring[c][m_write]=x;
            m_low[c]+=low*(x-m_low[c]);m_upper[c]+=upper*(x-m_upper[c]);
            const float bands[4]={x,m_low[c],m_upper[c]-m_low[c],x-m_upper[c]};
            for(int band=0;band<4;++band)energy[band]+=double(bands[band])*bands[band];
        }
          m_write=(m_write+1)%fftSize;
          m_filled=std::min(m_filled+1,fftSize);
          if(++m_hop>=hopSize && m_filled==fftSize){m_hop=0;publishSpectrum();}
          if(++m_waveHop>=waveHop){m_waveHop=0;publishWave();}
        }
        const float seconds=float(frames)/rate;
        float rms[4];for(int band=0;band<4;++band)rms[band]=float(std::sqrt(energy[band]/(2*frames)));
        auto smooth=[seconds](float before,float target,float attack,float release){return target+(before-target)*std::exp(-seconds/(target>before?attack:release));};
        auto normalized=[](float value){return 1.f-std::exp(-std::max(0.f,value)*4.f);};
        const float transient=std::max(0.f,rms[0]-m_slow);
        m_slow=smooth(m_slow,rms[0],.15f,.35f);
        m_level=smooth(m_level.load(),normalized(rms[0]),.012f,.20f);
        m_bass=smooth(m_bass.load(),normalized(rms[1]),.025f,.30f);
        m_mid=smooth(m_mid.load(),normalized(rms[2]),.015f,.22f);
        m_high=smooth(m_high.load(),normalized(rms[3]),.006f,.12f);
        m_onset=smooth(m_onset.load(),std::clamp(transient*12.f,0.f,1.f),.002f,.08f);
    }
private:
    void publishSpectrum(){
        std::array<float,binCount> power{};
        for(int c=0;c<2;++c){
            for(int i=0;i<fftSize;++i)m_fft[i]={m_ring[c][(m_write+i)%fftSize]*m_window[i],0};
            for(int i=1,j=0;i<fftSize;++i){int bit=fftSize>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(m_fft[i],m_fft[j]);}
            for(int length=2;length<=fftSize;length<<=1)
                for(int base=0;base<fftSize;base+=length)
                    for(int j=0;j<length/2;++j){const auto a=m_fft[base+j],b=m_fft[base+j+length/2]*m_twiddle[j*fftSize/length];m_fft[base+j]=a+b;m_fft[base+j+length/2]=a-b;}
            // Hann power correction (sum w²=3N/8), two one-sided spectra
            // averaged as powers, so opposite-phase stereo never cancels.
            for (int k = 1; k < binCount; ++k) {
                // Interior bins include their negative-frequency partner. The
                // Nyquist bin is its own partner, so it must not be doubled.
                // The stereo average already cancels the interior factor of 2.
                const float scale = k == fftSize / 2 ? .5f : 1.f;
                power[k] += scale * std::norm(m_fft[k]) / (fftSize * fftSize * .375f);
            }
        }
        m_spectra.publish(Spectrum{power, m_rate});
    }
    void publishWave(){
        Wave w;
        for(int c=0;c<2;++c)
            for(int i=0;i<waveSize;++i)w.samples[size_t(c)][size_t(i)]=m_ring[c][(m_write+fftSize-waveSize+i)%fftSize];
        m_waves.publish(w);
    }
    std::array<float,fftSize> m_window{};
    std::array<std::array<float,fftSize>,2> m_ring{};
    std::array<std::complex<float>,fftSize> m_fft{};
    std::array<std::complex<float>,fftSize/2> m_twiddle{};
    Published<Spectrum> m_spectra;
    Published<Wave> m_waves;
    OnsetAnalysis m_onsets;
    int m_rate=48000;
    int m_write=0,m_filled=0,m_hop=0,m_waveHop=0;
    float m_low[2]={},m_upper[2]={},m_slow=0;
    std::atomic<float> m_level{0},m_bass{0},m_mid{0},m_high{0},m_onset{0};
};
