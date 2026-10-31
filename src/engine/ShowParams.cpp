// The show's own actions as the composition's parameters: GO, a snapshot or a preset recalled, a timeline played,
// the blackout, the pause — so that an interaction (a key, a MIDI note, another value of the show) or an event of a
// timeline reaches them as it reaches any value, without OSC in between. Made again when the snapshots, the presets,
// the timelines or the sequences change (each is addressed by its stable id). Never saved: they are actions, or
// states of the show that a cue does not hold.
//
// A parameter is written with the engine's lock held, from the interface, a timeline (the render thread) or an
// interaction: what it does runs just after, on the engine's thread, as a button of the interface would.
#include "EngineInternal.h"

namespace {
QString named(const QString &name, quint64 id) { return name.isEmpty() ? QString::number(id) : name; }
} // namespace

void Engine::later(std::function<void()> f)
{
    QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection);
}

const std::vector<std::unique_ptr<Parameter>> &Engine::showParams()
{
    // What they are made from: the ids and names of what they act on (a hash: asked for at each frame an Internal
    // interaction follows one of them, without building anything)
    quint64 key = m_sequencer->revision();
    auto mix = [&key](quint64 tag, quint64 id, const QString &name) {
        key = (key ^ (tag + id * 0x9e3779b97f4a7c15ULL)) * 0x100000001b3ULL;
        key ^= qHash(name, size_t(id));
    };
    for (const Snapshot &s : m_snapshots) mix(1, s.id, s.name);
    for (const Preset &p : m_presets) mix(2, p.id, p.name);
    for (const Animation &a : m_animations) mix(3, a.id, a.name);
    if (key == m_showParamsKey && !m_showParams.empty()) return m_showParams;
    m_showParamsKey = key;
    m_showParams.clear();
    m_showParamIndex.clear();
    using T = Parameter::Type;
    auto add = [this](const QString &path, const QString &label, T type) -> Parameter & {
        m_showParams.push_back(std::make_unique<Parameter>(path, label, type));
        return *m_showParams.back();
    };
    auto action = [&](const QString &path, const QString &label, const QString &what, std::function<void()> f) {
        add(path, label, T::Trigger).bind(nullptr, [this, what, f](const QVariant &) {
            journal(QStringLiteral("show"), what);
            later(f);
        });
    };

    // The blackout (its own fade) and the pause, as the transport bar's switches
    add("show/blackout", "Show › Blackout", T::Bool)
        .ramp(Parameter::Ramp::Cut)
        .byDefault(false)
        .transient()
        .bind([this] { return QVariant(m_blackTarget < 0.5); },
              [this](const QVariant &v) {
                  const bool on = v.toBool();
                  journal(QStringLiteral("show"), on ? QStringLiteral("blackout on") : QStringLiteral("blackout off"));
                  later([this, on] { setBlackout(on); });
              });
    add("show/pause", "Show › Pause", T::Bool)
        .ramp(Parameter::Ramp::Cut)
        .byDefault(false)
        .transient()
        .bind([this] { return QVariant(m_paused.load()); },
              [this](const QVariant &v) {
                  const bool on = v.toBool();
                  journal(QStringLiteral("show"), on ? QStringLiteral("pause on") : QStringLiteral("pause off"));
                  later([this, on] { setPaused(on); });
              });

    // The selected sequence, as the GO bar
    action("show/go", "Show › GO", QStringLiteral("go"), [this] { m_sequencer->go(); });
    action("show/back", "Show › GO Back", QStringLiteral("go back"), [this] { m_sequencer->back(); });
    action("show/stop", "Show › Stop", QStringLiteral("stop"), [this] { m_sequencer->stop(); });
    action("show/stop_all", "Show › Stop All", QStringLiteral("stop all"), [this] { m_sequencer->stopAll(); });

    // Each sequence, whatever is selected
    for (int i = 0; i < m_sequencer->count(); ++i) {
        const quint64 id = m_sequencer->sequenceId(i);
        const QString base = QStringLiteral("sequence/%1/").arg(id);
        const QString cat = QStringLiteral("Sequence · %1 › ").arg(named(m_sequencer->sequence(i).name, i + 1));
        action(base + "go", cat + "GO", QStringLiteral("sequence %1 go").arg(id), [this, id] { m_sequencer->goById(id); });
        action(base + "back", cat + "GO Back", QStringLiteral("sequence %1 back").arg(id),
               [this, id] { m_sequencer->backById(id); });
        action(base + "stop", cat + "Stop", QStringLiteral("sequence %1 stop").arg(id),
               [this, id] { m_sequencer->stopById(id); });
    }

    // Each snapshot and each preset, recalled (with its fade)
    for (const Snapshot &s : m_snapshots)
        action(QStringLiteral("snapshot/%1/recall").arg(s.id), QStringLiteral("Snapshots › %1").arg(named(s.name, s.id)),
               QStringLiteral("snapshot %1 recall").arg(s.id), [this, id = s.id] { recallSnapshotById(id); });
    for (const Preset &p : m_presets)
        action(QStringLiteral("preset/%1/recall").arg(p.id), QStringLiteral("Presets › %1").arg(named(p.name, p.id)),
               QStringLiteral("preset %1 recall").arg(p.id), [this, id = p.id] {
                   const int i = indexOfPreset(id);
                   if (i >= 0) recallPreset(i);
               });

    // Each timeline: its transport, and whether it plays, its pass and its position (for an Internal interaction
    // to follow)
    for (const Animation &a : m_animations) {
        const QString base = QStringLiteral("timeline/%1/").arg(a.id);
        const QString cat = QStringLiteral("Timeline · %1 › ").arg(named(a.name, a.id));
        const quint64 id = a.id;
        const std::pair<const char *, AnimAction> transport[] = {
            {"play", AnimAction::Play}, {"pause", AnimAction::Pause}, {"stop", AnimAction::Stop}, {"rewind", AnimAction::Rewind}};
        const char *const labels[] = {"Play", "Pause", "Stop", "Rewind"};
        for (int k = 0; k < 4; ++k) {
            const AnimAction act = transport[k].second;
            action(base + transport[k].first, cat + labels[k], QStringLiteral("timeline %1 %2").arg(id).arg(transport[k].first),
                   [this, id, act] { controlAnimation(id, act); });
        }
        auto read = [this, id](std::function<QVariant(const Animation &)> f, QVariant none) {
            return [this, id, f, none] {
                for (const Animation &x : m_animations)
                    if (x.id == id) return f(x);
                return none;
            };
        };
        add(base + "playing", cat + "Playing", T::Bool).readOnly().bind(
            read([](const Animation &x) { return QVariant(x.state == AnimState::Playing); }, false), nullptr);
        // The pass it is in (1, 2…, as its card shows it; 0 stopped) and where it is within it (0..1, backwards on
        // a ping-pong's way back)
        add(base + "pass", cat + "Pass", T::Int).range(0, 100).readOnly().bind(
            read([](const Animation &x) { return QVariant(x.state == AnimState::Stopped ? 0 : x.pass()); }, 0), nullptr);
        add(base + "position", cat + "Position", T::Float).range(0, 1).readOnly().bind(
            read([](const Animation &x) { return QVariant(x.progress()); }, 0.0), nullptr);
    }
    for (const auto &p : m_showParams) m_showParamIndex.insert(p->path(), p.get());
    return m_showParams;
}

// Each sequence that follows a clock: the steps its clock passed since the last frame get their GO, in order. A jump
// (backwards, or more than a second ahead: a seek, a rewind, a loop of the soundtrack) chases: only the last step at or
// before where the clock landed. A clock that stands still does nothing.
void Engine::followSequenceClocks()
{
    Sequencer &seq = *m_sequencer;
    if (seq.revision() != m_sequenceClocksRevision) {
        m_sequenceClocksRevision = seq.revision();
        std::vector<SequenceClock> clocks;
        for (int i = 0; i < seq.count(); ++i) {
            const Sequencer::Sequence s = seq.sequence(i);
            if (s.clockParam.isEmpty()) continue;
            SequenceClock c{seq.sequenceId(i), s.clockLayer, s.clockParam, {}};
            c.muted = s.editMode;
            for (const Sequencer::Step &st : s.steps)
                if (st.at >= 0) c.cues.emplace_back(st.at, st.runtimeId);
            std::stable_sort(c.cues.begin(), c.cues.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            for (const SequenceClock &old : m_sequenceClocks) // where its clock was, when it is the same clock
                if (old.sequence == c.sequence && old.layer == c.layer && old.param == c.param && old.muted == c.muted)
                    c.last = old.last;
            if (!c.cues.empty()) clocks.push_back(std::move(c));
        }
        m_sequenceClocks = std::move(clocks);
    }
    for (SequenceClock &c : m_sequenceClocks) {
        const Parameter *p = findParameter(c.layer, c.param);
        const double t = p ? p->asNumber() : std::numeric_limits<double>::quiet_NaN();
        if (!std::isfinite(t)) continue;
        const double last = std::exchange(c.last, t);
        if (c.muted)
            continue;
        if (!std::isfinite(last) || t == last) continue; // its first reading, or standing still
        if (t > last && t - last <= 1.0) {
            for (const auto &[at, step] : c.cues)
                if (at > last && at <= t) seq.cueById(c.sequence, step);
            continue;
        }
        const auto landed = std::upper_bound(c.cues.begin(), c.cues.end(), t, [](double v, const auto &q) { return v < q.first; });
        if (landed != c.cues.begin()) seq.cueById(c.sequence, std::prev(landed)->second);
    }
}

bool Engine::isShowParam(const QString &path)
{
    for (const char *prefix : {"show/", "sequence/", "snapshot/", "preset/", "timeline/"})
        if (path.startsWith(QLatin1String(prefix))) return true;
    return false;
}
