#include "Commands.h"
#include "EngineInternal.h"
#include "Osc.h"

#include <QColor>
#include <QDateTime>
#include <QUndoStack>
#include <algorithm>

namespace cmd {

static qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }
static constexpr qint64 kMergeWindowMs = 1500; // continuous gestures merged into a single step

IsfInstance *resolveIsf(Engine *e, int layer, int slot)
{
    Layer *l = e->layer(layer);
    if (!l) return nullptr;
    if (slot < 0) return l->generator.get();
    return slot < int(l->effects.size()) ? l->effects[size_t(slot)].get() : nullptr;
}

// --- SetValues --------------------------------------------------------------

SetValues::SetValues(Engine *e, std::vector<ValueChange> changes, const QString &text)
    : m_e(e), m_changes(std::move(changes)), m_time(nowMs())
{
    setText(text);
}

void SetValues::apply(bool after)
{
    // Twice when several: values that hold each other in (a ROI's sides, in and out) all reach theirs
    const int passes = m_changes.size() > 1 ? 2 : 1;
    for (int k = 0; k < passes; ++k)
        for (const ValueChange &c : m_changes) m_e->setParameter(c.layer, c.path, after ? c.after : c.before);
}

bool SetValues::mergeWith(const QUndoCommand *other)
{
    auto *o = static_cast<const SetValues *>(other);
    if (o->m_changes.size() != m_changes.size() || o->m_time - m_time > kMergeWindowMs) return false;
    for (size_t k = 0; k < m_changes.size(); ++k)
        if (o->m_changes[k].layer != m_changes[k].layer || o->m_changes[k].path != m_changes[k].path) return false;
    for (size_t k = 0; k < m_changes.size(); ++k) m_changes[k].after = o->m_changes[k].after;
    m_time = o->m_time;
    return true;
}

bool setValues(QUndoStack *undo, Engine *e, quint64 layer, const QJsonObject &after, const QString &text)
{
    return setValues(undo, e, {{layer, after}}, text);
}

bool setValues(QUndoStack *undo, Engine *e, const std::vector<std::pair<quint64, QJsonObject>> &after, const QString &text)
{
    std::vector<ValueChange> changes;
    for (const auto &[layer, values] : after)
        for (auto it = values.begin(); it != values.end(); ++it) {
            const QJsonValue before = e->parameterValue(layer, it.key());
            if (before.isUndefined() || before == it.value()) continue;
            changes.push_back({layer, it.key(), before, it.value()});
        }
    if (changes.empty()) return false;
    undo->push(new SetValues(e, std::move(changes), text));
    return true;
}

QJsonObject isfValueJson(const QString &prefix, const IsfInput &in, const IsfValue &v)
{
    const QString path = prefix + QStringLiteral("param/") + osc::safeName(in.name);
    switch (in.type) {
    case IsfInput::Float: return {{path, v.f}};
    case IsfInput::Long: return {{path, v.l}};
    case IsfInput::Bool: return {{path, v.b}};
    case IsfInput::Point2D: return {{path + "/x", v.p.x()}, {path + "/y", v.p.y()}};
    case IsfInput::Color: {
        QJsonObject o;
        for (int c = 0; c < 4; ++c) o.insert(path + "/" + QChar("rgba"[c]), double(v.c[c]));
        return o;
    }
    default: return {};
    }
}

// --- SetLayerProp -----------------------------------------------------------

static QString propText(SetLayerProp::Prop p)
{
    switch (p) {
    case SetLayerProp::Name: return QStringLiteral("Rename Layer");
    case SetLayerProp::Enabled: return QStringLiteral("Change Enable");
    case SetLayerProp::Locked: return QStringLiteral("Lock / Unlock Layer");
    case SetLayerProp::Transition: return QStringLiteral("Change Transition");
    case SetLayerProp::ColorMask: return QStringLiteral("Change Color Mask");
    case SetLayerProp::Matte: return QStringLiteral("Change Layer Matte");
    }
    return {};
}

SetLayerProp::SetLayerProp(Engine *e, quint64 layer, Prop prop, const QVariant &before, const QVariant &after)
    : m_e(e), m_layer(layer), m_prop(prop), m_before(before), m_after(after), m_time(nowMs())
{
    setText(propText(prop));
}

QString SetLayerProp::textOf(Prop prop) { return propText(prop); }

QVariant SetLayerProp::read(Engine *e, quint64 layer, Prop prop)
{
    Engine::Lock lk(&e->mutex());
    const Layer *l = e->layer(e->indexOfId(layer));
    if (!l) return {};
    switch (prop) {
    case Name: return l->name;
    case Enabled: return l->enabled;
    case Locked: return l->locked;
    case Transition: return l->transition;
    case ColorMask: return QVariant(qulonglong(l->color.maskLayer));
    case Matte: return QVariant(l->matte.toJson());
    }
    return {};
}

void SetLayerProp::apply(const QVariant &v)
{
    if (m_prop == ColorMask) { // checked against feedback by the engine
        bool invert;
        int index;
        {
            Engine::Lock lk(&m_e->mutex());
            index = m_e->indexOfId(m_layer);
            const Layer *l = m_e->layer(index);
            if (!l) return;
            invert = l->color.maskInvert;
        }
        m_e->setColorMask(index, v.toULongLong(), invert);
        return;
    }
    if (m_prop == Matte) { // checked against feedback by the engine
        int index;
        {
            Engine::Lock lk(&m_e->mutex());
            index = m_e->indexOfId(m_layer);
        }
        m_e->setLayerMatte(index, LayerMatte::fromJson(v.toJsonObject()));
        return;
    }
    Engine::Lock lk(&m_e->mutex());
    Layer *l = m_e->layer(m_e->indexOfId(m_layer));
    if (!l) return;
    switch (m_prop) {
    case Name: l->name = v.toString(); break;
    case Enabled: l->enabled = v.toBool(); break;
    case Locked: l->locked = v.toBool(); break;
    case Transition: l->transition = v.toString(); break;
    case ColorMask:
    case Matte: break;
    }
}

bool SetLayerProp::mergeWith(const QUndoCommand *other)
{
    auto *o = static_cast<const SetLayerProp *>(other);
    if (o->m_layer != m_layer || o->m_prop != m_prop || m_prop != Name) return false;
    if (o->m_time - m_time > kMergeWindowMs) return false;
    m_after = o->m_after;
    m_time = o->m_time;
    return true;
}

// --- SetMapping -------------------------------------------------------------

SetMapping::SetMapping(Engine *e, int layer, const Mapping &before, const Mapping &after, const QString &text, bool nudge)
    : m_e(e), m_layer(layer), m_before(before), m_after(after), m_nudge(nudge), m_time(nowMs())
{
    setText(text);
}

Mapping SetMapping::read(Engine *e, int layer)
{
    Engine::Lock lk(&e->mutex());
    Layer *l = e->layer(layer);
    return l ? l->mapping : Mapping();
}

void SetMapping::apply(const Mapping &m)
{
    Engine::Lock lk(&m_e->mutex());
    Layer *l = m_e->layer(m_layer);
    if (!l) return;
    const unsigned rev = l->mapping.revision;
    const double aspect = l->mapping.aspect; // the composition's now
    l->mapping = m;
    l->mapping.aspect = aspect;
    l->mapping.revision = rev + 1;
}

bool SetMapping::mergeWith(const QUndoCommand *other)
{
    auto *o = static_cast<const SetMapping *>(other);
    if (!m_nudge || !o->m_nudge || o->m_layer != m_layer) return false;
    if (o->m_time - m_time > kMergeWindowMs) return false;
    m_after = o->m_after;
    m_time = o->m_time;
    return true;
}

SetMasks::SetMasks(Engine *e, quint64 layer, const std::vector<Mask> &before, const std::vector<Mask> &after, const QString &text)
    : m_e(e), m_layer(layer), m_before(before), m_after(after)
{
    setText(text);
}
void SetMasks::undo() { m_e->setLayerMasks(m_layer, m_before); }
void SetMasks::redo() { m_e->setLayerMasks(m_layer, m_after); }

SetLines::SetLines(Engine *e, quint64 layer, const std::vector<LinePath> &before, const std::vector<LinePath> &after,
                   const QString &text)
    : m_e(e), m_layer(layer), m_before(before), m_after(after)
{
    setText(text);
}
void SetLines::undo() { m_e->setLayerLines(m_layer, m_before); }
void SetLines::redo() { m_e->setLayerLines(m_layer, m_after); }

// --- Layers -----------------------------------------------------------------

AddLayer::AddLayer(Engine *e, int index, const QString &text) : m_e(e), m_index(index)
{
    m_json = e->layerJson(index);
    setText(text);
}

void AddLayer::undo() { m_e->removeLayer(m_index); }

void AddLayer::redo()
{
    if (m_first) {
        m_first = false;
        return;
    }
    m_e->insertLayerJson(m_index, m_json);
}

RemoveLayer::RemoveLayer(Engine *e, int index) : m_e(e), m_index(index)
{
    m_json = e->layerJson(index);
    setText(QStringLiteral("Delete \"%1\"").arg(m_json.value("name").toString()));
}

void RemoveLayer::undo() { m_e->insertLayerJson(m_index, m_json); }
void RemoveLayer::redo() { m_e->removeLayer(m_index); }

MoveLayer::MoveLayer(Engine *e, int from, int to) : m_e(e), m_from(from), m_to(to)
{
    setText(QStringLiteral("Reorder Layers"));
}

static Engine::AnimationTargetState targetsAfter(Engine *e, const Engine::AnimationTargetState &before)
{
    auto after = e->captureAnimationTargets(before.layer);
    // A timeline whose last target was removed must still have an explicit empty definition for Redo.
    for (const auto &a : before.timelines) {
        if (std::any_of(after.timelines.begin(), after.timelines.end(), [&](const auto &x) { return x.id == a.id; })) continue;
        const int index = e->indexOfAnimation(a.id);
        if (index >= 0) after.timelines.push_back(e->animation(index));
    }
    return after;
}

ReplaceLayer::ReplaceLayer(Engine *e, int index, const QJsonObject &before, const QString &text,
                           Engine::AnimationTargetState beforeAnimations)
    : m_e(e), m_index(index), m_before(before)
{
    m_after = e->layerJson(index);
    m_beforeAnimations = beforeAnimations.layer ? std::move(beforeAnimations) : e->captureAnimationTargets(e->layerId(index));
    m_afterAnimations = targetsAfter(e, m_beforeAnimations);
    setText(text);
}

void ReplaceLayer::undo()
{
    m_e->replaceLayerJson(m_index, m_before);
    m_e->restoreAnimationTargets(m_beforeAnimations);
}

void ReplaceLayer::redo()
{
    if (m_first) {
        m_first = false;
        return;
    }
    m_e->replaceLayerJson(m_index, m_after);
    m_e->restoreAnimationTargets(m_afterAnimations);
}

SetStructure::SetStructure(Engine *e, const LayerTree &before, const LayerTree &after, const QString &text)
    : m_e(e), m_before(before), m_after(after)
{
    setText(text);
}

RecallSnapshot::RecallSnapshot(Engine *e, int snapshot) : m_e(e), m_snapshot(e->snapshot(snapshot).id)
{
    m_before = e->captureRecallLayers(snapshot);
    m_beforeComposition = e->captureComposition();
    m_beforeInteractions = e->captureInteractions();
    m_beforeFixtures = e->captureFixtures();
    setText(QStringLiteral("Recall \"%1\"").arg(e->snapshot(snapshot).name));
}

RecallSnapshot::RecallSnapshot(Engine *e, int snapshot, const QJsonArray &before, const QJsonObject &beforeComposition,
                               const QJsonArray &beforeInteractions, const QJsonArray &beforeFixtures)
    : m_e(e), m_snapshot(e->snapshot(snapshot).id), m_before(before), m_beforeComposition(beforeComposition),
      m_beforeInteractions(beforeInteractions), m_beforeFixtures(beforeFixtures), m_done(true)
{
    setText(QStringLiteral("Recall \"%1\"").arg(e->snapshot(snapshot).name));
}

void RecallSnapshot::redo()
{
    const int index = m_e->indexOfSnapshot(m_snapshot);
    if (index < 0) return;
    if (m_first) {
        m_first = false;
        if (!m_done) m_e->recallSnapshot(index);
    } else {
        const Engine::Snapshot m = m_e->snapshot(index);
        if (!m.layers.isEmpty()) m_e->applyLayers(m.layers, 0, true);
        m_e->applyComposition(m.composition, 0);
        m_e->applyInteractions(m.interactions, 0);
        m_e->applyFixtures(m.fixtures, 0);
    }
}

SetEffects::SetEffects(Engine *e, int index, const QJsonArray &before, const QString &text,
                       Engine::AnimationTargetState beforeAnimations)
    : m_e(e), m_index(index), m_before(before)
{
    m_after = e->effectsJson(index);
    m_beforeAnimations = beforeAnimations.layer ? std::move(beforeAnimations) : e->captureAnimationTargets(e->layerId(index));
    m_afterAnimations = targetsAfter(e, m_beforeAnimations);
    setText(text);
}

void SetEffects::undo()
{
    m_e->setEffectsJson(m_index, m_before);
    m_e->restoreAnimationTargets(m_beforeAnimations);
}

void SetEffects::redo()
{
    if (m_first) {
        m_first = false;
        return;
    }
    m_e->setEffectsJson(m_index, m_after);
    m_e->restoreAnimationTargets(m_afterAnimations);
}

SetOpacityIn::SetOpacityIn(Engine *e, quint64 layer, quint64 viewport, float before, float after, const QString &text)
    : m_e(e), m_layer(layer), m_viewport(viewport), m_before(before), m_after(after), m_time(nowMs())
{
    setText(text);
}

bool SetOpacityIn::mergeWith(const QUndoCommand *o)
{
    auto *c = dynamic_cast<const SetOpacityIn *>(o);
    if (!c || c->m_layer != m_layer || c->m_viewport != m_viewport || c->m_time - m_time > kMergeWindowMs) return false;
    m_after = c->m_after;
    m_time = c->m_time;
    return true;
}

void SetOpacityIn::apply(float v) { m_e->setOpacityIn(m_e->indexOfId(m_layer), m_viewport, v); }

SetAnimation::SetAnimation(Engine *e, quint64 id, const Engine::Animation &before, const Engine::Animation &after,
                           const QString &text, const QString &mergeKey)
    : m_e(e), m_id(id), m_before(before), m_after(after), m_mergeKey(mergeKey)
{
    setText(text);
}

bool SetAnimation::mergeWith(const QUndoCommand *other)
{
    const auto *o = static_cast<const SetAnimation *>(other); // same id(): a SetAnimation
    if (o->m_id != m_id || o->m_mergeKey != m_mergeKey) return false;
    m_after = o->m_after;
    return true;
}

void SetAnimation::apply(const Engine::Animation &a)
{
    const int i = m_e->indexOfAnimation(m_id);
    if (i >= 0) m_e->setAnimation(i, a);
}

AddAnimation::AddAnimation(Engine *e, const Engine::Animation &a, int index, const QString &text)
    : m_e(e), m_a(a), m_index(index)
{
    setText(text);
}

void AddAnimation::redo()
{
    const int i = m_e->addAnimation(m_a, m_index); // the first time: gets its id
    m_a.id = m_e->animation(i).id;
}

void AddAnimation::undo()
{
    const int i = m_e->indexOfAnimation(m_a.id);
    if (i >= 0) {
        m_a = m_e->animation(i); // as it is now, should it be redone
        m_e->removeAnimation(i);
    }
}

RemoveAnimation::RemoveAnimation(Engine *e, int index) : m_e(e), m_a(e->animation(index)), m_index(index)
{
    setText(QStringLiteral("Delete Timeline"));
}

void RemoveAnimation::redo()
{
    const int i = m_e->indexOfAnimation(m_a.id);
    if (i >= 0) {
        m_index = i;
        m_e->removeAnimation(i);
    }
}

SetLayerAnim::SetLayerAnim(Engine *e, quint64 layer, const QString &param, std::optional<Animation> before,
                           std::optional<Animation> after, const QString &text, const QString &mergeKey)
    : m_e(e), m_layer(layer), m_param(param), m_before(std::move(before)), m_after(std::move(after)),
      m_index(e->layerAnimIndex(layer, param)), m_mergeKey(mergeKey)
{
    setText(text);
}

bool SetLayerAnim::mergeWith(const QUndoCommand *other)
{
    const auto *o = static_cast<const SetLayerAnim *>(other); // same id(): a SetLayerAnim
    if (o->m_layer != m_layer || o->m_param != m_param || o->m_mergeKey != m_mergeKey) return false;
    m_after = o->m_after;
    return true;
}

void SetLayerAnim::apply(const std::optional<Animation> &a)
{
    m_e->setLayerAnim(m_layer, m_param, a ? &*a : nullptr, m_index);
}

// --- Presets -----------------------------------------------------------------

RecallPreset::RecallPreset(Engine *e, quint64 preset) : m_e(e), m_preset(preset)
{
    Engine::Lock lock(&e->mutex()); // parameter outputs and animation clocks are one Undo state
    const Engine::Preset p = e->preset(e->indexOfPreset(preset));
    for (const Engine::PresetValue &v : p.values) m_before.push_back(e->presetValue(v.layer, v.path));
    for (auto it = p.geometry.begin(); it != p.geometry.end(); ++it) m_parts[it.key().toULongLong()] |= Engine::PartSpatial;
    for (auto it = p.categories.begin(); it != p.categories.end(); ++it) m_parts[it.key().section(':', 0, 0).toULongLong()] |= it.key().section(':', 1).toInt();
    for (const auto &animation : p.animations) if (!animation.tracks.empty()) {
        const auto &target = animation.tracks.front();
        const quint64 layer = target.layer;
        m_animations[layer] = e->layerAnims(layer);
        QStringList paths = target.drivenParams();
        for (const auto &old : m_animations[layer]) if (!old.tracks.empty())
            if (std::any_of(paths.begin(), paths.end(), [&](const QString &path) { return old.tracks.front().drives(path); }))
                paths += old.tracks.front().drivenParams();
        paths.removeDuplicates();
        for (const QString &path : paths) {
            if (std::none_of(m_before.begin(), m_before.end(), [&](const auto &value) { return value.layer == layer && value.path == path; }))
                m_before.push_back(e->presetValue(layer, path));
            m_animationParams[layer].push_back(path);
        }
    }
    for (const auto &[id, parts] : m_parts) {
        m_layers[id] = e->layerJson(e->indexOfId(id));
        if (parts & (Engine::PartAnimations | Engine::PartSource)) {
            m_animations[id] = e->layerAnims(id);
            m_animationParams.erase(id); // a source/animation category owns the complete list
        }
    }
    m_hasInteractions = !p.interactions.empty();
    if (m_hasInteractions) m_interactions = e->interactions();
    setText(QStringLiteral("Recall Preset \"%1\"").arg(p.name));
}

void RecallPreset::undo()
{
    for (const auto &value : m_before) m_e->cancelParameterRecall(value.layer, value.path);
    for (const auto &[id, state] : m_layers) {
        if (m_parts[id] & Engine::PartSpatial) m_e->cancelParameterRecall(id, QStringLiteral("mesh"));
        const int index = m_e->indexOfId(id);
        if (index >= 0) m_e->applyLayerParts(index, state, m_parts[id]);
    }
    {
        Engine::Lock lock(&m_e->mutex()); // keep output restoration and selective clock merge atomic
        for (const auto &value : m_before)
            if (!value.value.isUndefined()) m_e->setParameter(value.layer, value.path, value.value);
        for (const auto &[id, animations] : m_animations) {
            const auto paths = m_animationParams.find(id);
            if (paths == m_animationParams.end()) {
                m_e->restoreLayerAnims(id, animations);
                continue;
            }
            auto current = m_e->layerAnims(id);
            const auto matches = [&](const Animation &a) {
                return !a.tracks.empty() && std::any_of(paths->second.begin(), paths->second.end(),
                    [&](const QString &path) { return a.tracks.front().drives(path); });
            };
            current.erase(std::remove_if(current.begin(), current.end(), matches), current.end());
            for (size_t i = 0; i < animations.size(); ++i)
                if (matches(animations[i])) current.insert(current.begin() + std::min(i, current.size()), animations[i]);
            m_e->restoreLayerAnims(id, std::move(current));
        }
    }
    if (m_hasInteractions) m_e->setInteractions(m_interactions);
}

void RecallPreset::redo()
{
    const int index = m_e->indexOfPreset(m_preset);
    if (index >= 0) m_e->recallPreset(index);
}

static bool samePresets(const std::vector<Engine::Preset> &a, const std::vector<Engine::Preset> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t k = 0; k < a.size(); ++k)
        if (presetToJson(a[k]) != presetToJson(b[k])) return false;
    return true;
}

SetPresets::SetPresets(Engine *e, std::vector<Engine::Preset> before, std::vector<Engine::Preset> after, const QString &text,
                       const QString &mergeKey)
    : m_e(e), m_before(std::move(before)), m_after(std::move(after)), m_mergeKey(mergeKey), m_time(nowMs())
{
    setText(text);
}

bool SetPresets::mergeWith(const QUndoCommand *other)
{
    auto *o = static_cast<const SetPresets *>(other);
    if (o->m_mergeKey != m_mergeKey || o->m_time - m_time > kMergeWindowMs) return false;
    m_after = o->m_after;
    m_time = o->m_time;
    return true;
}

void editPresets(QUndoStack *undo, Engine *e, const QString &text, const std::function<void(std::vector<Engine::Preset> &)> &fn,
                 const QString &mergeKey)
{
    const std::vector<Engine::Preset> before = e->presets();
    std::vector<Engine::Preset> after = before;
    fn(after);
    if (samePresets(before, after)) return;
    undo->push(new SetPresets(e, before, after, text, mergeKey));
}

SetFixturePatch::SetFixturePatch(Engine *e, FixturePatch before, FixturePatch after, const QString &text, const QString &mergeKey)
    : m_e(e), m_before(std::move(before)), m_after(std::move(after)), m_mergeKey(mergeKey), m_time(nowMs())
{
    setText(text);
}

bool SetFixturePatch::mergeWith(const QUndoCommand *other)
{
    auto *o = static_cast<const SetFixturePatch *>(other);
    if (o->m_mergeKey != m_mergeKey || o->m_time - m_time > kMergeWindowMs) return false;
    m_after = o->m_after;
    m_time = o->m_time;
    return true;
}

void editFixtures(QUndoStack *undo, Engine *e, const QString &text, const std::function<void(FixturePatch &)> &fn,
                  const QString &mergeKey)
{
    FixturePatch before = e->fixturePatch();
    FixturePatch after = before;
    fn(after);
    after.normalize();
    if (after.toJson() == before.toJson()) return;
    if (undo) undo->push(new SetFixturePatch(e, std::move(before), std::move(after), text, mergeKey));
    else e->setFixturePatch(after);
}

namespace {
struct SnapshotLibraryState {
    std::vector<Engine::Snapshot> snapshots;
    std::vector<Engine::SnapshotNode> tree;
    explicit SnapshotLibraryState(Engine *engine) : tree(engine->snapshotTree())
    {
        for (int i = 0; i < engine->snapshotCount(); ++i)
            snapshots.push_back(engine->snapshot(i));
    }
    void restore(Engine *engine) const { engine->setSnapshots(snapshots, tree); }
};
class SnapshotLibraryEdit : public QUndoCommand {
    Engine *engine;
    SnapshotLibraryState before, after;
    bool first = true;

public:
    SnapshotLibraryEdit(Engine *e, SnapshotLibraryState b, SnapshotLibraryState a, const QString &text)
        : engine(e), before(std::move(b)), after(std::move(a))
    {
        setText(text);
    }
    void undo() override { before.restore(engine); }
    void redo() override
    {
        if (first)
            first = false;
        else
            after.restore(engine);
    }
};

} // namespace
void editSnapshotLibrary(QUndoStack *undo, Engine *engine, const QString &text, const std::function<void()> &edit)
{
    if (!undo) {
        edit();
        return;
    }
    SnapshotLibraryState before(engine);
    edit();
    undo->push(new SnapshotLibraryEdit(engine, std::move(before), SnapshotLibraryState(engine), text));
}

} // namespace cmd
