#pragma once
// Interactions: a value of the show driven from outside (a key, a MIDI controller, an OSC message) or by another value
// of the show (Internal), without a protocol in between.
//
// An interaction takes its source's value, as the source gives it (a key 1 / 0, MIDI 0..1, OSC as sent, Internal in
// the value's own units), through its filters, one after the other in their order, each on or off (as a layer's
// effects) — none: the value as it comes:
//     ranges — Remap (a range of values to another: In Min..Max to Out Min..Max, held within Out at its ends);
//     over time — Average (of the last moments), Weighted Average (smoothed), Pulse Detection (a pulse on each sudden
//     rise, decaying), Attack Release (rises and falls at speeds of their own), Damper (a spring that follows,
//     overshooting more or less), Delay (the value of a moment ago), Speed (how fast it moves);
//     on the value — Curve (0..1 to 0..1, drawn as an animation's keys), Math (+, −, ×, ÷, modulo, power, min, max,
//     absolute, floor / ceil / round to a step, 1 − x), Clamp (kept within two values), Steps (a few levels in 0..1),
//     Threshold (0 or 1, with a margin against flicker), Toggle (flips between 0 and 1 each time it goes up past its
//     threshold: a key, a pad: on, then off);
// then the master level scales the result, which the target receives as it is, in its own units: a number that
// value; a switch is on from 0.5; a choice takes the one of that index in its list; an action (trigger) fires each
// time it goes up past 0.5.
//
// Pick-up (faders, knobs): once the target was moved by something else (a snapshot, the interface), the interaction
// leaves it alone until its own value meets the target's (it is not made to jump). Jump: it sets it at once.
//
// A whole color (an OSC 'r', or 3 / 4 numbers) driving a color: each channel goes through the same chain — its
// filters, the master (not the alpha's) — with a filter state of its own, the result kept within 0..1; a Toggle does
// not apply. Pick-up waits for every channel to have met the target's (each within the tolerance, or crossed it) before
// it takes the color over. Alpha only with Map alpha and a fourth channel. A single number driving a color: a gray.
//
// Action mode (instead of that chain, the Mapping mode): each value the source gives (in its own units: a key 1 / 0,
// MIDI 0..1, OSC as sent, Internal in the value's own units — seconds, a switch 0 / 1, a choice's index) is tested
// against its conditions (all of them, or any). When the answer becomes true, the "if true" actions are done; when it
// becomes false, the "if false" ones (the very first answer: only if true); with no condition, each value does the
// "if true" ones. Changed conditions are tried again on an Internal source's current value, from the answer it had.
// Each action sets a value of the show — an
// action fired, a switch on / off, a choice (its index), a number — as an event of a timeline does.
//
// Sources feed events into the engine from any thread (Engine::feedInteraction); the engine runs the chains each frame
// (its lock held) and sets the targets, as an animation does (the snapshots' fades let them go).

#include "Anim.h"
#include "HandTracking.h"
#include "PixelAnalysis.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include "OnsetAnalysis.h"
#include <array>
#include <deque>
#include <optional>

enum class InteractionSource { Keyboard = 0, Midi, Osc, Internal, Audio, Pixels, Hands };
inline constexpr int kInteractionSourceCount = 7;
enum class InteractionFilter {
    Average = 0, WeightedAverage, PulseDetection, AttackRelease, Damper, // over time (as before: saved by these keys)
    Curve, Math, Clamp, Steps, Threshold, Delay, Speed, Remap, Toggle,
    Envelope // a trigger in, an ADSR out: Attack, Decay, Sustain, Release (the impact of a hit, a flash, a pulse)
};
inline constexpr int kInteractionFilterCount = 15;
inline constexpr int kFilterSettingCount = 4; // a, b, c, d (Remap: In Min, In Max, Out Min, Out Max)
// A Math filter's operation on the value x, with its value a
enum class MathOp { Add = 0, Subtract, Multiply, Divide, Modulo, Power, Min, Max, Abs, Floor, Ceil, Round, OneMinus };
inline constexpr int kMathOpCount = 13;
QString mathOpName(MathOp op); // "x + a", "x − a"…
bool mathOpUsesValue(MathOp op);
double applyMathOp(MathOp op, double x, double a);
enum class MidiMessageKind { ControlChange = 0, Note, PitchBend, ProgramChange };
enum class Takeover { PickUp = 0, Jump };
enum class InteractionMode { Mapping = 0, Action };
// A test of the source's value: Equal (within a millionth), Not Equal, Greater, Greater or Equal, Less, Less or Equal,
// In Range [a, b], Out of Range
enum class ConditionOp { Equal = 0, NotEqual, Greater, GreaterOrEqual, Less, LessOrEqual, InRange, OutOfRange };
inline constexpr int kConditionOpCount = 8;
QString conditionOpName(ConditionOp op); // "=", "≠", ">", "≥", "<", "≤", "in range", "out of range"

struct InteractionCondition {
    ConditionOp op = ConditionOp::GreaterOrEqual;
    double a = 0.5, b = 1; // b: the range's upper end
    bool test(double x) const;
    bool operator==(const InteractionCondition &o) const { return op == o.op && a == o.a && b == o.b; }
};

// What an Action does: sets a value of the show (a layer's id, 0: the composition) — fires an action, sets a switch
// (0 / 1), a choice (its index) or a number. A number goes there over its time (seconds; 0: at once), from where it
// is, eased by its curve (Engine::easingKeys) and the way its ramp says (an angle: the shortest way round); an action,
// a switch, a choice are set at once.
struct InteractionConsequence {
    quint64 layer = 0;
    QString param;
    double value = 1;
    double time = 0;
    int curve = 3; // ease in-out
    bool operator==(const InteractionConsequence &o) const
    {
        return layer == o.layer && param == o.param && value == o.value && time == o.time && curve == o.curve;
    }
};

QString interactionSourceName(InteractionSource s); // "Keyboard", "MIDI", "OSC", "Internal"
QString interactionFilterName(InteractionFilter f); // "Average"…
QString midiMessageName(MidiMessageKind k);         // "CC", "Note", "Pitch Bend", "Program"
// A filter's two settings: their names (empty: not used), their ranges and defaults
struct FilterSetting {
    QString name, suffix;
    double min = 0, max = 1, def = 0;
};
FilterSetting interactionFilterSetting(InteractionFilter f, int which); // which: 0..3 (a, b, c, d)
QString interactionFilterKey(InteractionFilter f); // as saved: "average", "math"…
InteractionFilter interactionFilterFromKey(const QString &k, bool *ok = nullptr);

// One filter of an interaction's chain
struct InteractionStage {
    InteractionFilter kind = InteractionFilter::Average;
    bool enabled = true;
    double a = 0, b = 0, c = 0, d = 0; // its settings (interactionFilterSetting)
    double &setting(int which) { return which == 0 ? a : which == 1 ? b : which == 2 ? c : d; }
    double setting(int which) const { return which == 0 ? a : which == 1 ? b : which == 2 ? c : d; }
    MathOp op = MathOp::Multiply; // Math
    AnimTrack curve;              // Curve: keys over 0..1 (time) to 0..1 (value)
    static InteractionStage made(InteractionFilter kind); // with its defaults (a Curve: straight)
    static InteractionStage remap(double inMin, double inMax, double outMin, double outMax);
    QJsonObject toJson() const;
    static InteractionStage fromJson(const QJsonObject &o);
    bool operator==(const InteractionStage &o) const;
};

// What came in from a source
struct InteractionEvent {
    InteractionSource source = InteractionSource::Keyboard;
    QString address;   // Keyboard: the key ("Space", "Shift+A"); OSC: the address
    QString port;      // MIDI: the input it came from
    int channel = 0;   // MIDI: 1..16
    MidiMessageKind message = MidiMessageKind::ControlChange;
    int number = 0;    // MIDI: the controller, the note, the program
    int argument = 0;  // OSC: which argument of the message
    double value = 0;  // Keyboard: 1 pressed, 0 released; MIDI: 0..1; OSC: the value as sent
    // A composite OSC color (native type 'r' or an RGB/RGBA number tuple).
    // 0: scalar event, 3: RGB with target alpha preserved, 4: RGBA.
    std::array<double, 4> color{{0, 0, 0, 1}};
    int colorComponents = 0;
    QString describe() const; // "MIDI · ch 1 · CC 7", "Key Space", "OSC /fader/1"
};

struct Interaction {
    quint64 id = 0;
    QString name; // its own (empty: described by its source and its target)
    bool enabled = true;
    InteractionSource source = InteractionSource::Keyboard;

    // The source
    QString key;          // Keyboard
    QString midiPort;     // MIDI: an input by name ("": any)
    int midiChannel = 0;  // MIDI: 1..16 (0: any)
    MidiMessageKind midiMessage = MidiMessageKind::ControlChange;
    int midiNumber = 0;
    QString oscAddress;   // OSC
    int oscArgument = 0;
    quint64 sourceLayer = 0; // Internal: a value of the show (a layer's id, 0: the composition) and its address
    QString sourceParam;
    // Audio is the program mix before the master fader, or the live input (AudioInput); band limits in Hz.
    double audioLow = 20, audioHigh = 20000;
    // Band: the level of the frequency region; Level: overall; Onset: how much attack there is (a smooth amount); Hit: a
    // trigger at each hit in the frequency region, with its velocity (OnsetAnalysis.h), for an Envelope or an Action
    enum class AudioFeature { Band = 0, Level, Onset, Hit };
    AudioFeature audioFeature = AudioFeature::Band;
    double audioSensitivity = 0.5; // Hit: how faint a hit it hears (0..1)
    double audioGap = 0.08;        // Hit: the shortest time between two hits (seconds)
    bool audioInput = false;
    PixelAnalysis::Settings pixels;
    PixelAnalysis::Feature pixelFeature = PixelAnalysis::Feature::Mean;
    HandTracking::Feature handFeature = HandTracking::Feature::Pinch;
    int handIndex = 0;
    double handConfidence = .7;

    // The target: a value of the show (a layer's id, 0: the composition) and its address
    quint64 layer = 0;
    QString param;
    // A complete color target normally changes RGB only. OSC 'r' and RGBA
    // tuples can carry an unused/zero alpha; opt in explicitly to map it.
    bool colorMapAlpha = false;
    // An XY mapping keeps X in this interaction and Y in one scalar child.
    // Each axis has its own input, range, filters and pick-up state.
    // The child has no independent ID or target and cannot contain children.
    std::vector<Interaction> xyInputs;
    bool drives(const QString &path) const
    {
        if (path == param) return true;
        if (!xyInputs.empty()) return path == param + "/x" || (xyInputs.front().enabled && path == param + "/y");
        return path == param + "/r" || path == param + "/g" || path == param + "/b" ||
               (colorMapAlpha && path == param + "/a");
    }

    // Action mode: its conditions on the source's value, all of them (or any) true, and what it does when that becomes
    // true, and when it becomes false (Mapping: the chain below drives the target)
    InteractionMode mode = InteractionMode::Mapping;
    std::vector<InteractionCondition> conditions;
    bool conditionsAll = true;
    std::vector<InteractionConsequence> onTrue, onFalse;
    bool isAction() const { return mode == InteractionMode::Action; }
    bool conditionsMet(double x) const; // none: true (and then each value does the "if true" actions)

    // The chain: its filters in their order, then the master level
    double master = 1;    // 0..1
    std::vector<InteractionStage> filters; // in their order
    bool movesOverTime() const; // a filter on that moves by itself between values (Average, Damper, Delay…)
    Takeover takeover = Takeover::Jump;

    bool matches(const InteractionEvent &e) const; // the event is from its source
    QString label() const; // its name, else its source ("Key Space", "MIDI ch 1 CC 7"…)
    // What a snapshot keeps of it: on / off and its chain (not its source, not its target, not its name)
    QJsonObject stateJson() const;
    void applyState(const QJsonObject &state);
    QJsonObject toJson() const;
    static Interaction fromJson(const QJsonObject &o);
    static std::vector<InteractionStage> filtersFromJson(const QJsonObject &o); // its "filters"
    static QJsonArray filtersToJson(const std::vector<InteractionStage> &filters);
    static QJsonObject merged(QJsonObject base, const QJsonObject &over); // `base` with `over`'s keys
    static AnimTrack straightCurve(); // 0 → 0, 1 → 1
    // A new one, set for its kind of source (MIDI and OSC: pick-up; a source range that suits it)
    static Interaction made(InteractionSource s);
};

// Live scalar values for UI telemetry; independent of curves and source definitions.
struct InteractionTelemetry {
    quint64 id = 0;
    double input = 0, output = 0, curveInput = 0;
    double yInput = 0, yOutput = 0, yCurveInput = 0;
    bool hasY = false;
    int met = -1; // an Action: its conditions held (1) or not (0) at the last value; -1: none yet, or a Mapping
    std::vector<double> stages, yStages; // the value after each filter (X's, Y's), in their order
};

// The chain of one interaction, from its source's values to the value of its target. Pure: no engine, no clock.
class InteractionProcessor
{
public:
    // A value from the source (in the source's units)
    void input(const Interaction &it, double raw);
    // One frame of `dt` seconds: the filters move on. The result after the filters and the master, for the target as it
    // is (none: nothing came in yet)
    std::optional<double> step(const Interaction &it, double dt);
    std::optional<double> level() const { return m_level; } // the source's last value (shown)
    std::optional<double> output() const { return m_out; }  // the last result of step
    std::optional<double> curveInput() const { return m_curveIn; } // what entered the first curve on
    const std::vector<double> &stageOutputs() const { return m_stageOut; } // the value after each filter (shown)
    void reset() { *this = InteractionProcessor(); }
    // The value of a Hit source: the pulse of this interaction's own detector, from the flux of its audio input
    double hit(const OnsetFlux &flux, const Interaction &it, double dt)
    {
        return m_hit.step(flux, it.audioLow, it.audioHigh, it.audioSensitivity, it.audioGap, dt);
    }

private:
    // Each filter's state, made again when it is another kind of filter
    struct StageState {
        InteractionFilter kind = InteractionFilter::Average;
        bool started = false, high = false;
        double y = 0, v = 0, slow = 0, prev = 0;     // its value, its speed (Damper), its slow average (Pulse), the last input
        int phase = 0;                                // Envelope: 0 idle, 1 attack, 2 decay, 3 sustain, 4 release
        double peak = 1;                              // Envelope: the level the trigger came in at (its velocity)
        std::deque<std::pair<double, double>> window; // Average, Delay: (time, value)
    };
    OnsetDetector m_hit;             // a Hit source: what it learned of the sound
    std::optional<double> m_level;   // the source's last value
    bool m_fresh = false;            // a value came in since the last step
    double m_time = 0;
    std::vector<StageState> m_stages;
    std::vector<double> m_stageOut;
    std::optional<double> m_out, m_curveIn;
};

// A color target's pick-up: every channel has to meet the target's before the interaction takes it over
struct InteractionColorTargetState {
    bool caught = false;
    std::array<bool, 4> met{{false, false, false, false}};
    std::optional<std::array<double, 4>> written, prevOut;
    // `v`: the interaction's color (0..1 per channel); `current`: the target's now;
    // `channels`: 3 or 4. The values to write, if any (none: held by pick-up, or already there)
    std::optional<std::array<double, 4>> toWrite(const Interaction &it, const std::array<double, 4> &v,
                                                  const std::array<double, 4> &current, int channels, double tolerance);
};

// Where a target stands for an interaction: what it wrote last, and whether it holds it (pick-up)
struct InteractionTargetState {
    bool caught = false;
    std::optional<double> written, prevOut;
    bool above = false; // trigger: the last output was above the middle
    // A result (the target's value to be) and the target's value now: the value to write, if any (number targets)
    std::optional<double> numberToWrite(const Interaction &it, double out, double current, double tolerance);
};
