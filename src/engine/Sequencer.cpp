// Sequences: ordered steps recalling snapshots, played with GO / GO BACK (the cue list of a show).
#include "Sequencer.h"
#include "EngineInternal.h"

#include <iterator>
#include <limits>

#include <QJsonArray>
#include <QThread>
#include <cmath>
#include <QTimer>

// A named slot avoids publishing a newly allocated functor through an
// uninstrumented Qt event queue on every GO. The callback is constructed before
// QThread::start, whose thread creation establishes its publication ordering.
class CueClockContext : public QObject
{
    Q_OBJECT
public:
    explicit CueClockContext(std::function<void()> wake) : m_wake(std::move(wake)) {}
public slots:
    void wake() { m_wake(); }
private:
    std::function<void()> m_wake;
};

Sequencer::Sequencer(Engine &engine, Annotated<QRecursiveMutex> &mutex) : QObject(&engine), m_engine(engine), m_mutex(mutex)
{
    connect(this, &Sequencer::changed, this, [this] { ++m_revision; }, Qt::DirectConnection);
}

Sequencer::~Sequencer() { shutdownClock(); }

void Sequencer::shutdownClock()
{
    if (!m_clockThread) return;
    m_clockThread->quit();
    m_clockThread->wait();
    tsanAcquire(this); // QThread::wait uses Qt futexes, invisible to TSan.
    m_clockContext = nullptr;
    m_timer = nullptr;
    m_clockThread.reset();
    m_ticking = false;
}

void Sequencer::setRecaller(std::function<void(int snapshotIndex)> f)
{
    QMutexLocker lk(&m_mutex);
    m_recaller = std::move(f);
}

void Sequencer::setPresetRecaller(std::function<void(int)> f)
{
    QMutexLocker lk(&m_mutex);
    m_presetRecaller = std::move(f);
}

void Sequencer::setRecallRecorder(std::function<void(int, const RecallBefore &)> f)
{
    QMutexLocker lk(&m_mutex);
    m_recorder = std::move(f);
}

double Sequencer::CueTiming::percentile(double p) const
{
    if (!fired) return 0;
    static constexpr double edges[] = {0.0005, 0.001, 0.002, 0.004, 0.008, 0.016, 0.032, 0.064, 0.128, 0.256, 0.512};
    const double want = std::ceil(p * double(fired));
    quint64 seen = 0;
    for (size_t k = 0; k < buckets.size(); ++k) {
        seen += buckets[k];
        if (double(seen) >= want) return k < std::size(edges) ? std::min(edges[k], max) : max;
    }
    return max;
}

void Sequencer::noteFired(double late)
{
    late = std::max(0.0, late);
    static constexpr double edges[] = {0.0005, 0.001, 0.002, 0.004, 0.008, 0.016, 0.032, 0.064, 0.128, 0.256, 0.512};
    size_t k = 0;
    while (k < std::size(edges) && late > edges[k]) ++k;
    ++m_timing.buckets[k];
    ++m_timing.fired;
    m_timing.last = late;
    m_timing.total += late;
    m_timing.max = std::max(m_timing.max, late);
}

Sequencer::CueTiming Sequencer::cueTiming() const
{
    QMutexLocker lk(&m_mutex);
    return m_timing;
}

Sequencer::CueWorkTiming Sequencer::cueWorkTiming() const
{
    QMutexLocker lk(&m_mutex);
    return m_workTiming;
}

void Sequencer::resetCueTiming()
{
    QMutexLocker lk(&m_mutex);
    m_timing = CueTiming();
    m_workTiming = CueWorkTiming();
}

bool Sequencer::clockRunning() const { return m_ticking.load(); }

int Sequencer::count() const
{
    QMutexLocker lk(&m_mutex);
    return int(m_sequences.size());
}

Sequencer::Sequence Sequencer::sequence(int i) const
{
    QMutexLocker lk(&m_mutex);
    return i >= 0 && i < int(m_sequences.size()) ? m_sequences[size_t(i)] : Sequence();
}

quint64 Sequencer::sequenceId(int index) const
{
    QMutexLocker lk(&m_mutex);
    return index >= 0 && index < int(m_sequenceIds.size()) ? m_sequenceIds[size_t(index)] : 0;
}

int Sequencer::indexOfSequence(quint64 id) const
{
    QMutexLocker lk(&m_mutex);
    return sequenceIndexLocked(id);
}

void Sequencer::setSequence(int i, const Sequence &s)
{
    {
        QMutexLocker lk(&m_mutex);
        if (i < 0 || i >= int(m_sequences.size())) return;
        const auto &old = m_sequences[size_t(i)].steps;
        const int position = m_positions[size_t(i)];
        const quint64 played = position >= 0 && position < int(old.size()) ? old[size_t(position)].runtimeId : 0;
        // The startup step follows its step when steps are inserted, removed or moved before it
        const quint64 startup = i == m_startupSequence && m_startupStep < int(old.size()) ? old[size_t(m_startupStep)].runtimeId : 0;
        m_sequences[size_t(i)] = s;
        auto &steps = m_sequences[size_t(i)].steps;
        QSet<quint64> seen;
        for (size_t other = 0; other < m_sequences.size(); ++other) if (int(other) != i)
            for (const auto &step : m_sequences[other].steps) seen.insert(step.runtimeId);
        for (Step &step : steps) {
            if (!step.runtimeId || seen.contains(step.runtimeId)) step.runtimeId = m_nextStepId++;
            seen.insert(step.runtimeId);
        }
        auto indexOf = [&](quint64 id) {
            for (size_t k = 0; k < steps.size(); ++k) if (steps[k].runtimeId == id) return int(k);
            return -1;
        };
        if (played) m_positions[size_t(i)] = indexOf(played);
        if (startup) m_startupStep = std::max(0, indexOf(startup)); // its step deleted: the first
        if (m_positions[size_t(i)] >= int(s.steps.size())) m_positions[size_t(i)] = int(s.steps.size()) - 1;
        const quint64 owner = m_sequenceIds[size_t(i)];
        m_runs.erase(std::remove_if(m_runs.begin(), m_runs.end(), [&](const Run &r) {
            return r.sequenceId == owner && indexOf(r.target.runtimeId) < 0;
        }), m_runs.end());
        for (Run &r : m_runs) if (r.sequenceId == owner) r.step = indexOf(r.target.runtimeId);
    }
    emit changed();
}

int Sequencer::add(const Sequence &s, int at)
{
    {
        QMutexLocker lk(&m_mutex);
        if (at < 0 || at > int(m_sequences.size())) at = int(m_sequences.size());
        m_sequences.insert(m_sequences.begin() + at, s);
        for (Step &step : m_sequences[size_t(at)].steps) step.runtimeId = m_nextStepId++;
        m_sequenceIds.insert(m_sequenceIds.begin() + at, m_nextSequenceId++);
        m_positions.insert(m_positions.begin() + at, -1);
        if (m_startupSequence >= at) ++m_startupSequence;
        if (m_current >= at) ++m_current;
        if (m_current < 0) m_current = at;
    }
    emit changed();
    emit positionChanged();
    return at;
}

void Sequencer::remove(int i)
{
    {
        QMutexLocker lk(&m_mutex);
        if (i < 0 || i >= int(m_sequences.size())) return;
        const quint64 owner = m_sequenceIds[size_t(i)];
        if (auto it = m_commandTokens.find(owner); it != m_commandTokens.end()) {
            *it->second = false;
            m_commandTokens.erase(it);
        }
        m_stepStartedNs.erase(owner);
        m_runs.erase(std::remove_if(m_runs.begin(), m_runs.end(),
            [owner](const Run &r) { return r.sequenceId == owner; }), m_runs.end());
        m_sequences.erase(m_sequences.begin() + i);
        m_positions.erase(m_positions.begin() + i);
        m_sequenceIds.erase(m_sequenceIds.begin() + i);
        if (m_startupSequence == i) {
            m_startupSequence = -1;
            m_startupStep = 0;
        } else if (m_startupSequence > i) --m_startupSequence;
        if (m_current == i) {
            m_current = m_sequences.empty() ? -1 : std::min(i, int(m_sequences.size()) - 1);
        } else if (m_current > i) {
            --m_current;
        }
    }
    emit changed();
    emit positionChanged();
}

int Sequencer::current() const
{
    QMutexLocker lk(&m_mutex);
    return m_current;
}

void Sequencer::setCurrent(int i)
{
    {
        QMutexLocker lk(&m_mutex);
        if (i < -1 || i >= int(m_sequences.size()) || i == m_current) return;
        m_current = i; // UI selection never interrupts background cues
    }
    emit positionChanged();
}

int Sequencer::startupSequence() const
{
    QMutexLocker lk(&m_mutex);
    return m_startupSequence;
}

void Sequencer::setStartupSequence(int i)
{
    {
        QMutexLocker lk(&m_mutex);
        if (i < -1 || i >= int(m_sequences.size()) || m_startupSequence == i) return;
        m_startupSequence = i;
    }
    emit changed();
}

int Sequencer::startupStep() const
{
    QMutexLocker lk(&m_mutex);
    return m_startupStep;
}

void Sequencer::setStartupStep(int step)
{
    {
        QMutexLocker lk(&m_mutex);
        step = std::max(0, step);
        if (m_startupStep == step) return;
        m_startupStep = step;
    }
    emit changed();
}

bool Sequencer::startupGo() const
{
    QMutexLocker lk(&m_mutex);
    return m_startupGo;
}

void Sequencer::setStartupGo(bool go)
{
    {
        QMutexLocker lk(&m_mutex);
        if (m_startupGo == go) return;
        m_startupGo = go;
    }
    emit changed();
}

void Sequencer::playStartup()
{
    int sequence = -1, step = 0;
    bool play = true;
    {
        QMutexLocker lk(&m_mutex);
        if (m_startupSequence < 0 || m_startupSequence >= int(m_sequences.size())) return;
        const int n = int(m_sequences[size_t(m_startupSequence)].steps.size());
        if (n == 0) return;
        sequence = m_startupSequence;
        step = std::clamp(m_startupStep, 0, n - 1); // its steps may have changed since it was chosen
        play = m_startupGo;
    }
    setCurrent(sequence);
    if (play) goTo(step);
    else setPosition(step - 1); // GO plays it
}

int Sequencer::position() const
{
    QMutexLocker lk(&m_mutex);
    return m_current >= 0 ? m_positions[size_t(m_current)] : -1;
}

void Sequencer::setPosition(int step)
{
    {
        QMutexLocker lk(&m_mutex);
        if (m_current < 0) return;
        const int n = int(m_sequences[size_t(m_current)].steps.size());
        m_positions[size_t(m_current)] = std::clamp(step, -1, n - 1);
    }
    emit positionChanged();
}

int Sequencer::next() const
{
    QMutexLocker lk(&m_mutex);
    return nextOfLocked(m_current);
}

int Sequencer::previous() const
{
    QMutexLocker lk(&m_mutex);
    return previousOfLocked(m_current);
}

// The step GO plays in sequence `index` (-1: none), from its own playhead
int Sequencer::nextOfLocked(int index) const
{
    if (index < 0 || index >= int(m_sequences.size())) return -1;
    const Sequence &s = m_sequences[size_t(index)];
    const int n = int(s.steps.size());
    if (n == 0) return -1;
    const int at = m_positions[size_t(index)];
    if (at + 1 < n) return at + 1;
    return s.loop ? 0 : -1;
}

int Sequencer::previousOfLocked(int index) const
{
    if (index < 0 || index >= int(m_sequences.size())) return -1;
    const Sequence &s = m_sequences[size_t(index)];
    const int n = int(s.steps.size());
    const int at = m_positions[size_t(index)];
    if (n == 0 || at < 0) return -1;
    if (at > 0) return at - 1;
    return s.loop ? n - 1 : -1;
}

double Sequencer::stepElapsed(int index) const
{
    QMutexLocker lock(&m_mutex);
    if (index < 0 || index >= int(m_sequenceIds.size())) return 0;
    const auto found = m_stepStartedNs.find(m_sequenceIds[size_t(index)]);
    return found == m_stepStartedNs.end() ? 0 : std::max(0.0, (FrameTiming::nowNs() - found->second) * 1e-9);
}

int Sequencer::positionOf(int index) const
{
    QMutexLocker lk(&m_mutex);
    return index >= 0 && index < int(m_positions.size()) ? m_positions[size_t(index)] : -1;
}

// ---------------------------------------------------------------------------
// Playing: pre-wait, action, post-wait, and what follows

// A step's run, from its GO
static Sequencer::Run runOf(const Sequencer::Step &st, int k, bool chain, quint64 owner, const BeatClock &clock, bool beats)
{
    Sequencer::Run r;
    r.step = k;
    r.sequenceId = owner;
    r.target = st;
    r.snapshot = st.timeline ? 0 : st.snapshot;
    // In beats: at the tempo of now. A GO that follows a grid waits for its line first, then the pre-wait counts
    const double unit = beats ? clock.secondsPerBeat() : 1.0;
    r.preWait = chain ? std::max(0.0, st.preWait) * unit + clock.secondsToNext(st.quantize) : 0.0;
    r.postWait = std::max(0.0, st.postWait) * unit;
    r.next = chain ? st.next : Sequencer::StepContinue::Wait;
    return r;
}

int Sequencer::sequenceIndexLocked(quint64 id) const
{
    const auto found = std::find(m_sequenceIds.begin(), m_sequenceIds.end(), id);
    return found == m_sequenceIds.end() ? -1 : int(found - m_sequenceIds.begin());
}

bool Sequencer::goTo(int step, bool chain)
{
    quint64 owner = 0;
    {
        QMutexLocker lk(&m_mutex);
        if (m_current < 0) return false;
        owner = m_sequenceIds[size_t(m_current)];
    }
    return goToSequence(owner, step, chain);
}

bool Sequencer::goToSequence(quint64 owner, int step, bool chain, std::shared_ptr<std::atomic<bool>> expected, quint64 cue)
{
    auto resolve = [cue](const Sequence &s, int index) {
        if (!cue) return index;
        for (size_t k = 0; k < s.steps.size(); ++k) if (s.steps[k].runtimeId == cue) return int(k);
        return -1;
    };
    if (QThread::currentThread() != thread()) {
        // Bind delayed OSC commands to their original sequence, even if selection changes.
        bool ok = false;
        quint64 stepId = 0;
        std::shared_ptr<std::atomic<bool>> valid;
        {
            QMutexLocker lk(&m_mutex);
            const int index = sequenceIndexLocked(owner);
            if (index >= 0) step = resolve(m_sequences[size_t(index)], step);
            ok = index >= 0 && step >= 0 && step < int(m_sequences[size_t(index)].steps.size());
            if (ok) {
                stepId = m_sequences[size_t(index)].steps[size_t(step)].runtimeId;
                valid = commandTokenLocked(owner);
            }
        }
        if (ok) QMetaObject::invokeMethod(this, [this, owner, stepId, chain, valid] {
            if (!valid->load()) return;
            goToSequence(owner, -1, chain, valid, stepId);
        }, Qt::QueuedConnection);
        return ok;
    }
    // Tracking: a jump (not the step GO plays next) first plays at once the steps from the last snapshot before it
    std::vector<Step> before;
    {
        QMutexLocker lk(&m_mutex);
        const int index = sequenceIndexLocked(owner);
        if (index >= 0 && (!expected || expected->load())) {
            const Sequence &s = m_sequences[size_t(index)];
            const int target = resolve(s, step);
            if (s.tracking && target >= 0 && target < int(s.steps.size()) && target != nextOfLocked(index) &&
                !s.steps[size_t(target)].snapshot) {
                int from = target - 1;
                while (from > 0 && !s.steps[size_t(from)].snapshot) --from;
                for (int k = std::max(0, from); k < target; ++k) before.push_back(s.steps[size_t(k)]);
            }
        }
    }
    if (!before.empty()) playAtOnce(before);
    {
        QMutexLocker lk(&m_mutex);
        if (expected && !expected->load()) return false;
        const int index = sequenceIndexLocked(owner);
        if (index < 0) return false; // removed before a queued GO arrived
        const Sequence &s = m_sequences[size_t(index)];
        step = resolve(s, step);
        if (step < 0 || step >= int(s.steps.size())) return false;
        const Step &st = s.steps[size_t(step)];
        if (!chain) {
            m_runs.erase(std::remove_if(m_runs.begin(), m_runs.end(),
                [owner](const Run &r) { return r.sequenceId == owner; }), m_runs.end());
        }
        m_engine.resumeAfterStop(); // a deliberate GO must also release Escape before its pre-wait
        Run r = runOf(st, step, chain, owner, m_engine.beatClock(), s.beats);
        r.valid = commandTokenLocked(owner);
        r.duration = stepDuration(st);
        m_runs.push_back(r);
        m_positions[size_t(index)] = step;
        m_stepStartedNs[owner] = FrameTiming::nowNs();
    }
    if (!m_engine.isThreaded() || m_engine.fadesManual()) advance(0);
    emit positionChanged();
    startTimer();
    return true;
}

// The steps a jump passes over, each played to its end at once, in order: its snapshot or its preset (their fades and
// delays over now), its timeline told what to do (the timeline then plays on its own time from now)
void Sequencer::playAtOnce(const std::vector<Step> &steps)
{
    for (const Step &st : steps) {
        if (st.sequence) continue; // a jump does not start other sequences
        if (st.timeline) {
            m_engine.controlAnimation(st.timeline, st.action, st.action == AnimAction::Speed ? st.speed : st.seekTime, st.loop, st.repeat);
            continue;
        }
        const int index = st.preset ? m_engine.indexOfPreset(st.preset) : m_engine.indexOfSnapshot(st.snapshot);
        if (index < 0) continue;
        if (st.preset) m_engine.recallPreset(index);
        else m_engine.recallSnapshot(index);
        m_engine.finishRecalls();
    }
}

bool Sequencer::go()
{
    int index;
    {
        QMutexLocker lk(&m_mutex);
        index = m_current;
    }
    return goSequence(index);
}

bool Sequencer::back()
{
    int index;
    {
        QMutexLocker lk(&m_mutex);
        index = m_current;
    }
    return backSequence(index);
}

// GO / GO BACK / a step of a given sequence, whatever is selected (OSC /sequence/<n>/…)
bool Sequencer::goSequence(int index)
{
    quint64 owner;
    {
        QMutexLocker lk(&m_mutex);
        if (index < 0 || index >= int(m_sequences.size())) return false;
        owner = m_sequenceIds[size_t(index)];
    }
    return relativeGo(owner, false);
}

bool Sequencer::backSequence(int index)
{
    quint64 owner;
    {
        QMutexLocker lk(&m_mutex);
        if (index < 0 || index >= int(m_sequences.size())) return false;
        owner = m_sequenceIds[size_t(index)];
    }
    return relativeGo(owner, true);
}

bool Sequencer::goById(quint64 id) { return relativeGo(id, false); }
bool Sequencer::backById(quint64 id) { return relativeGo(id, true); }
bool Sequencer::cueById(quint64 sequence, quint64 cue) { return cue && goToSequence(sequence, -1, true, {}, cue); }

bool Sequencer::selectById(quint64 id)
{
    {
        QMutexLocker lock(&m_mutex);
        const int index = sequenceIndexLocked(id);
        if (index < 0) return false;
        if (index == m_current) return true;
        m_current = index;
    }
    emit positionChanged();
    return true;
}

bool Sequencer::stopById(quint64 id)
{
    {
        QMutexLocker lock(&m_mutex);
        const int index = sequenceIndexLocked(id);
        if (index < 0) return false;
        stopSequenceLocked(index);
    }
    QMutexLocker dispatch(&m_dispatchMutex);
    return true;
}

std::shared_ptr<std::atomic<bool>> Sequencer::commandTokenLocked(quint64 owner)
{
    auto &token = m_commandTokens[owner];
    if (!token || !token->load()) token = std::make_shared<std::atomic<bool>>(true);
    return token;
}

bool Sequencer::relativeGo(quint64 owner, bool back, std::shared_ptr<std::atomic<bool>> expected)
{
    if (QThread::currentThread() != thread()) {
        std::shared_ptr<std::atomic<bool>> valid;
        {
            QMutexLocker lk(&m_mutex);
            if (sequenceIndexLocked(owner) < 0) return false;
            valid = commandTokenLocked(owner);
        }
        // Resolve the playhead when dequeued, not when several OSC commands arrive together.
        QMetaObject::invokeMethod(this, [this, owner, back, valid] {
            if (valid->load()) relativeGo(owner, back, valid);
        }, Qt::QueuedConnection);
        return true; // accepted; the sequence may reach its end before this command executes
    }
    int step;
    {
        QMutexLocker lk(&m_mutex);
        const int index = sequenceIndexLocked(owner);
        step = back ? previousOfLocked(index) : nextOfLocked(index);
    }
    return goToSequence(owner, step, !back, expected);
}

bool Sequencer::goToStepOf(int index, int step)
{
    quint64 owner;
    {
        QMutexLocker lk(&m_mutex);
        if (index < 0 || index >= int(m_sequences.size())) return false;
        owner = m_sequenceIds[size_t(index)];
    }
    return goToSequence(owner, step, true);
}

void Sequencer::stop()
{
    {
        QMutexLocker lk(&m_mutex);
        stopSequenceLocked(m_current);
    }
    QMutexLocker dispatch(&m_dispatchMutex); // drain an action that had already started
}

void Sequencer::stopSequence(int index)
{
    {
        QMutexLocker lk(&m_mutex);
        stopSequenceLocked(index);
    }
    QMutexLocker dispatch(&m_dispatchMutex);
}

void Sequencer::stopSequenceLocked(int index)
{
    if (index < 0 || index >= int(m_sequenceIds.size())) return;
    const quint64 owner = m_sequenceIds[size_t(index)];
    if (auto it = m_commandTokens.find(owner); it != m_commandTokens.end()) *it->second = false;
    m_runs.erase(std::remove_if(m_runs.begin(), m_runs.end(),
        [owner](const Run &r) { return r.sequenceId == owner; }), m_runs.end());
}

// Every sequence: the pre-waits and follows still to come, in the background too (the operator's panic button)
void Sequencer::stopAll()
{
    {
        QMutexLocker lk(&m_mutex);
        for (auto &[owner, token] : m_commandTokens) *token = false;
        m_pendingSnapshots.clear();
        m_runs.clear();
    }
    // Invalidate first so a batch cannot dispatch more actions while STOP waits.
    // Never wait for dispatch while holding the engine mutex: a recall needs it.
    QMutexLocker dispatch(&m_dispatchMutex);
}

bool Sequencer::anyActive() const
{
    QMutexLocker lk(&m_mutex);
    return !m_runs.empty();
}

std::vector<Sequencer::Run> Sequencer::runs() const
{
    QMutexLocker lk(&m_mutex);
    if (m_current < 0) return {};
    const quint64 owner = m_sequenceIds[size_t(m_current)];
    std::vector<Run> shown;
    for (const Run &r : m_runs)
        if (r.sequenceId == owner) shown.push_back(r);
    return shown;
}

bool Sequencer::running() const
{
    QMutexLocker lk(&m_mutex);
    if (m_current < 0) return false;
    const quint64 owner = m_sequenceIds[size_t(m_current)];
    for (const Run &r : m_runs)
        if (r.sequenceId == owner &&
            (!r.fired || (r.next != StepContinue::Wait && !r.continued))) return true;
    return false;
}

bool Sequencer::active(int index) const
{
    QMutexLocker lk(&m_mutex);
    if (index < 0 || index >= int(m_sequenceIds.size())) return false;
    const quint64 owner = m_sequenceIds[size_t(index)];
    return std::any_of(m_runs.begin(), m_runs.end(),
                       [owner](const Run &r) { return r.sequenceId == owner; });
}

void Sequencer::startTimer()
{
    if (m_engine.fadesManual()) return;
    {
        QMutexLocker lk(&m_mutex);
        if (m_runs.empty() && m_pendingSnapshots.empty()) return;
    }
    if (!m_clockThread) {
        m_clockThread = std::make_unique<QThread>();
        m_clockThread->setObjectName(QStringLiteral("NyxMap cue clock"));
        m_clockContext = new CueClockContext([this] { runClock(); });
        connect(m_clockThread.get(), &QThread::finished, m_clockContext, [this] { tsanRelease(this); }, Qt::DirectConnection);
        connect(m_clockThread.get(), &QThread::finished, m_clockContext, &QObject::deleteLater);
        m_clockContext->moveToThread(m_clockThread.get());
        m_clockThread->start(QThread::TimeCriticalPriority);
    }
    if (m_clockContext) QMetaObject::invokeMethod(m_clockContext, "wake", Qt::QueuedConnection);
}

void Sequencer::runClock()
{
    if (!m_timer) {
        m_timer = new QTimer(m_clockContext);
        m_timer->setTimerType(Qt::PreciseTimer);
        m_timer->setInterval(2); // a wait ends within ~2 ms of its time, whatever the interface does
        connect(m_timer, &QTimer::timeout, m_clockContext, [this] { tick(); });
    }
    if (!m_timer->isActive()) {
        m_clock.restart(); // the time since the GO is already in the runs: the clock counts from now
        m_ticking = true;
        m_timer->start();
    }
}

void Sequencer::tick()
{
    // The time that really went by (a monotonic clock): a tick that came late is caught up at once, the waits do not
    // drift with the timer's own lateness
    const double dt = m_clock.nsecsElapsed() * 1e-9 * timeScale();
    m_clock.restart();
    std::vector<PendingSnapshot> pending;
    { QMutexLocker lock(&m_mutex); pending.swap(m_pendingSnapshots); }
    for (const auto &request : pending) {
        QMutexLocker dispatch(&m_dispatchMutex);
        if (request.valid->load()) fireSnapshot(request.id);
    }
    advance(dt);
    bool empty;
    {
        QMutexLocker lk(&m_mutex);
        empty = m_runs.empty() && m_pendingSnapshots.empty();
    }
    if (empty && m_timer) {
        m_timer->stop();
        m_ticking = false;
    }
}

void Sequencer::fireSnapshot(quint64 snapshotId)
{
    std::function<void(int)> recaller;
    std::function<void(int, const RecallBefore &)> recorder;
    {
        QMutexLocker lk(&m_mutex);
        recaller = m_recaller;
        recorder = m_recorder;
    }
    if (QThread::currentThread() == thread()) { // a GO, on the Sequencer's own thread: through the interface
        // The interface edits snapshots on this thread: the index stays the same until the recall
        const int mi = m_engine.indexOfSnapshot(snapshotId);
        if (mi < 0) return;
        if (recaller) recaller(mi);
        else m_engine.recallSnapshot(mi);
        return;
    }
    // The cue clock: by id only, the interface may add or remove a snapshot meanwhile (an index would then recall
    // its neighbour). Record how long it takes to *actually* execute a
    // snapshot. CueTiming::noteFired() only measures timer lateness, which
    // can be tiny even when Undo capture or scene application is slow.
    quint64 generation;
    { QMutexLocker lock(&m_mutex); generation = m_generation; }
    const qint64 start = FrameTiming::nowNs();
    const auto recordWork = [this, start](qint64 captured, qint64 applied) {
        QMutexLocker lk(&m_mutex);
        m_workTiming.capture.add(double(captured - start) / 1e6);
        m_workTiming.recall.add(double(applied - captured) / 1e6);
        m_workTiming.total.add(double(applied - start) / 1e6);
    };
    if (!recorder) {
        if (!m_engine.recallSnapshotById(snapshotId)) return;
        const qint64 done = FrameTiming::nowNs();
        recordWork(start, done); // no UI Undo recording, so capture time is zero
        return;
    }
    RecallBefore before{m_engine.captureRecallLayersById(snapshotId), m_engine.captureComposition(), m_engine.captureInteractions(),
                        m_engine.captureFixtures()};
    const qint64 captured = FrameTiming::nowNs();
    if (!m_engine.recallSnapshotById(snapshotId)) return;
    recordWork(captured, FrameTiming::nowNs());
    {
        QMutexLocker lock(&m_mutex);
        if (generation != m_generation) return;
        m_recordedRecalls.push_back({snapshotId, generation, std::move(before)});
    }
    QMetaObject::invokeMethod(this, "deliverRecalls", Qt::QueuedConnection);

}

// Each step on its way moves on by dt: its snapshot once its pre-wait is over, the next step's GO when its
// continuation says so (the time beyond it carried over, so that chained waits do not drift). A step is
// forgotten once its action is over and nothing more comes from it.
void Sequencer::advance(double dt)
{
    dt = std::max(0.0, dt);
    bool moved = false;
    for (int round = 0; round < 256; ++round) { // steps without any wait follow each other in the same call
        std::vector<Run> fire;
        std::vector<Run> started;
        {
            QMutexLocker lk(&m_mutex);
            for (Run &r : m_runs) {
                // Active runs refer to stable identities rather than the UI selection.
                const int index = sequenceIndexLocked(r.sequenceId);
                if (index < 0) continue; // deleted during an earlier recall
                const Sequence &s = m_sequences[size_t(index)];
                const int n = int(s.steps.size());
                r.elapsed += dt;
                if (!r.fired && r.elapsed >= r.preWait) {
                    r.fired = true;
                    noteFired(r.elapsed - r.preWait); // how late the clock let it fire
                    if (r.target.preset) r.duration = stepDuration(r.target) + r.elapsed - r.preWait;
                    if (r.target.timeline) {
                        r.duration = stepDuration(r.target); // where the timeline is now
                        // A late Play starts now, not at the missed pre-wait deadline.
                        // Keep its full remaining duration before AutoFollow can run.
                        if (r.target.action == AnimAction::Play) r.duration += r.elapsed - r.preWait;
                    }
                    r.trackPending = r.target.timeline && r.target.action == AnimAction::Play;
                    fire.push_back(r);
                } else if (r.tracking) {
                    // Track the animation's real remaining time while it runs, including speed and loop edits.
                    // Freeze the action's duration when playback ends. Otherwise elapsed + 0 is recomputed
                    // on every tick, so AutoFollow's post-wait deadline moves forever.
                    const double remaining = m_engine.animationRemaining(r.target.timeline);
                    if (remaining <= 0.0) {
                        r.duration = std::max(0.0, r.elapsed - r.preWait);
                        r.tracking = false;
                    } else {
                        r.duration = std::max(0.0, r.elapsed - r.preWait) + remaining;
                    }
                }
                const double at = r.continueAt();
                if (!r.fired || r.continued || at < 0 || r.elapsed < at) continue;
                r.continued = true;
                const int k = r.step + 1 < n ? r.step + 1 : s.loop ? 0 : -1;
                if (k < 0) continue;
                const Step &st = s.steps[size_t(k)];
                Run x = runOf(st, k, true, r.sequenceId, m_engine.beatClock(), s.beats);
                x.valid = r.valid;
                x.callers = r.callers;
                x.elapsed = r.elapsed - at;
                x.duration = stepDuration(st);
                started.push_back(x);
            }
            m_runs.erase(std::remove_if(m_runs.begin(), m_runs.end(),
                                        [](const Run &r) {
                                            return r.fired && (r.next == StepContinue::Wait || r.continued) &&
                                                   r.elapsed >= r.preWait + r.duration;
                                        }),
                         m_runs.end());
        }
        // The snapshots and timelines, in the order of their GO (outside the lock: a recall loads, and may go
        // through the interface's undo stack)
        if (!fire.empty()) m_engine.noteInput(FrameTiming::Cue);
        bool called = false;
        for (const Run &action : fire) {
            QMutexLocker dispatch(&m_dispatchMutex);
            if (!action.valid->load()) continue;
            {
                QMutexLocker lk(&m_mutex);
                const int index = sequenceIndexLocked(action.sequenceId);
                if (index < 0) continue;
                const auto &steps = m_sequences[size_t(index)].steps;
                if (std::none_of(steps.begin(), steps.end(), [&](const Step &s) {
                    return s.runtimeId == action.target.runtimeId;
                })) continue;
            }
            const Step &st = action.target;
            if (st.sequence) {
                QMutexLocker lk(&m_mutex);
                const int sequence = sequenceIndexLocked(st.sequence);
                if (st.sequence == action.sequenceId || action.callers.size() >= 256 ||
                    std::find(action.callers.begin(), action.callers.end(), st.sequence) != action.callers.end() ||
                    sequence < 0 || m_sequences[size_t(sequence)].steps.empty()) continue;
                const auto target = m_sequences[size_t(sequence)].steps.front();
                const quint64 owner = st.sequence;
                stopSequenceLocked(sequence); // Play sequence starts fresh, without an earlier pending follow
                Run child = runOf(target, 0, true, owner, m_engine.beatClock(), m_sequences[size_t(sequence)].beats);
                child.valid = commandTokenLocked(owner);
                child.duration = stepDuration(target);
                child.callers = action.callers;
                child.callers.push_back(action.sequenceId);
                m_runs.push_back(child);
                m_positions[size_t(sequence)] = 0;
                m_stepStartedNs[owner] = FrameTiming::nowNs();
                called = moved = true;
                continue;
            }
            if (st.preset) {
                const int index = m_engine.indexOfPreset(st.preset);
                if (index < 0) continue;
                std::function<void(int)> recaller;
                { QMutexLocker lk(&m_mutex); recaller = m_presetRecaller; }
                if (QThread::currentThread() == thread() && recaller) recaller(index);
                else m_engine.recallPreset(index);
                continue;
            }
            if (st.timeline) {
                m_engine.controlAnimation(st.timeline, st.action, st.action == AnimAction::Speed ? st.speed : st.seekTime, st.loop, st.repeat);
                continue;
            }
            fireSnapshot(st.snapshot); // a step without its snapshot (gone): the position moves, nothing else
        }
        {
            QMutexLocker lk(&m_mutex); // the Plays are issued: their steps follow the timelines from now on
            for (Run &r : m_runs)
                if (r.trackPending) r.trackPending = false, r.tracking = true;
        }
        if (started.empty() && !called) break;
        {
            QMutexLocker lk(&m_mutex);
            for (Run &x : started) {
                if (!x.valid->load()) continue;
                const int index = sequenceIndexLocked(x.sequenceId);
                if (index < 0) continue; // sequence removed while an action fired
                const auto &steps = m_sequences[size_t(index)].steps;
                const auto found = std::find_if(steps.begin(), steps.end(), [&](const Step &s) {
                    return s.runtimeId == x.target.runtimeId;
                });
                if (found == steps.end()) continue;
                x.step = int(found - steps.begin());
                m_runs.push_back(x);
                m_positions[size_t(index)] = x.step;
                m_stepStartedNs[x.sequenceId] = FrameTiming::nowNs() - qint64(x.elapsed / std::max(1e-9, timeScale().load()) * 1e9);
                moved = true;
            }
        }
        dt = 0; // the next round: only what the new steps carry over
    }
    if (moved) emit positionChanged();
}

QJsonArray Sequencer::toJson() const
{
    QMutexLocker lk(&m_mutex);
    QJsonArray out;
    for (const Sequence &s : m_sequences) {
        QJsonArray steps;
        for (const Step &st : s.steps) {
            QJsonObject o;
            if (st.sequence) o["sequence"] = QString::number(st.sequence);
            else if (st.preset) o["preset"] = QString::number(st.preset);
            else if (st.timeline) {
                static const char *const actions[] = {"play", "pause", "stop", "rewind", "seek", "loop_mode", "speed"};
                o["timeline"] = QString::number(st.timeline);
                o["action"] = QString::fromLatin1(actions[std::clamp(int(st.action), 0, 6)]);
                if (st.action == AnimAction::Seek) o["time"] = st.seekTime;
                if (st.action == AnimAction::Speed) o["speed"] = st.speed;
                if (st.action == AnimAction::LoopMode) {
                    o["loop"] = animLoopKey(st.loop);
                    if (st.repeat > 0) o["repeat"] = st.repeat;
                }
            } else {
                o["snapshot"] = QString::number(st.snapshot);
            }
            if (!st.text.isEmpty()) o["text"] = st.text;
            if (!st.description.isEmpty()) o["description"] = st.description;
            if (st.preWait > 0) o["pre_wait"] = st.preWait;
            if (st.postWait > 0) o["post_wait"] = st.postWait;
            if (st.quantize > 0) o["quantize"] = st.quantize;
            if (st.next != StepContinue::Wait) o["continue"] = st.next == StepContinue::Follow ? "follow" : "auto_follow";
            if (st.at >= 0) o["at"] = st.at;
            steps.append(o);
        }
        QJsonObject so{{"id", QString::number(m_sequenceIds[size_t(out.size())])}, {"name", s.name}, {"loop", s.loop}, {"beats", s.beats},
                       {"tracking", s.tracking}, {"steps", steps}};
        if (!s.clockParam.isEmpty()) so["timecode"] = QJsonObject{{"layer", QString::number(s.clockLayer)}, {"param", s.clockParam}};
        out.append(so);
    }
    return out;
}

void Sequencer::fromJson(const QJsonArray &a, int current)
{
    QMutexLocker lk(&m_mutex);
    clear(); // invalidate queued commands before persistent sequence identities can be restored
    // Missing targets remain missing: never give their identity to a newly added sequence.
    for (const auto &value : a) {
        const auto o = value.toObject();
        m_nextSequenceId = std::max(m_nextSequenceId, o.value("id").toString().toULongLong() + 1);
        for (const auto &step : o.value("steps").toArray())
            m_nextSequenceId = std::max(m_nextSequenceId, step.toObject().value("sequence").toString().toULongLong() + 1);
    }
    QSet<quint64> seen;
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        quint64 id = o.value("id").toString().toULongLong();
        if (!id || seen.contains(id)) id = m_nextSequenceId++;
        seen.insert(id);
        m_sequenceIds.push_back(id);
        Sequence s;
        s.name = o.value("name").toString();
        s.loop = o.value("loop").toBool(false);
        s.beats = o.value("beats").toBool(false);
        s.tracking = o.value("tracking").toBool(true);
        const QJsonObject clock = o.value("timecode").toObject();
        s.clockLayer = clock.value("layer").toString().toULongLong();
        s.clockParam = clock.value("param").toString();
        for (const QJsonValue &sv : o.value("steps").toArray()) {
            const QJsonObject so = sv.toObject();
            Step st;
            st.runtimeId = m_nextStepId++;
            st.sequence = so.value("sequence").toString().toULongLong();
            st.preset = st.sequence ? 0 : so.value("preset").toString().toULongLong();
            st.snapshot = so.value("snapshot").toString().toULongLong();
            st.timeline = so.value("timeline").toString().toULongLong();
            if (st.sequence || st.preset) st.snapshot = st.timeline = 0;
            if (st.timeline) {
                const QString a = so.value("action").toString();
                st.action = a == "pause" ? AnimAction::Pause : a == "stop" ? AnimAction::Stop : a == "rewind" ? AnimAction::Rewind
                          : a == "seek" ? AnimAction::Seek : a == "loop_mode" ? AnimAction::LoopMode
                          : a == "speed" ? AnimAction::Speed : AnimAction::Play;
                st.speed = std::clamp(so.value("speed").toDouble(1), 0.0, 10.0);
                st.seekTime = std::max(0.0, so.value("time").toDouble(0));
                st.loop = animLoopFromKey(so.value("loop").toString());
                st.repeat = std::clamp(so.value("repeat").toInt(0), 0, 100000);
                st.snapshot = 0;
            }
            st.text = so.value("text").toString();
            st.description = so.value("description").toString();
            st.preWait = std::clamp(so.value("pre_wait").toDouble(0), 0.0, 3600.0);
            st.postWait = std::clamp(so.value("post_wait").toDouble(0), 0.0, 3600.0);
            st.quantize = std::clamp(so.value("quantize").toDouble(0), 0.0, 64.0);
            const QString c = so.value("continue").toString();
            st.next = c == "follow" ? StepContinue::Follow : c == "auto_follow" ? StepContinue::AutoFollow : StepContinue::Wait;
            const double at = so.value("at").toDouble(-1);
            st.at = std::isfinite(at) && at >= 0 ? at : -1;
            s.steps.push_back(st);
        }
        m_sequences.push_back(s);
    }
    m_positions.assign(m_sequences.size(), -1);
    m_current = m_sequences.empty() ? -1 : std::clamp(current, 0, int(m_sequences.size()) - 1);
    m_startupSequence = -1; // chosen by the project after sequences are parsed
    m_startupStep = 0;
    m_startupGo = true;
    m_runs.clear();
}

void Sequencer::clear()
{
    QMutexLocker lk(&m_mutex);
    for (auto &[owner, token] : m_commandTokens) *token = false;
    m_commandTokens.clear();
    m_sequences.clear();
    m_stepStartedNs.clear();
    m_positions.clear();
    m_sequenceIds.clear();
    m_startupSequence = -1;
    m_startupStep = 0;
    m_startupGo = true;
    m_current = -1;
    m_runs.clear();
    m_pendingSnapshots.clear();
    m_recordedRecalls.clear();
    ++m_generation;
}

double Sequencer::stepDuration(const Step &st) const
{
    if (st.sequence) return 0; // starts an independent sequence, which owns its waits and follows
    if (st.preset) return m_engine.presetDuration(st.preset);
    if (!st.timeline) return m_engine.snapshotDuration(st.snapshot);
    return st.action == AnimAction::Play ? m_engine.animationPlayTime(st.timeline) : 0;
}

#include "Sequencer.moc"


bool Sequencer::requestSnapshot(quint64 id)
{
    if (m_engine.indexOfSnapshot(id) < 0 || !m_engine.isThreaded() || m_engine.fadesManual()) return false;
    { QMutexLocker lock(&m_mutex); m_pendingSnapshots.push_back({id, commandTokenLocked(0)}); }
    startTimer();
    return true;
}

void Sequencer::deliverRecalls()
{
    std::vector<RecordedRecall> completed;
    std::function<void(int, const RecallBefore &)> recorder;
    quint64 generation;
    { QMutexLocker lock(&m_mutex); completed.swap(m_recordedRecalls); recorder = m_recorder; generation = m_generation; }
    if (!recorder) return;
    for (const auto &record : completed) {
        if (record.generation != generation) continue;
        const int index = m_engine.indexOfSnapshot(record.snapshot);
        if (index >= 0) recorder(index, record.before);
    }
}
