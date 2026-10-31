#include "Interactions.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------
static const char *const kSources[] = {"keyboard", "midi", "osc", "internal", "audio", "pixels", "hands"};
static const char *const kFilters[] = {"average", "weighted_average", "pulse_detection", "attack_release", "damper",
                                       "curve", "math", "clamp", "steps", "threshold", "delay", "speed", "remap", "toggle", "envelope"};
static const char *const kMathOps[] = {"add", "subtract", "multiply", "divide", "modulo", "power", "min", "max",
                                       "abs", "floor", "ceil", "round", "one_minus"};

template <size_t N> static int keyIndex(const char *const (&keys)[N], const QString &k, int def)
{
    for (size_t i = 0; i < N; ++i)
        if (k == QLatin1String(keys[i])) return int(i);
    return def;
}
static const char *const kMidi[] = {"cc", "note", "pitch_bend", "program"};

static const char *const kOps[] = {"eq", "ne", "gt", "ge", "lt", "le", "in", "out"};

QString conditionOpName(ConditionOp op)
{
    switch (op) {
    case ConditionOp::Equal: return QStringLiteral("=");
    case ConditionOp::NotEqual: return QStringLiteral("≠");
    case ConditionOp::Greater: return QStringLiteral(">");
    case ConditionOp::GreaterOrEqual: return QStringLiteral("≥");
    case ConditionOp::Less: return QStringLiteral("<");
    case ConditionOp::LessOrEqual: return QStringLiteral("≤");
    case ConditionOp::InRange: return QStringLiteral("in range");
    case ConditionOp::OutOfRange: return QStringLiteral("out of range");
    }
    return QString();
}

bool InteractionCondition::test(double x) const
{
    const bool same = std::abs(x - a) <= 1e-6 * std::max(1.0, std::abs(a));
    const double lo = std::min(a, b), hi = std::max(a, b);
    switch (op) {
    case ConditionOp::Equal: return same;
    case ConditionOp::NotEqual: return !same;
    case ConditionOp::Greater: return x > a && !same;
    case ConditionOp::GreaterOrEqual: return x >= a || same;
    case ConditionOp::Less: return x < a && !same;
    case ConditionOp::LessOrEqual: return x <= a || same;
    case ConditionOp::InRange: return x >= lo && x <= hi;
    case ConditionOp::OutOfRange: return x < lo || x > hi;
    }
    return false;
}

bool Interaction::movesOverTime() const
{
    return std::any_of(filters.begin(), filters.end(), [](const InteractionStage &f) {
        return f.enabled && f.kind != InteractionFilter::Curve && f.kind != InteractionFilter::Math &&
               f.kind != InteractionFilter::Clamp && f.kind != InteractionFilter::Steps && f.kind != InteractionFilter::Threshold;
    });
}

bool Interaction::conditionsMet(double x) const
{
    if (conditions.empty()) return true;
    if (conditionsAll) return std::all_of(conditions.begin(), conditions.end(), [x](const auto &c) { return c.test(x); });
    return std::any_of(conditions.begin(), conditions.end(), [x](const auto &c) { return c.test(x); });
}

QString interactionSourceName(InteractionSource s)
{
    switch (s) {
    case InteractionSource::Keyboard: return QStringLiteral("Keyboard");
    case InteractionSource::Midi: return QStringLiteral("MIDI");
    case InteractionSource::Osc: return QStringLiteral("OSC");
    case InteractionSource::Internal: return QStringLiteral("Internal");
    case InteractionSource::Audio: return QStringLiteral("Audio");
    case InteractionSource::Pixels:
        return QStringLiteral("Pixels");
    case InteractionSource::Hands:
        return QStringLiteral("Hands");
    }
    return QString();
}

QString interactionFilterName(InteractionFilter f)
{
    switch (f) {
    case InteractionFilter::Average: return QStringLiteral("Average");
    case InteractionFilter::WeightedAverage: return QStringLiteral("Weighted Average");
    case InteractionFilter::PulseDetection: return QStringLiteral("Pulse Detection");
    case InteractionFilter::AttackRelease: return QStringLiteral("Attack Release");
    case InteractionFilter::Damper: return QStringLiteral("Damper");
    case InteractionFilter::Curve: return QStringLiteral("Curve");
    case InteractionFilter::Math: return QStringLiteral("Math");
    case InteractionFilter::Clamp: return QStringLiteral("Clamp");
    case InteractionFilter::Steps: return QStringLiteral("Steps");
    case InteractionFilter::Threshold: return QStringLiteral("Threshold");
    case InteractionFilter::Delay: return QStringLiteral("Delay");
    case InteractionFilter::Speed: return QStringLiteral("Speed");
    case InteractionFilter::Remap: return QStringLiteral("Remap");
    case InteractionFilter::Toggle: return QStringLiteral("Toggle");
    case InteractionFilter::Envelope: return QStringLiteral("Envelope");
    }
    return QString();
}

QString interactionFilterKey(InteractionFilter f) { return QString::fromLatin1(kFilters[std::clamp(int(f), 0, kInteractionFilterCount - 1)]); }

InteractionFilter interactionFilterFromKey(const QString &k, bool *ok)
{
    for (int i = 0; i < kInteractionFilterCount; ++i)
        if (k == QLatin1String(kFilters[i])) {
            if (ok) *ok = true;
            return InteractionFilter(i);
        }
    if (ok) *ok = false;
    return InteractionFilter::Average;
}

QString mathOpName(MathOp op)
{
    switch (op) {
    case MathOp::Add: return QStringLiteral("x + a");
    case MathOp::Subtract: return QStringLiteral("x − a");
    case MathOp::Multiply: return QStringLiteral("x × a");
    case MathOp::Divide: return QStringLiteral("x ÷ a");
    case MathOp::Modulo: return QStringLiteral("x modulo a");
    case MathOp::Power: return QStringLiteral("x to the power a");
    case MathOp::Min: return QStringLiteral("smaller of x and a");
    case MathOp::Max: return QStringLiteral("larger of x and a");
    case MathOp::Abs: return QStringLiteral("absolute x");
    case MathOp::Floor: return QStringLiteral("x down to a step of a");
    case MathOp::Ceil: return QStringLiteral("x up to a step of a");
    case MathOp::Round: return QStringLiteral("x rounded to a step of a");
    case MathOp::OneMinus: return QStringLiteral("1 − x (invert)");
    }
    return QString();
}

bool mathOpUsesValue(MathOp op) { return op != MathOp::Abs && op != MathOp::OneMinus; }

double applyMathOp(MathOp op, double x, double a)
{
    switch (op) {
    case MathOp::Add: return x + a;
    case MathOp::Subtract: return x - a;
    case MathOp::Multiply: return x * a;
    case MathOp::Divide: return std::abs(a) < 1e-12 ? x : x / a; // ÷ 0: left as it is
    case MathOp::Modulo: return std::abs(a) < 1e-12 ? x : x - a * std::floor(x / a); // always ≥ 0 for a > 0
    case MathOp::Power: {
        const double p = std::pow(std::abs(x), a);
        return std::isfinite(p) ? std::copysign(p, x) : x;
    }
    case MathOp::Min: return std::min(x, a);
    case MathOp::Max: return std::max(x, a);
    case MathOp::Abs: return std::abs(x);
    case MathOp::Floor: return a > 1e-12 ? std::floor(x / a) * a : std::floor(x);
    case MathOp::Ceil: return a > 1e-12 ? std::ceil(x / a) * a : std::ceil(x);
    case MathOp::Round: return a > 1e-12 ? std::round(x / a) * a : std::round(x);
    case MathOp::OneMinus: return 1.0 - x;
    }
    return x;
}

QString midiMessageName(MidiMessageKind k)
{
    switch (k) {
    case MidiMessageKind::ControlChange: return QStringLiteral("CC");
    case MidiMessageKind::Note: return QStringLiteral("Note");
    case MidiMessageKind::PitchBend: return QStringLiteral("Pitch Bend");
    case MidiMessageKind::ProgramChange: return QStringLiteral("Program");
    }
    return QString();
}

FilterSetting interactionFilterSetting(InteractionFilter f, int which)
{
    const QString s = QStringLiteral(" s");
    auto two = [which](FilterSetting a, FilterSetting b = FilterSetting{}) { return which == 0 ? a : which == 1 ? b : FilterSetting{}; };
    switch (f) {
    case InteractionFilter::Average: return two({"Window", s, 0.01, 5, 0.2});
    case InteractionFilter::WeightedAverage: return two({"Smoothing", s, 0.01, 5, 0.2});
    case InteractionFilter::PulseDetection: return two({"Threshold", "", 0.01, 1, 0.2}, {"Decay", s, 0.01, 5, 0.3});
    case InteractionFilter::AttackRelease: return two({"Attack", s, 0.001, 5, 0.05}, {"Release", s, 0.001, 5, 0.5});
    case InteractionFilter::Damper: return two({"Response", s, 0.02, 5, 0.4}, {"Damping", "", 0.05, 2, 0.4});
    case InteractionFilter::Curve: return FilterSetting{};
    case InteractionFilter::Math: return two({"Value", "", -100, 100, 1});
    case InteractionFilter::Clamp: return two({"Min", "", 0, 1, 0}, {"Max", "", 0, 1, 1});
    case InteractionFilter::Steps: return two({"Steps", "", 2, 64, 4});
    case InteractionFilter::Threshold: return two({"Threshold", "", 0, 1, 0.5}, {"Margin", "", 0, 0.5, 0.05});
    case InteractionFilter::Delay: return two({"Delay", s, 0, 10, 0.5});
    case InteractionFilter::Speed: return two({"Full scale", " /s", 0.01, 100, 1}, {"Smoothing", s, 0.001, 5, 0.1});
    case InteractionFilter::Toggle: return two({"Threshold", "", 0, 1, 0.5});
    case InteractionFilter::Envelope: {
        // The defaults are an impact: a hit at once, gone in a third of a second. Sustain above 0 holds while the trigger is held
        const FilterSetting e[] = {{"Attack", s, 0.001, 5, 0.005}, {"Decay", s, 0.001, 10, 0.3}, {"Sustain", "", 0, 1, 0}, {"Release", s, 0.001, 10, 0.3}};
        return which >= 0 && which < 4 ? e[which] : FilterSetting{};
    }
    case InteractionFilter::Remap: {
        const FilterSetting r[] = {{"In Min", "", 0, 1, 0}, {"In Max", "", 0, 1, 1}, {"Out Min", "", 0, 1, 0}, {"Out Max", "", 0, 1, 1}};
        return which >= 0 && which < 4 ? r[which] : FilterSetting{};
    }
    }
    return FilterSetting{};
}

QString InteractionEvent::describe() const
{
    switch (source) {
    case InteractionSource::Keyboard: return QStringLiteral("Key %1").arg(address);
    case InteractionSource::Midi: {
        QString s = QStringLiteral("MIDI · ch %1 · %2").arg(channel).arg(midiMessageName(message));
        if (message != MidiMessageKind::PitchBend) s += QStringLiteral(" %1").arg(number);
        return s;
    }
    case InteractionSource::Osc: return QStringLiteral("OSC %1").arg(address) + (argument ? QStringLiteral(" [%1]").arg(argument) : QString());
    case InteractionSource::Internal: return QStringLiteral("Internal");
    case InteractionSource::Audio: return QStringLiteral("Audio");
    case InteractionSource::Pixels:
        return QStringLiteral("Pixels");
    case InteractionSource::Hands:
        return QStringLiteral("Hands");
    }
    return QString();
}

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------
bool Interaction::matches(const InteractionEvent &e) const
{
    if (e.source != source) return false;
    switch (source) {
    case InteractionSource::Keyboard: return !key.isEmpty() && e.address.compare(key, Qt::CaseInsensitive) == 0;
    case InteractionSource::Midi:
        if (!midiPort.isEmpty() && e.port != midiPort) return false;
        if (midiChannel && e.channel != midiChannel) return false;
        if (e.message != midiMessage) return false;
        return midiMessage == MidiMessageKind::PitchBend || e.number == midiNumber;
    case InteractionSource::Osc: return !oscAddress.isEmpty() && e.address == oscAddress && e.argument == oscArgument;
    case InteractionSource::Pixels:
    case InteractionSource::Hands:
    case InteractionSource::Audio:
    case InteractionSource::Internal: return false; // read from the show each frame, not sent
    }
    return false;
}

QString Interaction::label() const
{
    if (!name.isEmpty()) return name;
    switch (source) {
    case InteractionSource::Keyboard: return key.isEmpty() ? QStringLiteral("Key") : QStringLiteral("Key %1").arg(key);
    case InteractionSource::Midi:
        return QStringLiteral("MIDI %1 %2%3")
            .arg(midiChannel ? QStringLiteral("ch %1").arg(midiChannel) : QStringLiteral("any ch"), midiMessageName(midiMessage),
                 midiMessage == MidiMessageKind::PitchBend ? QString() : QStringLiteral(" %1").arg(midiNumber));
    case InteractionSource::Osc: return oscAddress.isEmpty() ? QStringLiteral("OSC") : QStringLiteral("OSC %1").arg(oscAddress);
    case InteractionSource::Internal: return QStringLiteral("Internal %1").arg(id);
    case InteractionSource::Pixels:
        return QStringLiteral("Pixels · ") + PixelAnalysis::featureNames().value(int(pixelFeature));
    case InteractionSource::Hands:
        return QStringLiteral("Hand %1 · ").arg(handIndex + 1) + HandTracking::featureNames().value(int(handFeature));
    case InteractionSource::Audio: {
        const QString what = audioFeature==AudioFeature::Band ? QStringLiteral("%1–%2 Hz").arg(audioLow,0,'f',0).arg(audioHigh,0,'f',0) : audioFeature==AudioFeature::Level ? QStringLiteral("level") : audioFeature==AudioFeature::Hit ? QStringLiteral("hits %1–%2 Hz").arg(audioLow,0,'f',0).arg(audioHigh,0,'f',0) : QStringLiteral("attacks");
        return (audioInput ? QStringLiteral("Audio in ") : QStringLiteral("Audio ")) + what;
    }
    }
    return QString();
}

static const char *const kNotState[] = {
    "id",           "name",           "source",          "key",         "port",         "channel",
    "message",      "number",         "address",         "argument",    "source_layer", "source_param",
    "audio_low",    "audio_high",     "audio_feature",   "audio_input", "pixels",       "pixel_feature",
    "hand_feature", "hand_index",     "hand_confidence", "layer",       "param",        "mode",
    "conditions",   "conditions_all", "if_true",         "if_false"}; // an Action's: its definition

QJsonObject Interaction::stateJson() const
{
    QJsonObject o = toJson();
    for (const char *k : kNotState) o.remove(QLatin1String(k));
    o.remove("xy_y"); // source definitions remain unchanged during snapshot recall
    if (!xyInputs.empty()) o["xy_y_state"] = xyInputs.front().stateJson();
    return o;
}

void Interaction::applyState(const QJsonObject &state)
{
    QJsonObject kept;
    for (auto it = state.begin(); it != state.end(); ++it) {
        bool keep = true;
        for (const char *k : kNotState) keep = keep && it.key() != QLatin1String(k);
        if (keep && it.key() != QLatin1String("xy_y") && it.key() != QLatin1String("xy_y_state")) kept[it.key()] = it.value();
    }
    *this = fromJson(merged(toJson(), kept));
    if (!xyInputs.empty() && state.contains("xy_y_state")) xyInputs.front().applyState(state.value("xy_y_state").toObject());
}

// ---------------------------------------------------------------------------
// The filters
// ---------------------------------------------------------------------------
InteractionStage InteractionStage::made(InteractionFilter kind)
{
    InteractionStage s;
    s.kind = kind;
    for (int w = 0; w < kFilterSettingCount; ++w) s.setting(w) = interactionFilterSetting(kind, w).def;
    if (kind == InteractionFilter::Curve) s.curve = Interaction::straightCurve();
    return s;
}

InteractionStage InteractionStage::remap(double inMin, double inMax, double outMin, double outMax)
{
    InteractionStage s = made(InteractionFilter::Remap);
    s.a = inMin;
    s.b = inMax;
    s.c = outMin;
    s.d = outMax;
    return s;
}

bool InteractionStage::operator==(const InteractionStage &o) const
{
    if (kind != o.kind || enabled != o.enabled || a != o.a || b != o.b || c != o.c || d != o.d || op != o.op ||
        curve.keys.size() != o.curve.keys.size())
        return false;
    for (size_t k = 0; k < curve.keys.size(); ++k) {
        const AnimKey &x = curve.keys[k], &y = o.curve.keys[k];
        if (x.t != y.t || x.v != y.v || x.curve != y.curve || x.inDt != y.inDt || x.inDv != y.inDv || x.outDt != y.outDt ||
            x.outDv != y.outDv)
            return false;
    }
    return true;
}

static QJsonArray curveToJson(const AnimTrack &curve)
{
    QJsonArray keys;
    for (const AnimKey &k : curve.keys) {
        QJsonObject kj{{"t", k.t}, {"v", k.v}, {"curve", k.curve}};
        if (k.hasIn()) kj["in"] = QJsonArray{k.inDt, k.inDv};
        if (k.hasOut()) kj["out"] = QJsonArray{k.outDt, k.outDv};
        keys.append(kj);
    }
    return keys;
}

static AnimTrack curveFromJson(const QJsonArray &a)
{
    AnimTrack curve;
    for (const QJsonValue &v : a) {
        const QJsonObject kj = v.toObject();
        AnimKey k;
        k.t = std::clamp(kj.value("t").toDouble(), 0.0, 1.0);
        k.v = kj.value("v").toDouble();
        k.curve = kj.value("curve").toInt(0);
        const QJsonArray in = kj.value("in").toArray(), out = kj.value("out").toArray();
        k.inDt = in.at(0).toDouble(0);
        k.inDv = in.at(1).toDouble(0);
        k.outDt = out.at(0).toDouble(0);
        k.outDv = out.at(1).toDouble(0);
        curve.keys.push_back(k);
    }
    std::sort(curve.keys.begin(), curve.keys.end(), [](const AnimKey &x, const AnimKey &y) { return x.t < y.t; });
    if (curve.keys.empty()) curve = Interaction::straightCurve();
    return curve;
}

QJsonObject InteractionStage::toJson() const
{
    QJsonObject o{{"type", interactionFilterKey(kind)}};
    if (!enabled) o["enabled"] = false;
    for (int w = 0; w < kFilterSettingCount; ++w)
        if (!interactionFilterSetting(kind, w).name.isEmpty()) o[QString(QChar('a' + w))] = setting(w);
    if (kind == InteractionFilter::Math) o["op"] = QString::fromLatin1(kMathOps[std::clamp(int(op), 0, kMathOpCount - 1)]);
    if (kind == InteractionFilter::Curve) o["curve"] = curveToJson(curve);
    return o;
}

InteractionStage InteractionStage::fromJson(const QJsonObject &o)
{
    InteractionStage s = made(interactionFilterFromKey(o.value("type").toString()));
    s.enabled = o.value("enabled").toBool(true);
    for (int w = 0; w < kFilterSettingCount; ++w) {
        const double v = o.value(QString(QChar('a' + w))).toDouble(s.setting(w));
        if (std::isfinite(v)) s.setting(w) = v;
    }
    s.op = MathOp(keyIndex(kMathOps, o.value("op").toString(), int(MathOp::Multiply)));
    if (s.kind == InteractionFilter::Curve) s.curve = curveFromJson(o.value("curve").toArray());
    return s;
}

QJsonArray Interaction::filtersToJson(const std::vector<InteractionStage> &filters)
{
    QJsonArray a;
    for (const InteractionStage &s : filters) a.append(s.toJson());
    return a;
}

std::vector<InteractionStage> Interaction::filtersFromJson(const QJsonObject &o)
{
    std::vector<InteractionStage> out;
    for (const QJsonValue &v : o.value("filters").toArray())
        if (v.isObject()) out.push_back(InteractionStage::fromJson(v.toObject()));
    return out;
}

QJsonObject Interaction::merged(QJsonObject base, const QJsonObject &over)
{
    for (auto it = over.begin(); it != over.end(); ++it) base[it.key()] = it.value();
    return base;
}

AnimTrack Interaction::straightCurve()
{
    AnimTrack t;
    AnimKey a, b;
    a.t = 0;
    a.v = 0;
    b.t = 1;
    b.v = 1;
    t.keys = {a, b};
    return t;
}

Interaction Interaction::made(InteractionSource s)
{
    Interaction it;
    it.source = s;
    if (s == InteractionSource::Audio || s == InteractionSource::Pixels ||
        s == InteractionSource::Hands) { // smoothed: a band's level flickers
        InteractionStage smooth = InteractionStage::made(InteractionFilter::AttackRelease);
        smooth.a = .012;
        smooth.b = .2;
        it.filters = {smooth};
    }
    // A fader or a knob: it waits to meet the value (pick-up); a key, a link: it sets it
    it.takeover = s == InteractionSource::Midi || s == InteractionSource::Osc ? Takeover::PickUp : Takeover::Jump;
    return it;
}

QJsonObject Interaction::toJson() const
{
    QJsonObject o;
    o["id"] = QString::number(id); // ids are strings in files
    if (!name.isEmpty()) o["name"] = name;
    o["enabled"] = enabled;
    o["source"] = kSources[int(source)];
    switch (source) {
    case InteractionSource::Keyboard: o["key"] = key; break;
    case InteractionSource::Midi:
        if (!midiPort.isEmpty()) o["port"] = midiPort;
        o["channel"] = midiChannel;
        o["message"] = kMidi[int(midiMessage)];
        o["number"] = midiNumber;
        break;
    case InteractionSource::Osc:
        o["address"] = oscAddress;
        o["argument"] = oscArgument;
        break;
    case InteractionSource::Internal:
        o["source_layer"] = QString::number(sourceLayer);
        o["source_param"] = sourceParam;
        break;
    case InteractionSource::Pixels:
    case InteractionSource::Hands:
        o["pixels"] = pixels.toJson();
        o["pixel_feature"] = int(pixelFeature);
        o["hand_feature"] = int(handFeature);
        o["hand_index"] = handIndex;
        o["hand_confidence"] = handConfidence;
        break;
    case InteractionSource::Audio:
        o["audio_low"] = audioLow;
        o["audio_high"] = audioHigh;
        o["audio_feature"] = int(audioFeature);
        o["audio_input"] = audioInput;
        if (audioFeature == AudioFeature::Hit) {
            o["audio_sensitivity"] = audioSensitivity;
            o["audio_gap"] = audioGap;
        }
        break;
    }
    o["layer"] = QString::number(layer);
    o["param"] = param;
    o["color_map_alpha"] = colorMapAlpha;
    if (!xyInputs.empty()) {
        Interaction y = xyInputs.front();
        y.xyInputs.clear();
        o["xy_y"] = y.toJson();
    }
    o["master"] = master;
    o["filters"] = filtersToJson(filters);
    o["takeover"] = takeover == Takeover::PickUp ? "pickup" : "jump";
    if (isAction()) {
        o["mode"] = QStringLiteral("action");
        QJsonArray conds;
        for (const InteractionCondition &c : conditions)
            conds.append(QJsonObject{{"op", kOps[std::clamp(int(c.op), 0, kConditionOpCount - 1)]}, {"a", c.a}, {"b", c.b}});
        o["conditions"] = conds;
        o["conditions_all"] = conditionsAll;
        auto list = [](const std::vector<InteractionConsequence> &v) {
            QJsonArray a;
            for (const InteractionConsequence &c : v)
                a.append(QJsonObject{{"layer", QString::number(c.layer)}, {"param", c.param}, {"value", c.value},
                                     {"time", c.time}, {"curve", c.curve}});
            return a;
        };
        o["if_true"] = list(onTrue);
        o["if_false"] = list(onFalse);
    }
    return o;
}


Interaction Interaction::fromJson(const QJsonObject &o)
{
    Interaction it;
    it.id = o.value("id").toString().toULongLong();
    it.name = o.value("name").toString();
    it.enabled = o.value("enabled").toBool(true);
    it.source = InteractionSource(keyIndex(kSources, o.value("source").toString(), 0));
    it.key = o.value("key").toString();
    it.midiPort = o.value("port").toString();
    it.midiChannel = std::clamp(o.value("channel").toInt(0), 0, 16);
    it.midiMessage = MidiMessageKind(keyIndex(kMidi, o.value("message").toString(), 0));
    it.midiNumber = std::clamp(o.value("number").toInt(0), 0, 127);
    it.oscAddress = o.value("address").toString();
    it.oscArgument = std::max(0, o.value("argument").toInt(0));
    it.sourceLayer = o.value("source_layer").toString().toULongLong();
    it.sourceParam = o.value("source_param").toString();
    auto bounded=[](double v,double fallback){return std::isfinite(v)?std::clamp(v,20.,20000.):fallback;};
    it.audioLow=bounded(o.value("audio_low").toDouble(20),20);
    it.audioHigh=bounded(o.value("audio_high").toDouble(20000),20000);
    if(it.audioLow>it.audioHigh)std::swap(it.audioLow,it.audioHigh);
    it.audioFeature=AudioFeature(std::clamp(o.value("audio_feature").toInt(),0,3));
    { const double s=o.value("audio_sensitivity").toDouble(0.5),g=o.value("audio_gap").toDouble(0.08);
      it.audioSensitivity=std::isfinite(s)?std::clamp(s,0.,1.):0.5; it.audioGap=std::isfinite(g)?std::clamp(g,0.04,2.):0.08; }
    it.audioInput=o.value("audio_input").toBool(false);
    it.pixels = PixelAnalysis::Settings::fromJson(o.value("pixels").toObject());
    it.pixelFeature = PixelAnalysis::Feature(std::clamp(o.value("pixel_feature").toInt(), 0, 10));
    it.handFeature = HandTracking::Feature(std::clamp(o.value("hand_feature").toInt(8), 0, 11));
    it.handIndex = std::clamp(o.value("hand_index").toInt(), 0, 1);
    it.handConfidence = std::clamp(o.value("hand_confidence").toDouble(.7), .5, .99);
    it.layer = o.value("layer").toString().toULongLong();
    it.param = o.value("param").toString();
    it.colorMapAlpha = o.value("color_map_alpha").toBool(false);
    if (o.value("xy_y").isObject()) {
        auto y = o.value("xy_y").toObject();
        y.remove("xy_y"); // fixed arity; never parse nested interactions
        it.xyInputs.push_back(fromJson(y));
    }
    it.master = std::clamp(o.value("master").toDouble(1), 0.0, 1.0);
    it.filters = filtersFromJson(o);
    it.takeover = o.value("takeover").toString() == QLatin1String("pickup") ? Takeover::PickUp : Takeover::Jump;
    // An XY mapping drives its point: never an Action
    it.mode = o.value("mode").toString() == QLatin1String("action") && it.xyInputs.empty() ? InteractionMode::Action
                                                                                           : InteractionMode::Mapping;
    for (const QJsonValue &v : o.value("conditions").toArray()) {
        const QJsonObject c = v.toObject();
        InteractionCondition cond;
        cond.op = ConditionOp(keyIndex(kOps, c.value("op").toString(), int(ConditionOp::GreaterOrEqual)));
        const double a = c.value("a").toDouble(0.5), b = c.value("b").toDouble(1);
        cond.a = std::isfinite(a) ? a : 0.5;
        cond.b = std::isfinite(b) ? b : 1;
        it.conditions.push_back(cond);
    }
    it.conditionsAll = o.value("conditions_all").toBool(true);
    auto list = [](const QJsonValue &v) {
        std::vector<InteractionConsequence> out;
        for (const QJsonValue &x : v.toArray()) {
            const QJsonObject c = x.toObject();
            InteractionConsequence q;
            q.layer = c.value("layer").toString().toULongLong();
            q.param = c.value("param").toString();
            const double value = c.value("value").toDouble(1);
            q.value = std::isfinite(value) ? value : 1;
            const double time = c.value("time").toDouble(0);
            q.time = std::isfinite(time) ? std::clamp(time, 0.0, 600.0) : 0;
            q.curve = std::clamp(c.value("curve").toInt(3), 0, 5);
            if (!q.param.isEmpty()) out.push_back(q);
        }
        return out;
    };
    it.onTrue = list(o.value("if_true"));
    it.onFalse = list(o.value("if_false"));
    return it;
}

// ---------------------------------------------------------------------------
// The chain
// ---------------------------------------------------------------------------
void InteractionProcessor::input(const Interaction &it, double raw)
{
    (void)it;
    if (!std::isfinite(raw)) return;
    m_level = raw;
    m_fresh = true;
}

std::optional<double> InteractionProcessor::step(const Interaction &it, double dt)
{
    if (!m_level) return std::nullopt;
    dt = std::clamp(dt, 0.0, 0.25); // a long stall is not a long smoothing
    m_time += dt;
    double y = *m_level;
    m_curveIn.reset();
    if (m_stages.size() != it.filters.size()) m_stages.resize(it.filters.size());
    m_stageOut.resize(it.filters.size());
    auto toward = [dt](double from, double to, double tau) { return from + (to - from) * (1.0 - std::exp(-dt / tau)); };
    for (size_t i = 0; i < it.filters.size(); ++i) {
        const InteractionStage &f = it.filters[i];
        StageState &st = m_stages[i];
        if (st.kind != f.kind) st = StageState{f.kind}; // another kind of filter: from scratch
        if (!f.enabled) {
            m_stageOut[i] = y; // through
            continue;
        }
        const double x = y;
        if (!st.started) { // starts where its input is (not from 0)
            st.y = f.kind == InteractionFilter::PulseDetection || f.kind == InteractionFilter::Speed || f.kind == InteractionFilter::Envelope ? 0.0 : x;
            st.slow = st.prev = x;
            st.v = 0;
            st.high = f.kind != InteractionFilter::Toggle && f.kind != InteractionFilter::Envelope && x >= f.a; // a Toggle: off, its first press turns it on; an Envelope: a trigger held at the start fires it
            if (f.kind == InteractionFilter::Toggle) st.v = 0;
            st.started = true;
        }
        const double a = std::max(1e-3, f.a), b = std::max(1e-3, f.b);
        switch (f.kind) {
        case InteractionFilter::Average: {
            st.window.push_back({m_time, x});
            while (!st.window.empty() && st.window.front().first < m_time - a) st.window.pop_front();
            double sum = 0;
            for (const auto &[t, v] : st.window) sum += v;
            y = st.window.empty() ? x : sum / double(st.window.size());
            break;
        }
        case InteractionFilter::WeightedAverage: y = st.y = toward(st.y, x, a / 3.0); break; // ~95% of the way in `a`
        case InteractionFilter::PulseDetection:
            // A rise above the slow average by more than the threshold: a pulse, decaying in `b`
            if (m_fresh && x - st.slow > a) st.y = 1.0;
            else st.y = std::max(0.0, st.y - dt / b);
            st.slow = toward(st.slow, x, 0.25);
            y = st.y;
            break;
        case InteractionFilter::AttackRelease: y = st.y = toward(st.y, x, (x > st.y ? a : b) / 3.0); break;
        case InteractionFilter::Damper: {
            // A spring of period `a`, damped by `b` (1: no overshoot), in small steps for stability
            const double w = 2.0 * M_PI / a;
            const int n = std::max(1, int(std::ceil(dt / 0.002)));
            const double h = dt / n;
            for (int k = 0; k < n; ++k) {
                st.v += (w * w * (x - st.y) - 2.0 * b * w * st.v) * h;
                st.y += st.v * h;
            }
            y = st.y;
            break;
        }
        case InteractionFilter::Curve:
            if (!m_curveIn) m_curveIn = x; // the first curve's: shown on its lane
            if (!f.curve.keys.empty()) {
                const double c = f.curve.valueAt(std::clamp(x, 0.0, 1.0), std::clamp(x, 0.0, 1.0));
                if (std::isfinite(c)) y = c;
            }
            break;
        case InteractionFilter::Math: {
            const double r = applyMathOp(f.op, x, f.a);
            if (std::isfinite(r)) y = r;
            break;
        }
        case InteractionFilter::Clamp: y = std::clamp(x, std::min(f.a, f.b), std::max(f.a, f.b)); break;
        case InteractionFilter::Steps: {
            const int n = std::clamp(int(std::lround(f.a)), 2, 1024); // levels, 0 and 1 included
            y = std::round(std::clamp(x, 0.0, 1.0) * (n - 1)) / (n - 1);
            break;
        }
        case InteractionFilter::Threshold: {
            // On above the threshold plus half the margin, off below it minus half: no flicker around it
            const double half = std::max(0.0, f.b) / 2;
            if (x >= f.a + half) st.high = true;
            else if (x < f.a - half) st.high = false;
            y = st.high ? 1.0 : 0.0;
            break;
        }
        case InteractionFilter::Delay: {
            // The value of `a` seconds ago (the first one until then)
            st.window.push_back({m_time, x});
            const double then = m_time - std::max(0.0, f.a);
            while (st.window.size() > 1 && st.window[1].first <= then) st.window.pop_front();
            y = st.window.front().second;
            break;
        }
        case InteractionFilter::Remap: {
            // In Min..In Max to Out Min..Out Max, held within Out at its ends (either way round)
            const double span = f.b - f.a;
            const double n = std::abs(span) < 1e-12 ? (x >= f.b ? 1.0 : 0.0) : std::clamp((x - f.a) / span, 0.0, 1.0);
            y = f.c + n * (f.d - f.c);
            break;
        }
        case InteractionFilter::Envelope: {
            // A rise from 0 is a trigger, at the level it comes in (its velocity); falling back to 0 releases. Attack is a
            // ramp from where the envelope is (a retrigger does not jump), decay and release fall exponentially (the
            // shape of a hit) and end exactly on their target. A sustain of 0 makes a pulse that ends by itself, held or not.
            const double attack = std::max(1e-3, f.a), decay = std::max(1e-3, f.b), release = std::max(1e-3, f.d);
            const double sustain = std::clamp(f.c, 0.0, 1.0);
            const bool gate = x > 1e-3;
            if (gate && !st.high) {
                st.phase = 1;
                st.peak = std::clamp(x, 0.0, 1.0);
            } else if (!gate && st.high && st.phase != 0) {
                st.phase = 4;
            }
            st.high = gate;
            auto fall = [&](double to, double time) {
                st.y += (to - st.y) * (1.0 - std::exp(-dt * 4.6 / time)); // within 1% in `time`
                if (std::abs(st.y - to) < 1e-3) st.y = to;
            };
            switch (st.phase) {
            case 1:
                st.y += dt / attack;
                if (st.y >= 1.0) {
                    st.y = 1.0;
                    st.phase = 2;
                }
                break;
            case 2:
                fall(sustain, decay);
                if (st.y == sustain) st.phase = sustain > 0 ? 3 : 0;
                break;
            case 3: st.y = sustain; break;
            case 4:
                fall(0.0, release);
                if (st.y == 0.0) st.phase = 0;
                break;
            default: break;
            }
            y = st.y * st.peak;
            break;
        }
        case InteractionFilter::Toggle: {
            // Each time it goes up past its threshold: the other state (0, 1, 0…)
            const bool above = x > f.a;
            if (above && !st.high) st.v = st.v > 0.5 ? 0.0 : 1.0;
            st.high = above;
            y = st.v;
            break;
        }
        case InteractionFilter::Speed: {
            // How fast it moves (either way), `a` per second being 1, smoothed over `b`
            const double rate = dt > 0 ? std::abs(x - st.prev) / dt : 0.0;
            st.prev = x;
            st.y = toward(st.y, std::clamp(rate / a, 0.0, 1.0), b);
            y = st.y;
            break;
        }
        }
        if (!std::isfinite(y)) y = x;
        m_stageOut[i] = y;
    }
    m_fresh = false;
    y *= std::clamp(it.master, 0.0, 1.0);
    m_out = y;
    return y;
}

std::optional<double> InteractionTargetState::numberToWrite(const Interaction &it, double out, double current, double tolerance)
{
    const double v = out;
    if (it.takeover == Takeover::Jump) {
        caught = true;
    } else {
        // Moved by something else since it was written: let go until met again
        if (caught && written && std::abs(current - *written) > tolerance) caught = false;
        if (!caught) {
            const bool met = std::abs(v - current) <= tolerance || (prevOut && (*prevOut - current) * (v - current) <= 0);
            prevOut = v;
            if (!met) return std::nullopt;
            caught = true;
        }
    }
    prevOut = v;
    if (written && std::abs(*written - v) < 1e-12 && std::abs(current - v) < 1e-12) return std::nullopt; // already there
    written = v;
    return v;
}

std::optional<std::array<double, 4>> InteractionColorTargetState::toWrite(const Interaction &it, const std::array<double, 4> &v,
                                                                          const std::array<double, 4> &current, int channels,
                                                                          double tolerance)
{
    channels = std::clamp(channels, 1, 4);
    auto farFrom = [&](const std::array<double, 4> &a, const std::array<double, 4> &b) {
        for (int c = 0; c < channels; ++c)
            if (std::abs(a[size_t(c)] - b[size_t(c)]) > tolerance) return true;
        return false;
    };
    if (it.takeover == Takeover::Jump) {
        caught = true;
    } else {
        // Moved by something else since it was written: let go, every channel has to meet it again
        if (caught && written && farFrom(current, *written)) {
            caught = false;
            met = {{false, false, false, false}};
        }
        if (!caught) {
            bool all = true;
            for (int c = 0; c < channels; ++c) {
                const size_t k = size_t(c);
                const bool crossed = prevOut && ((*prevOut)[k] - current[k]) * (v[k] - current[k]) <= 0;
                met[k] = met[k] || std::abs(v[k] - current[k]) <= tolerance || crossed;
                all = all && met[k];
            }
            prevOut = v;
            if (!all) return std::nullopt;
            caught = true;
        }
    }
    prevOut = v;
    if (written && !farFrom(*written, v) && !farFrom(current, v)) {
        bool same = true; // within a hair: nothing to write
        for (int c = 0; c < channels; ++c) same = same && std::abs((*written)[size_t(c)] - v[size_t(c)]) < 1e-9 &&
                                                 std::abs(current[size_t(c)] - v[size_t(c)]) < 1e-9;
        if (same) return std::nullopt;
    }
    written = v;
    return v;
}
