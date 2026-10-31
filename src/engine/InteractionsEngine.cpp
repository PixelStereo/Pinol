// Engine: the interactions (Interactions.h) — their list, their sources' events, and the chains run each frame.
#include "EngineInternal.h"
#include "Midi.h"
#include "EventLog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTimer>
#include <QThread>
#include <cmath>

// Each one has a name of its own (its address in OSC, its name in the presets and the snapshots): an empty one is
// given "Interaction N", one taken gets a number after it
static void nameInteractions(std::vector<Interaction> &list)
{
    QSet<QString> taken;
    int next = 1;
    for (Interaction &it : list) {
        QString base = it.name.trimmed();
        if (base.isEmpty()) {
            while (taken.contains(QStringLiteral("Interaction %1").arg(next))) ++next;
            base = QStringLiteral("Interaction %1").arg(next);
        }
        QString n = base;
        for (int k = 2; taken.contains(n); ++k) n = QStringLiteral("%1 %2").arg(base).arg(k);
        it.name = n;
        taken.insert(n);
    }
}

static bool sameInputBinding(const Interaction &a, const Interaction &b)
{
    return a.source == b.source && a.key == b.key && a.midiPort == b.midiPort && a.midiChannel == b.midiChannel &&
           a.midiMessage == b.midiMessage && a.midiNumber == b.midiNumber && a.oscAddress == b.oscAddress &&
           a.oscArgument == b.oscArgument && a.sourceLayer == b.sourceLayer && a.sourceParam == b.sourceParam &&
           a.audioLow == b.audioLow && a.audioHigh == b.audioHigh && a.audioFeature == b.audioFeature &&
           a.audioInput == b.audioInput && a.pixels == b.pixels && a.pixelFeature == b.pixelFeature &&
           a.handFeature == b.handFeature && a.handIndex == b.handIndex && a.handConfidence == b.handConfidence &&
           a.layer == b.layer && a.param == b.param;
}

static bool sameXyInput(const Interaction &a, const Interaction &b)
{
    if (a.xyInputs.empty() != b.xyInputs.empty()) return false;
    return a.xyInputs.empty() || sameInputBinding(a.xyInputs.front(), b.xyInputs.front());
}

void Engine::configureInteractionTarget(Interaction &it, quint64 layer, const QString &path) const
{
    if (it.layer == layer && it.param == path && !it.xyInputs.empty()) return;
    it.layer = layer;
    it.param = path;
    it.xyInputs.clear();
    ParamInfo x, y;
    if (parameterInfo(layer, path + "/x", &x) && x.pointComponent == 0 &&
        parameterInfo(layer, path + "/y", &y) && y.pointComponent == 1) {
        Interaction second = it;
        second.id = 0;
        second.name.clear();
        second.layer = 0;
        second.param.clear();
        if (second.source == InteractionSource::Osc) second.oscArgument = std::min(63, second.oscArgument + 1);
        if (second.source == InteractionSource::Midi) second.midiNumber = std::min(127, second.midiNumber + 1);
        if (second.source == InteractionSource::Internal && second.sourceParam.endsWith("/x"))
            second.sourceParam = second.sourceParam.chopped(2) + "/y";
        it.xyInputs.push_back(second);
    }
}

std::vector<Interaction> Engine::interactions() const
{
    Lock lk(&m_mutex);
    return m_interactions;
}

void Engine::setInteractions(const std::vector<Interaction> &list)
{
    {
        Lock lk(&m_mutex);
        for (const auto &it : list) {
            const auto old = std::find_if(m_interactions.begin(), m_interactions.end(), [&](const auto &x) { return x.id == it.id; });
            if (old != m_interactions.end() && (!sameXyInput(*old, it) ||
                ((!old->xyInputs.empty() || !it.xyInputs.empty()) && !sameInputBinding(*old, it))))
                m_interactionRun.erase(it.id);
        }
        m_interactions = list;
        nameInteractions(m_interactions);
        for (const Interaction &it : m_interactions) m_nextInteractionId = std::max(m_nextInteractionId, it.id + 1);
        for (Interaction &it : m_interactions)
            if (!it.id) it.id = m_nextInteractionId++;
        m_interactionParamsDirty = true;
        // Those gone: their state goes; those that stay keep theirs (a fader held is not let go by an edit)
        for (auto r = m_interactionRun.begin(); r != m_interactionRun.end();) {
            const bool kept = std::any_of(m_interactions.begin(), m_interactions.end(), [&](const Interaction &it) { return it.id == r->first; });
            r = kept ? std::next(r) : m_interactionRun.erase(r);
        }
    }
    updateMidiInputs();
    emit interactionsChanged();
}

quint64 Engine::addInteraction(const Interaction &it)
{
    quint64 id;
    {
        Lock lk(&m_mutex);
        Interaction n = it;
        n.id = id = m_nextInteractionId++;
        m_interactions.push_back(n);
        nameInteractions(m_interactions);
        m_interactionParamsDirty = true;
    }
    updateMidiInputs();
    emit interactionsChanged();
    return id;
}

bool Engine::setInteraction(const Interaction &it)
{
    {
        Lock lk(&m_mutex);
        auto f = std::find_if(m_interactions.begin(), m_interactions.end(), [&](const Interaction &x) { return x.id == it.id; });
        if (f == m_interactions.end()) return false;
        // (its filters: each one's state is kept, made again when it becomes another kind of filter)
        const bool sourceChanged =
            f->source != it.source || f->layer != it.layer || f->param != it.param ||
            f->sourceLayer != it.sourceLayer || f->sourceParam != it.sourceParam || !sameXyInput(*f, it) ||
            ((!f->xyInputs.empty() || !it.xyInputs.empty()) && !sameInputBinding(*f, it)) || f->mode != it.mode ||
            ((it.source == InteractionSource::Pixels || it.source == InteractionSource::Hands) &&
             !sameInputBinding(*f, it));
        // An Action's test changed: tried again on the source's current value, from the answer it had (so that the
        // change itself can make it true, or false)
        const bool testChanged = f->conditions != it.conditions || f->conditionsAll != it.conditionsAll;
        *f = it;
        nameInteractions(m_interactions);
        m_interactionParamsDirty = true;
        if (sourceChanged) m_interactionRun.erase(it.id); // starts again from what comes next
        else if (testChanged)
            if (auto run = m_interactionRun.find(it.id); run != m_interactionRun.end())
                run->second.lastInternal = std::numeric_limits<double>::quiet_NaN();
    }
    updateMidiInputs();
    emit interactionsChanged();
    return true;
}

void Engine::removeInteraction(quint64 id)
{
    {
        Lock lk(&m_mutex);
        const auto n = m_interactions.size();
        m_interactions.erase(std::remove_if(m_interactions.begin(), m_interactions.end(), [id](const Interaction &x) { return x.id == id; }),
                             m_interactions.end());
        if (n == m_interactions.size()) return;
        m_interactionRun.erase(id);
        m_interactionParamsDirty = true;
    }
    updateMidiInputs();
    emit interactionsChanged();
}

bool Engine::interactionLevels(quint64 id, double *level, double *output, double *curveIn) const
{
    Lock lk(&m_mutex);
    const auto r = m_interactionRun.find(id);
    if (r == m_interactionRun.end() || !r->second.proc.level()) return false;
    if (level) *level = *r->second.proc.level();
    if (output) *output = r->second.proc.output().value_or(*r->second.proc.level());
    if (curveIn) *curveIn = r->second.proc.curveInput().value_or(*r->second.proc.level());
    return true;
}

bool Engine::sampleInteractionLevels(std::vector<InteractionLevel> *out) const
{
    if (!out || !m_mutex.tryLock()) return false;
    // Reuse the caller's capacity; copy only numbers, never interaction curves.
    out->clear();
    out->reserve(m_interactionRun.size());
    for (const auto &[id, run] : m_interactionRun) {
        const auto level = run.proc.level();
        if (!level && !run.xyY.level()) continue;
        const double x = level.value_or(0);
        out->push_back({id, x, run.proc.output().value_or(x), run.proc.curveInput().value_or(x)});
        if (run.actionMet) out->back().met = *run.actionMet ? 1 : 0;
        out->back().stages = run.proc.stageOutputs();
        out->back().yStages = run.xyY.stageOutputs();
        if (const auto y = run.xyY.level()) {
            auto &sample = out->back();
            sample.hasY = true;
            sample.yInput = *y;
            sample.yOutput = run.xyY.output().value_or(*y);
            sample.yCurveInput = run.xyY.curveInput().value_or(*y);
        }
    }
    m_mutex.unlock();
    std::sort(out->begin(), out->end(), [](const InteractionLevel &a, const InteractionLevel &b) { return a.id < b.id; });
    return true;
}

bool Engine::oscAddressClaimed(const QString &address) const
{
    Lock lk(&m_mutex);
    return std::any_of(m_interactions.cbegin(), m_interactions.cend(), [&](const Interaction &it) {
        const auto listens = [&](const Interaction &input) {
            return input.enabled && input.source == InteractionSource::Osc && !input.oscAddress.isEmpty() && input.oscAddress == address;
        };
        return it.enabled && (listens(it) || (!it.xyInputs.empty() && listens(it.xyInputs.front())));
    });
}

void Engine::feedInteraction(const InteractionEvent &e)
{
    if (e.source == InteractionSource::Keyboard || e.source == InteractionSource::Midi) {
        const int channel = e.source == InteractionSource::Keyboard ? EventLog::Keyboard : EventLog::MidiIn;
        if (EventLog::instance().wants(channel)) {
            const QString line = e.describe() + QStringLiteral(" = ") + QString::number(e.value, 'g', 5);
            EventLog::instance().append(channel, line);
            emit eventObserved(EventLog::channelName(channel), line);
        }
    }
    std::lock_guard<std::mutex> lk(m_interactionEventsMutex);
    if (m_interactionEvents.size() < 8192) m_interactionEvents.push_back(e); // a flood is cut, not kept forever
    else m_interactionEventsDropped.fetch_add(1, std::memory_order_relaxed);
    if (!m_interactionLearn || (m_learnKind && e.source != *m_learnKind)) return;
    if (e.source == InteractionSource::Keyboard && e.value < 0.5) return; // a key: when pressed
    for (InteractionEvent &x : m_learned)
        if (x.source == e.source && x.address == e.address && x.port == e.port && x.channel == e.channel &&
            x.message == e.message && x.number == e.number && x.argument == e.argument) {
            x.value = e.value;
            x.color = e.color;
            x.colorComponents = e.colorComponents; // update tuple after scalar arg 0
            return;
        }
    if (m_learned.empty()) m_learnClock.start();
    if (m_learned.size() < 32) m_learned.push_back(e);
}

void Engine::setInteractionLearn(bool on, std::optional<InteractionSource> kind)
{
    {
        std::lock_guard<std::mutex> lk(m_interactionEventsMutex);
        m_interactionLearn = on;
        m_learnKind = kind;
        m_learned.clear();
    }
    updateMidiInputs(); // a controller is listened to while learning
}

bool Engine::interactionLearning() const
{
    std::lock_guard<std::mutex> lk(const_cast<Engine *>(this)->m_interactionEventsMutex);
    return m_interactionLearn;
}

std::vector<InteractionEvent> Engine::takeLearnedInteractions(int settleMs)
{
    std::lock_guard<std::mutex> lk(m_interactionEventsMutex);
    if (m_learned.empty() || m_learnClock.elapsed() < settleMs) return {};
    std::vector<InteractionEvent> out;
    out.swap(m_learned);
    return out;
}

// ---------------------------------------------------------------------------
// MIDI: every input of the machine, open while a MIDI interaction is there (or while learning); looked for again every
// few seconds (a controller plugged in)
// ---------------------------------------------------------------------------
static bool parseMidi(const uint8_t *m, size_t n, InteractionEvent &e)
{
    if (n < 2) return false;
    const int status = m[0] & 0xF0;
    e.source = InteractionSource::Midi;
    e.channel = (m[0] & 0x0F) + 1;
    switch (status) {
    case 0xB0:
        if (n < 3) return false;
        e.message = MidiMessageKind::ControlChange;
        e.number = m[1] & 0x7F;
        e.value = (m[2] & 0x7F) / 127.0;
        return true;
    case 0x90:
    case 0x80:
        if (n < 3) return false;
        e.message = MidiMessageKind::Note;
        e.number = m[1] & 0x7F;
        e.value = status == 0x90 ? (m[2] & 0x7F) / 127.0 : 0.0; // note on at velocity 0: off
        return true;
    case 0xE0:
        if (n < 3) return false;
        e.message = MidiMessageKind::PitchBend;
        e.value = (((m[2] & 0x7F) << 7) | (m[1] & 0x7F)) / 16383.0;
        return true;
    case 0xC0:
        e.message = MidiMessageKind::ProgramChange;
        e.number = m[1] & 0x7F;
        e.value = 1.0; // a program chosen: up (then down at once, below)
        return true;
    default: return false;
    }
}

void Engine::requestMidiInputUpdate()
{
    if (!m_midiUpdatePending.exchange(true))
        QMetaObject::invokeMethod(this, "applyMidiInputUpdate", Qt::QueuedConnection);
}

void Engine::applyMidiInputUpdate()
{
    m_midiUpdatePending = false;
    updateMidiInputs();
}

void Engine::updateMidiInputs()
{
    // Presets can restore interactions on the cue-clock thread. Timers and the
    // MIDI port list are owned exclusively by the Engine's QObject thread.
    if (QThread::currentThread() != thread()) {
        requestMidiInputUpdate();
        return;
    }
    bool wanted;
    {
        Lock lk(&m_mutex);
        wanted = std::any_of(m_interactions.begin(), m_interactions.end(),
                             [](const Interaction &it) { return it.enabled && (it.source == InteractionSource::Midi ||
                                 (!it.xyInputs.empty() && it.xyInputs.front().source == InteractionSource::Midi)); });
    }
    {
        std::lock_guard<std::mutex> lk(m_interactionEventsMutex);
        wanted = wanted || m_interactionLearn;
    }
    if (!wanted) {
        m_midiIns.clear();
        m_midiInNames.clear();
        if (m_midiScan) m_midiScan->stop();
        return;
    }
    if (!m_midiScan) {
        m_midiScan = new QTimer(this);
        m_midiScan->setObjectName(QStringLiteral("midiInputScan"));
        m_midiScan->setInterval(2000);
        connect(m_midiScan, &QTimer::timeout, this, &Engine::updateMidiInputs);
    }
    if (!m_midiScan->isActive()) m_midiScan->start();
    const QStringList names = midi::inputNames();
    // m_midiInNames: every port tried, opened or not. A port that failed to open is not retried every scan (that
    // would close and reopen all the others, losing their messages), only when the list of devices changes
    if (names == m_midiInNames) return;
    m_midiIns.clear();
    m_midiInNames.clear();
    for (const QString &name : names) {
        auto in = std::make_unique<midi::Input>();
        QString err;
        const bool ok = in->open(name, [this, name](const uint8_t *m, size_t n) {
            InteractionEvent e;
            e.port = name;
            if (!parseMidi(m, n, e)) return;
            feedInteraction(e);
            if (e.message == MidiMessageKind::ProgramChange) { // an action: up, then down
                e.value = 0.0;
                feedInteraction(e);
            }
        }, &err);
        if (!ok) continue;
        m_midiIns.push_back(std::move(in));
        m_midiInNames << name;
    }
    m_midiInNames = names; // the ones that could not be opened are not tried again until the list changes
}

// ---------------------------------------------------------------------------
// The chains, each frame
// ---------------------------------------------------------------------------
// An Action's value: a number with a time goes there from where it is, a step each frame (stepActionFades); the rest,
// at once. Either way, what was moving it (a snapshot's fade, another action's) lets go.
void Engine::runConsequence(const InteractionConsequence &c)
{
    releaseFromFades(c.layer, c.param);
    Parameter *p = c.time > 0 ? findParameter(c.layer, c.param) : nullptr;
    const ParamInfo info = p ? p->info() : ParamInfo{};
    const bool locked = c.layer && std::any_of(m_layers.begin(), m_layers.end(), [&](const auto &l) { return l->id == c.layer && l->locked; });
    if (!p || locked || !info.isNumber() || !(info.access & ParamInfo::Write) || info.ramp == ParamInfo::Ramp::Cut) {
        setEventValue(c.layer, c.param, c.value, true);
        return;
    }
    const double from = p->asNumber();
    m_actionFades.push_back(ActionFade{c.layer, c.param, info.ramp, from, c.value, c.time, 0, from, c.curve});
}

void Engine::stepInteractions(double dt)
{
    std::vector<InteractionEvent> events;
    {
        std::lock_guard<std::mutex> lk(m_interactionEventsMutex);
        events.swap(m_interactionEvents);
    }
    if (m_interactions.empty()) return;
    // The program mix and the live input, each read only when an Audio interaction measures its bands
    auto needsSpectrum = [this](bool input) {
        const auto needs = [input](const Interaction &it) {
            return it.source == InteractionSource::Audio && it.audioInput == input && it.audioFeature == Interaction::AudioFeature::Band;
        };
        return std::any_of(m_interactions.begin(), m_interactions.end(), [&](const Interaction &it) {
            return it.enabled && (needs(it) || (!it.xyInputs.empty() && needs(it.xyInputs.front())));
        });
    };
    const AudioAnalysis::Spectrum spectra[2]={needsSpectrum(false)?m_audio->spectrum():AudioAnalysis::Spectrum{},
                                              needsSpectrum(true)?m_audioInput->spectrum():AudioAnalysis::Spectrum{}};
    const AudioAnalysis::Values audios[2]={m_audio->analysis(),m_audioInput->analysis()};
    // The flux of the last hops, read only when an interaction listens for hits (its own detector turns it into a trigger)
    auto needsFlux = [this](bool input) {
        const auto needs = [input](const Interaction &it) {
            return it.source == InteractionSource::Audio && it.audioInput == input && it.audioFeature == Interaction::AudioFeature::Hit;
        };
        return std::any_of(m_interactions.begin(), m_interactions.end(), [&](const Interaction &it) {
            return it.enabled && (needs(it) || (!it.xyInputs.empty() && needs(it.xyInputs.front())));
        });
    };
    const OnsetFlux fluxes[2] = {needsFlux(false) ? m_audio->onsetFlux() : OnsetFlux{}, needsFlux(true) ? m_audioInput->onsetFlux() : OnsetFlux{}};
    // A show commonly maps the same few audio regions to several layers.
    // Integrate each exact region once per frame; fixed storage avoids heap work.
    struct BandSample { bool input; float low, high, value; };
    std::array<BandSample, 32> bands{};
    size_t bandCount = 0;
    auto bandValue = [&](bool input, float low, float high) {
        for (size_t i = 0; i < bandCount; ++i)
            if (bands[i].input == input && bands[i].low == low && bands[i].high == high) return bands[i].value;
        const float value = spectra[input].band(low, high);
        if (bandCount < bands.size()) bands[bandCount++] = {input, low, high, value};
        return value;
    };
    std::vector<double> values; // reused from one Action to the next: no allocation each frame
    for (const Interaction &it : m_interactions) {
        if (it.enabled && it.isAction() && it.xyInputs.empty()) {
            InteractionRun &run = m_interactionRun[it.id];
            // Each value the source gives, in order (a key pressed then let go in one frame: both), in its own units
            values.clear();
            if (it.source == InteractionSource::Pixels || it.source == InteractionSource::Hands) {
                values.push_back(visionValue(it));
            } else if (it.source == InteractionSource::Audio) {
                const AudioAnalysis::Values &audio = audios[it.audioInput];
                values.push_back(it.audioFeature == Interaction::AudioFeature::Band
                                     ? bandValue(it.audioInput, float(it.audioLow), float(it.audioHigh))
                                     : it.audioFeature == Interaction::AudioFeature::Level ? audio.level
                                     : it.audioFeature == Interaction::AudioFeature::Hit ? run.proc.hit(fluxes[it.audioInput], it, dt)
                                                                                          : audio.onset);
            } else if (it.source == InteractionSource::Internal) {
                if (const Parameter *src = findParameter(it.sourceLayer, it.sourceParam)) {
                    const double x = src->asNumber();
                    if (std::isfinite(x) && !(x == run.lastInternal)) {
                        run.lastInternal = x;
                        values.push_back(x);
                    }
                }
            } else {
                for (const InteractionEvent &e : events)
                    if (it.matches(e)) values.push_back(e.value);
            }
            for (const double x : values) {
                if (!std::isfinite(x)) continue;
                run.proc.input(it, x); // shown (In)
                if (it.conditions.empty()) { // no test: each value the source gives does the "if true" actions
                    run.actionMet = true;
                    for (const InteractionConsequence &c : it.onTrue) runConsequence(c);
                    journal(QStringLiteral("interaction"), QStringLiteral("%1 true").arg(it.id));
                    continue;
                }
                const bool met = it.conditionsMet(x);
                const bool first = !run.actionMet;
                if (!first && *run.actionMet == met) continue; // only when the answer changes
                run.actionMet = met;
                if (first && !met) continue; // nothing was true before: nothing to undo
                for (const InteractionConsequence &c : met ? it.onTrue : it.onFalse) runConsequence(c);
                journal(QStringLiteral("interaction"), QStringLiteral("%1 %2").arg(it.id).arg(met ? QStringLiteral("true") : QStringLiteral("false")));
            }
            continue;
        }
        if (!it.enabled || (it.isAction() && it.xyInputs.empty()) || (it.param.isEmpty() && !it.xyInputs.empty())) continue;
        InteractionRun &run = m_interactionRun[it.id];
        if (!it.xyInputs.empty()) {
            std::array<Parameter *, 2> params{};
            bool valid = true;
            for (int axis = 0; axis < 2; ++axis) {
                auto *p = findParameter(it.layer, it.param + (axis ? "/y" : "/x"));
                params[size_t(axis)] = p;
                valid &= p && p->info().pointComponent == axis && p->isNumber() && (p->info().access & ParamInfo::Write);
            }
            if (!valid) continue; // no partial write to a replaced source or FX
            auto evaluate = [&](const Interaction &input, InteractionProcessor &proc, InteractionTargetState &targetState,
                                std::optional<double> &lastOut, double &lastInternal, Parameter *targetParam) -> std::optional<double> {
                if (!input.enabled) return std::nullopt;
                bool freshInput = false;
                if (input.source == InteractionSource::Pixels || input.source == InteractionSource::Hands) {
                    proc.input(input, visionValue(input));
                    freshInput = true;
                } else if (input.source == InteractionSource::Audio) {
                    const auto &audio = audios[input.audioInput];
                    const double raw = input.audioFeature == Interaction::AudioFeature::Band
                        ? bandValue(input.audioInput, float(input.audioLow), float(input.audioHigh))
                        : input.audioFeature == Interaction::AudioFeature::Level ? audio.level
                        : input.audioFeature == Interaction::AudioFeature::Hit ? proc.hit(fluxes[input.audioInput], input, dt)
                                                                              : audio.onset;
                    proc.input(input, raw);
                    freshInput = true;
                } else if (input.source == InteractionSource::Internal) {
                    if (const auto *source = findParameter(input.sourceLayer, input.sourceParam)) {
                        const double raw = source->asNumber();
                        if (std::isfinite(raw) && raw != lastInternal) {
                            lastInternal = raw;
                            proc.input(input, raw);
                            freshInput = true;
                        }
                    }
                } else
                    for (const auto &event : events)
                        if (input.matches(event) && event.colorComponents == 0) {
                            proc.input(input, event.value); // preserve raw XY units; never apply RGB byte conversion
                            freshInput = true;
                        }
                const auto output = proc.step(input, dt);
                if (!output) return std::nullopt;
                const bool changed = freshInput || !lastOut || std::abs(*lastOut - *output) > 1e-9;
                lastOut = output;
                if (!changed) return std::nullopt;
                const auto info = targetParam->info();
                return targetState.numberToWrite(input, *output, targetParam->number(),
                                                  std::max(info.step, 0.01 * std::abs(info.max - info.min)));
            };
            const auto x = evaluate(it, run.proc, run.target, run.lastOut, run.lastInternal, params[0]);
            const auto y = evaluate(it.xyInputs.front(), run.xyY, run.xyYTarget, run.xyYLastOut, run.xyYLastInternal, params[1]);
            if (it.layer) {
                const int index = indexOfId(it.layer);
                if (index < 0 || m_layers[size_t(index)]->locked) continue;
            }
            for (int axis = 0; axis < 2; ++axis) {
                const auto &value = axis ? y : x;
                if (value && std::isfinite(*value) && params[size_t(axis)]->setNumber(*value) && it.layer)
                    releaseFromFades(it.layer, params[size_t(axis)]->path());
            }
            continue;
        }
        bool fresh = false;
        // Stateless trigger mappings retain every edge in a batch. Continuous
        // values and filters still advance once per frame with the full dt.
        Parameter *const target = findParameter(it.layer, it.param);
        const bool eventTrigger = !it.movesOverTime() && target && target->info().type == ParamInfo::Type::Trigger;
        // A Toggle counts each press, even several in a frame: the chain takes each value as it comes
        const bool eachValue = eventTrigger || std::any_of(it.filters.begin(), it.filters.end(), [](const InteractionStage &f) {
            return f.enabled && f.kind == InteractionFilter::Toggle;
        });
        // A whole color: each channel through the chain, but a Toggle (a channel is not pressed)
        auto colorChain = [](const Interaction &from) {
            Interaction chain = from;
            chain.filters.erase(std::remove_if(chain.filters.begin(), chain.filters.end(),
                                               [](const InteractionStage &f) { return f.kind == InteractionFilter::Toggle; }),
                                chain.filters.end());
            return chain;
        };
        unsigned rises = 0;
        if (it.source == InteractionSource::Pixels || it.source == InteractionSource::Hands) {
            run.proc.input(it, visionValue(it));
            fresh = true;
        } else if (it.source == InteractionSource::Audio) {
            const AudioAnalysis::Values &audio=audios[it.audioInput];
            const double x=it.audioFeature==Interaction::AudioFeature::Band?bandValue(it.audioInput,float(it.audioLow),float(it.audioHigh)):
                it.audioFeature==Interaction::AudioFeature::Level?audio.level:
                it.audioFeature==Interaction::AudioFeature::Hit?run.proc.hit(fluxes[it.audioInput],it,dt):audio.onset;
            run.proc.input(it,x);
            fresh=true;
        } else if (it.source == InteractionSource::Internal) {
            if (const Parameter *src = findParameter(it.sourceLayer, it.sourceParam)) {
                const double x = src->asNumber();
                if (std::isfinite(x) && !(x == run.lastInternal)) {
                    run.lastInternal = x;
                    run.proc.input(it, x);
                    fresh = true;
                }
            }
        } else {
            for (const InteractionEvent &e : events)
                if (it.matches(e)) {
                    run.proc.input(it, e.value);
                    if (eachValue) {
                        const auto value = run.proc.step(it, 0);
                        if (eventTrigger) {
                            const bool above = value && *value > 0.5;
                            if (above && !run.target.above) ++rises;
                            run.target.above = above;
                        }
                    }
                    run.color = e.color;
                    run.colorComponents = e.colorComponents;
                    if (e.colorComponents >= 3) { // a whole color: each channel through its own chain (no toggle)
                        const Interaction chain = colorChain(it);
                        for (int c = 0; c < 4; ++c) run.channels[size_t(c)].input(chain, e.color[size_t(c)]);
                    }
                    fresh = true;
                }
        }
        const std::optional<double> out = run.proc.step(it, dt);
        if (!out) continue;
        // A whole color: its channels move on too (a filter keeps moving them after the last message); the alpha
        // without the master level
        bool colorMoved = false;
        if (run.colorComponents >= 3) {
            Interaction chain = colorChain(it);
            for (int c = 0; c < 4; ++c) {
                if (c == 3) chain.master = 1;
                const std::optional<double> o = run.channels[size_t(c)].step(chain, dt);
                if (o && (!run.channelOut[size_t(c)] || std::abs(*run.channelOut[size_t(c)] - *o) > 1e-9)) colorMoved = true;
                run.channelOut[size_t(c)] = o;
            }
        }
        const bool changed = fresh || colorMoved || !run.lastOut || std::abs(*run.lastOut - *out) > 1e-9;
        run.lastOut = out;
        if (!changed) continue; // nothing new: the target is left to whatever else moves it
        if (it.param.isEmpty()) continue; // no target yet: its source and its filters still shown moving
        if (it.layer) {
            const int li = indexOfId(it.layer);
            if (li < 0 || m_layers[size_t(li)]->locked) continue; // a locked layer is left alone
        }
        Parameter *p = target; // nothing in between adds or removes a parameter
        if (!p) {
            // An ISF color has four scalar Parameter entries but OSC and the
            // ColorPicker expose the natural group address (param/color).
            // Resolve that group here without changing scalar parameter storage
            // or disrupting existing animations, snapshots and presets.
            const QString prefix = it.param + QLatin1Char('/');
            std::array<Parameter *, 4> channels{};
            for (int c = 0; c < 4; ++c)
                channels[size_t(c)] = findParameter(it.layer, prefix + QChar("rgba"[c]));
            bool valid = true;
            for (int c = 0; c < 3; ++c)
                valid &= channels[size_t(c)] && (channels[size_t(c)]->info().access & ParamInfo::Write) &&
                         channels[size_t(c)]->info().isNumber();
            if (!valid) continue;
            // A whole color (OSC 'r', three / four numbers): each channel through its own chain, then the target
            // range, with a pick-up on the whole color. Alpha only with Map alpha and a fourth channel (some senders
            // fill an unused alpha with zero). A single number: a gray, through the usual chain.
            const bool whole = run.colorComponents >= 3;
            const int n = whole && it.colorMapAlpha && run.colorComponents >= 4 && channels[3] &&
                                  (channels[3]->info().access & ParamInfo::Write)
                              ? 4
                              : 3;
            std::array<double, 4> want{{0, 0, 0, 1}}, current{{0, 0, 0, 1}};
            bool ready = true;
            for (int c = 0; c < n; ++c) {
                const std::optional<double> o = whole ? run.channelOut[size_t(c)] : out;
                if (!o) ready = false;
                want[size_t(c)] = std::clamp(o.value_or(0), 0.0, 1.0);
                current[size_t(c)] = channels[size_t(c)]->number();
            }
            if (!ready) continue;
            const double tolerance = 0.004;
            const std::optional<std::array<double, 4>> w = run.colorTarget.toWrite(it, want, current, n, tolerance);
            if (!w) continue;
            for (int c = 0; c < n; ++c) {
                Parameter *ch = channels[size_t(c)];
                if (std::abs(ch->number() - (*w)[size_t(c)]) > 1e-6 && ch->setNumber((*w)[size_t(c)]) && it.layer)
                    releaseFromFades(it.layer, prefix + QChar("rgba"[c]));
            }
            continue;
        }
        if (!(p->info().access & ParamInfo::Write)) continue;
        const ParamInfo info = p->info();
        bool wrote = false;
        switch (info.type) {
        case ParamInfo::Type::Float:
        case ParamInfo::Type::Int: {
            const double tolerance = std::max(info.step, 0.01 * std::abs(info.max - info.min));
            if (const std::optional<double> v = run.target.numberToWrite(it, *out, p->number(), tolerance))
                wrote = p->setNumber(*v);
            break;
        }
        case ParamInfo::Type::Bool: {
            const bool on = *out >= 0.5;
            if (on != p->value().toBool()) wrote = p->setValue(on);
            break;
        }
        case ParamInfo::Type::Choice: {
            if (info.choices.isEmpty()) break;
            const int n = int(info.choices.size());
            const int k = std::clamp(int(std::lround(*out)), 0, n - 1); // the one of that index
            if (!run.choiceSet || k != run.lastChoice) {
                run.choiceSet = true;
                run.lastChoice = k;
                wrote = p->setValue(info.choices[k]);
            }
            break;
        }
        case ParamInfo::Type::Trigger: {
            const bool above = *out > 0.5;
            if (above && !run.target.above) ++rises;
            run.target.above = above;
            for (unsigned edge = 0; edge < rises; ++edge) {
                // An action may reload its source: resolve its address again for
                // each edge rather than retaining a parameter across actions.
                if (Parameter *action = findParameter(it.layer, it.param)) wrote |= action->setValue(QVariant());
            }
            break;
        }
        case ParamInfo::Type::Text: break;
        }
        if (wrote && it.layer) releaseFromFades(it.layer, it.param); // a fade running on it lets go
    }
}

// ---------------------------------------------------------------------------
// Their values as the composition's parameters, and in the snapshots
// ---------------------------------------------------------------------------
void Engine::interactionValuesTouched()
{
    if (m_interactionValuesPending.exchange(true)) return; // one is on its way
    QMetaObject::invokeMethod(this, "notifyInteractionValuesChanged", Qt::QueuedConnection);
}

void Engine::notifyInteractionValuesChanged()
{
    m_interactionValuesPending = false;
    emit interactionValuesChanged();
}

void Engine::makeInteractionParams()
{
    m_interactionParams.clear();
    using T = Parameter::Type;
    for (const Interaction &root : m_interactions) for (int axis = 0; axis < (root.xyInputs.empty() ? 1 : 2); ++axis) {
        const Interaction &it = axis ? root.xyInputs.front() : root;
        const quint64 id = root.id;
        const QString prefix = QStringLiteral("interaction/%1/%2").arg(id).arg(axis ? QStringLiteral("xy/y/") : QString());
        const QString cat = QStringLiteral("Interactions › %1 › %2").arg(root.name).arg(axis ? QStringLiteral("Y › ") : QString());
        // Read and written in the interaction of that id (the lock held by the caller); a write tells the interface
        auto find = [this, id, axis]() -> Interaction * {
            for (Interaction &x : m_interactions)
                if (x.id == id) return axis ? (x.xyInputs.empty() ? nullptr : &x.xyInputs.front()) : &x;
            return nullptr;
        };
        auto add = [&](const QString &path, const QString &name, T type) -> Parameter & {
            m_interactionParams.push_back(std::make_unique<Parameter>(prefix + path, cat + name, type));
            return *m_interactionParams.back();
        };
        auto number = [&](const QString &path, const QString &name, double lo, double hi, double def,
                          std::function<double &(Interaction &)> field) -> Parameter & {
            return add(path, name, T::Float)
                .range(lo, hi)
                .limits(-1e9, 1e9)
                .step((hi - lo) / 100.0)
                .byDefault(def)
                .bind([find, field] {
                          Interaction *x = find();
                          return x ? QVariant(field(*x)) : QVariant();
                      },
                      [this, find, field](const QVariant &v) {
                          if (Interaction *x = find()) {
                              field(*x) = v.toDouble();
                              interactionValuesTouched();
                          }
                      });
        };
        auto flag = [&](const QString &path, const QString &name, bool def, std::function<bool &(Interaction &)> field) {
            add(path, name, T::Bool).byDefault(def).ramp(Parameter::Ramp::Cut).bind(
                [find, field] {
                    Interaction *x = find();
                    return x ? QVariant(field(*x)) : QVariant();
                },
                [this, find, field](const QVariant &v) {
                    if (Interaction *x = find()) {
                        field(*x) = v.toBool();
                        interactionValuesTouched();
                    }
                });
        };
        // The source's values and the target's, as the interface shows them. The composition's own values are read
        // in its own list (the whole one is being made)
        auto lookup = [this](quint64 layer, const QString &path) -> const Parameter * {
            if (layer) return findParameter(layer, path);
            for (const auto &p : m_compParams)
                if (p->path() == path) return p.get();
            return nullptr;
        };
        // (a Remap's: its In shown in the source's, its Out in the target's)
        double sLo = 0, sHi = 1, tLo = 0, tHi = 1;
        if (it.source == InteractionSource::Internal) {
            if (const Parameter *p = lookup(it.sourceLayer, it.sourceParam); p && p->isNumber()) {
                sLo = p->info().min;
                sHi = p->info().max;
            }
        }
        if (const Parameter *p = lookup(root.layer, root.xyInputs.empty() ? root.param : root.param + (axis ? "/y" : "/x")); p && p->isNumber()) {
                tLo = p->info().min;
                tHi = p->info().max;
            }

        flag(QStringLiteral("enable"), QStringLiteral("On"), true, [](Interaction &x) -> bool & { return x.enabled; });
        number(QStringLiteral("master"), QStringLiteral("Master Level"), 0, 1, 1, [](Interaction &x) -> double & { return x.master; })
            .limits(0, 1)
            .animatable();
        flag(QStringLiteral("color/map_alpha"), QStringLiteral("Map Color Alpha"), false,
             [](Interaction &x) -> bool & { return x.colorMapAlpha; });
        add(QStringLiteral("takeover"), QStringLiteral("Takeover"), T::Choice)
            .choices({QStringLiteral("pickup"), QStringLiteral("jump")})
            .byDefault(QStringLiteral("jump"))
            .bind([find] {
                      Interaction *x = find();
                      return x ? QVariant(x->takeover == Takeover::PickUp ? QStringLiteral("pickup") : QStringLiteral("jump")) : QVariant();
                  },
                  [this, find](const QVariant &v) {
                      if (Interaction *x = find()) {
                          x->takeover = v.toString() == QLatin1String("pickup") ? Takeover::PickUp : Takeover::Jump;
                          interactionValuesTouched();
                      }
                  });
        // Each filter in its order: on / off and its settings (a preset, a snapshot, OSC, a timeline reach them); what
        // the filters are, all together, below ("filters")
        for (size_t k = 0; k < it.filters.size(); ++k) {
            const InteractionStage &f = it.filters[k];
            const QString path = QStringLiteral("filters/%1/").arg(k + 1);
            const QString name = QStringLiteral("Filter %1 %2 › ").arg(k + 1).arg(interactionFilterName(f.kind));
            flag(path + QStringLiteral("enable"), name + QStringLiteral("On"), true, [k](Interaction &x) -> bool & {
                static bool gone = false; // the filters changed meanwhile (the list is made again)
                return k < x.filters.size() ? x.filters[k].enabled : gone;
            });
            for (int which = 0; which < kFilterSettingCount; ++which) {
                const FilterSetting set = interactionFilterSetting(f.kind, which);
                if (set.name.isEmpty()) continue;
                const bool remap = f.kind == InteractionFilter::Remap;
                const double lo = remap ? (which < 2 ? sLo : tLo) : set.min, hi = remap ? (which < 2 ? sHi : tHi) : set.max;
                Parameter &p = number(path + QChar('a' + which), name + set.name, lo, hi, set.def, [k, which](Interaction &x) -> double & {
                    static double gone = 0;
                    return k < x.filters.size() ? x.filters[k].setting(which) : gone;
                });
                if (remap && which >= 2) p.animatable(); // its Out: where the target goes (a timeline can move it)
            }
        }

        // What it is: its name, its source, its target, its curve — set at once (presets and snapshots hold them; a
        // snapshot holds them all, so that one removed since is made again as it was)
        auto changed = [this] { // its name, its source or its target: the list, the MIDI inputs, the labels
            m_interactionParamsDirty = true;
            interactionValuesTouched();
            requestMidiInputUpdate();
        };
        auto text = [&](const QString &path, const QString &name, std::function<QString(const Interaction &)> get,
                        std::function<void(Interaction &, const QString &)> set) {
            add(path, name, T::Text).ramp(Parameter::Ramp::Cut).bind(
                [find, get] {
                    Interaction *x = find();
                    return x ? QVariant(get(*x)) : QVariant();
                },
                [this, find, set, changed](const QVariant &v) {
                    if (Interaction *x = find()) {
                        set(*x, v.toString());
                        nameInteractions(m_interactions);
                        changed();
                    }
                });
        };
        auto whole = [&](const QString &path, const QString &name, int lo, int hi, int def, std::function<int &(Interaction &)> field) {
            add(path, name, T::Int).range(lo, hi).limits(lo, hi).step(1).byDefault(def).ramp(Parameter::Ramp::Cut).bind(
                [find, field] {
                    Interaction *x = find();
                    return x ? QVariant(field(*x)) : QVariant();
                },
                [find, field, changed](const QVariant &v) {
                    if (Interaction *x = find()) {
                        field(*x) = v.toInt();
                        changed();
                    }
                });
        };
        auto choice = [&](const QString &path, const QString &name, const QStringList &choiceKeys,
                          std::function<int(const Interaction &)> get, std::function<void(Interaction &, int)> set) {
            QStringList keys;
            for (const auto &key : choiceKeys)
                keys << key.toLower();
            add(path, name, T::Choice).choices(keys).byDefault(keys.front()).ramp(Parameter::Ramp::Cut).bind(
                [find, get, keys] {
                    Interaction *x = find();
                    return x ? QVariant(keys.value(get(*x))) : QVariant();
                },
                [find, set, keys, changed](const QVariant &v) {
                    const int k = int(keys.indexOf(v.toString()));
                    if (Interaction *x = find(); x && k >= 0) {
                        set(*x, k);
                        changed();
                    }
                });
        };
        // A value of the show, as text: "<layer id>:<address>" (0: the composition)
        auto valueRef = [](quint64 layer, const QString &path) { return path.isEmpty() ? QString() : QStringLiteral("%1:%2").arg(layer).arg(path); };
        auto parseRef = [](const QString &s, quint64 *layer, QString *path) {
            const int colon = s.indexOf(QLatin1Char(':'));
            *layer = colon < 0 ? 0 : s.left(colon).toULongLong();
            *path = colon < 0 ? QString() : s.mid(colon + 1);
        };
        text(QStringLiteral("name"), QStringLiteral("Name"), [](const Interaction &x) { return x.name; },
             [](Interaction &x, const QString &v) {
                 if (!v.trimmed().isEmpty()) x.name = v.trimmed();
             });
        choice(
            QStringLiteral("source/kind"), QStringLiteral("Source"),
            {QStringLiteral("keyboard"), QStringLiteral("midi"), QStringLiteral("osc"), QStringLiteral("internal"),
             QStringLiteral("audio"), QStringLiteral("pixels"), QStringLiteral("hands")},
            [](const Interaction &x) { return int(x.source); },
            [](Interaction &x, int k) { x.source = InteractionSource(k); });
        text(QStringLiteral("source/key"), QStringLiteral("Key"), [](const Interaction &x) { return x.key; },
             [](Interaction &x, const QString &v) { x.key = v; });
        text(QStringLiteral("source/midi/input"), QStringLiteral("MIDI Input"), [](const Interaction &x) { return x.midiPort; },
             [](Interaction &x, const QString &v) { x.midiPort = v; });
        whole(QStringLiteral("source/midi/channel"), QStringLiteral("MIDI Channel"), 0, 16, 0, [](Interaction &x) -> int & { return x.midiChannel; });
        choice(QStringLiteral("source/midi/message"), QStringLiteral("MIDI Message"),
               {QStringLiteral("cc"), QStringLiteral("note"), QStringLiteral("pitch_bend"), QStringLiteral("program")},
               [](const Interaction &x) { return int(x.midiMessage); },
               [](Interaction &x, int k) { x.midiMessage = MidiMessageKind(k); });
        whole(QStringLiteral("source/midi/number"), QStringLiteral("MIDI Number"), 0, 127, 0, [](Interaction &x) -> int & { return x.midiNumber; });
        text(QStringLiteral("source/osc/address"), QStringLiteral("OSC Address"), [](const Interaction &x) { return x.oscAddress; },
             [](Interaction &x, const QString &v) { x.oscAddress = v; });
        whole(QStringLiteral("source/osc/argument"), QStringLiteral("OSC Argument"), 0, 63, 0, [](Interaction &x) -> int & { return x.oscArgument; });
        text(QStringLiteral("source/internal"), QStringLiteral("Follows"),
             [valueRef](const Interaction &x) { return valueRef(x.sourceLayer, x.sourceParam); },
             [parseRef](Interaction &x, const QString &v) { parseRef(v, &x.sourceLayer, &x.sourceParam); });
        number(QStringLiteral("source/audio/low"),QStringLiteral("Audio Low Hz"),20,20000,20,[](Interaction &x)->double &{return x.audioLow;}).limits(20,20000);
        number(QStringLiteral("source/audio/high"),QStringLiteral("Audio High Hz"),20,20000,20000,[](Interaction &x)->double &{return x.audioHigh;}).limits(20,20000);
        number(QStringLiteral("source/audio/sensitivity"),QStringLiteral("Audio Hit Sensitivity"),0,1,0.5,[](Interaction &x)->double &{return x.audioSensitivity;}).limits(0,1);
        number(QStringLiteral("source/audio/gap"),QStringLiteral("Audio Hit Minimum Gap"),0.04,2,0.08,[](Interaction &x)->double &{return x.audioGap;}).limits(0.04,2);
        choice(QStringLiteral("source/audio/feature"),QStringLiteral("Audio Measure"),{QStringLiteral("band"),QStringLiteral("level"),QStringLiteral("onset"),QStringLiteral("hit")},
            [](const Interaction &x){return int(x.audioFeature);},[](Interaction &x,int k){x.audioFeature=Interaction::AudioFeature(k);});
        choice(QStringLiteral("source/audio/input"),QStringLiteral("Audio Source"),{QStringLiteral("program"),QStringLiteral("input")},
            [](const Interaction &x){return x.audioInput?1:0;},[](Interaction &x,int k){x.audioInput=k==1;});
        text(
            QStringLiteral("source/vision/layer"), QStringLiteral("Image layer"),
            [](const Interaction &x) { return QString::number(x.pixels.layer); },
            [](Interaction &x, const QString &v) { x.pixels.layer = v.toULongLong(); });
        choice(
            QStringLiteral("source/vision/tap"), QStringLiteral("Image tap"), {"source", "prefx", "postfx"},
            [](const Interaction &x) { return x.pixels.tap; }, [](Interaction &x, int n) { x.pixels.tap = n; });
        choice(
            QStringLiteral("source/pixels/channel"), QStringLiteral("Pixel channel"), PixelAnalysis::channelNames(),
            [](const Interaction &x) { return int(x.pixels.channel); },
            [](Interaction &x, int n) { x.pixels.channel = PixelAnalysis::Channel(n); });
        choice(
            QStringLiteral("source/pixels/feature"), QStringLiteral("Pixel measure"), PixelAnalysis::featureNames(),
            [](const Interaction &x) { return int(x.pixelFeature); },
            [](Interaction &x, int n) { x.pixelFeature = PixelAnalysis::Feature(n); });
        number(QStringLiteral("source/pixels/threshold"), QStringLiteral("Pixel active threshold"), 0, 1, .05,
               [](Interaction &x) -> double & { return x.pixels.threshold; })
            .limits(0, 1);
        for (int component = 0; component < 4; ++component) {
            const char *keys[] = {"x", "y", "width", "height"};
            add(QStringLiteral("source/pixels/roi/") + keys[component], QStringLiteral("Pixel ROI ") + keys[component],
                T::Float)
                .range(0, 1)
                .limits(0, 1)
                .byDefault(component < 2 ? 0. : 1.)
                .bind(
                    [find, component] {
                        auto *x = find();
                        if (!x)
                            return QVariant();
                        const auto r = x->pixels.roi;
                        return QVariant(component == 0   ? r.x()
                                        : component == 1 ? r.y()
                                        : component == 2 ? r.width()
                                                         : r.height());
                    },
                    [find, changed, component](const QVariant &value) {
                        if (auto *x = find()) {
                            auto r = x->pixels.roi;
                            const double v = value.toDouble();
                            x->pixels.roi = QRectF(component == 0 ? v : r.x(), component == 1 ? v : r.y(),
                                                   component == 2 ? v : r.width(), component == 3 ? v : r.height());
                            changed();
                        }
                    });
        }
        choice(
            QStringLiteral("source/hands/feature"), QStringLiteral("Hand measure"), HandTracking::featureNames(),
            [](const Interaction &x) { return int(x.handFeature); },
            [](Interaction &x, int n) { x.handFeature = HandTracking::Feature(n); });
        whole(QStringLiteral("source/hands/index"), QStringLiteral("Tracked hand"), 0, 1, 0,
              [](Interaction &x) -> int & { return x.handIndex; });
        number(QStringLiteral("source/hands/confidence"), QStringLiteral("Hand confidence"), .5, .99, .7,
               [](Interaction &x) -> double & { return x.handConfidence; })
            .limits(.5, .99);
        text(QStringLiteral("target"), QStringLiteral("Target"), [valueRef](const Interaction &x) { return valueRef(x.layer, x.param); },
             [this, parseRef, axis](Interaction &x, const QString &v) {
                 if (axis) return; // the Y chain shares the root target
                 quint64 layer = 0;
                 QString path;
                 parseRef(v, &layer, &path);
                 configureInteractionTarget(x, layer, path);
             });
        // Its filters, all of them: their kinds, order, settings and curves, set at once (their settings then fade)
        text(QStringLiteral("filters"), QStringLiteral("Filters"),
             [](const Interaction &x) { return QString::fromUtf8(QJsonDocument(Interaction::filtersToJson(x.filters)).toJson(QJsonDocument::Compact)); },
             [](Interaction &x, const QString &v) {
                 const QJsonDocument d = QJsonDocument::fromJson(v.toUtf8());
                 if (d.isArray()) x.filters = Interaction::filtersFromJson(QJsonObject{{"filters", d.array()}});
             });
    }
}
