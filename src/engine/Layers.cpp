// Engine: the layers — creation, order and groups, sources (video, image, sound, ISF generator, another
// layer), transport, effect chain, and the copy of parameters from one layer to another.
#include "EngineInternal.h"
#include "Midi.h"
#include "ImageFile.h"
#include "Psd.h"

#include <QFontDatabase>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QColorSpace>
#include "ColorInfo.h"
#include <QJsonArray>
#include <cmath>

QString blendModeName(BlendMode m)
{
    switch (m) {
    case BlendMode::Add: return QStringLiteral("Add");
    case BlendMode::Screen: return QStringLiteral("Screen");
    case BlendMode::Multiply: return QStringLiteral("Multiply");
    case BlendMode::Subtract: return QStringLiteral("Subtract");
    case BlendMode::Difference: return QStringLiteral("Difference");
    default: return QStringLiteral("Normal");
    }
}

QString blendModeKey(BlendMode m)
{
    switch (m) {
    case BlendMode::Add: return "add";
    case BlendMode::Screen: return "screen";
    case BlendMode::Multiply: return "multiply";
    case BlendMode::Subtract: return "subtract";
    case BlendMode::Difference: return "difference";
    default: return "normal";
    }
}

QString playModeName(PlayMode m)
{
    switch (m) {
    case PlayMode::OneShot: return QStringLiteral("One-shot");
    case PlayMode::PingPong: return QStringLiteral("Ping-pong");
    case PlayMode::Stop: return QStringLiteral("Stop");
    default: return QStringLiteral("Loop");
    }
}

QString playModeKey(PlayMode m)
{
    switch (m) {
    case PlayMode::OneShot: return "oneshot";
    case PlayMode::PingPong: return "pingpong";
    case PlayMode::Stop: return "stop";
    default: return "loop";
    }
}

PlayMode playModeFromKey(const QString &k, PlayMode fallback)
{
    if (k == "oneshot") return PlayMode::OneShot;
    if (k == "loop") return PlayMode::Loop;
    if (k == "pingpong") return PlayMode::PingPong;
    if (k == "stop") return PlayMode::Stop;
    return fallback;
}

QString layerTapKey(LayerTap t)
{
    return t == LayerTap::Source ? QStringLiteral("source") : t == LayerTap::PreFx ? QStringLiteral("prefx") : QStringLiteral("postfx");
}

LayerTap layerTapFromKey(const QString &k)
{
    return k == "source" ? LayerTap::Source : k == "prefx" ? LayerTap::PreFx : LayerTap::PostFx;
}

QString layerTapName(LayerTap t)
{
    return t == LayerTap::Source ? QStringLiteral("Source") : t == LayerTap::PreFx ? QStringLiteral("Pre-FX") : QStringLiteral("Post-FX");
}

BlendMode blendModeFromKey(const QString &k)
{
    if (k == "add") return BlendMode::Add;
    if (k == "screen") return BlendMode::Screen;
    if (k == "multiply") return BlendMode::Multiply;
    if (k == "subtract") return BlendMode::Subtract;
    if (k == "difference") return BlendMode::Difference;
    return BlendMode::Normal;
}

void Engine::attachAudio(Layer &l, std::shared_ptr<AudioStream> s)
{
    releaseAudio(*m_audio, l.audio);
    l.audio = std::move(s);
    if (!l.audio) return;
    if (!l.audioEffects)
        l.audioEffects = std::make_shared<AudioEffectChain>();
    l.audio->setEffects(l.audioEffects);
    l.audio->setTransport(l.clock, l.playing, std::abs(l.speed) * timeScale(), l.timeline(), l.timelineId, l.audioGain());
    m_audio->addStream(l.audio);
}

int Engine::layerCount() const
{
    Lock lk(&m_mutex);
    return int(m_layers.size());
}

Layer *Engine::layer(int i)
{
    if (i == kBuildSlot) return t_layerBuild.layer; // the one this thread is making (buildLayer)
    Lock lk(&m_mutex);
    return (i >= 0 && i < int(m_layers.size())) ? m_layers[size_t(i)].get() : nullptr;
}

int Engine::indexOf(const Layer *l) const
{
    Lock lk(&m_mutex);
    for (size_t i = 0; i < m_layers.size(); ++i)
        if (m_layers[i].get() == l) return int(i);
    return -1;
}

std::unique_ptr<Layer> Engine::makeLayerLocked(const QString &name)
{
    auto l = std::make_unique<Layer>();
    l->engine = this;
    l->compSize = &m_compSize;
    l->mapping.aspect = double(m_compSize.width()) / std::max(1, m_compSize.height());
    l->id = newIdLocked();
    l->name = name.isEmpty() ? QStringLiteral("Layer %1").arg(m_layers.size() + 1) : name;
    l->genWidth = m_compSize.width();
    l->genHeight = m_compSize.height();
    return l;
}

int Engine::addLayer(const QString &name, int at)
{
    {
        Lock lk(&m_mutex);
        auto l = makeLayerLocked(name);
        at = std::clamp(at, viewportCountLocked(), int(m_layers.size()));
        // Inserted inside a group's block: the new layer joins that group
        if (at < int(m_layers.size())) l->parent = m_layers[size_t(at)]->parent;
        m_layers.insert(m_layers.begin() + at, std::move(l));
    }
    emit layersChanged();
    return at;
}

// ---------------------------------------------------------------------------
// Viewports
// ---------------------------------------------------------------------------

int Engine::viewportCountLocked() const
{
    int n = 0;
    for (const auto &l : m_layers) n += l->isViewport;
    return n;
}

// A new viewport is placed to the right of the last one, at its size in the composition's pixels
int Engine::addViewport(const QString &name, QSize size)
{
    quint64 id;
    {
        Lock lk(&m_mutex);
        auto l = std::make_unique<Layer>();
        l->compSize = &m_compSize;
        l->mapping.aspect = double(m_compSize.width()) / std::max(1, m_compSize.height());
        l->id = id = newIdLocked();
        l->isViewport = true;
        // By default, the size of the last viewport (the same projectors, side by side)
        QSize px = m_compSize;
        for (const auto &o : m_layers)
            if (o->isViewport) px = o->viewportSize();
        if (size.isValid() && !size.isEmpty()) px = size;
        l->vpWidth = px.width();
        l->vpHeight = px.height();
        const int n = viewportCountLocked();
        l->name = name.isEmpty() ? QStringLiteral("Viewport %1").arg(n + 1) : name;
        l->vpPublish = PublishSettings();
        for (PublishTarget &t : l->vpPublish.targets) t.name = QStringLiteral("NyxMap %1").arg(l->name);
        double x = 0;
        for (const auto &o : m_layers)
            if (o->isViewport) x = std::max(x, o->mapping.bounds().right());
        const double w = double(px.width()) / std::max(1, m_compSize.width());
        const double h = double(px.height()) / std::max(1, m_compSize.height());
        if (x + w > 1.0 + 1e-9) x = 0; // no room left on the right: over the first one
        l->mapping.size = QSizeF(w, h);
        l->mapping.position = QPointF(x + w / 2, h / 2);
        m_layers.push_back(std::move(l));
        normalizeLocked();
    }
    emit layersChanged();
    return indexOfId(id);
}

bool Engine::isViewport(int i) const
{
    Lock lk(&m_mutex);
    return i >= 0 && i < int(m_layers.size()) && m_layers[size_t(i)]->isViewport;
}

QList<int> Engine::viewports() const
{
    Lock lk(&m_mutex);
    QList<int> out;
    for (int i = 0; i < int(m_layers.size()); ++i)
        if (m_layers[size_t(i)]->isViewport) out << i;
    return out;
}

quint64 Engine::mainViewportId() const
{
    Lock lk(&m_mutex);
    for (const auto &l : m_layers)
        if (l->isViewport) return l->id;
    return 0;
}

void Engine::setViewportSize(int i, QSize size)
{
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l || !l->isViewport) return;
        l->vpWidth = std::clamp(size.width(), 1, 16384);
        l->vpHeight = std::clamp(size.height(), 1, 16384);
    }
    emit layersChanged();
}

void Engine::setViewportOutput(int i, const QString &screen, int mode)
{
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l || !l->isViewport) return;
        l->vpScreen = screen;
        l->vpMode = std::clamp(mode, 0, 2);
    }
    emit layersChanged();
}

// Only an item at the top of the list is routed; inside a group, the group decides
void Engine::setOpacityIn(int i, quint64 viewport, float opacity)
{
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l || l->isViewport || l->parent) return;
        opacity = std::clamp(opacity, 0.0f, 1.0f);
        if (l->opacityIn(viewport) == opacity) return;
        if (opacity >= 1.0f) l->viewportOpacity.erase(viewport);
        else l->viewportOpacity[viewport] = opacity;
    }
    emit layersChanged();
}

// A composition always has a viewport: the one the screen shows
void Engine::ensureViewport()
{
    bool has = false;
    {
        Lock lk(&m_mutex);
        has = viewportCountLocked() > 0;
    }
    if (!has) addViewport();
}

int Engine::addGroup(const QString &name, int at)
{
    {
        Lock lk(&m_mutex);
        int groups = 0;
        for (const auto &l : m_layers) groups += l->isGroup;
        auto l = std::make_unique<Layer>();
        l->compSize = &m_compSize;
        l->mapping.aspect = double(m_compSize.width()) / std::max(1, m_compSize.height());
        l->id = newIdLocked();
        l->isGroup = true;
        l->name = name.isEmpty() ? QStringLiteral("Group %1").arg(groups + 1) : name;
        at = std::clamp(at, viewportCountLocked(), int(m_layers.size()));
        // Inside a group's block, the new group goes into that group
        if (at < int(m_layers.size())) l->parent = m_layers[size_t(at)]->parent;
        m_layers.insert(m_layers.begin() + at, std::move(l));
    }
    emit layersChanged();
    return at;
}

quint64 Engine::newIdLocked()
{
    for (const auto &l : m_layers) m_nextId = std::max(m_nextId, l->id + 1);
    return m_nextId++;
}

quint64 Engine::layerId(int i) const
{
    Lock lk(&m_mutex);
    return (i >= 0 && i < int(m_layers.size())) ? m_layers[size_t(i)]->id : 0;
}

int Engine::indexOfId(quint64 id) const
{
    Lock lk(&m_mutex);
    if (!id) return -1;
    for (size_t i = 0; i < m_layers.size(); ++i)
        if (m_layers[i]->id == id) return int(i);
    return -1;
}

// Node of a layer in the structure
static TreeNode nodeOf(const Layer &l)
{
    TreeNode n;
    n.id = l.id;
    n.parent = l.parent;
    n.kind = l.isViewport ? TreeNode::Viewport : l.isGroup ? TreeNode::Group : TreeNode::Item;
    return n;
}

LayerTree Engine::structure() const
{
    Lock lk(&m_mutex);
    LayerTree t;
    t.reserve(m_layers.size());
    for (const auto &l : m_layers) t.push_back(nodeOf(*l));
    return t;
}

void Engine::normalizeLocked()
{
    LayerTree t;
    for (const auto &l : m_layers) t.push_back(nodeOf(*l));
    const LayerTree n = tree::normalized(t);
    if (n == t) return;
    std::vector<std::unique_ptr<Layer>> out;
    out.reserve(m_layers.size());
    for (const TreeNode &node : n) {
        for (auto &l : m_layers) {
            if (l && l->id == node.id) {
                l->parent = node.parent;
                out.push_back(std::move(l));
                break;
            }
        }
    }
    m_layers.swap(out);
}

void Engine::setStructure(const LayerTree &t)
{
    {
        Lock lk(&m_mutex);
        std::vector<std::unique_ptr<Layer>> out;
        out.reserve(m_layers.size());
        for (const TreeNode &node : t) {
            for (auto &l : m_layers) {
                if (l && l->id == node.id) {
                    l->parent = node.parent;
                    out.push_back(std::move(l));
                    break;
                }
            }
        }
        for (auto &l : m_layers) // layers missing from the structure keep their place at the end
            if (l) out.push_back(std::move(l));
        m_layers.swap(out);
        normalizeLocked();
    }
    emit layersChanged();
}

int Engine::groupIndexOf(int i) const
{
    Lock lk(&m_mutex);
    if (i < 0 || i >= int(m_layers.size()) || !m_layers[size_t(i)]->parent) return -1;
    return indexOfId(m_layers[size_t(i)]->parent);
}

QList<int> Engine::groupMembers(int g) const
{
    Lock lk(&m_mutex);
    QList<int> out;
    if (g < 0 || g >= int(m_layers.size()) || !m_layers[size_t(g)]->isGroup) return out;
    const quint64 id = m_layers[size_t(g)]->id;
    for (int i = 0; i < int(m_layers.size()); ++i)
        if (m_layers[size_t(i)]->parent == id) out << i;
    return out;
}

std::vector<LinePath> Engine::layerLines(quint64 id) const
{
    Lock lk(&m_mutex);
    const int i = indexOfId(id);
    return i >= 0 ? m_layers[size_t(i)]->lines : std::vector<LinePath>{};
}

void Engine::setLayerLines(quint64 id, const std::vector<LinePath> &lines)
{
    {
        Lock lk(&m_mutex);
        const int i = indexOfId(id);
        if (i < 0) return;
        Layer &l = *m_layers[size_t(i)];
        if (l.lines == lines) return;
        l.lines = lines;
        ++l.linesRevision;
    }
    emit layerLinesChanged(id);
}

std::vector<Mask> Engine::layerMasks(quint64 id) const
{
    Lock lk(&m_mutex);
    const int i = indexOfId(id);
    return i >= 0 ? m_layers[size_t(i)]->masks : std::vector<Mask>{};
}

void Engine::setLayerMasks(quint64 id, const std::vector<Mask> &m)
{
    Lock lk(&m_mutex);
    const int i = indexOfId(id);
    if (i < 0) return;
    takeOverFades(id, {QStringLiteral("masks")}); // set here: a snapshot's fade no longer moves them
    Layer &l = *m_layers[size_t(i)];
    l.masks = m;
    ++l.masksRevision;
}

QSize Engine::maskFrame(quint64 id) const
{
    Lock lk(&m_mutex);
    const int i = indexOfId(id);
    if (i < 0) return m_compSize;
    const Layer &l = *m_layers[size_t(i)];
    if (l.isViewport) return l.viewportSize();
    if (l.isGroup) return m_compSize;
    if (l.finalW > 0 && l.finalH > 0) return QSize(l.finalW, l.finalH); // its picture, after its ROI
    if (l.sourceWidth() > 0 && l.sourceHeight() > 0) return QSize(l.sourceWidth(), l.sourceHeight());
    return m_compSize;
}

bool Engine::isLocked(int i) const
{
    Lock lk(&m_mutex);
    if (i < 0 || i >= int(m_layers.size())) return false;
    const Layer &l = *m_layers[size_t(i)];
    if (l.locked) return true;
    for (int g = groupIndexOf(i); g >= 0; g = groupIndexOf(g))
        if (m_layers[size_t(g)]->locked) return true;
    return false;
}

void Engine::releaseLayer(Layer &l)
{
    if (l.video) l.video->close();
    l.video.reset();
    l.frame = VideoFrame{};
    if (l.videoTex) l.videoTex->destroy();
    l.videoTex.reset();
    releaseAudio(*m_audio, l.audio);
    l.toxPlayer.reset();
    l.sourceTex.destroy();
    l.imageAutoTex.destroy();
    l.sourceColorTarget.destroy();
    l.textTex.destroy();
    if (l.generator) l.generator->releaseGl();
    l.generator.reset();
    l.generatorTarget.destroy();
    for (auto &fx : l.effects) fx->releaseGl();
    l.effects.clear();
    l.fxTarget[0].destroy();
    l.fxTarget[1].destroy();
    l.maskTarget[0].destroy();
    l.maskTarget[1].destroy();
    l.groupTarget.destroy();
    l.prepTarget.destroy();
    l.matteTarget.destroy();
    l.mattePicture.destroy();
    l.shapeMaskSurface.destroy();
    l.linesTex.destroy();
    l.linesTexRev = 0;
    l.shapeMaskComp.destroy();
    l.shapeMaskTarget.destroy();
    l.shapeMaskSurfaceRev = l.shapeMaskCompRev = 0;
    for (RenderTarget &out : l.vpOut) out.destroy();
    if (l.meshVbo) gl()->glDeleteBuffers(1, &l.meshVbo);
    l.meshVbo = 0;
    l.finalTex = l.rawTex = 0;
}

void Engine::removeLayer(int i)
{
    auto g = std::make_shared<Garbage>();
    {
        Lock lk(&m_mutex);
        if (i < 0 || i >= int(m_layers.size())) return;
        if (m_layers[size_t(i)]->isViewport && viewportCountLocked() <= 1) return; // there is always one
        g->layer = std::move(m_layers[size_t(i)]);
        m_layers.erase(m_layers.begin() + i);
        // A source transition running on it ends with it (the id may come back, undo: not the transition)
        if (auto t = m_transitions.find(g->layer->id); t != m_transitions.end()) {
            retireTransition(std::move(t->second));
            m_transitions.erase(t);
        }
        if (g->layer->isGroup) // its contents go up one level, where they are
            for (auto &l : m_layers)
                if (l->parent == g->layer->id) l->parent = g->layer->parent;
        normalizeLocked();
    }
    if (g->layer->video) g->layer->video->close(); // stop the decode thread outside the render thread
    releaseAudio(*m_audio, g->layer->audio);
    runGl([this, g] { releaseLayer(*g->layer); }, false);
    emit layersChanged();
}

void Engine::moveLayer(int from, int to)
{
    {
        Lock lk(&m_mutex);
        const int n = int(m_layers.size());
        if (from < 0 || from >= n) return;
        to = std::clamp(to, 0, n - 1);
        if (from == to) return;
        auto l = std::move(m_layers[size_t(from)]);
        m_layers.erase(m_layers.begin() + from);
        m_layers.insert(m_layers.begin() + to, std::move(l));
        normalizeLocked();
    }
    emit layersChanged();
}

QJsonObject Engine::layerJson(int i) const
{
    Lock lk(&m_mutex);
    if (i < 0 || i >= int(m_layers.size())) return {};
    return layerToJson(*m_layers[size_t(i)], QString());
}

int Engine::insertLayerJson(int at, const QJsonObject &o)
{
    const int idx = addLayer(QString(), at);
    layerFromJson(idx, o, QString(), nullptr);
    quint64 id;
    {
        Lock lk(&m_mutex);
        id = m_layers[size_t(idx)]->id;
        // A viewport (undo of its deletion) goes back to its place among the viewports
        if (m_layers[size_t(idx)]->isViewport && at >= 0 && at < idx) {
            auto l = std::move(m_layers[size_t(idx)]);
            m_layers.erase(m_layers.begin() + idx);
            m_layers.insert(m_layers.begin() + at, std::move(l));
        }
        normalizeLocked();
    }
    emit layersChanged();
    return indexOfId(id);
}

void Engine::replaceLayerJson(int i, const QJsonObject &o)
{
    quint64 id = 0;
    {
        Lock lk(&m_mutex);
        if (i < 0 || i >= int(m_layers.size())) return;
        id = m_layers[size_t(i)]->id;
    }
    replaceLayerById(id, o);
}

std::unique_ptr<Layer> Engine::buildLayer(const QJsonObject &o, quint64 replaces)
{
    std::unique_ptr<Layer> l;
    {
        Lock lk(&m_mutex);
        l = makeLayerLocked(QStringLiteral("Layer %1").arg(m_layers.size()));
        if (replaces) l->id = replaces; // kept unless the state names another free id (layerFromJson)
    }
    l->transitionGain = 0.0f; // its sound is heard once it is in place
    struct Scope {
        LayerBuild saved = t_layerBuild;
        Scope(Layer *l, quint64 replaces) { t_layerBuild = {l, replaces}; }
        ~Scope() { t_layerBuild = saved; }
    } scope(l.get(), replaces);
    QJsonObject state = o;
    state.remove(QStringLiteral("anims")); // started once it is in place (they act on the layer of their id)
    layerFromJson(kBuildSlot, state, QString(), nullptr);
    return l;
}

void Engine::discardLayer(std::unique_ptr<Layer> l)
{
    if (!l) return;
    auto g = std::make_shared<Garbage>();
    g->layer = std::move(l);
    if (g->layer->video) g->layer->video->close(); // stop the decode thread outside the render thread
    releaseAudio(*m_audio, g->layer->audio);
    runGl([this, g] { releaseLayer(*g->layer); }, false);
}

// The layer made again from a state: built aside (its media opened), then put in the place of the one it replaces in
// one step, so that no frame is drawn without it or with it half made. Its group keeps its members.
void Engine::replaceLayerById(quint64 id, const QJsonObject &o)
{
    if (indexOfId(id) < 0) return;
    std::unique_ptr<Layer> fresh = buildLayer(o, id), old;
    {
        Lock lk(&m_mutex);
        const int i = indexOfId(id);
        if (i >= 0) {
            old = std::move(m_layers[size_t(i)]);
            // A source transition running on it ends with it (as when it is removed)
            if (auto t = m_transitions.find(id); t != m_transitions.end()) {
                retireTransition(std::move(t->second));
                m_transitions.erase(t);
            }
            fresh->transitionGain = 1.0f;
            const quint64 nid = fresh->id;
            m_layers[size_t(i)] = std::move(fresh);
            if (nid != id)
                for (auto &l : m_layers)
                    if (l->parent == id) l->parent = nid;
            loadLayerAnims(*m_layers[size_t(i)], o.value("anims").toArray());
            normalizeLocked();
        }
    }
    discardLayer(std::move(fresh)); // its place gone meanwhile
    discardLayer(std::move(old));
    emit layersChanged();
}

// The part of a layer a parameter belongs to (what a paste of some parts carries), by its address
int Engine::partOf(const QString &path)
{
    auto under = [&path](const char *base) { return path.startsWith(QLatin1String(base)); };
    if (under("roi/")) return PartRoi;
    if (path == "color/enable" || path == "temp" || path == "tint" || under("temp/") || under("tint/") || path == "add" ||
        path == "remove" || under("add/") || under("remove/") || path == "mask/invert")
        return PartColor;
    if (path == "rotation" || path == "width" || path == "height" || under("pivot/") || under("position/") ||
        under("scale/") || under("corner/"))
        return PartSpatial;
    if (under("fx/")) return PartEffects;
    if (path == "opacity" || path == "blend_mode" || under("output/")) return PartCompositing;
    if (path == "speed" || path == "play_mode" || path == "in" || path == "out" || path == "play" || path == "restart" ||
        path == "volume" || path == "mute")
        return PartTransport;
    return PartSource; // the generator's, the text's, the timecode's
}

bool Engine::applyLayerParts(int i, const QJsonObject &o, int parts)
{
    if (i < 0 || i >= layerCount() || o.isEmpty() || !parts || isLocked(i)) return false;
    bool group = false;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l) return false;
        group = l->isGroup || l->isViewport;
    }
    if (group) parts &= ~PartSource; // a group has no source of its own: it composites its layers
    // The values of those parts, the others as the target has them
    const QJsonObject from = o.value("params").toObject();
    auto merge = [&](QJsonObject params) {
        for (auto it = params.begin(); it != params.end();)
            it = (partOf(it.key()) & parts) ? params.erase(it) : it + 1;
        for (auto it = from.constBegin(); it != from.constEnd(); ++it)
            if (partOf(it.key()) & parts) params.insert(it.key(), it.value());
        return params;
    };

    // With the source, the layer is rebuilt from a merged state (the media is opened again, as when one is
    // loaded); the target keeps its identity and whatever the paste does not carry.
    if (parts & PartSource) {
        {
            Lock lk(&m_mutex);
            if (Layer *l = layer(i)) removeSourceAnimations(*l);
        }
        QJsonObject merged = layerJson(i);
        merged["source"] = o.value("source");
        if ((parts & PartTransport) && o.contains("play")) merged["play"] = o.value("play");
        if (parts & PartColor) merged["mask"] = o.value("mask");
        if (parts & PartSpatial) merged["mesh"] = o.value("mesh");
        if (parts & PartEffects) merged["fx"] = o.value("fx");
        if (parts & PartMasks) {
            merged["masks"] = o.value("masks");
            merged["matte"] = o.value("matte");
        }
        if (parts & PartViewports) merged["viewports"] = o.value("viewports");
        if (parts & PartAnimations) merged["anims"] = o.value("anims");
        merged["params"] = merge(merged.value("params").toObject());
        replaceLayerJson(i, merged);
        fixLayerReferences(); // a pasted layer source must not make the picture feed back
        return true;
    }

    // Everything else is set in place: the media goes on playing.
    if (parts & PartEffects) setEffectsJson(i, o.value("fx").toArray());
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l) return false;
        if (parts & PartColor) l->color.maskLayer = o.value("mask").toString().toULongLong();
        if ((parts & PartSpatial) && o.contains("mesh") && !l->isViewport) l->mapping.setMeshJson(o.value("mesh").toObject());
        if (parts & PartMasks) {
            l->masks = masks::fromJson(o.value("masks").toArray());
            ++l->masksRevision; // drawn again
            l->matte = LayerMatte::fromJson(o.value("matte").toObject());
        }
        if ((parts & PartViewports) && !l->isViewport) {
            l->viewportOpacity.clear();
            const QJsonObject vo = o.value("viewports").toObject();
            for (auto v = vo.begin(); v != vo.end(); ++v)
                if (const quint64 vid = v.key().toULongLong(); vid && indexOfId(vid) >= 0)
                    l->viewportOpacity[vid] = float(std::clamp(v.value().toDouble(1.0), 0.0, 1.0));
        }
        if (parts & PartAnimations) recallLayerAnims(*l, o.value("anims").toArray());
        QJsonObject values;
        for (auto it = from.constBegin(); it != from.constEnd(); ++it)
            if (partOf(it.key()) & parts) values.insert(it.key(), it.value());
        QJsonObject unknown;
        parametersFromJson(l->parameters(), values, &unknown);
    }
    fixLayerReferences();
    emit layersChanged();
    return true;
}

int Engine::duplicateLayer(int i)
{
    std::vector<QJsonObject> states;
    std::map<QString, quint64> copies;
    quint64 original = 0;
    {
        Lock lock(&m_mutex);
        original = layerId(i);
        if (!original) return -1;
        states.push_back(layerJson(i));
        for (quint64 id : tree::descendants(structure(), original)) states.push_back(layerJson(indexOfId(id)));
        // Allocate the entire identity map before restoring any state: references may
        // point forward to siblings as well as backward to a source or mask.
        for (const auto &state : states) copies[state.value("id").toString()] = newIdLocked();
    }
    auto remap = [&](QJsonValue value) -> QJsonValue {
        const auto found = copies.find(value.toString());
        return found == copies.end() ? value : QJsonValue(QString::number(found->second));
    };
    auto remapShader = [&](QJsonObject shader) {
        if (shader.contains("mask")) shader["mask"] = remap(shader.value("mask"));
        QJsonObject images = shader.value("image_layers").toObject();
        for (auto it = images.begin(); it != images.end(); ++it) it.value() = remap(it.value());
        if (!images.isEmpty()) shader["image_layers"] = images;
        return shader;
    };
    int at = i;
    for (size_t k = 0; k < states.size(); ++k) {
        QJsonObject state = states[k];
        state["id"] = remap(state.value("id"));
        if (k == 0) state["name"] = state.value("name").toString() + QStringLiteral(" copy");
        if (state.contains("parent")) state["parent"] = remap(state.value("parent"));
        if (state.contains("mask")) state["mask"] = remap(state.value("mask"));
        if (state.contains("matte")) {
            QJsonObject matte = state.value("matte").toObject();
            matte["layer"] = remap(matte.value("layer"));
            state["matte"] = matte;
        }
        QJsonObject source = remapShader(state.value("source").toObject());
        if (source.value("type") == QLatin1String("layer")) source["layer"] = remap(source.value("layer"));
        if (source.value("type") == QLatin1String("tox")) {
            QJsonObject inputs = source.value("inputs").toObject();
            for (auto it = inputs.begin(); it != inputs.end(); ++it) it.value() = remap(it.value());
            source["inputs"] = inputs;
        }
        state["source"] = source;
        QJsonArray effects;
        for (const auto &effect : state.value("fx").toArray()) effects.append(remapShader(effect.toObject()));
        state["fx"] = effects;
        const int inserted = insertLayerJson(at, state);
        if (inserted < 0) return -1;
        at = inserted + 1;
    }
    fixLayerReferences();
    return indexOfId(copies[QString::number(original)]);
}

QJsonArray Engine::effectsJson(int i) const
{
    Lock lk(&m_mutex);
    QJsonArray a;
    if (i < 0 || i >= int(m_layers.size())) return a;
    for (const auto &e : m_layers[size_t(i)]->effects) a.append(e->save(QString()));
    return a;
}

void Engine::setEffectsJson(int i, const QJsonArray &a)
{
    quint64 id = 0;
    {
        Lock lk(&m_mutex);
        if (i < 0 || i >= int(m_layers.size())) return;
        id = m_layers[size_t(i)]->id;
    }
    setEffectsJsonById(id, a);
}

// The layer is found by its id at every step (a recall from the cue clock may run meanwhile); each effect is loaded
// and given its values before it joins the chain, so that no other thread can free it while it is being set.
void Engine::setEffectsJsonById(quint64 id, const QJsonArray &a)
{
    auto g = std::make_shared<Garbage>();
    std::vector<std::pair<QString, QString>> before; // old prefix and shader path, matched by occurrence
    {
        Lock lk(&m_mutex);
        Layer *l = layer(indexOfId(id));
        if (!l) return;
        for (int fx = 0; fx < int(l->effects.size()); ++fx)
            before.push_back({shaderPrefix(id, fx).chopped(1), l->effects[size_t(fx)]->path()});
        g->effects = std::move(l->effects);
        l->effects.clear();
    }
    runGl([g] { for (auto &e : g->effects) e->releaseGl(); }, false);
    for (const QJsonValue &v : a) {
        const QJsonObject e = v.toObject();
        // An undo, a snapshot: the chain as it was, a broken effect included (its path and values are not lost)
        auto inst = std::make_unique<IsfInstance>();
        IsfInstance *raw = inst.get();
        const QString path = resolvePath(e, QString());
        runGl([raw, path, e] {
            raw->load(path);
            raw->restore(e, QString());
        });
        std::shared_ptr<IsfInstance> unused;
        {
            Lock lk(&m_mutex);
            if (Layer *l = layer(indexOfId(id))) l->effects.push_back(std::move(inst));
            else unused.reset(inst.release()); // the layer is gone
        }
        if (unused) {
            runGl([unused] { unused->releaseGl(); }, false);
            return;
        }
    }
    {
        Lock lk(&m_mutex);
        Layer *l = layer(indexOfId(id));
        if (!l) return;
        std::vector<bool> used(l->effects.size(), false);
        std::vector<std::pair<QString, QString>> targets;
        for (const auto &[prefix, path] : before) {
            QString next;
            for (size_t fx = 0; fx < l->effects.size(); ++fx) if (!used[fx] && l->effects[fx]->path() == path) {
                used[fx] = true;
                next = shaderPrefix(id, int(fx)).chopped(1);
                break;
            }
            targets.push_back({prefix, next});
        }
        remapAnimationTargets(id, targets);
    }
}

std::shared_ptr<Engine::Garbage> Engine::detachSource(Layer &l)
{
    removeSourceAnimations(l);
    auto g = std::make_shared<Garbage>();
    l.extraParams = QJsonObject(); // another source: what its missing file kept is not for this one
    g->video = std::move(l.video);
    g->videoTex = std::move(l.videoTex);
    l.frame = VideoFrame{}; // its upload buffer belongs to the texture that goes
    l.frameUploaded = true;
    g->audio = std::move(l.audio);
    g->tex = l.sourceTex;
    l.sourceTex = Texture2D{};
    g->imageAutoTex = l.imageAutoTex;
    l.imageAutoTex = Texture2D{};
    l.imageProfile = QColorSpace();
    l.imageColorInfo.clear();
    l.sourceColorSpace = QStringLiteral("auto");
    l.pendingAutoImage = QImage();
    l.sourceColorCachedKey.clear();
    g->textTex = l.textTex;
    l.textTex = Texture2D{};
    l.textKey.clear();
    l.text.stopTyping();
    g->generator = std::move(l.generator);
    g->input = std::move(l.input);
    g->webPage = std::move(l.webPage);
    g->toxPlayer = std::move(l.toxPlayer);
    l.tox = ToxSettings();
    g->timecode = std::move(l.tcClock);
    l.timecode.reset();
    l.clockGen.reset();
    g->generatorTarget = l.generatorTarget;
    l.generatorTarget = RenderTarget{};
    l.pendingImage = QImage();
    l.type = SourceType::None;
    l.sourcePath.clear();
    l.psdLayer = -1;
    l.psdLayerName.clear();
    l.videoSource = VideoSource();
    l.error.clear();
    l.srcWidth = l.srcHeight = 0;
    l.missingType = SourceType::None;
    l.sourceLayer = 0;
    l.finalTex = 0;
    l.preFxTex = 0;
    return g;
}

// Releases a detached source: decode thread stopped here, GL resources in the render thread.
void Engine::releaseGarbage(const std::shared_ptr<Garbage> &g)
{
    if (g->video) g->video->close();
    releaseAudio(*m_audio, g->audio);
    runGl([g] {
        g->toxPlayer.reset();
        g->tex.destroy();
        g->imageAutoTex.destroy();
        g->textTex.destroy();
        if (g->videoTex) g->videoTex->destroy();
        if (g->generator) g->generator->releaseGl();
        g->generatorTarget.destroy();
    }, false);
}

void Engine::clearLayerSource(int i)
{
    std::shared_ptr<Garbage> g;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l) return;
        g = detachSource(*l);
    }
    releaseGarbage(g);
}

// True when the picture of layer `id` depends on layer `onId`: directly (it is used as its source),
// through a chain of such references, or because `id` is a group and one of its members depends on it.
// The lock must be held.
bool Engine::layerDependsOn(quint64 id, quint64 onId) const
{
    if (!id || !onId) return false;
    std::vector<quint64> seen;
    std::function<bool(quint64)> visit = [&](quint64 cur) -> bool {
        if (cur == onId) return true;
        if (std::find(seen.begin(), seen.end(), cur) != seen.end()) return false;
        seen.push_back(cur);
        for (const auto &l : m_layers) {
            if (l->id != cur) continue;
            if (l->type == SourceType::Layer && l->sourceLayer && visit(l->sourceLayer)) return true;
            if (l->type == SourceType::Tox)
                for (quint64 input : l->tox.inputs) if (input && visit(input)) return true;
            for (const auto &fx : l->effects) // the masks of its effects
                if (fx->maskLayer && visit(fx->maskLayer)) return true;
            if (l->color.maskLayer && visit(l->color.maskLayer)) return true; // the mask of its color
            if (l->matte.layer && visit(l->matte.layer)) return true;         // its matte
            // the layers its shaders' image inputs show
            auto viaImages = [&](const IsfInstance *inst) {
                if (inst)
                    for (const IsfInput &in : inst->inputs())
                        if (in.imageLayer && visit(in.imageLayer)) return true;
                return false;
            };
            if (viaImages(l->generator.get())) return true;
            for (const auto &fx : l->effects)
                if (viaImages(fx.get())) return true;
            if (l->isGroup)
                for (const auto &m : m_layers)
                    if (m->parent == cur && visit(m->id)) return true;
            break;
        }
        return false;
    };
    return visit(id);
}

// After a project is read (or a layer re-created): drops the references to a layer that is not there, and any
// that would make a picture feed back on itself — a project edited by hand must not be able to do that.
void Engine::fixLayerReferences(QStringList *warnings)
{
    Lock lk(&m_mutex);
    for (auto &l : m_layers) {
        if (l->type != SourceType::Layer) continue;
        const bool exists = indexOfId(l->sourceLayer) >= 0;
        if (exists && l->sourceLayer != l->id && !layerDependsOn(l->sourceLayer, l->id)) continue;
        if (warnings)
            *warnings << l->name + (exists ? QStringLiteral(": the layer it used feeds back on it, source dropped.")
                                           : QStringLiteral(": the layer it used is gone, source dropped."));
        l->type = SourceType::None;
        l->sourceLayer = 0;
    }
    for (auto &l : m_layers) {
        if (l->type != SourceType::Tox) continue;
        for (auto it = l->tox.inputs.begin(); it != l->tox.inputs.end(); ++it) {
            if (!it.value()) continue;
            const Layer *src = layer(indexOfId(it.value()));
            if (src && !src->isViewport && src->id != l->id && !layerDependsOn(src->id, l->id)) continue;
            if (warnings) *warnings << l->name + QStringLiteral(": invalid or cyclic TOP input disconnected: ") + it.key();
            it.value() = 0;
        }
        l->applyTox();
    }
    // Masks of the effects: a layer that is gone, a viewport, or one that would feed back
    for (auto &l : m_layers)
        for (auto &fx : l->effects) {
            if (!fx->maskLayer) continue;
            const int mi = indexOfId(fx->maskLayer);
            const bool bad = mi < 0 || m_layers[size_t(mi)]->isViewport || fx->maskLayer == l->id;
            if (!bad && !layerDependsOn(fx->maskLayer, l->id)) continue;
            if (warnings)
                *warnings << l->name + " / " + fx->name() +
                                 (mi < 0 ? QStringLiteral(": its mask layer is gone, mask dropped.")
                                         : QStringLiteral(": its mask would feed back on the layer, mask dropped."));
            fx->maskLayer = 0;
        }
    // Mattes: the same rules
    for (auto &l : m_layers) {
        if (!l->matte.layer) continue;
        const int mi = indexOfId(l->matte.layer);
        const bool bad = l->isViewport || mi < 0 || m_layers[size_t(mi)]->isViewport || l->matte.layer == l->id;
        if (!bad && !layerDependsOn(l->matte.layer, l->id)) continue;
        if (warnings)
            *warnings << l->name + (mi < 0 ? QStringLiteral(": its matte layer is gone, matte dropped.")
                                           : QStringLiteral(": its matte would feed back on it, matte dropped."));
        l->matte = LayerMatte{};
    }
    // Layers shown by the image inputs of its shaders: the same rules
    for (auto &l : m_layers) {
        std::vector<IsfInstance *> shaders;
        if (l->generator) shaders.push_back(l->generator.get());
        for (auto &fx : l->effects) shaders.push_back(fx.get());
        for (IsfInstance *inst : shaders)
            for (IsfInput &in : inst->inputs()) {
                if (!in.imageLayer) continue;
                const int si = indexOfId(in.imageLayer);
                const bool bad = si < 0 || m_layers[size_t(si)]->isViewport || in.imageLayer == l->id;
                if (!bad && !layerDependsOn(in.imageLayer, l->id)) continue;
                if (warnings)
                    *warnings << l->name + " / " + inst->name() +
                                     (si < 0 ? QStringLiteral(": the layer its image showed is gone, image dropped.")
                                             : QStringLiteral(": its image would feed back on the layer, image dropped."));
                in.imageLayer = 0;
                in.extTex = 0;
            }
    }
    // Mask of the color section: the same rules
    for (auto &l : m_layers) {
        if (!l->color.maskLayer) continue;
        const int mi = indexOfId(l->color.maskLayer);
        const bool bad = mi < 0 || m_layers[size_t(mi)]->isViewport || l->color.maskLayer == l->id;
        if (!bad && !layerDependsOn(l->color.maskLayer, l->id)) continue;
        if (warnings)
            *warnings << l->name + (mi < 0 ? QStringLiteral(": the mask of its color is gone, mask dropped.")
                                           : QStringLiteral(": the mask of its color would feed back on it, mask dropped."));
        l->color.maskLayer = 0;
    }
}

QJsonObject LayerMatte::toJson() const
{
    if (!layer) return {};
    return {{"layer", QString::number(layer)}, // ids are strings: JSON numbers are doubles
            {"channel", channel == Channel::Alpha ? "alpha" : "luma"},
            {"invert", invert},
            {"space", space == Space::Picture ? "picture" : "composition"}};
}

LayerMatte LayerMatte::fromJson(const QJsonObject &o)
{
    LayerMatte m;
    m.layer = o.value("layer").toString().toULongLong();
    m.channel = o.value("channel").toString() == QLatin1String("alpha") ? Channel::Alpha : Channel::Luma;
    m.invert = o.value("invert").toBool(false);
    m.space = o.value("space").toString() == QLatin1String("picture") ? Space::Picture : Space::Composition;
    return m;
}

bool Engine::setLayerMatte(int layerIndex, const LayerMatte &matte, QString *err)
{
    auto fail = [err](const QString &m) {
        if (err) *err = m;
        return false;
    };
    {
        Lock lk(&m_mutex);
        Layer *l = layer(layerIndex);
        if (!l) return false;
        if (l->isViewport && matte.layer) return fail(QStringLiteral("A viewport has no matte: use a mask."));
        if (matte.layer) {
            const int mi = indexOfId(matte.layer);
            if (mi < 0) return fail(QStringLiteral("That layer no longer exists."));
            if (m_layers[size_t(mi)]->isViewport) return fail(QStringLiteral("A viewport cannot be a matte."));
            if (matte.layer == l->id) return fail(QStringLiteral("A layer cannot be its own matte."));
            if (layerDependsOn(matte.layer, l->id))
                return fail(QStringLiteral("\"%1\" already uses this layer: the picture would feed back on itself.")
                                .arg(m_layers[size_t(mi)]->name));
        }
        l->matte = matte; // its targets are released by the render thread (prepareMatte, applyPictureMatte)
    }
    emit layersChanged();
    return true;
}

bool Engine::setColorMask(int layerIndex, quint64 maskId, bool invert, QString *err)
{
    auto fail = [err](const QString &m) {
        if (err) *err = m;
        return false;
    };
    {
        Lock lk(&m_mutex);
        Layer *l = layer(layerIndex);
        if (!l) return false;
        if (maskId) {
            const int mi = indexOfId(maskId);
            if (mi < 0) return fail(QStringLiteral("That layer no longer exists."));
            if (m_layers[size_t(mi)]->isViewport) return fail(QStringLiteral("A viewport cannot be a mask."));
            if (maskId == l->id) return fail(QStringLiteral("A layer cannot mask its own color."));
            if (layerDependsOn(maskId, l->id))
                return fail(QStringLiteral("\"%1\" already uses this layer: the picture would feed back on itself.")
                                .arg(m_layers[size_t(mi)]->name));
        }
        l->color.maskLayer = maskId;
        l->color.maskInvert = invert;
    }
    emit layersChanged();
    return true;
}

bool Engine::setEffectMaskTap(int layerIndex, int effect, bool preFx)
{
    {
        Lock lk(&m_mutex);
        Layer *l = layer(layerIndex);
        if (!l || effect < 0 || effect >= int(l->effects.size())) return false;
        l->effects[size_t(effect)]->maskPreFx = preFx;
    }
    emit layersChanged();
    return true;
}

bool Engine::setEffectMask(int layerIndex, int effect, quint64 maskId, bool invert, QString *err)
{
    auto fail = [err](const QString &m) {
        if (err) *err = m;
        return false;
    };
    {
        Lock lk(&m_mutex);
        Layer *l = layer(layerIndex);
        if (!l || effect < 0 || effect >= int(l->effects.size())) return false;
        if (maskId) {
            const int mi = indexOfId(maskId);
            if (mi < 0) return fail(QStringLiteral("That layer no longer exists."));
            if (m_layers[size_t(mi)]->isViewport) return fail(QStringLiteral("A viewport cannot be a mask."));
            if (maskId == l->id) return fail(QStringLiteral("A layer cannot mask its own FX."));
            if (layerDependsOn(maskId, l->id))
                return fail(QStringLiteral("\"%1\" already uses this layer: the picture would feed back on itself.")
                                .arg(m_layers[size_t(mi)]->name));
        }
        IsfInstance &fx = *l->effects[size_t(effect)];
        fx.maskLayer = maskId;
        fx.maskInvert = invert;
    }
    emit layersChanged();
    return true;
}

bool Engine::setLayerSourceLayer(int i, quint64 sourceId, LayerTap tap, QString *err)
{
    auto fail = [err](const QString &m) {
        if (err) *err = m;
        return false;
    };
    std::shared_ptr<Garbage> g;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l) return false;
        if (l->isGroup) return fail(QStringLiteral("A group has no source: its picture is the composite of its layers."));
        if (l->isViewport) return fail(QStringLiteral("A viewport has no source: it shows the composition."));
        const int si = indexOfId(sourceId);
        if (si < 0) return fail(QStringLiteral("That layer no longer exists."));
        if (m_layers[size_t(si)]->isViewport) return fail(QStringLiteral("A viewport cannot be used as a source."));
        if (sourceId == l->id) return fail(QStringLiteral("A layer cannot be its own source."));
        if (layerDependsOn(sourceId, l->id))
            return fail(QStringLiteral("\"%1\" already uses this layer: the picture would feed back on itself.")
                            .arg(m_layers[size_t(si)]->name));
        g = detachSource(*l);
        l->type = SourceType::Layer;
        l->sourceLayer = sourceId;
        l->sourceTap = tap;
    }
    releaseGarbage(g);
    emit layersChanged();
    return true;
}

// Tap of a layer already using another layer as its source
bool Engine::setLayerTap(int i, LayerTap tap)
{
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l || l->type != SourceType::Layer) return false;
        if (l->sourceTap == tap) return true;
        l->sourceTap = tap;
    }
    emit layersChanged();
    return true;
}

static bool isDefaultMapping(const Mapping &m) { return m.isIdentity(); }

bool Engine::setLayerVideo(int i, const QString &path, QString *err, const VideoSource &given)
{
    if (!layer(i)) return false;
    VideoSource source = given;
    if (source.live && !isStreamUrl(path)) {
        if (err) *err = QStringLiteral("Not a stream URL (rtsp://, rtmp://, srt://, udp://, rtp://, http(s)://…): ") + path;
        return false;
    }
    if (!source.live && !source.isSequence() && QFileInfo(path).isDir()) { // a folder: its numbered pictures
        SequenceInfo seq;
        if (!detectImageSequence(path, &seq)) {
            if (err)
                *err = QStringLiteral("No image sequence in this folder (numbered pictures: %1).")
                           .arg(sequenceExtensions().join(QStringLiteral(", ")));
            return false;
        }
        source.pattern = seq.pattern;
        source.start = seq.start;
    }
    if (source.isSequence()) source.fps = source.fps > 0 ? std::clamp(source.fps, 1.0, 240.0) : 25.0;
    auto dec = std::make_unique<VideoDecoder>();
    QString e;
    if (!dec->open(path, &e, source)) { // opened outside the lock: may take a while (a stream connects later)
        if (err) *err = e;
        return false;
    }
    std::shared_ptr<AudioStream> sound;
    if (!source.live && !source.isSequence()) { // the file's sound, if it has any (a stream's is not played)
        sound = std::make_shared<AudioStream>();
        if (!sound->open(path, AudioOutput::kSampleRate, nullptr)) sound.reset();
    }
    std::shared_ptr<Garbage> g;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l) return false;
        g = detachSource(*l);
        l->mode = m_defaultPlayMode; // a newly loaded video takes the default mode (preferences)
        l->ended = false;
        l->srcWidth = dec->width();
        l->srcHeight = dec->height();
        l->video = std::move(dec);
        l->type = SourceType::Video;
        l->sourcePath = source.live ? path.trimmed() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        l->videoSource = source;
        if (source.live) l->mode = PlayMode::Loop; // no end to reach
        l->playing = true;
        l->inPoint = 0; // a new media is played whole
        l->outPoint = -1;
        attachAudio(*l, std::move(sound));
        startMedia(*l);
        if (isDefaultMapping(l->mapping) && l->srcHeight > 0)
            l->mapping.fitAspect(double(l->srcWidth) / l->srcHeight, double(m_compSize.width()) / m_compSize.height());
    }
    releaseGarbage(g);
    return true;
}

bool Engine::setLayerStream(int i, const QString &url, bool rtspTcp, QString *err)
{
    VideoSource source;
    source.live = true;
    source.rtspTcp = rtspTcp;
    return setLayerVideo(i, url, err, source);
}

bool Engine::setLayerVideoSource(int i, const VideoSource &source, QString *err)
{
    QString path;
    PlayMode mode = PlayMode::Loop;
    double in = 0, out = -1;
    bool playing = true;
    Mapping mapping;
    {
        Lock lk(&m_mutex);
        const Layer *l = layer(i);
        if (!l || l->type != SourceType::Video || !l->video) return false;
        if (l->videoSource == source) return true;
        if (l->videoSource.live != source.live || l->videoSource.isSequence() != source.isSequence()) return false;
        path = l->sourcePath;
        mode = l->mode;
        in = l->inPoint;
        out = l->outPoint;
        playing = l->playing;
        mapping = l->mapping;
    }
    if (!setLayerVideo(i, path, err, source)) return false;
    // The same media at another rate (or through another transport): the layer keeps its mode, range and mapping
    setLayerPlayMode(i, mode);
    setLayerInOut(i, in, out);
    if (!playing) setLayerPlaying(i, false);
    Lock lk(&m_mutex);
    if (Layer *l = layer(i)) {
        const unsigned rev = l->mapping.revision;
        l->mapping = mapping;
        l->mapping.revision = rev + 1;
    }
    return true;
}

bool Engine::setLayerFile(int i, const QString &path, QString *err)
{
    if (const QString url = streamUrlOfPath(path); !url.isEmpty()) return setLayerStream(i, url, binStreamTcp(url), err);
    if (QFileInfo(path).isDir()) return setLayerVideo(i, path, err); // an image sequence
    if (isToxFile(path)) return setLayerTox(i, path, {}, err);
    if (isImageFile(path)) return setLayerImage(i, path, err);
    if (isAudioFile(path)) return setLayerAudio(i, path, err);
    if (const QString uri = liveInputUriOfPath(path); isAudioInputUri(uri)) return setLayerAudio(i, uri, err); // a sound card's input
    if (isAudioInputUri(path)) return setLayerAudio(i, path, err);
    QString e;
    if (setLayerVideo(i, path, &e)) return true; // video and unknown extensions: FFmpeg decides
    if (AudioStream::probe(path, nullptr) && setLayerAudio(i, path, err)) return true; // sound only
    if (err) *err = e;
    return false;
}

bool Engine::setLayerAudio(int i, const QString &path, QString *err)
{
    if (!layer(i)) return false;
    auto sound = std::make_shared<AudioStream>();
    QString e;
    if (!sound->open(path, AudioOutput::kSampleRate, &e)) {
        if (err) *err = e;
        return false;
    }
    std::shared_ptr<Garbage> g;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l) return false;
        g = detachSource(*l);
        l->type = SourceType::Audio;
        l->sourcePath = isAudioInputUri(path) ? path : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        l->mode = m_defaultPlayMode;
        l->ended = false;
        l->playing = true;
        l->inPoint = 0; // a new media is played whole
        l->outPoint = -1;
        attachAudio(*l, std::move(sound));
        startMedia(*l);
    }
    releaseGarbage(g);
    return true;
}

// A picture read for a layer, outside the lock: ready for its upload, and in sRGB too when its profile needs it
struct DecodedImage {
    QImage image, autoImage;
    QColorSpace profile;
    QString colorInfo;
};

// svgLongSide: an SVG drawing is rendered at least this large (the composition's longer side: crisp)
static bool decodeImage(const QString &path, int psdLayer, DecodedImage &out, QString *err, int svgLongSide = 0)
{
    QString why;
    QImage img = readImageFile(path, psdLayer, &why, svgLongSide);
    if (img.isNull()) {
        if (err) *err = (psdLayer >= 0 || isSvgFile(path)) && !why.isEmpty() ? why : QStringLiteral("Unreadable image: ") + path;
        return false;
    }
    out.profile = img.colorSpace();
    out.colorInfo = imageSourceDepth(img.format()) + QStringLiteral(" source · ") + imageColorDescription(out.profile);
    if (out.profile.isValid() && imageSourceColorKey(out.profile).isEmpty() && !hdrImageColorSpace(out.profile)) {
        // Outside the render lock/thread. Preserve integer precision and alpha;
        // Qt handles ICC matrices, curves and chromatic adaptation here.
        out.autoImage = prepareImageUpload(img.convertToFormat(imageUploadFormat(img)).convertedToColorSpace(QColorSpace::SRgb));
        if (out.autoImage.isNull()) {
            if (err) *err = QStringLiteral("Unable to convert the embedded image profile to sRGB");
            return false;
        }
    }
    out.image = prepareImageUpload(img);
    return true;
}

// The layer of a Photoshop file a layer was showing: at its index while the layer there has its name, else the first
// one with that name; -1 (all of them) with a warning when it is not in the file any more
static int findPsdLayer(const QString &path, int index, const QString &name, QString *found, QString *warning)
{
    if (index < 0 || !isPsdFile(path)) return -1;
    QString why;
    const QVector<PsdLayerInfo> layers = psdLayers(path, &why);
    auto fits = [&](int k) { return k >= 0 && k < layers.size() && !layers[k].group && (name.isEmpty() || layers[k].name == name); };
    int k = fits(index) ? index : -1;
    for (int c = 0; k < 0 && !name.isEmpty() && c < layers.size(); ++c)
        if (fits(c)) k = c;
    if (k < 0) {
        *warning = why.isEmpty() ? QStringLiteral("Photoshop layer \"%1\" not found in %2: all its layers are shown")
                                       .arg(name.isEmpty() ? QString::number(index) : name, QFileInfo(path).fileName())
                                 : why;
        return -1;
    }
    *found = layers[k].name;
    return k;
}

bool Engine::setLayerImage(int i, const QString &path, QString *err, int psdLayer, const QString &psdLayerName)
{
    if (!layer(i)) return false;
    QString name, warning;
    psdLayer = findPsdLayer(path, psdLayer, psdLayerName, &name, &warning);
    DecodedImage img;
    if (psdLayer >= 0 && !decodeImage(path, psdLayer, img, &warning)) psdLayer = -1; // all of them, said so
    const QSize comp = compositionSize();
    if (psdLayer < 0 && !decodeImage(path, -1, img, err, std::max(comp.width(), comp.height()))) return false;
    std::shared_ptr<Garbage> g;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l) return false;
        g = detachSource(*l);
        l->pendingImage = img.image; // uploaded to the GPU by the render thread
        l->imageColorInfo = img.colorInfo;
        l->imageProfile = img.profile;
        l->pendingAutoImage = img.autoImage;
        l->srcWidth = img.image.width();
        l->srcHeight = img.image.height();
        l->type = SourceType::Image;
        l->sourcePath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        l->psdLayer = psdLayer;
        l->psdLayerName = psdLayer >= 0 ? name : QString();
        l->error = warning;
        if (isDefaultMapping(l->mapping))
            l->mapping.fitAspect(double(img.image.width()) / img.image.height(), double(m_compSize.width()) / m_compSize.height());
    }
    releaseGarbage(g);
    return true;
}

bool Engine::setLayerPsdLayer(int i, int psdLayer, QString *err)
{
    psdLayer = std::max(psdLayer, -1);
    QString path;
    {
        Lock lk(&m_mutex);
        const Layer *l = layer(i);
        if (!l || l->type != SourceType::Image || !isPsdFile(l->sourcePath)) return false;
        if (l->psdLayer == psdLayer && l->error.isEmpty()) return true;
        path = l->sourcePath;
    }
    QString name;
    if (psdLayer >= 0) {
        QString why;
        const QVector<PsdLayerInfo> layers = psdLayers(path, &why);
        if (psdLayer >= layers.size() || layers[psdLayer].group) {
            if (err) *err = why.isEmpty() ? QStringLiteral("This layer is not in the Photoshop file any more") : why;
            return false;
        }
        name = layers[psdLayer].name;
    }
    // Read outside the lock; the picture, of the same file, takes the place of the one shown (its texture reused)
    DecodedImage img;
    if (!decodeImage(path, psdLayer, img, err)) return false;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l || l->type != SourceType::Image || l->sourcePath != path) return false; // another source meanwhile
        l->pendingImage = img.image;
        l->imageColorInfo = img.colorInfo;
        l->imageProfile = img.profile;
        l->pendingAutoImage = img.autoImage;
        l->sourceColorCachedKey.clear(); // its source color conversion is made again
        l->srcWidth = img.image.width();
        l->srcHeight = img.image.height();
        l->psdLayer = psdLayer;
        l->psdLayerName = name;
        l->error.clear();
    }
    emit layersChanged();
    return true;
}

bool Engine::setLayerInput(int i, const QString &uri, QString *err)
{
    if (!layer(i)) return false;
    QString e;
    std::unique_ptr<LiveInput> input = createLiveInput(uri, QString(), &e);
    if (!input) {
        if (err) *err = e;
        return false;
    }
    std::shared_ptr<Garbage> g;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l || l->isGroup || l->isViewport) return false;
        g = detachSource(*l);
        l->input = std::move(input);
        l->type = SourceType::Input;
        l->sourcePath = uri;
    }
    releaseGarbage(g);
    return true;
}

bool Engine::setLayerWeb(int i, const WebSource &settings, QString *err, bool keepWithoutBrowser)
{
    if (!layer(i)) return false;
    WebSource s = settings;
    s.sanitize();
    QString e;
    std::unique_ptr<WebPage> page = createWebPage(s, &e); // it starts loading on the application's thread
    if (!page && !keepWithoutBrowser) {
        if (err) *err = e;
        return false;
    }
    std::shared_ptr<Garbage> g;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l || l->isGroup || l->isViewport) return false;
        g = detachSource(*l);
        l->type = SourceType::Web;
        l->web = s;
        l->webPage = std::move(page);
        l->error = e;
        l->srcWidth = s.width;
        l->srcHeight = s.height;
        if (isDefaultMapping(l->mapping))
            l->mapping.fitAspect(double(s.width) / s.height, double(m_compSize.width()) / m_compSize.height());
    }
    releaseGarbage(g);
    if (err) *err = e;
    return true;
}

void Engine::reloadLayerWeb(int i)
{
    Lock lk(&m_mutex);
    if (Layer *l = layer(i); l && l->webPage) l->webPage->reload();
}

QString Engine::layerWebStatus(int i) const
{
    Lock lk(&m_mutex);
    if (i < 0 || i >= int(m_layers.size())) return {};
    const Layer &l = *m_layers[size_t(i)];
    return l.webPage ? l.webPage->status() : l.error;
}

bool Engine::setLayerIsf(int i, const QString &path, QString *err, IsfFailure onFailure)
{
    if (!layer(i)) return false;
    auto inst = std::make_unique<IsfInstance>();
    IsfInstance *raw = inst.get();
    bool ok = false;
    runGl([raw, path, &ok] { ok = raw->load(path); });
    if (!ok) {
        if (err) *err = raw->error();
        if (onFailure == IsfFailure::Refuse) {
            runGl([raw] { raw->releaseGl(); });
            return false; // leave the previous source and GPU picture untouched
        }
    }
    std::shared_ptr<Garbage> g;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l) {
            // Not waited for under the lock: the render thread may need it before it runs its tasks
            runGl([gone = std::shared_ptr<IsfInstance>(std::move(inst))] { gone->releaseGl(); }, false);
            return false;
        }
        g = detachSource(*l);
        if (!isLineGeneratorPath(path) && !l->lines.empty()) { // its lines are a Line generator's
            l->lines.clear();
            ++l->linesRevision;
        }
        l->type = SourceType::Isf;
        l->sourcePath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        l->error = inst->error();
        l->generator = std::move(inst);
    }
    releaseGarbage(g);
    return ok;
}

void Engine::setGeneratorSize(int i, int w, int h)
{
    Lock lk(&m_mutex);
    if (Layer *l = layer(i)) {
        l->genWidth = std::clamp(w, 1, 16384);
        l->genHeight = std::clamp(h, 1, 16384);
    }
}

void Engine::setLayerPlaying(int i, bool playing)
{
    Lock lk(&m_mutex);
    Layer *l = layer(i);
    if (!l || !l->hasTransport()) return;
    if (playing) resumeAfterStop();
    if (playing && l->atEnd()) startMedia(*l); // played to the end: starts again
    l->playing = playing;
    if (playing) l->ended = false;
}

void Engine::setLayerPlayMode(int i, PlayMode mode)
{
    Lock lk(&m_mutex);
    if (Layer *l = layer(i)) l->setPlayMode(mode);
}

void Engine::setLayerInOut(int i, double in, double out)
{
    Lock lk(&m_mutex);
    if (Layer *l = layer(i)) l->setInOut(in, out);
}

void Engine::setLayerSpeed(int i, double speed)
{
    Lock lk(&m_mutex);
    if (Layer *l = layer(i)) l->setSpeed(speed);
}

void Engine::seekLayer(int i, double t)
{
    Lock lk(&m_mutex);
    Layer *l = layer(i);
    if (!l || !l->hasTransport()) return;
    journal(QStringLiteral("seek"), QStringLiteral("%1 @ %2 s").arg(l->name).arg(t, 0, 'f', 3));
    reposition(*l, t, l->dir);
}

void Engine::setLayerVolume(int i, float volume)
{
    Lock lk(&m_mutex);
    if (Layer *l = layer(i)) l->volume = std::clamp(volume, 0.0f, 2.0f);
}

void Engine::setLayerMuted(int i, bool muted)
{
    Lock lk(&m_mutex);
    if (Layer *l = layer(i)) l->muted = muted;
}

bool Engine::setLayerText(int i)
{
    std::shared_ptr<Garbage> g;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l || l->isGroup || l->isViewport) return false;
        g = detachSource(*l);
        l->type = SourceType::Text;
        l->srcWidth = l->text.width;
        l->srcHeight = l->text.height;
    }
    releaseGarbage(g);
    return true;
}

bool Engine::setLayerTimecode(int i, const TimecodeSettings &s)
{
    bool already = false;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l || l->isGroup || l->isViewport) return false;
        if (s.output && !hasTimecodeOutput(s.output)) return false;
        const int user = s.output ? timecodeOutputUser(s.output) : -1;
        const bool replacing = i == kBuildSlot && t_layerBuild.replaces && user >= 0 &&
                               m_layers[size_t(user)]->id == t_layerBuild.replaces;
        if (user >= 0 && user != i && !replacing) return false; // one layer per output
        already = l->type == SourceType::Text && l->timecode;
        if (already && *l->timecode == s) return true;
    }
    if (!already) { // a text source first, in a style that suits a timecode
        if (!setLayerText(i)) return false;
        Lock lk(&m_mutex);
        if (Layer *l = layer(i)) {
            l->text.font = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
            l->text.size = 220;
            l->text.bold = true;
            l->text.width = 1600;
            l->text.height = 320;
            l->srcWidth = l->text.width;
            l->srcHeight = l->text.height;
        }
    }
    auto clock = std::make_shared<TimecodeClock>(s); // it may open devices: outside the lock
    std::shared_ptr<TimecodeClock> old;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l || l->type != SourceType::Text) return false;
        old = std::move(l->tcClock);
        if (old) clock->takeTransport(*old); // the transport goes on where it was
        l->timecode = s;
        l->tcClock = clock;
    }
    old.reset(); // its devices closed outside the lock too
    emit timecodeRoutingChanged();
    return true;
}

bool Engine::setLayerClock(int i, ClockKind kind)
{
    bool text = false, timecode = false;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l || l->isGroup || l->isViewport) return false;
        if (l->type == SourceType::Text && l->clockGen && l->clockGen->kind() == kind) return true; // as it is
        timecode = l->timecode.has_value();
        text = l->type == SourceType::Text && (l->clockGen || timecode);
    }
    // A text source first, in a style that suits a time (one that showed a time already keeps its own)
    if (!setLayerText(i)) return false;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l) return false;
        if (!text) {
            l->text.font = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
            l->text.size = 220;
            l->text.bold = true;
            l->text.width = 1600;
            l->text.height = 320;
            l->srcWidth = l->text.width;
            l->srcHeight = l->text.height;
        }
        l->clockGen = std::make_shared<ClockGenerator>(kind);
    }
    if (timecode) emit timecodeRoutingChanged(); // the timecode it showed is gone, and the output it drove is free
    return true;
}

std::shared_ptr<ClockGenerator> Engine::layerClock(int i) const
{
    Lock lk(&m_mutex);
    return i >= 0 && i < int(m_layers.size()) ? m_layers[size_t(i)]->clockGen : nullptr;
}

int Engine::timecodeOutputUser(quint64 output) const
{
    Lock lk(&m_mutex);
    if (!output) return -1;
    for (size_t k = 0; k < m_layers.size(); ++k)
        if (m_layers[k]->timecode && m_layers[k]->timecode->output == output) return int(k);
    return -1;
}

// ---------------------------------------------------------------------------
// Timecode outputs and their patch (MIDI outputs, audio channels)
// ---------------------------------------------------------------------------
QString Engine::uniquePatchName(const QString &base, PatchKind kind) const
{
    auto taken = [&](const QString &n) {
        switch (kind) {
        case PatchKind::Timecode: return std::any_of(m_tcOutputs.begin(), m_tcOutputs.end(), [&](const auto &x) { return x.name == n; });
        case PatchKind::Midi: return std::any_of(m_midiOutputs.begin(), m_midiOutputs.end(), [&](const auto &x) { return x.name == n; });
        case PatchKind::Audio: return std::any_of(m_audioOutputs.begin(), m_audioOutputs.end(), [&](const auto &x) { return x.name == n; });
        }
        return false;
    };
    if (!base.isEmpty() && !taken(base)) return base;
    const QString stem = !base.isEmpty()                 ? base
                         : kind == PatchKind::Midi      ? QStringLiteral("MIDI")
                         : kind == PatchKind::Audio     ? QStringLiteral("LTC")
                                                        : QStringLiteral("Timecode");
    for (int k = base.isEmpty() ? 1 : 2;; ++k)
        if (!taken(QStringLiteral("%1 %2").arg(stem).arg(k))) return QStringLiteral("%1 %2").arg(stem).arg(k);
}

bool Engine::routeTargetExists(const QString &route) const
{
    const quint64 id = tcroute::id(route);
    switch (tcroute::kind(route)) {
    case tcroute::Kind::Midi: return std::any_of(m_midiOutputs.begin(), m_midiOutputs.end(), [id](const auto &x) { return x.id == id; });
    case tcroute::Kind::Audio: return std::any_of(m_audioOutputs.begin(), m_audioOutputs.end(), [id](const auto &x) { return x.id == id; });
    case tcroute::Kind::Viewport: {
        const int i = indexOfId(id);
        return i >= 0 && m_layers[size_t(i)]->isViewport;
    }
    case tcroute::Kind::None: break;
    }
    return false;
}

bool Engine::hasTimecodeOutput(quint64 id) const
{
    Lock lk(&m_mutex);
    return std::any_of(m_tcOutputs.begin(), m_tcOutputs.end(), [id](const TimecodeOutput &t) { return t.id == id; });
}

std::vector<TimecodeOutput> Engine::timecodeOutputs() const
{
    Lock lk(&m_mutex);
    return m_tcOutputs;
}

quint64 Engine::addTimecodeOutput(const QString &name)
{
    quint64 id;
    {
        Lock lk(&m_mutex);
        TimecodeOutput o;
        o.id = id = m_nextPatchId++;
        o.name = uniquePatchName(name.trimmed(), PatchKind::Timecode);
        m_tcOutputs.push_back(o);
    }
    emit timecodeRoutingChanged();
    return id;
}

bool Engine::setTimecodeOutput(const TimecodeOutput &o)
{
    {
        Lock lk(&m_mutex);
        auto it = std::find_if(m_tcOutputs.begin(), m_tcOutputs.end(), [&](const TimecodeOutput &t) { return t.id == o.id; });
        if (it == m_tcOutputs.end()) return false;
        TimecodeOutput n = o;
        n.name = n.name.trimmed();
        if (n.name.isEmpty()) n.name = it->name;
        n.routes.removeDuplicates();
        n.routes.removeIf([&](const QString &r) {
            if (!routeTargetExists(r)) return true; // a destination that is not there (yet): left out
            for (const TimecodeOutput &t : m_tcOutputs) // a destination takes one timecode output
                if (t.id != n.id && t.routes.contains(r)) return true;
            return false;
        });
        if (n == *it) return true;
        *it = n;
    }
    emit timecodeRoutingChanged();
    return true;
}

void Engine::removeTimecodeOutput(quint64 id)
{
    const int user = timecodeOutputUser(id);
    if (user >= 0) {
        if (std::optional<TimecodeSettings> s = layerTimecode(user)) {
            s->output = 0;
            setLayerTimecode(user, *s);
        }
    }
    {
        Lock lk(&m_mutex);
        const auto n = m_tcOutputs.size();
        m_tcOutputs.erase(std::remove_if(m_tcOutputs.begin(), m_tcOutputs.end(), [id](const TimecodeOutput &t) { return t.id == id; }),
                          m_tcOutputs.end());
        if (n == m_tcOutputs.size()) return;
    }
    emit timecodeRoutingChanged();
}

QString Engine::routeName(const QString &route) const
{
    Lock lk(&m_mutex);
    const quint64 id = tcroute::id(route);
    switch (tcroute::kind(route)) {
    case tcroute::Kind::Midi:
        for (const MidiOutput &x : m_midiOutputs)
            if (x.id == id) return x.name;
        break;
    case tcroute::Kind::Audio:
        for (const AudioChannelOutput &x : m_audioOutputs)
            if (x.id == id) return x.name;
        break;
    case tcroute::Kind::Viewport: {
        const int i = indexOfId(id);
        if (i >= 0) return m_layers[size_t(i)]->name;
        break;
    }
    case tcroute::Kind::None: break;
    }
    return QString();
}

QString Engine::timecodeOutputStatus(quint64 id) const
{
    Lock lk(&m_mutex);
    auto it = std::find_if(m_tcOutputs.begin(), m_tcOutputs.end(), [&](const TimecodeOutput &t) { return t.id == id; });
    if (it == m_tcOutputs.end()) return QString();
    const int user = timecodeOutputUser(id);
    QString s = user >= 0 ? QStringLiteral("From %1").arg(m_layers[size_t(user)]->name) : QStringLiteral("No layer sends to it");
    QStringList to;
    for (const QString &r : it->routes) {
        const QString name = routeName(r);
        if (!name.isEmpty()) to << QStringLiteral("%1 to %2").arg(tcroute::protocol(tcroute::kind(r)), name);
    }
    return s + QStringLiteral(" · ") + (to.isEmpty() ? QStringLiteral("not patched") : to.join(QStringLiteral(", ")));
}

std::vector<MidiOutput> Engine::midiOutputs() const
{
    Lock lk(&m_mutex);
    return m_midiOutputs;
}

quint64 Engine::addMidiOutput(const QString &name)
{
    quint64 id;
    {
        Lock lk(&m_mutex);
        MidiOutput m;
        m.id = id = m_nextPatchId++;
        m.name = uniquePatchName(name.trimmed(), PatchKind::Midi);
        m_midiOutputs.push_back(m);
    }
    emit timecodeRoutingChanged();
    return id;
}

bool Engine::setMidiOutput(const MidiOutput &o)
{
    {
        Lock lk(&m_mutex);
        auto it = std::find_if(m_midiOutputs.begin(), m_midiOutputs.end(), [&](const MidiOutput &m) { return m.id == o.id; });
        if (it == m_midiOutputs.end()) return false;
        MidiOutput n = o;
        n.name = n.name.trimmed();
        if (n.name.isEmpty()) n.name = it->name;
        if (n == *it) return true;
        *it = n;
    }
    emit timecodeRoutingChanged();
    return true;
}

void Engine::removeMidiOutput(quint64 id)
{
    {
        Lock lk(&m_mutex);
        const auto n = m_midiOutputs.size();
        m_midiOutputs.erase(std::remove_if(m_midiOutputs.begin(), m_midiOutputs.end(), [id](const MidiOutput &m) { return m.id == id; }),
                            m_midiOutputs.end());
        if (n == m_midiOutputs.size()) return;
        for (TimecodeOutput &t : m_tcOutputs) t.routes.removeAll(tcroute::midi(id));
    }
    emit timecodeRoutingChanged();
}

QString Engine::midiOutputStatus(quint64 id) const { return m_tcRouter->midiStatus(id); }

std::vector<AudioChannelOutput> Engine::audioChannelOutputs() const
{
    Lock lk(&m_mutex);
    return m_audioOutputs;
}

quint64 Engine::addAudioChannelOutput(const QString &name)
{
    quint64 id;
    {
        Lock lk(&m_mutex);
        AudioChannelOutput a;
        a.id = id = m_nextPatchId++;
        a.name = uniquePatchName(name.trimmed(), PatchKind::Audio);
        m_audioOutputs.push_back(a);
    }
    emit timecodeRoutingChanged();
    return id;
}

bool Engine::setAudioChannelOutput(const AudioChannelOutput &o)
{
    {
        Lock lk(&m_mutex);
        auto it = std::find_if(m_audioOutputs.begin(), m_audioOutputs.end(), [&](const AudioChannelOutput &a) { return a.id == o.id; });
        if (it == m_audioOutputs.end()) return false;
        AudioChannelOutput n = o;
        n.name = n.name.trimmed();
        if (n.name.isEmpty()) n.name = it->name;
        n.channel = std::clamp(n.channel, 0, 63);
        if (n == *it) return true;
        *it = n;
    }
    emit timecodeRoutingChanged();
    return true;
}

void Engine::removeAudioChannelOutput(quint64 id)
{
    {
        Lock lk(&m_mutex);
        const auto n = m_audioOutputs.size();
        m_audioOutputs.erase(std::remove_if(m_audioOutputs.begin(), m_audioOutputs.end(), [id](const AudioChannelOutput &a) { return a.id == id; }),
                             m_audioOutputs.end());
        if (n == m_audioOutputs.size()) return;
        for (TimecodeOutput &t : m_tcOutputs) t.routes.removeAll(tcroute::audio(id));
    }
    emit timecodeRoutingChanged();
}

QString Engine::audioChannelOutputStatus(quint64 id) const { return m_tcRouter->audioStatus(id); }

QStringList Engine::midiPorts() { return midi::outputNames(); }
QStringList Engine::midiInputPorts() { return midi::inputNames(); }
bool Engine::midiVirtualPorts() { return midi::virtualPorts(); }
QStringList Engine::audioOutputDevices() { return AudioOutput::deviceNames(); }
QStringList Engine::audioInputDevices() { return AudioOutput::captureDeviceNames(); }

void Engine::updateTimecodeRouting()
{
    std::vector<TimecodeRouter::Stream> streams;
    std::vector<MidiOutput> midi;
    std::vector<AudioChannelOutput> audio;
    {
        Lock lk(&m_mutex);
        midi = m_midiOutputs;
        audio = m_audioOutputs;
        m_atc.clear();
        for (const auto &l : m_layers) {
            if (!l->tcClock || !l->timecode || !l->timecode->output) continue;
            for (const TimecodeOutput &t : m_tcOutputs) {
                if (t.id != l->timecode->output) continue;
                TimecodeRouter::Stream s;
                s.output = t.id;
                s.clock = l->tcClock;
                for (const QString &r : t.routes) {
                    const quint64 id = tcroute::id(r);
                    switch (tcroute::kind(r)) {
                    case tcroute::Kind::Midi: s.midi.push_back(id); break;
                    case tcroute::Kind::Audio: s.audio.push_back(id); break;
                    case tcroute::Kind::Viewport: m_atc[id] = l->tcClock; break; // read by Engine::frame
                    case tcroute::Kind::None: break;
                    }
                }
                streams.push_back(std::move(s));
            }
        }
    }
    m_tcRouter->configure(std::move(streams), std::move(midi), std::move(audio));
}

std::shared_ptr<TimecodeClock> Engine::layerTimecodeClock(int i) const
{
    Lock lk(&m_mutex);
    return i >= 0 && i < int(m_layers.size()) ? m_layers[size_t(i)]->tcClock : nullptr;
}

std::optional<TimecodeSettings> Engine::layerTimecode(int i) const
{
    Lock lk(&m_mutex);
    if (i < 0 || i >= int(m_layers.size())) return std::nullopt;
    return m_layers[size_t(i)]->timecode;
}

QString Engine::layerTimecodeStatus(int i) const
{
    std::shared_ptr<TimecodeClock> c;
    {
        Lock lk(&m_mutex);
        if (i >= 0 && i < int(m_layers.size())) c = m_layers[size_t(i)]->tcClock;
    }
    return c ? c->status() : QString();
}

void Engine::restartLayerTimecode(int i)
{
    Lock lk(&m_mutex);
    if (i >= 0 && i < int(m_layers.size()) && m_layers[size_t(i)]->tcClock) m_layers[size_t(i)]->tcClock->restart();
}

void Engine::setLayerTextContent(int i, const QString &text)
{
    Lock lk(&m_mutex);
    if (Layer *l = layer(i)) {
        l->text.content = text;
        l->text.stopTyping(); // typed by hand: no typewriter in progress
    }
}

void Engine::editLayerText(int i, const std::function<void(Layer &)> &edit)
{
    Lock lk(&m_mutex);
    if (Layer *l = layer(i)) edit(*l);
}

int Engine::addEffect(int li, const QString &path, QString *err, IsfFailure onFailure)
{
    if (!layer(li)) return -1;
    auto inst = std::make_unique<IsfInstance>();
    IsfInstance *raw = inst.get();
    bool ok = false;
    runGl([raw, path, &ok] { ok = raw->load(path); });
    if (!ok) {
        if (err) *err = raw->error();
        if (onFailure == IsfFailure::Refuse) {
            runGl([raw] { raw->releaseGl(); });
            return -1; // an operator adding it live: a broken effect never joins the chain
        }
    }
    Lock lk(&m_mutex);
    Layer *l = layer(li);
    if (!l) {
        runGl([raw] { raw->releaseGl(); });
        return -1;
    }
    l->effects.push_back(std::move(inst));
    return int(l->effects.size()) - 1;
}

void Engine::removeEffect(int li, int fx)
{
    std::shared_ptr<IsfInstance> victim;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(li);
        if (!l || fx < 0 || fx >= int(l->effects.size())) return;
        std::vector<QString> before;
        for (int i = 0; i < int(l->effects.size()); ++i) before.push_back(shaderPrefix(l->id, i).chopped(1));
        victim.reset(l->effects[size_t(fx)].release());
        l->effects.erase(l->effects.begin() + fx);
        std::vector<std::pair<QString, QString>> targets;
        for (int i = 0; i < int(before.size()); ++i)
            targets.push_back({before[size_t(i)], i == fx ? QString() : shaderPrefix(l->id, i < fx ? i : i - 1).chopped(1)});
        remapAnimationTargets(l->id, targets);
    }
    runGl([victim] { victim->releaseGl(); }, false);
}

void Engine::moveEffect(int li, int from, int to)
{
    Lock lk(&m_mutex);
    Layer *l = layer(li);
    if (!l) return;
    const int n = int(l->effects.size());
    if (from < 0 || from >= n) return;
    to = std::clamp(to, 0, n - 1);
    if (from == to) return;
    std::vector<std::pair<IsfInstance *, QString>> before;
    for (int i = 0; i < n; ++i) before.push_back({l->effects[size_t(i)].get(), shaderPrefix(l->id, i).chopped(1)});
    auto e = std::move(l->effects[size_t(from)]);
    l->effects.erase(l->effects.begin() + from);
    l->effects.insert(l->effects.begin() + to, std::move(e));
    std::vector<std::pair<QString, QString>> targets;
    for (const auto &[instance, prefix] : before)
        for (int i = 0; i < n; ++i) if (l->effects[size_t(i)].get() == instance)
            targets.push_back({prefix, shaderPrefix(l->id, i).chopped(1)});
    remapAnimationTargets(l->id, targets);
}

bool Engine::ownsIsfLocked(const IsfInstance *inst) const
{
    if (!inst) return false;
    for (const auto &l : m_layers) {
        if (l->generator.get() == inst) return true;
        for (const auto &fx : l->effects)
            if (fx.get() == inst) return true;
    }
    return false;
}

bool Engine::setIsfImageInput(IsfInstance *inst, int input, const QString &path, QString *err)
{
    if (!inst) return false;
    bool ok = false;
    QString e;
    // The GL task takes the lock itself: the instance stays alive and its inputs are not walked meanwhile, and no
    // thread waits for the render thread while holding the lock (the render thread takes it outside its tasks too)
    runGl([&] {
        Lock lk(&m_mutex);
        if (!ownsIsfLocked(inst)) return; // replaced or removed meanwhile
        ok = inst->setImageInput(input, path, &e);
    });
    if (err) *err = e;
    return ok;
}

bool Engine::setIsfImageLayer(IsfInstance *inst, int input, quint64 layerId, QString *err)
{
    auto fail = [err](const QString &m) {
        if (err) *err = m;
        return false;
    };
    {
        Lock lk(&m_mutex);
        if (!inst) return false;
        const Layer *owner = nullptr;
        for (const auto &l : m_layers) {
            if (l->generator.get() == inst) owner = l.get();
            for (const auto &fx : l->effects)
                if (fx.get() == inst) owner = l.get();
        }
        if (!owner) return false; // replaced or removed meanwhile: not to be touched
        if (input < 0 || input >= int(inst->inputs().size())) return false;
        IsfInput &in = inst->inputs()[size_t(input)];
        if (in.type != IsfInput::Image || in.isInputImage) return false;
        if (layerId) {
            const int si = indexOfId(layerId);
            if (si < 0) return fail(QStringLiteral("That layer no longer exists."));
            if (m_layers[size_t(si)]->isViewport) return fail(QStringLiteral("A viewport cannot be an image."));
            if (layerId == owner->id) return fail(QStringLiteral("A layer cannot be its own image."));
            if (layerDependsOn(layerId, owner->id))
                return fail(QStringLiteral("\"%1\" already uses this layer: the picture would feed back on itself.")
                                .arg(m_layers[size_t(si)]->name));
        }
        in.imageLayer = layerId;
        if (!layerId) in.extTex = 0; // back to its file
    }
    emit layersChanged();
    return true;
}

void Engine::setIsfAudioSource(IsfInstance *inst, int input, IsfInput::AudioSource source)
{
    {
        Lock lk(&m_mutex);
        if (!inst || input < 0 || input >= int(inst->inputs().size())) return;
        IsfInput &in = inst->inputs()[size_t(input)];
        if (in.type != IsfInput::Audio && in.type != IsfInput::AudioFFT) return;
        in.audioSource = source;
    }
    emit layersChanged();
}

bool Engine::reloadIsf(IsfInstance *inst)
{
    if (!inst) return false;
    bool ok = false;
    // As setIsfImageInput: the GL task holds the lock, so the instance stays alive and its inputs (swapped by the
    // reload) are not walked by a parameter meanwhile
    runGl([&] {
        Lock lk(&m_mutex);
        if (!ownsIsfLocked(inst)) return; // replaced or removed meanwhile
        // Transactional reload: the previous shader, its uniforms, images
        // and last good output survive any parse/compile failure.
        ok = inst->reloadSafely();
        for (auto &l : m_layers) {
            if (l->generator.get() == inst) {
                l->error = inst->error();
                if (ok) l->paramKey = 0; // changed input defaults/ranges need a new Parameter registry
            }
            for (auto &fx : l->effects)
                if (fx.get() == inst && ok) l->paramKey = 0;
        }
    });
    return ok;
}

int Engine::reloadIsfFile(const QString &path, QString *error)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    int reloaded = 0;
    QStringList errors;
    // The GL task holds the lock (instances kept alive); nothing waits for the render thread while holding it
    runGl([&] {
        Lock lock(&m_mutex);
        for (auto &layer : m_layers) {
            auto reload = [&](IsfInstance *shader) {
                if (!shader || QFileInfo(shader->path()).absoluteFilePath() != absolute) return;
                if (shader->reloadSafely()) { ++reloaded; layer->paramKey = 0; }
                else errors << shader->error();
                if (shader == layer->generator.get()) layer->error = shader->error();
            };
            reload(layer->generator.get());
            for (auto &effect : layer->effects) reload(effect.get());
        }
    });
    if (error) *error = errors.join(QLatin1Char('\n'));
    return reloaded;
}


bool Engine::setLayerTox(int i, const QString &path, const ToxSettings &settings, QString *err)
{
    if (!isToxFile(path)) { if (err) *err = QStringLiteral("Expected a .tox component"); return false; }
    ToxSettings s = settings;
    s.sanitize();
    auto player = createToxPlayer(path, s); // SDK initialization is deferred to the render thread
    if (!player) { if (err) *err = QStringLiteral("Cannot create TouchEngine source"); return false; }
    std::shared_ptr<Garbage> g;
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l || l->isViewport || l->isGroup) return false;
        g = detachSource(*l);
        l->type = SourceType::Tox;
        l->sourcePath = path;
        l->tox = s;
        l->toxPlayer = std::move(player);
        l->error = l->toxPlayer->error();
        if (err) *err = l->error;
    }
    releaseGarbage(g);
    emit layersChanged();
    return true; // keep the component and its settings when its runtime/file is unavailable
}

bool Engine::setLayerToxInput(int i, const QString &input, quint64 source, QString *err)
{
    {
        Lock lk(&m_mutex);
        Layer *l = layer(i);
        if (!l || l->type != SourceType::Tox || !l->toxPlayer) return false;
        const auto inputs = l->toxPlayer->inputs();
        if (std::none_of(inputs.begin(), inputs.end(), [&](const auto &p) { return p.first == input; })) {
            if (err) *err = QStringLiteral("That TOP input is unavailable");
            return false;
        }
        if (source) {
            const Layer *s = layer(indexOfId(source));
            if (!s || s->isViewport || source == l->id || layerDependsOn(source, l->id)) {
                if (err) *err = QStringLiteral("This TOP connection is missing, uses a viewport, or would create a feedback loop");
                return false;
            }
        }
        l->tox.inputs[input] = source;
        l->applyTox();
    }
    emit layersChanged();
    return true;
}
