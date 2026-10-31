// The show's musical clock, and what it does to a sequence: steps counted in beats, GO quantized to the beat or the bar.
#include "BeatClock.h"
#include "Engine.h"
#include <QGuiApplication>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

static void require(bool ok, const char *why)
{
    if (!ok) throw std::runtime_error(why);
}
static bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; }

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    try {
        // --- The clock as a function of time
        {
            BeatClock c;
            require(near(c.bpm(), 120) && c.beatsPerBar() == 4 && !c.running(), "defaults: 120 BPM, 4/4, stopped");
            c.setBpm(60);
            require(near(c.secondsPerBeat(), 1), "60 BPM is a beat a second");
            c.setBpm(5);
            require(near(c.bpm(), BeatClock::kMinBpm), "the tempo is bounded");
            c.setBpm(NAN);
            require(near(c.bpm(), BeatClock::kMinBpm), "a non-finite tempo is ignored");
            c.setBpm(120);
            require(near(c.secondsToNext(1), 0), "a stopped clock never quantizes");
            c.start();
            const int64_t t0 = BeatClock::nowNs();
            require(c.running() && c.beatAt(t0) < 0.5, "start: from the first beat");
            // Between start() and t0 a slow machine may take a few tens of milliseconds: the difference is what is measured
            const double drift = c.beatAt(t0);
            require(near(c.beatAt(t0 + 1'000'000'000) - drift, 2.0, 1e-3), "120 BPM: two beats a second");
            // Quantize: from just after a beat, the next one is nearly a beat away
            const int64_t at = t0 + 100'000'000; // about 0.2 beat in
            const double toBeat = c.secondsToNextAt(1, at), toBar = c.secondsToNextAt(4, at);
            const double beatNow = c.beatAt(at);
            require(near(toBeat, (std::ceil(beatNow) - beatNow) * 0.5, 1e-3), "to the next beat: what is left of it, at 120 BPM");
            require(near(toBar, (std::ceil(beatNow / 4) * 4 - beatNow) * 0.5, 1e-3), "to the next bar: what is left of it");
            require(near(c.secondsToNextAt(0, at), 0), "no grid: no wait");
            const int64_t online = t0 + int64_t((1.0 - c.beatAt(t0)) * 0.5e9); // exactly on the next beat
            require(c.secondsToNextAt(1, online) < 1e-3, "on a line: now");
            c.setBpm(60); // the position goes on from where it is
            const double before = c.beat();
            require(c.beat() >= before, "a tempo change does not move the position back");
            c.stop();
            const double held = c.beat();
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            require(near(c.beat(), held), "stopped: the position is kept");
            c.resume();
            require(c.running() && c.beat() >= held, "resume: from where it stopped");
            c.setBeatsPerBar(99);
            require(c.beatsPerBar() == 16, "the bar is bounded");
            c.setBeatsPerBar(3);
            const auto saved = c.toJson();
            BeatClock d;
            d.fromJson(saved);
            require(near(d.bpm(), 60) && d.beatsPerBar() == 3 && !d.running(), "bpm and bar are saved, not the running state");
        }
        // --- Tap tempo and an external clock
        {
            BeatClock c;
            const int64_t start = BeatClock::nowNs();
            for (int i = 0; i < 4; ++i) c.tapAt(start + i * 250'000'000LL); // a quarter second apart, exactly
            require(near(c.bpm(), 240, 0.5), "four taps a quarter second apart: 240 BPM");
            c.tapAt(start + 10'000'000'000LL); // a long pause: a new count, the tempo is kept
            require(near(c.bpm(), 240, 0.5), "a tap after a pause starts counting again");
            require(c.source() == BeatClock::Source::Internal, "taps are not an external source");
            c.sync(8.0, 90, true);
            require(c.source() == BeatClock::Source::External && near(c.bpm(), 90) && c.running() && near(c.beat(), 8.0, 0.5),
                    "sync: tempo, position and running state taken, source external");
            c.sync(NAN, 90, true);
            require(near(c.bpm(), 90), "a non-finite sync is ignored");
            c.sync(0, 500, false);
            require(near(c.bpm(), BeatClock::kMaxBpm) && !c.running(), "a sync is bounded too");
        }
        // --- A sequence in beats, quantized
        {
            Engine engine;
            engine.beatClock().setBpm(120); // a beat is half a second
            auto &sequencer = engine.sequencer();
            Sequencer::Sequence sequence;
            sequence.name = "Beats";
            sequence.beats = true;
            Sequencer::Step a, b;
            a.preWait = 2; // two beats: one second
            a.postWait = 4; // four beats: two seconds
            b.quantize = 4;
            sequence.steps = {a, b};
            sequencer.add(sequence);
            sequencer.setCurrent(0);
            require(sequencer.goTo(0), "GO");
            auto runs = sequencer.runs();
            require(runs.size() == 1 && near(runs[0].preWait, 1.0) && near(runs[0].postWait, 2.0), "waits in beats become seconds at the tempo");
            sequencer.stop();
            // The same in seconds
            auto seconds = sequence;
            seconds.beats = false;
            sequencer.setSequence(0, seconds);
            require(sequencer.goTo(0), "GO again");
            runs = sequencer.runs();
            require(runs.size() == 1 && near(runs[0].preWait, 2.0) && near(runs[0].postWait, 4.0), "in seconds they stay seconds");
            sequencer.stop();
            // Quantize waits for the bar of a running clock; a stopped clock does not hold it
            require(sequencer.goTo(1), "GO on the quantized step");
            runs = sequencer.runs();
            require(runs.empty() || near(runs[0].preWait, 0), "stopped clock: no grid, the step starts at once (nothing left waiting)");
            sequencer.stop();
            engine.beatClock().sync(0.25, 120, true); // a quarter beat into the bar: 3.75 beats, 1.875 s, to the next one
            require(sequencer.goTo(1), "GO on the quantized step, clock running");
            runs = sequencer.runs();
            require(runs.size() == 1 && runs[0].preWait > 1.5 && runs[0].preWait < 1.9, "it waits for the next bar (1.875 s at 120 BPM, less what a slow machine took)");
            sequencer.stop();
            // Saved and read back
            const auto json = sequencer.toJson();
            Sequencer::Sequence again;
            again.name = "x";
            sequencer.fromJson(json, 0);
            require(sequencer.sequence(0).steps[1].quantize == 4 && !sequencer.sequence(0).beats, "quantize and the unit are saved");
            auto inBeats = sequencer.sequence(0);
            inBeats.beats = true;
            sequencer.setSequence(0, inBeats);
            sequencer.fromJson(sequencer.toJson(), 0);
            require(sequencer.sequence(0).beats, "the unit is read back");
            engine.shutdown();
        }
        std::cout << "Beat clock: tempo, quantize, tap, external sync, beats in sequences passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
