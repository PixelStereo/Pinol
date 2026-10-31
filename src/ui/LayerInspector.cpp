#include "LayerInspector.h"
#include "AudioOutput.h"
#include "AudioStream.h"
#include "ColorPicker.h"
#include "Commands.h"
#include "DeckLink.h"
#include "Engine.h"
#include "LayerTable.h"
#include "LiveInput.h"
#include "MaskPanel.h"
#include "Midi.h"
#include "ParamAnimPanel.h"
#include "ParamPanel.h"
#include "PresetEditor.h"
#include "Psd.h"
#include "SettingsPanel.h"
#include "SourceColor.h"
#include "ViewportOutput.h"
#include "Widgets.h"
#include <QFileDialog>
#include <QGroupBox>
#include <QTabBar>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QStandardItemModel>
#include <QTabWidget>
#include "FxChainPanel.h"
#include <QUrl>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QDoubleValidator>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <cmath>
#include <memory>

// Copy of a layer's state taken under the lock: widgets are built afterwards without blocking rendering.
struct LayerValues {
    bool valid = false;
    quint64 id = 0;
    bool isGroup = false, isViewport = false, locked = false, lockedByGroup = false;
    int members = 0;
    QSize vpSize;
    // Routing: an item at the top of the list chooses its viewports; inside a group, its top group does
    struct Route {
        quint64 id;
        QString name;
        float opacity; // how much of it the viewport shows
    };
    std::vector<Route> routes;
    QString routedBy; // the top group of an item inside a group
    QRectF roi{0, 0, 1, 1};
    ColorAdjust color;
    bool effectsEnabled = true;
    bool solo = false;
    double aspect = 16.0 / 9.0; // source picture (before roi)
    QString name, sourcePath, error;
    bool enabled = true;
    float opacity = 1;
    BlendMode blend = BlendMode::Normal;
    SourceType type = SourceType::None;
    bool hasVideo = false, playing = false;
    PlayMode mode = PlayMode::Loop;
    int videoW = 0, videoH = 0;
    double fps = 0, duration = 0, speed = 1;
    double inPoint = 0, outPoint = -1;
    QString codec;
    VideoDecoder::Info sourceVideoInfo;
    QString imageColorInfo;
    QString sourceColorSpace, sourceColorStatus;
    QString inputStatus; // a live input's: why there is no picture (empty: fine)
    bool hasAudio = false, muted = false;
    float volume = 1;
    AudioStream::Info audio;
    bool hasGenerator = false;
    int genW = 0, genH = 0;
    double genSpeed = 1;
    int srcW = 0, srcH = 0; // an image's
    int psdLayer = -1;      // the layer of its Photoshop file it shows (-1: all of them)
    VideoSource videoSource; // a video's image sequence or live stream
    // Another layer as the source, and the layers that could be chosen (id, name; cycles left out)
    quint64 sourceLayer = 0;
    LayerTap sourceTap = LayerTap::PostFx;
    QString transition; // when a snapshot changes the source (empty: the default)
    std::vector<std::pair<quint64, QString>> candidates;
    struct Fx {
        QString name, error;
        bool valid = false, enabled = true, masked = false;
    };
    std::vector<Fx> effects;
    bool meshMode = false;
    int cols = 4, rows = 4;
    TextSource text; // Text generator
    std::optional<TimecodeSettings> timecode; // a Timecode generator
    std::optional<ClockSettings> clock;       // a Countdown, Stopwatch or Clock generator
    ToxSettings tox;
    std::vector<ToxControl> toxControls;
    std::vector<std::pair<QString, QString>> toxInputs, toxOutputs;
    QString toxStatus;
    WebSource web;     // Web Page generator
    QString webStatus; // what its page is doing (empty: shown)

    static LayerValues take(Engine *e, int index)
    {
        LayerValues s;
        Engine::Lock lk(&e->mutex());
        Layer *l = e->layer(index);
        if (!l) return s;
        s.valid = true;
        s.id = l->id;
        s.isGroup = l->isGroup;
        s.isViewport = l->isViewport;
        s.vpSize = l->viewportSize();
        s.locked = l->locked;
        s.solo = l->solo;
        s.lockedByGroup = !l->locked && e->isLocked(index);
        s.members = e->groupMembers(index).size();
        s.roi = l->roi;
        s.color = l->color;
        s.effectsEnabled = l->effectsEnabled;
        const QSize comp = e->compositionSize();
        const QSize own = l->isViewport ? l->viewportSize() : l->isGroup ? comp : QSize(l->sourceWidth(), l->sourceHeight());
        const int sw = own.width(), sh = own.height();
        if (sw > 0 && sh > 0) s.aspect = double(sw) / sh;
        s.name = l->name;
        s.sourcePath = l->sourcePath;
        s.error = l->error;
        s.enabled = l->enabled;
        s.opacity = l->opacity;
        s.blend = l->blend;
        s.type = l->type;
        if (l->video) {
            s.hasVideo = true;
            s.videoW = l->video->width();
            s.videoH = l->video->height();
            s.fps = l->video->fps();
            s.codec = l->video->codecName();
            s.sourceVideoInfo = effectiveVideoColorInfo(*l);
            s.duration = l->duration();
        }
        if (l->audio) {
            s.hasAudio = true;
            s.audio = l->audio->info();
            s.duration = l->duration();
        }
        s.volume = l->volume;
        s.muted = l->muted;
        s.playing = l->playing;
        s.mode = l->mode;
        s.speed = l->speed;
        s.inPoint = l->inPoint;
        s.outPoint = l->outPoint;
        s.hasGenerator = l->generator != nullptr;
        s.genW = l->genWidth;
        s.genH = l->genHeight;
        s.genSpeed = l->generator ? l->generator->speed : 1.0;
        s.srcW = l->srcWidth;
        s.srcH = l->srcHeight;
        if (l->input) s.inputStatus = l->input->status();
        s.psdLayer = l->psdLayer;
        s.videoSource = l->videoSource;
        s.imageColorInfo = l->imageColorInfo;
        s.sourceColorSpace = l->sourceColorSpace;
        s.sourceColorStatus = ::sourceColorStatus(*l);
        s.sourceLayer = l->sourceLayer;
        s.sourceTap = l->sourceTap;
        s.transition = l->transition;
        if (!l->isGroup && !l->isViewport)
            for (int k = 0; k < e->layerCount(); ++k) {
                const Layer *o = e->layer(k);
                // Not itself, not a viewport, and not a layer that already depends on this one (the picture would feed back)
                if (!o || o->isViewport || o->id == l->id || e->layerDependsOn(o->id, l->id)) continue;
                s.candidates.push_back({o->id, o->isGroup ? o->name + QStringLiteral(" (group)") : o->name});
            }
        for (const auto &fx : l->effects)
            s.effects.push_back({fx->name(), fx->error(), fx->isValid(), fx->enabled, fx->maskLayer != 0});
        s.meshMode = l->mapping.meshMode;
        s.cols = l->mapping.cols;
        s.rows = l->mapping.rows;
        s.text = l->text;
        s.timecode = l->timecode;
        if (l->clockGen) s.clock = l->clockGen->settings();
        s.tox = l->tox;
        if (l->toxPlayer) {
            s.toxControls = l->toxPlayer->controls();
            s.toxInputs = l->toxPlayer->inputs();
            s.toxOutputs = l->toxPlayer->outputs();
            s.toxStatus = l->toxPlayer->status();
        }
        s.web = l->web;
        s.webStatus = l->webPage ? l->webPage->status() : l->error;
        if (!l->isViewport) {
            const Layer *top = l;
            while (top->parent) {
                const Layer *g = e->layer(e->indexOfId(top->parent));
                if (!g) break;
                top = g;
            }
            if (top != l) s.routedBy = top->name;
            for (int v : e->viewports())
                if (const Layer *vp = e->layer(v)) s.routes.push_back({vp->id, vp->name, top->opacityIn(vp->id)});
        }
        return s;
    }
};

static QString fmtTime(double s)
{
    if (s < 0) s = 0;
    const int m = int(s) / 60;
    const double r = s - m * 60;
    return QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0')).arg(r, 5, 'f', 2, QLatin1Char('0'));
}

// The layers of a Photoshop file, read again only when the file changes (the inspector is rebuilt often)
static QVector<PsdLayerInfo> cachedPsdLayers(const QString &path, QString *err)
{
    struct Entry {
        QDateTime modified;
        qint64 size = -1;
        QVector<PsdLayerInfo> layers;
        QString error;
    };
    static QHash<QString, Entry> cache;
    const QFileInfo info(path);
    Entry &e = cache[path];
    if (e.modified != info.lastModified() || e.size != info.size()) {
        e.modified = info.lastModified();
        e.size = info.size();
        e.error.clear();
        e.layers = psdLayers(path, &e.error);
    }
    if (err) *err = e.error;
    return e.layers;
}

// Empties a layout, the rows nested in it too (their widgets would stay, hidden, with their connections)
static void clearLayout(QLayout *lay)
{
    while (QLayoutItem *item = lay->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->setParent(nullptr);
            w->deleteLater();
        } else if (QLayout *l = item->layout()) {
            clearLayout(l);
        }
        delete item;
    }
}

// Thin rule between the blocks of a panel
static QWidget *separator()
{
    auto *line = new QFrame;
    line->setFrameShape(QFrame::NoFrame);
    line->setStyleSheet("background:#62626e;");
    line->setFixedHeight(1);
    return line;
}

// Thin rule in the interface's accent color
static QWidget *accentSeparator()
{
    auto *line = new QFrame;
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background:%1;").arg(theme::css()));
    return line;
}

static QLabel *errorLabel(const QString &text)
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet("color:#ff6b5b; font-family:monospace; font-size:11px;");
    return l;
}

static QToolButton *toolButton(const QString &text, const QString &tip)
{
    auto *b = new QToolButton;
    b->setText(text);
    b->setToolTip(tip);
    b->setMinimumWidth(28);
    return b;
}

namespace {
// Drop zone of the source (top of the inspector): a file from the Media Bin or the Finder is loaded into the layer.
class DropZone : public QLabel
{
public:
    std::function<void(const QString &)> onDrop; // a file
    std::function<void(int)> onLayer;           // a layer dragged from the list (its row)
    explicit DropZone(const QString &text) : QLabel(text)
    {
        setAcceptDrops(true);
        setAlignment(Qt::AlignCenter);
        setWordWrap(true);
        setMinimumHeight(64);
        setHover(false);
    }
    // Loaded: it stands out (a filled, framed card), empty: a dashed outline; missing: in red
    void setLoaded(bool on, bool missing = false)
    {
        m_loaded = on;
        m_missing = missing;
        setHover(false);
    }

protected:
    void setHover(bool on)
    {
        if (on)
            setStyleSheet(QStringLiteral("QLabel { border:2px dashed %1; border-radius:6px; background:%2;"
                                         " color:%3; padding:8px; }")
                              .arg(theme::css(), theme::css(40), theme::accent().lighter(130).name()));
        else if (m_missing)
            setStyleSheet(QStringLiteral("QLabel { border:2px solid #b4483c; border-radius:6px; background:#3a2422;"
                                         " color:#ffb4a8; padding:8px; }"));
        else if (m_loaded)
            setStyleSheet(QStringLiteral("QLabel { border:2px solid %1; border-radius:6px; background:%2;"
                                         " color:#f4f4f6; padding:8px; }")
                              .arg(theme::css(), theme::css(55)));
        else
            setStyleSheet(QStringLiteral("QLabel { border:2px dashed #55555c; border-radius:6px; "
                                         "color:#9a9aa0; padding:8px; }"));
    }
    bool m_loaded = false, m_missing = false;
    static int layerRow(const QMimeData *m)
    {
        if (!m->hasFormat(kLayerRowsMime)) return -1;
        bool ok = false;
        const int row = m->data(kLayerRowsMime).split(',').value(0).toInt(&ok);
        return ok ? row : -1;
    }
    static QString firstFile(const QMimeData *m)
    {
        for (const QUrl &u : m->urls())
            if (u.isLocalFile()) return u.toLocalFile();
        return {};
    }
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (firstFile(e->mimeData()).isEmpty() && (layerRow(e->mimeData()) < 0 || !onLayer)) return e->ignore();
        setHover(true);
        e->acceptProposedAction();
    }
    void dragLeaveEvent(QDragLeaveEvent *) override { setHover(false); }
    void dropEvent(QDropEvent *e) override
    {
        setHover(false);
        const QString f = firstFile(e->mimeData());
        const int row = layerRow(e->mimeData());
        e->acceptProposedAction();
        if (!f.isEmpty() && onDrop) onDrop(f);
        else if (row >= 0 && onLayer) onLayer(row);
    }
};

QWidget *page(QWidget *content)
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 6, 0, 0);
    v->addWidget(content);
    v->addStretch();
    return w;
}
} // namespace

LayerInspector::LayerInspector(Engine *engine, QUndoStack *undo, QWidget *parent)
    : QWidget(parent), m_engine(engine), m_undo(undo)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(8, 8, 8, 8);
    buildPicker();
    m_multiNote = new QLabel;
    m_multiNote->setObjectName(QStringLiteral("multiSelectionNote"));
    m_multiNote->setWordWrap(true);
    m_multiNote->setStyleSheet("background:#3a3320; color:#f0d890; border-radius:3px; padding:4px 6px;");
    m_multiNote->hide();
    m_layout->addWidget(m_multiNote);
    // A number animated or no longer: the tabs again (the ∿ by its name); an edit of an animation: its card follows
    connect(engine, &Engine::layerAnimsChanged, this, [this](quint64 layer) {
        if (layer != m_layerId) return;
        QStringList now;
        for (const Animation &a : m_engine->layerAnims(layer))
            if (!a.tracks.empty()) now << a.tracks.front().param;
        if (now != m_animated) QMetaObject::invokeMethod(this, &LayerInspector::rebuild, Qt::QueuedConnection);
    });
    rebuild();
}

// The menu at the top of the inspector: every layer, group and viewport of the composition, in the order of the Layers list, a group's
// members indented under it; picking one inspects it (the Layers list and the preview follow). ◀ ▶ step to the one before / after.
void LayerInspector::buildPicker()
{
    auto *row = new QWidget;
    row->setObjectName(QStringLiteral("layerPickerRow"));
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);
    m_pickerPrev = toolButton(QStringLiteral("◀"), QStringLiteral("The layer before"));
    m_pickerNext = toolButton(QStringLiteral("▶"), QStringLiteral("The layer after"));
    m_pickerPrev->setObjectName(QStringLiteral("layerPickerPrev"));
    m_pickerNext->setObjectName(QStringLiteral("layerPickerNext"));
    m_picker = new QComboBox;
    m_picker->setObjectName(QStringLiteral("layerPicker"));
    m_picker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_picker->setMinimumContentsLength(12);
    m_picker->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_picker->setMaxVisibleItems(24);
    m_picker->setToolTip(QStringLiteral("The layer, group or viewport inspected: pick another (the Layers list follows)"));
    m_picker->setProperty("allowLocked", true);
    h->addWidget(m_pickerPrev);
    h->addWidget(m_picker, 1);
    h->addWidget(m_pickerNext);
    m_layout->addWidget(row);
    connect(m_picker, qOverload<int>(&QComboBox::activated), this, [this](int k) {
        const quint64 id = m_picker->itemData(k).toULongLong();
        if (id && id != m_layerId) emit layerPickRequested(id);
    });
    auto step = [this](int delta) {
        const int k = m_picker->currentIndex() + delta;
        if (k < 0 || k >= m_picker->count()) return;
        const quint64 id = m_picker->itemData(k).toULongLong();
        if (id) emit layerPickRequested(id);
    };
    connect(m_pickerPrev, &QToolButton::clicked, this, [step] { step(-1); });
    connect(m_pickerNext, &QToolButton::clicked, this, [step] { step(1); });
    connect(m_engine, &Engine::layersChanged, this, [this] { QMetaObject::invokeMethod(this, &LayerInspector::refreshPicker, Qt::QueuedConnection); });
    refreshPicker();
}

void LayerInspector::refreshPicker()
{
    if (!m_picker) return;
    struct Entry {
        quint64 id;
        QString text;
    };
    std::vector<Entry> entries, viewports;
    QString key = QString::number(m_layerId) + QLatin1Char('|');
    {
        Engine::Lock lk(&m_engine->mutex());
        std::map<quint64, const Layer *> byId;
        for (int i = 0; i < m_engine->layerCount(); ++i)
            if (const Layer *l = m_engine->layer(i)) byId[l->id] = l;
        auto glyph = [](const Layer &l) -> QString {
            if (l.isGroup) return QStringLiteral("▾ ");
            switch (l.type) {
            case SourceType::Video: return QStringLiteral("▶ ");
            case SourceType::Image: return QStringLiteral("▣ ");
            case SourceType::Audio: return QStringLiteral("♪ ");
            case SourceType::Isf: return QStringLiteral("◇ ");
            case SourceType::Text: return QStringLiteral("T ");
            case SourceType::Input: return QStringLiteral("◉ ");
            case SourceType::Layer: return QStringLiteral("↳ ");
            default: return QStringLiteral("· ");
            }
        };
        for (int i = 0; i < m_engine->layerCount(); ++i) {
            const Layer *l = m_engine->layer(i);
            if (!l) continue;
            int depth = 0;
            for (quint64 p = l->parent; p && depth < 8; ++depth) {
                const auto up = byId.find(p);
                p = up == byId.end() ? 0 : up->second->parent;
            }
            const QString indent = QString(depth * 2, QChar(0x2003)); // em spaces: a menu does not trim them
            const Entry e{l->id, indent + (l->isViewport ? QStringLiteral("◫ ") : glyph(*l)) + (l->name.isEmpty() ? QStringLiteral("Untitled") : l->name)};
            (l->isViewport ? viewports : entries).push_back(e);
            key += QStringLiteral("%1:%2:%3;").arg(l->id).arg(depth).arg(e.text);
        }
    }
    entries.insert(entries.end(), viewports.begin(), viewports.end()); // the viewports last
    if (key == m_pickerKey) return;
    m_pickerKey = key;
    const QSignalBlocker block(m_picker);
    m_picker->clear();
    int current = -1;
    for (const Entry &e : entries) {
        if (e.id == m_layerId) current = m_picker->count();
        m_picker->addItem(e.text, QVariant::fromValue<qulonglong>(e.id));
    }
    m_picker->setCurrentIndex(current);
    m_pickerPrev->setEnabled(current > 0);
    m_pickerNext->setEnabled(current >= 0 && current + 1 < m_picker->count());
}

std::pair<double, double> LayerInspector::limitsOf(const QString &path, double lo, double hi) const
{
    ParamInfo p;
    if (!m_engine->parameterInfo(m_layerId, path, &p)) return {lo, hi};
    return {p.lo, p.hi};
}

void LayerInspector::showAnimation(const QString &param)
{
    QStringList now;
    for (const Animation &a : m_engine->layerAnims(m_layerId))
        if (!a.tracks.empty()) now << a.tracks.front().param;
    if (now != m_animated) { // just made: shown once the tabs are built again
        m_revealAnim = param;
        return;
    }
    if (m_tabs && m_animTab >= 0) m_tabs->setCurrentIndex(m_animTab);
    if (m_anims) m_anims->reveal(param);
}

void LayerInspector::setLayer(int index)
{
    if (index != m_layer) {
        m_selectedEffect = 0;
        if (m_maskIndex >= 0) {
            m_maskIndex = -1;
            emit maskSelected(-1);
        }
    }
    m_layer = index;
    rebuild();
    m_pickerKey.clear(); // its current one is the layer shown now
    refreshPicker();
}

void LayerInspector::setCoTargets(const QList<quint64> &ids)
{
    m_selection = ids;
    showSelectionNote();
}

// The others are told apart from the layer shown when they are used: the selection may arrive before the inspector
// shows its new layer
QList<quint64> LayerInspector::coTargets() const
{
    QList<quint64> out;
    for (quint64 id : m_selection)
        if (id && id != m_layerId && !out.contains(id)) out << id;
    return out;
}

void LayerInspector::showSelectionNote()
{
    const QList<quint64> others = coTargets();
    m_multiNote->setText(QStringLiteral("%1 selected: the values changed here are set on all of them.").arg(others.size() + 1));
    m_multiNote->setVisible(!others.isEmpty());
    for (ParamPanel *panel : {m_generatorParams.data(), m_effectParams.data()})
        if (panel) panel->setCoTargets(others);
}

// --- Undoable edits ----------------------------------------------------------

void LayerInspector::setProp(int prop, const QVariant &value)
{
    const auto p = cmd::SetLayerProp::Prop(prop);
    const bool always = p == cmd::SetLayerProp::Enabled || p == cmd::SetLayerProp::Locked; // even on a locked layer
    if (!always && m_engine->isLocked(m_layer)) return;
    // Its name and its color mask are its own; its switch, lock and transition go to the whole selection
    QList<quint64> targets{m_layerId};
    if (p != cmd::SetLayerProp::Name && p != cmd::SetLayerProp::ColorMask)
        for (quint64 id : coTargets())
            if (always || !m_engine->isLocked(m_engine->indexOfId(id))) targets << id;
    std::vector<std::pair<quint64, QVariant>> changes;
    for (quint64 id : targets) {
        const QVariant before = cmd::SetLayerProp::read(m_engine, id, p);
        if (before.isValid() && before != value) changes.push_back({id, before});
    }
    if (changes.empty()) return;
    if (changes.size() > 1) m_undo->beginMacro(cmd::SetLayerProp::textOf(p));
    for (const auto &[id, before] : changes) m_undo->push(new cmd::SetLayerProp(m_engine, id, p, before, value));
    if (changes.size() > 1) m_undo->endMacro();
}

void LayerInspector::setValues(const QJsonObject &values, const QString &text)
{
    if (m_engine->isLocked(m_layer)) return;
    std::vector<std::pair<quint64, QJsonObject>> changes{{m_layerId, values}};
    for (quint64 id : coTargets())
        if (!m_engine->isLocked(m_engine->indexOfId(id))) changes.push_back({id, values});
    cmd::setValues(m_undo, m_engine, changes, text);
}

void LayerInspector::setRoi(const QRectF &r)
{
    const QRectF n = r.normalized() & Layer::fullRoi();
    const QRectF roi = n.isEmpty() ? Layer::fullRoi() : n;
    setValues({{"roi/left", roi.left()}, {"roi/top", roi.top()}, {"roi/right", roi.right()}, {"roi/bottom", roi.bottom()}},
              QStringLiteral("Change ROI"));
}

void LayerInspector::editMapping(const QString &text, const std::function<void(Mapping &)> &fn, bool merge)
{
    if (m_engine->isLocked(m_layer)) return;
    const Mapping before = cmd::SetMapping::read(m_engine, m_layer);
    Mapping after = before;
    fn(after);
    // merge: successive changes of the same field (spin box arrows, typing) form one undo step
    m_undo->push(new cmd::SetMapping(m_engine, m_layer, before, after, text, merge));
    emit mappingChanged();
}

void LayerInspector::editEffects(const QString &text, const std::function<void()> &op)
{
    if (m_engine->isLocked(m_layer)) {
        QMetaObject::invokeMethod(this, &LayerInspector::rebuild, Qt::QueuedConnection); // undo the click on a checkbox
        return;
    }
    const QJsonArray before = m_engine->effectsJson(m_layer);
    const auto beforeAnimations = m_engine->captureAnimationTargets(m_engine->layerId(m_layer));
    op();
    m_undo->push(new cmd::SetEffects(m_engine, m_layer, before, text, beforeAnimations));
}

void LayerInspector::editSource(const QString &text, const std::function<void()> &op)
{
    if (m_engine->isLocked(m_layer)) return;
    const QJsonObject before = m_engine->layerJson(m_layer);
    const auto beforeAnimations = m_engine->captureAnimationTargets(m_engine->layerId(m_layer));
    op();
    m_undo->push(new cmd::ReplaceLayer(m_engine, m_layer, before, text, beforeAnimations));
}

// --- Building ----------------------------------------------------------------

// A Countdown, Stopwatch or Clock in a few words: where it starts, where it goes, which time it tells
static QString clockSummary(const ClockSettings &c)
{
    switch (c.kind) {
    case ClockKind::Countdown:
        return c.target == ClockSettings::Target::TimeOfDay
                   ? QStringLiteral("to %1").arg(c.targetTime.toString(QStringLiteral("HH:mm:ss")))
                   : QStringLiteral("from %1").arg(ClockGenerator::formatDuration(c.duration, QStringLiteral("hh:mm:ss")));
    case ClockKind::Stopwatch: return QStringLiteral("from %1").arg(ClockGenerator::formatDuration(c.offset, QStringLiteral("hh:mm:ss")));
    case ClockKind::Clock: return c.zone.trimmed().isEmpty() ? QStringLiteral("Local time") : c.zone.trimmed();
    }
    return {};
}

// A duration typed: "90", "1:30", "01:00:00", "2:30.5" (a minus before it: below zero)
static std::optional<double> parseClockDuration(QString text)
{
    text = text.trimmed();
    const bool minus = text.startsWith(QLatin1Char('-'));
    if (minus) text.remove(0, 1);
    const QStringList parts = text.split(QLatin1Char(':'));
    if (text.isEmpty() || parts.size() > 3) return std::nullopt;
    double total = 0;
    for (const QString &p : parts) {
        bool ok = false;
        const double x = p.trimmed().toDouble(&ok);
        if (!ok || x < 0 || !std::isfinite(x)) return std::nullopt;
        total = total * 60 + x;
    }
    return minus ? -total : total;
}

static QString layerNameOf(Engine *e, int i)
{
    Engine::Lock lk(&e->mutex());
    const Layer *l = e->layer(i);
    return l ? l->name : QString();
}

void LayerInspector::rebuild()
{
    m_layoutKey = layoutKey();
    m_tcTransport.reset();
    m_clockUi.reset();
    m_sourceColorSpace = nullptr;
    m_sourceColorStatus = nullptr;
    m_streamState = nullptr;
    if (m_content) {
        m_content->hide();
        m_content->deleteLater();
    }
    m_content = new QWidget;
    m_textColors.clear();
    m_toxFields.clear();
    m_toxColors.clear();
    m_toxStatus = nullptr;
    auto *v = new QVBoxLayout(m_content);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);
    m_layout->addWidget(m_content);

    const LayerValues s = LayerValues::take(m_engine, m_layer);
    if (!s.valid) {
        emit kindChanged(QStringLiteral("Layer"));
        auto *empty = new QLabel(QStringLiteral("No layer selected.\n\nCreate a layer with the + button,\n"
                                                "then drop a video, image, sound\nor ISF generator onto it."));
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet("color:#888;");
        v->addWidget(empty);
        v->addStretch();
        return;
    }

    m_layerId = s.id;
    showSelectionNote(); // the layer shown left out of "the others"
    m_locked = s.locked || s.lockedByGroup;
    m_animated.clear();
    for (const Animation &a : m_engine->layerAnims(s.id))
        if (!a.tracks.empty()) m_animated << a.tracks.front().param;
    m_animate = new AnimateMenu(m_engine, m_undo, s.id, [this](const QString &p) { showAnimation(p); }, m_content);
    const QString kind = s.isViewport ? QStringLiteral("Viewport") : s.isGroup ? QStringLiteral("Group") : QStringLiteral("Layer");
    emit kindChanged(kind);
    // Header: visibility, lock, name
    auto *head = new QHBoxLayout;
    auto *name = new QLineEdit(s.name);
    name->setStyleSheet("font-weight:bold; font-size:14px;");
    name->setToolTip(kind + QStringLiteral(" name"));
    auto *vis = new FlagBox(QStringLiteral("Enable"));
    vis->setChecked(s.enabled);
    vis->setProperty("allowLocked", true);
    auto *lock = new QToolButton;
    lock->setCheckable(true);
    lock->setChecked(s.locked);
    lock->setIcon(padlockIcon(s.locked, s.lockedByGroup));
    lock->setIconSize(QSize(18, 18));
    lock->setToolTip(s.lockedByGroup ? QStringLiteral("Locked by its group") : QStringLiteral("Lock: no edit allowed on this layer (Ctrl+L)"));
    lock->setStyleSheet("QToolButton:checked { background:#c62828; border:1px solid #ff6f60; border-radius:3px; }");
    lock->setProperty("allowLocked", true);
    lock->setEnabled(!s.lockedByGroup);
    // Solo: only the soloed layers are drawn (a setup aid: not undone, not saved)
    auto *solo = new QToolButton;
    solo->setText(QStringLiteral("S"));
    solo->setCheckable(true);
    solo->setChecked(s.solo);
    solo->setToolTip(QStringLiteral("Solo: draw only this layer (and the other soloed ones)"));
    solo->setStyleSheet("QToolButton { font-weight:bold; min-width:22px; } QToolButton:checked { background:#5a4a1a; color:#ffd65a; }");
    solo->setProperty("allowLocked", true);
    solo->setVisible(!s.isViewport);
    head->addWidget(vis);
    head->addWidget(lock);
    head->addWidget(solo);
    head->addWidget(name, 1);
    connect(solo, &QToolButton::toggled, this, [this](bool on) {
        {
            Engine::Lock lk(&m_engine->mutex());
            if (Layer *l = m_engine->layer(m_layer)) l->solo = on;
        }
        emit layerChanged();
    });
    v->addLayout(head);
    connect(lock, &QToolButton::toggled, this, [this](bool on) {
        setProp(cmd::SetLayerProp::Locked, on);
        emit layerChanged();
        QMetaObject::invokeMethod(this, &LayerInspector::rebuild, Qt::QueuedConnection);
    });
    if (m_locked) {
        auto *banner = new QLabel(s.lockedByGroup ? QStringLiteral("Locked by its group — unlock the group to edit")
                                                  : QStringLiteral("Locked — unlock (padlock) to edit; the transport stays available"));
        banner->setStyleSheet("background:#3a2422; color:#ffb4a8; padding:4px 8px; border-radius:4px;");
        v->addWidget(banner);
    }
    connect(name, &QLineEdit::textEdited, this, [this](const QString &t) {
        setProp(cmd::SetLayerProp::Name, t);
        emit layerChanged();
    });
    connect(vis, &QCheckBox::toggled, this, [this](bool on) {
        setProp(cmd::SetLayerProp::Enabled, on);
        emit layerChanged();
    });

    // At the top, always shown: its source (media, transport, sound, generator) and how it is composited (opacity,
    // blend, the viewports it appears in)
    auto *top = new QWidget;
    {
        auto *tv = new QVBoxLayout(top);
        tv->setContentsMargins(0, 0, 0, 0);
        tv->setSpacing(8);
        tv->addWidget(buildSource(s));
        if (s.type != SourceType::Audio) { // a sound has no picture to composite
            tv->addWidget(separator());
            tv->addWidget(buildCompositing(s));
        }
        lockInputs(top, m_locked);
    }
    v->addWidget(top);

    // Below, in sub-tabs; the current one is kept from one layer to the next
    auto *tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("layerCategoryTabs"));
    tabs->setDocumentMode(true);
    tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabs->tabBar(), &QWidget::customContextMenuRequested, this, [this, tabs, id = s.id](const QPoint &point) {
        const int tab = tabs->tabBar()->tabAt(point);
        if (tab < 0) return;
        if (PresetEditor *editor = PresetEditor::instance()) {
            QMenu menu(this);
            const QString category = tabs->tabText(tab).section(' ', 0, 0);
            editor->fillCategoryMenu(&menu, id, category);
            if (!menu.isEmpty()) menu.exec(tabs->tabBar()->mapToGlobal(point));
        }
    });
    // Tabs that read as tabs (as the ones above): the current one lighter, underlined with the accent
    tabs->setStyleSheet(theme::tabStyle());
    const bool hasSource = !s.isGroup && !s.isViewport;
    if (hasSource) tabs->addTab(page(buildSourceTab(s)), QStringLiteral("Source"));
    tabs->addTab(page(buildRoi(s)), QStringLiteral("ROI"));
    tabs->addTab(page(buildColor(s)), QStringLiteral("Color"));
    tabs->addTab(page(buildMapping(s)), QStringLiteral("Spatial"));
    {
        m_maskPanel = new MaskPanel(m_engine, m_undo, s.id, s.isViewport, m_locked);
        int count = 0;
        {
            Engine::Lock lk(&m_engine->mutex());
            if (const Layer *l = m_engine->layer(m_layer)) count = int(l->masks.size());
        }
        if (m_maskIndex >= count) m_maskIndex = -1;
        m_maskPanel->select(m_maskIndex);
        connect(m_maskPanel, &MaskPanel::currentChanged, this, [this](int i) {
            m_maskIndex = i;
            emit maskSelected(i);
        });
        connect(m_maskPanel, &MaskPanel::drawRequested, this, &LayerInspector::maskDrawRequested);
        connect(m_maskPanel, &MaskPanel::edited, this, &LayerInspector::mappingChanged);
        const int t = tabs->addTab(page(m_maskPanel), count ? QStringLiteral("Masks (%1)").arg(count) : QStringLiteral("Masks"));
        tabs->setTabToolTip(t, QStringLiteral("Shapes that keep or cut its picture (drawn and edited in the preview)"));
    }
    tabs->addTab(page(buildEffects(s)), QStringLiteral("FX"));
    if (s.hasAudio) { // the sound's own chain, the same mechanism
        const int t = tabs->addTab(page(buildAudioEffects()), QStringLiteral("Audio FX"));
        tabs->setTabToolTip(t, QStringLiteral("Real-time Faust effects on the sound of this layer, before its volume"));
    }
    if (!s.isViewport) tabs->addTab(page(buildViewports(s)), QStringLiteral("Viewports"));
    m_anims = new ParamAnimPanel(m_engine, m_undo, s.id);
    connect(m_anims, &ParamAnimPanel::projectEdited, this, &LayerInspector::projectEdited);
    {
        auto *w = new QWidget; // no stretch below: the cards that are not pinned scroll in the room left
        auto *av = new QVBoxLayout(w);
        av->setContentsMargins(0, 6, 0, 0);
        av->addWidget(m_anims, 1);
        m_animTab = tabs->addTab(w, m_animated.isEmpty() ? QStringLiteral("Anim") : QStringLiteral("Anim (%1)").arg(m_animated.size()));
        tabs->setTabToolTip(m_animTab, QStringLiteral("The animations of this layer's numbers (right-click a parameter › Animate)"));
    }
    m_output = nullptr;
    if (s.isViewport) m_routeFields.clear();
    if (s.isViewport) { // its size, screen and publishing
        m_output = new ViewportOutputPanel(m_engine, s.id, m_undo);
        connect(m_output, &ViewportOutputPanel::edited, this, &LayerInspector::projectEdited);
        connect(m_output, &ViewportOutputPanel::edited, this, &LayerInspector::layerChanged);
        tabs->addTab(page(m_output), QStringLiteral("Output"));
    }
    lockInputs(tabs, m_locked);
    name->setEnabled(!m_locked);
    if (s.type == SourceType::Audio) { // a sound has no picture: no ROI, color, mapping, masks, effects or routing
        for (int t = 0; t < tabs->count(); ++t) {
            const QString name = tabs->tabText(t).section(' ', 0, 0);
            if (name == QLatin1String("Source") || name == QLatin1String("Audio") || name == QLatin1String("Anim")) continue; // transport, volume, speed, the sound's FX
            tabs->setTabEnabled(t, false);
            tabs->setTabVisible(t, false); // not shown at all (kept built: the inspector's fields stay valid)
        }
    }
    m_tabs = tabs;
    // The tab shown last, by its name (the tabs differ from one kind of layer to the next)
    int current = -1;
    for (int t = 0; t < tabs->count(); ++t)
        if (tabs->tabText(t).section(' ', 0, 0) == m_subTab && tabs->isTabEnabled(t)) current = t;
    if (!m_revealAnim.isEmpty() || current < 0) current = m_animTab;
    if (current == m_animTab && m_revealAnim.isEmpty() && m_subTab != QLatin1String("Anim"))
        for (int t = 0; t < tabs->count(); ++t) // its first tab that can be used
            if (tabs->isTabEnabled(t)) {
                current = t;
                break;
            }
    tabs->setCurrentIndex(current);
    connect(tabs, &QTabWidget::currentChanged, this, [this, tabs](int i) {
        if (tabs->isTabEnabled(i)) m_subTab = tabs->tabText(i).section(' ', 0, 0);
    });
    v->addWidget(separator()); // above the tabs
    v->addWidget(tabs, 1);
    if (!m_revealAnim.isEmpty()) {
        m_anims->reveal(m_revealAnim);
        m_revealAnim.clear();
    }
    refreshDynamic();
}

// The picture of the layer at `row` as this one's source (dropped from the list onto the source)
void LayerInspector::useLayer(int row, const std::vector<std::pair<quint64, QString>> &candidates)
{
    const quint64 id = m_engine->layerId(row);
    if (!id || row == m_layer) return;
    const bool allowed = std::any_of(candidates.begin(), candidates.end(), [id](const auto &c) { return c.first == id; });
    if (!allowed) {
        QMessageBox::warning(this, QStringLiteral("Source"),
                             QStringLiteral("This layer cannot be used here: its picture would feed back on itself."));
        return;
    }
    QString err;
    bool ok = false;
    editSource(QStringLiteral("Use Layer as Source"),
               [this, id, &err, &ok] { ok = m_engine->setLayerSourceLayer(m_layer, id, LayerTap::PostFx, &err); });
    if (!ok && !err.isEmpty()) QMessageBox::warning(this, QStringLiteral("Source"), err);
    emit layerChanged();
    rebuild();
}

QWidget *LayerInspector::buildSource(const LayerValues &s)
{
    auto *g = new QWidget;
    auto *v = new QVBoxLayout(g);
    v->setContentsMargins(0, 0, 0, 0);

    if (s.isViewport) {
        auto *info = new QLabel(QStringLiteral("<b>Viewport</b> %1 × %2<br><span style='font-size:11px; color:#999'>"
                                               "A window onto the composition: it shows the region set in Spatial, "
                                               "at its own size (Output). The layers at the top of the list choose "
                                               "the viewports they appear in (below their opacity).</span>")
                                    .arg(s.vpSize.width())
                                    .arg(s.vpSize.height()));
        info->setWordWrap(true);
        v->addWidget(info);
        return g;
    }
    if (s.isGroup) {
        auto *info = new QLabel(QStringLiteral("<b>Group</b> of %1 layer(s)<br><span style='font-size:11px; color:#999'>"
                                               "Its picture is the composite of its layers. Drag layers onto it in the "
                                               "layer list to add them.</span>")
                                    .arg(s.members));
        info->setWordWrap(true);
        v->addWidget(info);
        return g;
    }

    // What feeds it, in the drop zone: its kind, its name and what it is (resolution, rate, length, codec), or that it is
    // empty. A file dropped there (Media Bin, Finder) replaces it; a layer dragged from the list becomes its source.
    QString kind, name, details;
    auto audioText = [](const AudioStream::Info &a) {
        const QString ch = a.channels == 1 ? QStringLiteral("mono") : a.channels == 2 ? QStringLiteral("stereo")
                                                                                        : QStringLiteral("%1 ch").arg(a.channels);
        return QStringLiteral("%1 · %2 kHz · %3").arg(a.codec).arg(a.sampleRate / 1000.0, 0, 'g', 3).arg(ch);
    };
    QStringList d;
    switch (s.type) {
    case SourceType::Video:
        kind = QStringLiteral("Video");
        name = QFileInfo(s.sourcePath).fileName();
        d << QStringLiteral("%1 × %2").arg(s.videoW).arg(s.videoH) << QStringLiteral("%1 fps").arg(s.fps, 0, 'f', 2)
          << fmtTime(s.duration) << s.codec << (QStringLiteral("Color space: ") + s.sourceVideoInfo.colorSpaceSummary())
          << s.sourceVideoInfo.colorDescription()
          << (s.hasAudio ? QStringLiteral("sound: ") + audioText(s.audio) : QStringLiteral("no sound"));
        break;
    case SourceType::Audio:
        kind = AudioGenerator::isRecipe(s.sourcePath) ? QStringLiteral("Audio Generator") : QStringLiteral("Sound");
        name = QFileInfo(s.sourcePath).fileName();
        d << (AudioGenerator::isRecipe(s.sourcePath) ? QStringLiteral("Continuous, real time") : fmtTime(s.duration))
          << audioText(s.audio);
        break;
    case SourceType::Image:
        kind = QStringLiteral("Image");
        name = QFileInfo(s.sourcePath).fileName();
        d << QStringLiteral("%1 × %2").arg(s.srcW).arg(s.srcH)
          << s.imageColorInfo;
        break;
    case SourceType::Isf:
        d << QStringLiteral("Color space: Unknown (shader RGB)");
        kind = isLineGeneratorPath(s.sourcePath) ? QStringLiteral("Generator") : QStringLiteral("ISF Generator");
        name = QFileInfo(s.sourcePath).completeBaseName();
        d << QStringLiteral("%1 × %2").arg(s.genW).arg(s.genH);
        break;
    case SourceType::Input:
        d << QStringLiteral("Color space: Unknown (live input)");
        kind = liveInputKind(s.sourcePath) + QStringLiteral(" Input");
        name = liveInputName(s.sourcePath);
        if (s.srcW > 0) d << QStringLiteral("%1 × %2").arg(s.srcW).arg(s.srcH);
        d << (s.inputStatus.isEmpty() && s.srcW <= 0 ? QStringLiteral("waiting for its first picture") : s.inputStatus);
        break;
    case SourceType::Text:
        d << QStringLiteral("Color space: Unspecified (generated)");
        kind = s.clock ? clockKindName(s.clock->kind) : s.timecode ? QStringLiteral("Timecode") : QStringLiteral("Text");
        name = s.clock      ? clockSummary(*s.clock)
               : s.timecode ? TimecodeSettings::sourceName(s.timecode->source)
                            : s.text.content.section('\n', 0, 0).left(40);
        d << QStringLiteral("%1 × %2").arg(s.text.width).arg(s.text.height);
        break;
    case SourceType::Tox:
        kind = QStringLiteral("TouchDesigner");
        name = QFileInfo(s.sourcePath).fileName();
        d << QStringLiteral("%1 × %2").arg(s.srcW).arg(s.srcH) << s.toxStatus;
        break;
    case SourceType::Web:
        d << QStringLiteral("Color space: sRGB (web page)");
        kind = QStringLiteral("Web Page");
        name = s.web.url.isEmpty() ? QStringLiteral("(blank page)") : s.web.url;
        d << QStringLiteral("%1 × %2 · %3 fps").arg(s.web.width).arg(s.web.height).arg(s.web.fps);
        break;
    case SourceType::Layer: {
        d << QStringLiteral("Color space: Inherited (not tagged)");
        kind = QStringLiteral("Layer");
        name = QStringLiteral("(gone)");
        for (const auto &c : s.candidates)
            if (c.first == s.sourceLayer) name = c.second;
        d << (s.sourceTap == LayerTap::Source ? QStringLiteral("its source, before ROI, color and FX")
              : s.sourceTap == LayerTap::PreFx ? QStringLiteral("before its FX") : QStringLiteral("after its FX"));
        break;
    }
    default:
        if (!s.sourcePath.isEmpty()) { // its file is missing
            kind = QStringLiteral("Missing");
            name = QFileInfo(s.sourcePath).fileName();
        }
        break;
    }
    d.removeAll(QString());
    details = d.join(QStringLiteral(" · "));
    const bool loaded = !kind.isEmpty();
    auto *zoneRow = new QHBoxLayout;
    auto *zone = new DropZone(loaded ? QStringLiteral("<span style='font-size:11px; letter-spacing:1px'>%1</span><br>"
                                                      "<b style='font-size:14px'>%2</b><br>"
                                                      "<span style='font-size:11px'>%3</span>")
                                           .arg(kind.toUpper().toHtmlEscaped(), name.toHtmlEscaped(), details.toHtmlEscaped())
                                     : QStringLiteral("<b>Empty layer</b><br><span style='font-size:11px'>Drop a video, image, "
                                                      "sound or ISF generator here (Media Bin, Finder), or a layer from the list</span>"));
    zone->setTextFormat(Qt::RichText);
    zone->setLoaded(loaded, s.type == SourceType::None && loaded); // a missing file: in red
    zone->setToolTip(s.sourcePath.isEmpty() ? QStringLiteral("Drop a file, or a layer from the list (its picture becomes this one's)")
                                            : s.sourcePath + QStringLiteral("\nDrop another media or a layer to replace it"));
    zone->onDrop = [this](const QString &f) { QTimer::singleShot(0, this, [this, f] { emit fileDropped(f); }); };
    zone->onLayer = [this, candidates = s.candidates](int row) {
        // After the drag is over (the zone is rebuilt with the inspector)
        QTimer::singleShot(0, this, [this, candidates, row] { useLayer(row, candidates); });
    };
    auto *bClear = toolButton(QStringLiteral("×"), QStringLiteral("Eject the source from the layer"));
    bClear->setEnabled(loaded || s.error.size());
    zoneRow->addWidget(zone, 1);
    zoneRow->addWidget(bClear, 0, Qt::AlignTop);
    v->addLayout(zoneRow);
    connect(bClear, &QToolButton::clicked, this, [this] {
        editSource(QStringLiteral("Eject Media"), [this] { m_engine->clearLayerSource(m_layer); });
        emit layerChanged();
        rebuild();
    });
    if (!s.error.isEmpty()) v->addWidget(errorLabel(s.error));

    if (s.type == SourceType::Image || s.type == SourceType::Video || s.type == SourceType::Input ||
        s.type == SourceType::Isf || s.type == SourceType::Text || s.type == SourceType::Web || s.type == SourceType::Tox) {
        auto *row = new QHBoxLayout;
        auto *pick = new QComboBox;
        pick->setObjectName(QStringLiteral("sourceColorSpace"));
        m_sourceColorSpace = pick;
        const auto keys = sourceColorSpaceKeys(), labels = sourceColorSpaceLabels();
        for (int i = 0; i < keys.size(); ++i) pick->addItem(labels[i], keys[i]);
        pick->setCurrentIndex(std::max(0, pick->findData(s.sourceColorSpace)));
        pick->setEnabled(!m_locked);
        pick->setToolTip(QStringLiteral("Interpret the source RGB in this space, then convert to sRGB before Color, FX and compositing. "
                                       "This changes pixels; it does not edit the file's profile. SDR only."));
        auto *label = new ResetLabel(QStringLiteral("Source color space"), [this] {
            setValue("source/color_space", "auto", "Reset Source Color Space");
            rebuild();
        });
        row->addWidget(label);
        row->addWidget(pick, 1);
        v->addLayout(row);
        auto *status = new QLabel(s.sourceColorStatus);
        m_sourceColorStatus = status;
        status->setObjectName(QStringLiteral("sourceColorStatus"));
        status->setWordWrap(true);
        status->setTextFormat(Qt::PlainText);
        v->addWidget(status);
        connect(pick, &QComboBox::activated, this, [this, pick](int i) {
            setValue("source/color_space", pick->itemData(i).toString(), "Change Source Color Space");
            refreshDynamic();
            emit layerChanged();
        });
    }

    // Transition when a snapshot gives this layer another source (a sound: its levels cross, nothing to choose)
    if (s.type != SourceType::Audio) {
        auto *row = new QHBoxLayout;
        auto *label = new ResetLabel(QStringLiteral("Transition"), [this] {
            setProp(cmd::SetLayerProp::Transition, QString());
            rebuild();
        });
        auto *pick = new QComboBox;
        const QString def = m_engine->defaultTransition();
        pick->addItem(QStringLiteral("Default (%1)").arg(def.isEmpty() ? QStringLiteral("Crossfade")
                                                                      : QFileInfo(def).completeBaseName()),
                      QString());
        for (const IsfEntry &t : m_engine->library().transitions()) {
            pick->addItem(t.name, t.path);
            pick->setItemData(pick->count() - 1, t.description, Qt::ToolTipRole);
        }
        int k = pick->findData(s.transition);
        if (k < 0 && !s.transition.isEmpty()) { // not in the library (any more): kept, shown by its name
            pick->addItem(QFileInfo(s.transition).completeBaseName(), s.transition);
            k = pick->count() - 1;
        }
        pick->setCurrentIndex(std::max(0, k));
        pick->setToolTip(QStringLiteral("When a snapshot gives this layer another source, the outgoing one keeps playing and "
                                        "this ISF transition takes it to the new one, over the snapshot's fade "
                                        "(or the time the snapshot gives the source)"));
        row->addWidget(label);
        row->addWidget(pick, 1);
        v->addLayout(row);
        connect(pick, &QComboBox::activated, this,
                [this, pick](int i) { setProp(cmd::SetLayerProp::Transition, pick->itemData(i).toString()); });
    }


    // Its speed: the media's (below 0 backwards), or the pace of the generator's time; a click on its name puts it back
    if ((s.type == SourceType::Video && s.hasVideo) || (s.type == SourceType::Audio && s.hasAudio)) {
        auto *row = new QHBoxLayout;
        m_speed = new SliderField;
        m_speed->setRange(-200, 200);      // the bar: the speeds actually used
        m_speed->setTypedRange(-800, 800); // faster or more backwards: typed
        m_speed->setDecimals(0);
        m_speed->setSuffix(QStringLiteral(" %"));
        m_speed->setSingleStep(5);
        m_speed->setOrigin(0); // the fill grows either side of a standstill
        m_speed->setTicks(8);
        m_speed->setSnaps({-200, -100, 0, 100, 200});
        m_speed->setValue(s.speed * 100);
        m_speed->setToolTip(QStringLiteral("Playback speed — below 0 the media plays backwards, 100 % is its own rate"));
        auto *label = new ResetLabel(QStringLiteral("Speed"), [this] {
            setValue(QStringLiteral("speed"), 1.0, QStringLiteral("Change Speed"));
            m_speed->setValue(100);
        });
        m_animate->attach({label, m_speed}, {QStringLiteral("speed")});
        row->addWidget(label);
        row->addWidget(m_speed, 1);
        v->addLayout(row);
        connect(m_speed, &SliderField::valueEdited, this, [this](double pct) {
            setValue(QStringLiteral("speed"), pct / 100.0, QStringLiteral("Change Speed"));
            emit layerChanged();
        });
    } else if (s.type == SourceType::Isf && s.hasGenerator) {
        auto *row = new QHBoxLayout;
        m_genSpeed = new SliderField;
        m_genSpeed->setRange(0, 10);
        m_genSpeed->setDecimals(2);
        m_genSpeed->setSingleStep(0.05);
        m_genSpeed->setTicks(10);
        m_genSpeed->setSnaps({1});
        m_genSpeed->setValue(s.genSpeed);
        m_genSpeed->setToolTip(QStringLiteral("Speed of the shader's time (1 = normal, 0 = stopped)"));
        auto setSpeed = [this](double v) { setValue(QStringLiteral("speed"), v, QStringLiteral("Change Speed")); };
        auto *label = new ResetLabel(QStringLiteral("Speed"), [this, setSpeed] {
            m_genSpeed->setValue(1);
            setSpeed(1);
        });
        m_animate->attach({label, m_genSpeed}, {QStringLiteral("speed")});
        row->addWidget(label);
        row->addWidget(m_genSpeed, 1);
        v->addLayout(row);
        connect(m_genSpeed, &SliderField::valueEdited, this, setSpeed);
    }
    return g;
}

// The Source tab: what the source is made of and how it plays — the media's transport and sound, the generator's
// parameters and resolution, the text and its style, the tap of a layer used as the source
QWidget *LayerInspector::buildSourceTab(const LayerValues &s)
{
    auto *g = new QWidget;
    auto *v = new QVBoxLayout(g);
    v->setContentsMargins(0, 0, 0, 0);

    // Another layer's picture as this one's source, already made this frame (nothing decoded, generated or computed
    // again): which layer, and where in it — its source as it is, before its FX, after them
    {
        auto *form = new QFormLayout;
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *pick = new QComboBox;
        pick->setObjectName(QStringLiteral("sourceLayer"));
        pick->addItem(s.type == SourceType::Layer ? QStringLiteral("(choose a layer)") : QStringLiteral("None (its own source)"),
                      QVariant::fromValue<qulonglong>(0));
        for (const auto &c : s.candidates) pick->addItem(c.second, QVariant::fromValue<qulonglong>(c.first));
        if (s.type == SourceType::Layer && s.sourceLayer && pick->findData(QVariant::fromValue<qulonglong>(s.sourceLayer)) < 0)
            pick->addItem(QStringLiteral("(layer gone)"), QVariant::fromValue<qulonglong>(s.sourceLayer));
        pick->setCurrentIndex(s.type == SourceType::Layer
                                  ? std::max(0, pick->findData(QVariant::fromValue<qulonglong>(s.sourceLayer))) : 0);
        pick->setEnabled(!m_locked && !s.candidates.empty());
        pick->setToolTip(QStringLiteral("Use another layer's picture as this one's source: the texture it already made "
                                        "this frame, without reading its file or running its shader again.\n"
                                        "Layers that use this one are not offered (the picture would feed back on itself)."));
        form->addRow(QStringLiteral("From layer"), pick);
        const LayerTap currentTap = s.type == SourceType::Layer ? s.sourceTap : LayerTap::PostFx;
        connect(pick, &QComboBox::activated, this, [this, pick, currentTap, candidates = s.candidates](int k) {
            const quint64 id = pick->itemData(k).toULongLong();
            if (!id || !std::any_of(candidates.begin(), candidates.end(), [id](const auto &c) { return c.first == id; })) return;
            QString err;
            bool ok = false;
            editSource(QStringLiteral("Use Layer as Source"),
                       [this, id, currentTap, &err, &ok] { ok = m_engine->setLayerSourceLayer(m_layer, id, currentTap, &err); });
            if (!ok && !err.isEmpty()) QMessageBox::warning(this, QStringLiteral("Source"), err);
            emit layerChanged();
            QMetaObject::invokeMethod(this, &LayerInspector::rebuild, Qt::QueuedConnection);
        });
        if (s.type == SourceType::Layer) {
            auto *tap = new QComboBox;
            tap->setObjectName(QStringLiteral("sourceTap"));
            for (LayerTap t : {LayerTap::Source, LayerTap::PreFx, LayerTap::PostFx}) tap->addItem(layerTapName(t), int(t));
            tap->setCurrentIndex(std::max(0, tap->findData(int(s.sourceTap))));
            tap->setEnabled(!m_locked);
            tap->setToolTip(QStringLiteral("Where the picture is taken in that layer:\n"
                                           "Source — its source as decoded or generated, before its ROI, color and FX\n"
                                           "Pre-FX — after its ROI and color, before its FX\n"
                                           "Post-FX — after its FX (its own mapping and opacity are never part of it)"));
            auto setTap = [this](LayerTap t) {
                editSource(QStringLiteral("Change Source Tap"), [this, t] { m_engine->setLayerTap(m_layer, t); });
                emit layerChanged();
                QMetaObject::invokeMethod(this, &LayerInspector::rebuild, Qt::QueuedConnection);
            };
            form->addRow(new ResetLabel(QStringLiteral("Taken"), [setTap] { setTap(LayerTap::PostFx); }), tap);
            connect(tap, &QComboBox::activated, this, [tap, setTap](int i) { setTap(LayerTap(tap->itemData(i).toInt())); });
        }
        v->addLayout(form);
        if (s.type == SourceType::Layer) return g;
    }

    // A Photoshop file: all its layers, or one of them alone at its place on the canvas (groups are not a choice).
    // Listed from the top, as Photoshop lists them.
    if (s.type == SourceType::Image && isPsdFile(s.sourcePath)) {
        QString why;
        const QVector<PsdLayerInfo> layers = cachedPsdLayers(s.sourcePath, &why);
        auto *row = new QHBoxLayout;
        auto *pick = new QComboBox;
        pick->setObjectName(QStringLiteral("psdLayer"));
        pick->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        pick->addItem(QStringLiteral("All layers"), -1);
        for (int k = int(layers.size()) - 1; k >= 0; --k)
            if (!layers[k].group)
                pick->addItem(layers[k].path + (layers[k].visible ? QString() : QStringLiteral(" (hidden)")), k);
        pick->setCurrentIndex(std::max(0, pick->findData(s.psdLayer)));
        pick->setEnabled(!m_locked && pick->count() > 1); // a flat file: nothing else to choose
        pick->setToolTip(!why.isEmpty() ? why
                         : QStringLiteral("All layers: the picture as Photoshop shows it (the file's merged image)\n"
                                          "A layer: that layer alone, at its place on the canvas, transparent elsewhere"));
        auto apply = [this, id = s.id](int chosen) {
            if (m_layerId != id || m_engine->isLocked(m_layer)) return; // rebuilt for another layer meanwhile
            const QJsonObject before = m_engine->layerJson(m_layer);
            const auto beforeAnimations = m_engine->captureAnimationTargets(id);
            QString err;
            if (!m_engine->setLayerPsdLayer(m_layer, chosen, &err)) {
                if (!err.isEmpty()) QMessageBox::warning(this, QStringLiteral("PSD layer"), err);
            } else if (m_engine->layerJson(m_layer) != before) {
                m_undo->push(new cmd::ReplaceLayer(m_engine, m_layer, before, QStringLiteral("Change PSD Layer"), beforeAnimations));
            }
            emit layerChanged();
            QMetaObject::invokeMethod(this, &LayerInspector::rebuild, Qt::QueuedConnection);
        };
        row->addWidget(new ResetLabel(QStringLiteral("PSD layer"), [apply] { apply(-1); }));
        row->addWidget(pick, 1);
        v->addLayout(row);
        // The inspector may be rebuilt while the list is open
        connect(pick, &QComboBox::activated, this, [apply, guard = QPointer<QComboBox>(pick)](int k) {
            if (guard) apply(guard->itemData(k).toInt());
        });
    }

    // A video source opened again with other settings (a sequence's rate, a stream's transport), as one undo step
    auto reopen = [this, id = s.id](const VideoSource &source, const QString &label) {
        if (m_layerId != id || m_engine->isLocked(m_layer)) return; // rebuilt for another layer meanwhile
        const QJsonObject before = m_engine->layerJson(m_layer);
        const auto beforeAnimations = m_engine->captureAnimationTargets(id);
        QString err;
        if (!m_engine->setLayerVideoSource(m_layer, source, &err)) {
            if (!err.isEmpty()) QMessageBox::warning(this, label, err);
        } else if (m_engine->layerJson(m_layer) != before) {
            m_undo->push(new cmd::ReplaceLayer(m_engine, m_layer, before, label, beforeAnimations));
        }
        emit layerChanged();
        QMetaObject::invokeMethod(this, &LayerInspector::rebuild, Qt::QueuedConnection);
    };

    // An image sequence: the rate its pictures are played at (a sequence has none of its own)
    if (s.type == SourceType::Video && s.videoSource.isSequence()) {
        auto *row = new QHBoxLayout;
        auto *rate = new QComboBox;
        rate->setObjectName(QStringLiteral("sequenceFps"));
        rate->setEditable(true);
        rate->setInsertPolicy(QComboBox::NoInsert);
        for (double f : {23.976, 24.0, 25.0, 29.97, 30.0, 48.0, 50.0, 59.94, 60.0})
            rate->addItem(QString::number(f, 'g', 5), f);
        rate->setValidator(new QDoubleValidator(1, 240, 3, rate));
        const int k = rate->findData(s.videoSource.fps);
        if (k >= 0) rate->setCurrentIndex(k);
        else rate->setEditText(QString::number(s.videoSource.fps, 'g', 5));
        rate->setEnabled(!m_locked);
        rate->setToolTip(QStringLiteral("Frames per second of the image sequence %1 (from %2). A gap in the numbering "
                                        "ends the clip.")
                             .arg(s.videoSource.pattern)
                             .arg(s.videoSource.start));
        auto apply = [reopen, source = s.videoSource](double fps) {
            if (fps < 1 || fps > 240) return;
            VideoSource v = source;
            v.fps = fps;
            reopen(v, QStringLiteral("Change Sequence Frame Rate"));
        };
        row->addWidget(new ResetLabel(QStringLiteral("Frame rate"), [apply] { apply(25); }));
        row->addWidget(rate, 1);
        row->addWidget(new QLabel(QStringLiteral("fps")));
        v->addLayout(row);
        connect(rate, &QComboBox::activated, this, [apply, guard = QPointer<QComboBox>(rate)](int i) {
            if (guard) apply(guard->itemData(i).toDouble());
        });
        connect(rate->lineEdit(), &QLineEdit::editingFinished, this, [apply, guard = QPointer<QComboBox>(rate)] {
            bool ok = false;
            const double f = guard ? guard->currentText().toDouble(&ok) : 0;
            if (ok) apply(f);
        });
    }

    // A live stream: its URL, its state, RTSP's transport
    if (s.type == SourceType::Video && s.videoSource.live) {
        auto *form = new QFormLayout;
        form->setContentsMargins(0, 2, 0, 2);
        auto *url = new QLabel(s.sourcePath);
        url->setObjectName(QStringLiteral("streamUrl"));
        url->setTextInteractionFlags(Qt::TextSelectableByMouse);
        url->setWordWrap(true);
        form->addRow(QStringLiteral("Stream"), url);
        m_streamState = new QLabel(QStringLiteral("…"));
        m_streamState->setObjectName(QStringLiteral("streamState"));
        m_streamState->setWordWrap(true);
        m_streamState->setToolTip(QStringLiteral("Played live: no duration, no seeking, the newest picture shown. When "
                                                 "the stream drops or stalls (%1 s without data) it is connected again, "
                                                 "after a pause that grows to 10 s.")
                                      .arg(VideoDecoder::kLiveTimeoutUs / 1000000));
        form->addRow(QStringLiteral("State"), m_streamState);
        v->addLayout(form);
        if (s.sourcePath.startsWith(QLatin1String("rtsp"), Qt::CaseInsensitive)) {
            auto *tcp = new QCheckBox(QStringLiteral("TCP for RTSP"));
            tcp->setObjectName(QStringLiteral("streamTcp"));
            tcp->setChecked(s.videoSource.rtspTcp);
            tcp->setEnabled(!m_locked);
            tcp->setToolTip(QStringLiteral("RTSP over TCP: through firewalls and without lost packets. Off: UDP, lower "
                                           "latency on a clean network. The stream connects again."));
            connect(tcp, &QCheckBox::toggled, this, [reopen, source = s.videoSource](bool on) {
                VideoSource v = source;
                v.rtspTcp = on;
                reopen(v, QStringLiteral("Change RTSP Transport"));
            });
            v->addWidget(tcp);
        }
    }

    if (s.type == SourceType::Tox) {
        auto *box = new QGroupBox(QStringLiteral("TouchDesigner"));
        auto *form = new QFormLayout(box);
        auto *requirements = new QLabel(touchEngineRequirements());
        requirements->setWordWrap(true);
        requirements->setTextFormat(Qt::PlainText);
        form->addRow(requirements);
        m_toxStatus = new QLabel(s.toxStatus);
        m_toxStatus->setWordWrap(true);
        m_toxStatus->setTextFormat(Qt::PlainText);
        form->addRow(QStringLiteral("Status"), m_toxStatus);
        auto *output = new QComboBox;
        output->setObjectName(QStringLiteral("toxOutput"));
        output->addItem(QStringLiteral("First output TOP"), QString());
        for (const auto &top : s.toxOutputs) output->addItem(top.second, top.first);
        if (!s.tox.output.isEmpty() && output->findData(s.tox.output) < 0)
            output->addItem(s.tox.output + QStringLiteral(" (unavailable)"), s.tox.output);
        output->setCurrentIndex(std::max(0, output->findData(s.tox.output)));
        form->addRow(QStringLiteral("Output TOP"), output);
        m_toxFields.push_back({output, QStringLiteral("tox/output")});
        connect(output, &QComboBox::activated, this, [this, output](int i) {
            setValue(QStringLiteral("tox/output"), output->itemData(i).toString(), QStringLiteral("Change Output TOP"));
        });
        auto *fps = new NumberBox;
        fps->setRange(1, 120); fps->setValue(s.tox.fps); fps->setSuffix(QStringLiteral(" fps"));
        form->addRow(QStringLiteral("Frame rate"), fps);
        m_toxFields.push_back({fps, QStringLiteral("tox/fps")});
        connect(fps, &QDoubleSpinBox::editingFinished, this, [this, fps] {
            setValue(QStringLiteral("tox/fps"), fps->value(), QStringLiteral("Change TouchDesigner Frame Rate"));
        });
        auto *play = new QCheckBox(QStringLiteral("Play"));
        play->setChecked(s.tox.playing);
        m_toxFields.push_back({play, QStringLiteral("tox/play")});
        connect(play, &QCheckBox::toggled, this, [this](bool on) { setValue(QStringLiteral("tox/play"), on, QStringLiteral("TouchDesigner Playback")); });
        auto *reload = new QPushButton(QStringLiteral("Reload component"));
        connect(reload, &QPushButton::clicked, this, [this] {
            Engine::Lock lock(&m_engine->mutex());
            if (Layer *l = m_engine->layer(m_engine->indexOfId(m_layerId)); l && l->toxPlayer) l->toxPlayer->reload();
        });
        auto *transport = new QHBoxLayout;
        transport->addWidget(play); transport->addWidget(reload);
        form->addRow(transport);
        for (const auto &top : s.toxInputs) {
            auto *pick = new QComboBox;
            pick->setObjectName(QStringLiteral("toxInput_") + top.first);
            pick->addItem(QStringLiteral("Disconnected"), QVariant::fromValue<qulonglong>(0));
            for (const auto &candidate : s.candidates) pick->addItem(candidate.second, QVariant::fromValue<qulonglong>(candidate.first));
            const quint64 current = s.tox.inputs.value(top.first);
            if (current && pick->findData(QVariant::fromValue<qulonglong>(current)) < 0)
                pick->addItem(QStringLiteral("Unavailable layer (%1)").arg(current), QVariant::fromValue<qulonglong>(current));
            pick->setCurrentIndex(std::max(0, pick->findData(QVariant::fromValue<qulonglong>(s.tox.inputs.value(top.first)))));
            pick->setToolTip(QStringLiteral("The selected layer's picture after Color and FX, before Spatial mapping. Cyclic connections are refused."));
            form->addRow(QStringLiteral("Input TOP · ") + top.second, pick);
            connect(pick, &QComboBox::activated, this, [this, pick, id = top.first](int k) {
                const quint64 source = pick->itemData(k).toULongLong();
                editSource(QStringLiteral("Connect Input TOP"), [this, id, source] {
                    QString error;
                    if (!m_engine->setLayerToxInput(m_engine->indexOfId(m_layerId), id, source, &error))
                        QMessageBox::warning(this, QStringLiteral("Input TOP"), error);
                });
                QTimer::singleShot(0, this, [this] { rebuild(); });
            });
        }
        for (const ToxControl &c : s.toxControls) {
            if (c.color && c.count >= 3 && c.count <= 4) {
                auto *picker = new ColorPicker(c.count == 4);
                picker->setSettingsKey(QStringLiteral("ui/colorPickerMode/layer/%1/tox/%2").arg(m_layerId).arg(c.id)); // its own view
                auto color = [&] { return QColor::fromRgbF(float(c.value(s.tox, 0).toDouble()), float(c.value(s.tox, 1).toDouble()),
                                  float(c.value(s.tox, 2).toDouble()), c.count == 4 ? float(c.value(s.tox, 3).toDouble()) : 1.f); };
                picker->setColor(color());
                auto *label = new QLabel(c.label);
                form->addRow(label, picker);
                QStringList paths;
                for (int k = 0; k < c.count; ++k) paths << c.path(k);
                m_animate->attach({label, picker}, paths);
                connect(picker, &ColorPicker::colorEdited, this, [this, c](const QColor &color) {
                    QJsonObject values{{c.path(0), color.redF()}, {c.path(1), color.greenF()}, {c.path(2), color.blueF()}};
                    if (c.count == 4) values[c.path(3)] = color.alphaF();
                    setValues(values, QStringLiteral("Change ") + c.label);
                });
                m_toxColors.push_back({picker, c});
                continue;
            }
            for (int k = 0; k < c.count; ++k) {
                const QString path = c.path(k);
                auto *label = new QLabel(c.label + (c.count > 1 ? " " + path.section('/', -1).toUpper() : QString()));
                QWidget *field = nullptr;
                if (c.type == ParamInfo::Type::Float || c.type == ParamInfo::Type::Int) {
                    auto *number = new NumberBox;
                    const auto limits = limitsOf(path, -10000, 10000);
                    number->setRange(limits.first, limits.second);
                    number->setDecimals(c.type == ParamInfo::Type::Int ? 0 : 4);
                    number->setSingleStep(c.type == ParamInfo::Type::Int ? 1 : .01);
                    number->setValue(c.value(s.tox, k).toDouble());
                    connect(number, &QDoubleSpinBox::editingFinished, this, [this, number, path] { setValue(path, number->value(), QStringLiteral("Change TouchDesigner Parameter")); });
                    field = number;
                } else if (c.type == ParamInfo::Type::Bool) {
                    auto *flag = new QCheckBox;
                    flag->setChecked(c.value(s.tox, k).toBool());
                    connect(flag, &QCheckBox::toggled, this, [this, path](bool on) { setValue(path, on, QStringLiteral("Change TouchDesigner Parameter")); });
                    field = flag;
                } else if (c.type == ParamInfo::Type::Trigger) {
                    auto *button = new QPushButton(c.label);
                    connect(button, &QPushButton::clicked, this, [this, path] {
                        Engine::Lock lock(&m_engine->mutex());
                        if (Layer *l = m_engine->layer(m_engine->indexOfId(m_layerId)))
                            for (Parameter *p : l->parameters()) if (p->path() == path) { p->setValue(true); break; }
                    });
                    field = button;
                } else {
                    auto *text = new QLineEdit(c.value(s.tox, k).toString());
                    connect(text, &QLineEdit::editingFinished, this, [this, text, path] { setValue(path, text->text(), QStringLiteral("Change TouchDesigner Parameter")); });
                    field = text;
                }
                form->addRow(label, field);
                m_animate->attach({label, field}, {path});
                if (c.type != ParamInfo::Type::Trigger) m_toxFields.push_back({field, path});
            }
        }
        v->addWidget(box);
    }

    // Web Page generator: its address, size, frame rate, zoom and background; each one a parameter (undoable, held by
    // snapshots and presets, reached by OSC)
    if (s.type == SourceType::Web) {
        auto *box = new QGroupBox(QStringLiteral("Web Page"));
        auto *wf = new QFormLayout(box);
        wf->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        const WebSource def;
        auto *url = new QLineEdit(s.web.url);
        url->setObjectName(QStringLiteral("webUrl"));
        url->setPlaceholderText(QStringLiteral("https://… or a local .html file"));
        url->setToolTip(QStringLiteral("The page shown: an address, or the path of a local HTML file. Return loads it."));
        url->setEnabled(!m_locked);
        connect(url, &QLineEdit::editingFinished, this, [this, url] {
            setValue(QStringLiteral("web/url"), url->text().trimmed(), QStringLiteral("Change Web Page URL"));
            emit layerChanged();
        });
        auto *reload = new QPushButton(QStringLiteral("Reload"));
        reload->setToolTip(QStringLiteral("Load the page again (also: the web/reload parameter, by OSC or an interaction)"));
        reload->setEnabled(!m_locked);
        connect(reload, &QPushButton::clicked, this, [this] { m_engine->reloadLayerWeb(m_layer); });
        auto *urlRow = new QHBoxLayout;
        urlRow->addWidget(url, 1);
        urlRow->addWidget(reload);
        wf->addRow(QStringLiteral("URL"), urlRow);
        auto intBox = [this](int value, const QString &suffix) {
            auto *x = new IntBox;
            x->setRange(1, WebSource::kMaxSize);
            x->setValue(value);
            x->setSuffix(suffix);
            x->setEnabled(!m_locked);
            return x;
        };
        auto *w = intBox(s.web.width, QStringLiteral(" px"));
        auto *h = intBox(s.web.height, QStringLiteral(" px"));
        w->setToolTip(QStringLiteral("The size of its picture, in pixels of the page (CSS pixels)"));
        h->setToolTip(w->toolTip());
        auto *sizeRow = new QHBoxLayout;
        sizeRow->addWidget(w);
        sizeRow->addWidget(new QLabel(QStringLiteral("×")));
        sizeRow->addWidget(h);
        sizeRow->addStretch(1);
        wf->addRow(new ResetLabel(QStringLiteral("Size"), [this] {
                       const QSize c = m_engine->compositionSize(); // the composition's, by default
                       setValues({{QStringLiteral("web/width"), c.width()}, {QStringLiteral("web/height"), c.height()}},
                                 QStringLiteral("Reset Web Page Size"));
                       rebuild();
                   }),
                   sizeRow);
        connect(w, &QSpinBox::editingFinished, this, [this, w] {
            setValue(QStringLiteral("web/width"), w->value(), QStringLiteral("Change Web Page Width"));
            emit layerChanged();
        });
        connect(h, &QSpinBox::editingFinished, this, [this, h] {
            setValue(QStringLiteral("web/height"), h->value(), QStringLiteral("Change Web Page Height"));
            emit layerChanged();
        });
        auto number = [this](double value, double lo, double hi, double step, const QString &suffix) {
            auto *x = new NumberBox;
            x->setRange(lo, hi);
            x->setSingleStep(step);
            x->setDecimals(0);
            x->setSuffix(suffix);
            x->setValue(value);
            x->setEnabled(!m_locked);
            return x;
        };
        auto *fps = number(s.web.fps, WebSource::kMinFps, WebSource::kMaxFps, 1, QStringLiteral(" fps"));
        fps->setToolTip(QStringLiteral("The pictures taken of the page per second"));
        wf->addRow(new ResetLabel(QStringLiteral("Frame rate"),
                                  [this, def] {
                                      setValue(QStringLiteral("web/fps"), def.fps, QStringLiteral("Reset Web Page Frame Rate"));
                                      rebuild();
                                  }),
                   fps);
        connect(fps, &QDoubleSpinBox::editingFinished, this, [this, fps] {
            setValue(QStringLiteral("web/fps"), fps->value(), QStringLiteral("Change Web Page Frame Rate"));
        });
        auto *zoom = number(s.web.zoom * 100, WebSource::kMinZoom * 100, WebSource::kMaxZoom * 100, 5, QStringLiteral(" %"));
        zoom->setToolTip(QStringLiteral("The page's zoom: its content larger or smaller, its picture the same size"));
        wf->addRow(new ResetLabel(QStringLiteral("Zoom"),
                                  [this, def] {
                                      setValue(QStringLiteral("web/zoom"), def.zoom, QStringLiteral("Reset Web Page Zoom"));
                                      rebuild();
                                  }),
                   zoom);
        connect(zoom, &QDoubleSpinBox::editingFinished, this, [this, zoom] {
            setValue(QStringLiteral("web/zoom"), zoom->value() / 100.0, QStringLiteral("Change Web Page Zoom"));
        });
        auto *transparent = new FlagBox(QStringLiteral("Transparent background"));
        transparent->setChecked(s.web.transparent);
        transparent->setEnabled(!m_locked);
        transparent->setToolTip(QStringLiteral("Where the page paints no background of its own, the layers below show through"));
        wf->addRow(QString(), transparent);
        connect(transparent, &QCheckBox::toggled, this, [this](bool on) {
            setValue(QStringLiteral("web/transparent"), on, QStringLiteral("Change Web Page Background"));
        });
        auto *status = new QLabel(s.webStatus);
        status->setObjectName(QStringLiteral("webStatus"));
        status->setWordWrap(true);
        status->setTextFormat(Qt::PlainText);
        wf->addRow(QString(), status);
        auto *poll = new QTimer(status); // its loading goes on by itself
        poll->setInterval(500);
        connect(poll, &QTimer::timeout, status, [this, status] { status->setText(m_engine->layerWebStatus(m_layer)); });
        poll->start();
        auto *note = new QLabel(QStringLiteral("Its sound is muted, and it does not take clicks or keys."));
        note->setWordWrap(true);
        wf->addRow(QString(), note);
        v->addWidget(box);
    }

    // Text generator: the text typed here, and how it is set
    if (s.type == SourceType::Text) {
        // Edits apply at once; the undo step is pushed when the editing pauses
        struct Pending { QJsonObject before; Engine::AnimationTargetState animations; QString label; bool has = false; };
        auto pend = std::make_shared<Pending>();
        auto *commit = new QTimer(g);
        commit->setSingleShot(true);
        commit->setInterval(700);
        connect(commit, &QTimer::timeout, this, [this, pend] {
            if (!pend->has) return;
            pend->has = false;
            m_undo->push(new cmd::ReplaceLayer(m_engine, m_layer, pend->before, pend->label, pend->animations));
        });
        auto live = [this, pend, commit](const QString &label, const std::function<void()> &op) {
            if (m_engine->isLocked(m_layer)) return;
            if (!pend->has) {
                pend->has = true;
                pend->before = m_engine->layerJson(m_layer);
                pend->animations = m_engine->captureAnimationTargets(m_engine->layerId(m_layer));
            }
            pend->label = label;
            op();
            commit->start();
        };
        auto style = [this, live](const QString &label, std::function<void(Layer &)> fn) {
            live(label, [this, fn] { m_engine->editLayerText(m_layer, fn); });
        };
        using ColorSet = std::function<void(Layer &, const QColor &)>;
        auto paint = [](QPushButton *b, const QColor &x) {
            b->setProperty("color", x);
            b->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #666; border-radius: 2px;").arg(x.name()));
        };
        // A color of the text set from its button (picked, or put back by the name of its row)
        auto applyColor = [style, paint](QPushButton *b, const QColor &x, const QString &label, ColorSet set) {
            paint(b, x);
            style(label, [set, x](Layer &l) { set(l, x); });
        };
        auto colorButton = [this, paint, applyColor](const QColor &c, const QString &label, ColorSet set) {
            auto *b = new QPushButton;
            b->setFixedWidth(48);
            b->setEnabled(!m_locked);
            paint(b, c);
            connect(b, &QPushButton::clicked, this, [this, b, label, set, applyColor] {
                const QPointer<QPushButton> guard(b); // the inspector may be rebuilt while the picker is open
                const QColor picked = ColorPicker::getColor(b->property("color").value<QColor>(), this, label,
                    QStringLiteral("ui/colorPickerMode/layer/%1/text/%2").arg(m_layerId).arg(label));
                if (guard && picked.isValid()) applyColor(b, picked, label, set);
            });
            return b;
        };
        const TextSource def; // what the names of the rows put back
        auto spin = [this](double v, double lo, double hi, double step, int dec, const QString &suffix) {
            auto *x = new NumberBox;
            x->setRange(lo, hi);
            x->setSingleStep(step);
            x->setDecimals(dec);
            x->setSuffix(suffix);
            x->setValue(v);
            x->setEnabled(!m_locked);
            return x;
        };

        auto trackRow = [this](QFormLayout *form, QWidget *field, const QStringList &paths) {
            m_animate->attach(field, paths);
            m_animate->attach(form->labelForField(field), paths);
        };
        auto trackLayoutRow = [this](QFormLayout *form, QLayout *row, const QStringList &paths) {
            m_animate->attach(form->labelForField(row), paths);
        };
        if (s.clock) { // a Countdown, Stopwatch or Clock: its settings and its transport (clock/*) instead of a text typed
            const ClockSettings cs = *s.clock;
            const ClockKind kind = cs.kind;
            auto *box = new QGroupBox(clockKindName(kind));
            auto *cf = new QFormLayout(box);
            cf->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
            // Each setting is a value of the layer: undone, held by snapshots and presets, sent by OSC
            auto set = [this](const QString &path, const QJsonValue &v, const QString &label) {
                if (m_engine->isLocked(m_layer)) return;
                cmd::setValue(m_undo, m_engine, m_layerId, path, v, label);
                refreshClock();
                // Its fields follow its settings (a target, a format, what it does at zero): made again when they change
                QMetaObject::invokeMethod(this, [this] {
                    if (m_layoutKey != layoutKey()) rebuild();
                }, Qt::QueuedConnection);
            };
            auto choose = [this, set](QComboBox *box, const QString &path, const QString &label) {
                connect(box, qOverload<int>(&QComboBox::activated), this,
                        [set, box, path, label](int) { set(path, box->currentData().toString(), label); });
            };
            auto combo = [this](const QList<QPair<QString, QString>> &items, const QString &current) {
                auto *b = new QComboBox;
                for (const auto &[label, key] : items) b->addItem(label, key);
                b->setCurrentIndex(std::max(0, b->findData(current)));
                b->setEnabled(!m_locked);
                return b;
            };
            auto line = [this](const QString &text, const QString &tip) {
                auto *e = new QLineEdit(text);
                e->setToolTip(tip);
                e->setEnabled(!m_locked);
                return e;
            };
            auto durationText = [](double secs) {
                const bool whole = std::abs(secs - std::round(secs)) < 1e-6;
                return ClockGenerator::formatDuration(secs, whole ? QStringLiteral("hh:mm:ss") : QStringLiteral("hh:mm:ss.t"));
            };
            // A duration typed (h:m:s, m:s or seconds): set, or shown again as it is
            auto durationField = [this, set, durationText](QLineEdit *e, const QString &path, double now, const QString &label,
                                                           bool negative) {
                connect(e, &QLineEdit::editingFinished, this, [e, path, now, label, negative, set, durationText] {
                    const std::optional<double> v = parseClockDuration(e->text());
                    if (v && (negative || *v >= 0)) set(path, *v, label);
                    else e->setText(durationText(now));
                    e->clearFocus();
                });
            };
            const bool timeOfDay = kind == ClockKind::Countdown && cs.target == ClockSettings::Target::TimeOfDay;
            if (kind == ClockKind::Countdown) {
                auto *target = combo({{QStringLiteral("A duration"), QStringLiteral("duration")},
                                      {QStringLiteral("A time of day"), QStringLiteral("time_of_day")}},
                                     timeOfDay ? QStringLiteral("time_of_day") : QStringLiteral("duration"));
                target->setToolTip(QStringLiteral("Counts down from a duration, or to a time of day (the next one to come: "
                                                  "after midnight when it is already past today)"));
                cf->addRow(QStringLiteral("Count down"), target);
                trackRow(cf, target, {QStringLiteral("clock/target")});
                choose(target, QStringLiteral("clock/target"), QStringLiteral("Countdown Target"));
                if (timeOfDay) {
                    auto *time = line(cs.targetTime.toString(QStringLiteral("HH:mm:ss")),
                                      QStringLiteral("The time of day it counts down to: 20:30, 20:30:00, 8:30 PM"));
                    cf->addRow(QStringLiteral("To"), time);
                    trackRow(cf, time, {QStringLiteral("clock/target_time")});
                    connect(time, &QLineEdit::editingFinished, this, [time, set, cs] {
                        const QTime t = ClockGenerator::parseTime(time->text());
                        if (t.isValid()) set(QStringLiteral("clock/target_time"), t.toString(QStringLiteral("HH:mm:ss")),
                                             QStringLiteral("Countdown Target Time"));
                        else time->setText(cs.targetTime.toString(QStringLiteral("HH:mm:ss")));
                        time->clearFocus();
                    });
                } else {
                    auto *duration = line(durationText(cs.duration),
                                          QStringLiteral("Where it starts: hours:minutes:seconds (1:30:00), minutes:seconds "
                                                         "(5:00) or seconds (90)"));
                    cf->addRow(QStringLiteral("Duration"), duration);
                    trackRow(cf, duration, {QStringLiteral("clock/duration")});
                    durationField(duration, QStringLiteral("clock/duration"), cs.duration, QStringLiteral("Countdown Duration"), false);
                }
            }
            if (kind == ClockKind::Stopwatch) {
                auto *offset = line(durationText(cs.offset), QStringLiteral("Where it starts counting up from (0: zero)"));
                cf->addRow(QStringLiteral("Start at"), offset);
                trackRow(cf, offset, {QStringLiteral("clock/offset")});
                durationField(offset, QStringLiteral("clock/offset"), cs.offset, QStringLiteral("Chronometer Start"), true);
            }

            // Its format: a preset, or one of its own (tokens; a Qt date-time format for the clock)
            QList<QPair<QString, QString>> formats;
            if (kind == ClockKind::Clock) {
                formats = {{QStringLiteral("24 h"), QStringLiteral("24h")}, {QStringLiteral("12 h (AM / PM)"), QStringLiteral("12h")}};
            } else {
                for (const QString &f : ClockGenerator::durationFormats())
                    if (f != QLatin1String("custom"))
                        formats.append({f.toUpper().replace(QStringLiteral(".T"), QStringLiteral(".t")), f});
            }
            formats.append({QStringLiteral("Custom"), QStringLiteral("custom")});
            auto *format = combo(formats, cs.format);
            format->setToolTip(kind == ClockKind::Clock ? QStringLiteral("How the time is written")
                                                        : QStringLiteral("How the time is written: hours, minutes, seconds, "
                                                                         "tenths (.t), frames (:ff)"));
            cf->addRow(QStringLiteral("Format"), format);
            trackRow(cf, format, {QStringLiteral("clock/format")});
            choose(format, QStringLiteral("clock/format"), QStringLiteral("%1 Format").arg(clockKindName(kind)));
            if (cs.format == QLatin1String("custom")) {
                auto *custom = line(cs.customFormat,
                                    kind == ClockKind::Clock
                                        ? QStringLiteral("A Qt date-time format: HH:mm:ss, h:mm AP, dddd d MMMM yyyy…")
                                        : QStringLiteral("Tokens: h hh (hours), m mm (minutes), s ss (seconds), t tt ttt "
                                                         "(tenths, hundredths, thousandths), ff (frames); 'text' quoted. The "
                                                         "largest unit holds all of the time above it (mm:ss shows 90:00)"));
                custom->setPlaceholderText(kind == ClockKind::Clock ? QStringLiteral("HH:mm:ss") : QStringLiteral("'T-'mm:ss"));
                cf->addRow(QStringLiteral("Custom"), custom);
                trackRow(cf, custom, {QStringLiteral("clock/custom_format")});
                connect(custom, &QLineEdit::editingFinished, this, [custom, set, kind] {
                    set(QStringLiteral("clock/custom_format"), custom->text(), QStringLiteral("%1 Custom Format").arg(clockKindName(kind)));
                    custom->clearFocus();
                });
            }
            const bool frames = kind != ClockKind::Clock &&
                                (cs.format.contains(QLatin1String("ff")) ||
                                 (cs.format == QLatin1String("custom") && cs.customFormat.contains(QLatin1Char('f'), Qt::CaseInsensitive)));
            if (frames) {
                auto *fps = new IntBox;
                fps->setRange(1, 1000);
                fps->setValue(cs.fps);
                fps->setSuffix(QStringLiteral(" fps"));
                fps->setEnabled(!m_locked);
                fps->setToolTip(QStringLiteral("The frames counted in a second (the ff token)"));
                cf->addRow(QStringLiteral("Frames"), fps);
                trackRow(cf, fps, {QStringLiteral("clock/fps")});
                connect(fps, &QSpinBox::editingFinished, this, [fps, set, kind] {
                    set(QStringLiteral("clock/fps"), fps->value(), QStringLiteral("%1 Frame Rate").arg(clockKindName(kind)));
                });
            }

            if (kind == ClockKind::Countdown) { // at zero: holds, a text, or overtime in its own color
                static const char *const kZero[] = {"stop", "text", "overtime"};
                auto *zero = combo({{QStringLiteral("Stop at zero"), QStringLiteral("stop")},
                                    {QStringLiteral("Show a text"), QStringLiteral("text")},
                                    {QStringLiteral("Overtime (counts on, below zero)"), QStringLiteral("overtime")}},
                                   QString::fromLatin1(kZero[int(cs.atZero)]));
                zero->setToolTip(QStringLiteral("What it shows once at zero (its value clock/zero turns on: an interaction "
                                                "can follow it)"));
                cf->addRow(QStringLiteral("At zero"), zero);
                trackRow(cf, zero, {QStringLiteral("clock/at_zero")});
                choose(zero, QStringLiteral("clock/at_zero"), QStringLiteral("Countdown At Zero"));
                if (cs.atZero == ClockSettings::AtZero::Text) {
                    auto *text = line(cs.zeroText, QStringLiteral("Shown once it is at zero"));
                    cf->addRow(QStringLiteral("Text"), text);
                    trackRow(cf, text, {QStringLiteral("clock/zero_text")});
                    connect(text, &QLineEdit::editingFinished, this, [text, set] {
                        set(QStringLiteral("clock/zero_text"), text->text(), QStringLiteral("Countdown Text at Zero"));
                        text->clearFocus();
                    });
                } else if (cs.atZero == ClockSettings::AtZero::Overtime) {
                    auto *color = new QPushButton;
                    color->setFixedWidth(48);
                    color->setEnabled(!m_locked);
                    color->setToolTip(QStringLiteral("The color of the text in overtime"));
                    color->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #666; border-radius: 2px;")
                                             .arg(cs.overtimeColor.name()));
                    auto *row = new QHBoxLayout;
                    row->addWidget(color);
                    row->addStretch();
                    cf->addRow(QStringLiteral("Overtime"), row);
                    const QStringList colorPaths{"clock/overtime_color/r", "clock/overtime_color/g", "clock/overtime_color/b", "clock/overtime_color/a"};
                    m_animate->attach(color, colorPaths);
                    trackLayoutRow(cf, row, colorPaths);
                    connect(color, &QPushButton::clicked, this, [this, color, cs] {
                        const QPointer<QPushButton> guard(color);
                        const QColor c = ColorPicker::getColor(cs.overtimeColor, this, QStringLiteral("Overtime Color"),
                            QStringLiteral("ui/colorPickerMode/layer/%1/clock/overtime").arg(m_layerId));
                        if (!guard || !c.isValid() || m_engine->isLocked(m_layer)) return;
                        cmd::setValues(m_undo, m_engine, m_layerId,
                                       QJsonObject{{"clock/overtime_color/r", c.redF()}, {"clock/overtime_color/g", c.greenF()},
                                                   {"clock/overtime_color/b", c.blueF()}, {"clock/overtime_color/a", c.alphaF()}},
                                       QStringLiteral("Countdown Overtime Color"));
                    });
                }
            }
            if (kind == ClockKind::Clock) {
                auto *row = new QHBoxLayout;
                auto *secs = new QCheckBox(QStringLiteral("Seconds"));
                auto *date = new QCheckBox(QStringLiteral("Date"));
                secs->setChecked(cs.seconds);
                date->setChecked(cs.date);
                date->setToolTip(QStringLiteral("The date under the time, as the system writes it"));
                for (QCheckBox *b : {secs, date}) {
                    b->setEnabled(!m_locked && cs.format != QLatin1String("custom"));
                    row->addWidget(b);
                }
                row->addStretch();
                cf->addRow(QStringLiteral("Show"), row);
                m_animate->attach(secs, {QStringLiteral("clock/seconds")});
                m_animate->attach(date, {QStringLiteral("clock/date")});
                trackLayoutRow(cf, row, {QStringLiteral("clock/seconds"), QStringLiteral("clock/date")});
                connect(secs, &QCheckBox::toggled, this,
                        [set](bool on) { set(QStringLiteral("clock/seconds"), on, QStringLiteral("Clock Seconds")); });
                connect(date, &QCheckBox::toggled, this,
                        [set](bool on) { set(QStringLiteral("clock/date"), on, QStringLiteral("Clock Date")); });
            }
            if (kind == ClockKind::Clock || timeOfDay) {
                auto *zone = line(cs.zone, QStringLiteral("Empty: the system's time zone. Or UTC, an offset (+02:00, -5), "
                                                          "a zone name (Europe/Paris, America/New_York)"));
                zone->setPlaceholderText(QStringLiteral("System"));
                cf->addRow(QStringLiteral("Time zone"), zone);
                trackRow(cf, zone, {QStringLiteral("clock/zone")});
                connect(zone, &QLineEdit::editingFinished, this, [zone, set] {
                    set(QStringLiteral("clock/zone"), zone->text().trimmed(), QStringLiteral("Time Zone"));
                    zone->clearFocus();
                });
            }

            m_clockUi = std::make_shared<ClockUi>();
            auto *shown = new QLabel;
            shown->setTextInteractionFlags(Qt::TextSelectableByMouse);
            shown->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            m_clockUi->shown = shown;
            if (kind != ClockKind::Clock) { // Start, Pause, Reset: its values clock/play, clock/reset (OSC, cues…)
                auto button = [](const QIcon &icon, const QString &tip, bool checkable) {
                    auto *b = new QToolButton;
                    b->setIcon(icon);
                    b->setIconSize(QSize(18, 18));
                    b->setToolTip(tip);
                    b->setCheckable(checkable);
                    b->setAutoRaise(true);
                    b->setProperty("allowLocked", true); // the transport stays available on a locked layer, as a media's
                    b->setStyleSheet(QStringLiteral("QToolButton { padding:3px 7px; border-radius:3px; }"
                                                    "QToolButton:checked { background:%1; }"
                                                    "QToolButton:hover { background:#3a3a3f; }")
                                         .arg(theme::css()));
                    return b;
                };
                auto *row = new QHBoxLayout;
                row->setSpacing(2);
                auto *pause = button(transportIcon(TransportIcon::Pause), QStringLiteral("Pause"), true);
                auto *play = button(transportIcon(TransportIcon::PlayForward),
                                    QStringLiteral("Start (at its end: from the start again)"), true);
                auto *reset = button(transportIcon(TransportIcon::ToStart),
                                     QStringLiteral("Reset: back to its start, paused"), false);
                auto *group = new QButtonGroup(box);
                group->setExclusive(true);
                group->addButton(pause, 0);
                group->addButton(play, 1);
                row->addWidget(pause);
                row->addWidget(play);
                row->addWidget(reset);
                row->addSpacing(8);
                row->addWidget(shown, 1);
                cf->addRow(QStringLiteral("Transport"), row);
                m_animate->attach({play, pause}, {QStringLiteral("clock/play")});
                m_animate->attach(reset, {QStringLiteral("clock/elapsed")});
                m_animate->attach(shown, {QStringLiteral("clock/elapsed")});
                trackLayoutRow(cf, row, {QStringLiteral("clock/play"), QStringLiteral("clock/elapsed")});
                m_clockUi->play = play;
                m_clockUi->pause = pause;
                connect(group, &QButtonGroup::idClicked, this, [this, kind](int id) {
                    cmd::setValue(m_undo, m_engine, m_layerId, QStringLiteral("clock/play"), id == 1,
                                  QStringLiteral("%1 %2").arg(id == 1 ? QStringLiteral("Start") : QStringLiteral("Pause"),
                                                              clockKindName(kind)));
                    refreshClock();
                });
                connect(reset, &QToolButton::clicked, this, [this] {
                    m_engine->setParameter(m_layerId, QStringLiteral("clock/reset"), true); // an action, not undone
                    refreshClock();
                });
            } else {
                cf->addRow(QStringLiteral("Now"), shown);
            }
            auto *status = new QLabel;
            status->setWordWrap(true);
            status->setStyleSheet("color:#9ad;");
            cf->addRow(QString(), status);
            m_clockUi->status = status;
            refreshClock();
            v->addWidget(box);
        } else if (s.timecode) { // a Timecode generator: its clock instead of a text typed
            const TimecodeSettings ts = *s.timecode;
            auto *box = new QGroupBox(QStringLiteral("Timecode"));
            auto *tf = new QFormLayout(box);
            tf->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
            auto apply = [this](const QString &label, const std::function<void(TimecodeSettings &)> &fn) {
                auto settings = m_engine->layerTimecode(m_layer);
                if (!settings) return;
                const QJsonObject before = settings->toJson();
                fn(*settings);
                QJsonObject changes;
                const QJsonObject after = settings->toJson();
                for (const char *key : {"source", "internal", "rate", "offset", "input", "output"})
                    if (before.value(key) != after.value(key))
                        changes.insert(QStringLiteral("tc/") + key, after.value(key).isUndefined() ? QJsonValue(QString()) : after.value(key));
                setValues(changes, label);
            };
            auto *source = new QComboBox;
            for (int k = 0; k < 5; ++k) source->addItem(TimecodeSettings::sourceName(TimecodeSettings::Source(k)));
            source->setCurrentIndex(int(ts.source));
            source->setToolTip(QStringLiteral("Internal: its own clock · LTC: from an audio input · MTC: from a MIDI input · "
                                              "ATC: carried by the video (an NDI source, a DeckLink SDI input)"));
            tf->addRow(QStringLiteral("Source"), source);
            trackRow(tf, source, {QStringLiteral("tc/source")});
            auto *clockRow = new QHBoxLayout;
            auto *clock = new QComboBox;
            clock->addItems({QStringLiteral("Time of day"), QStringLiteral("Free run")});
            clock->setCurrentIndex(int(ts.internal));
            clock->setToolTip(QStringLiteral("Time of day: the computer's clock · Free run: a transport of its own (played, "
                                             "paused, sought, in / out points), held by snapshots and presets"));
            clockRow->addWidget(clock, 1);
            auto *clockLabel = new QLabel(QStringLiteral("Clock"));
            tf->addRow(clockLabel, clockRow);
            m_animate->attach({clockLabel, clock}, {QStringLiteral("tc/internal")});
            const bool internal = ts.source == TimecodeSettings::Source::Internal;
            clockLabel->setVisible(internal);
            clock->setVisible(internal);
            auto *rate = new QComboBox;
            for (int k = 0; k < 4; ++k) rate->addItem(Timecode::rateName(TcRate(k)));
            rate->setCurrentIndex(int(ts.rate));
            rate->setToolTip(QStringLiteral("Its rate (a timecode received keeps its own)"));
            tf->addRow(QStringLiteral("Rate"), rate);
            trackRow(tf, rate, {QStringLiteral("tc/rate")});
            auto *offset = new QLineEdit;
            {
                const Timecode o = Timecode::fromFrames(std::llabs(ts.offset), ts.rate);
                offset->setText((ts.offset < 0 ? QStringLiteral("-") : QString()) + o.toString());
            }
            offset->setToolTip(QStringLiteral("Added to what is shown and sent: HH:MM:SS:FF, a minus before it to take away"));
            tf->addRow(QStringLiteral("Offset"), offset);
            trackRow(tf, offset, {QStringLiteral("tc/offset")});

            // Where an external timecode comes from: the devices of its kind, the one chosen kept when it is not here
            using Src = TimecodeSettings::Source;
            auto fillDevices = [](QComboBox *box, const QList<QPair<QString, QString>> &items, const QString &current,
                                  const QString &first) {
                box->addItem(first, QString());
                for (const auto &[label, value] : items) box->addItem(label, value);
                int at = box->findData(current);
                if (at < 0 && !current.isEmpty()) {
                    box->addItem(QStringLiteral("%1 (not found)").arg(current), current);
                    at = box->count() - 1;
                }
                box->setCurrentIndex(std::max(0, at));
            };
            auto names = [](const QStringList &l) {
                QList<QPair<QString, QString>> out;
                for (const QString &n : l) out.append({n, n});
                return out;
            };
            QComboBox *input = nullptr;
            if (ts.source != Src::Internal) {
                input = new QComboBox;
                input->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                input->setMinimumContentsLength(10);
                QList<QPair<QString, QString>> items;
                QString first = QStringLiteral("First one");
                if (ts.source == Src::Ltc) {
                    items = names(AudioOutput::captureDeviceNames());
                    first = QStringLiteral("Default input");
                } else if (ts.source == Src::Mtc) {
                    items = names(midi::inputNames());
                } else if (ts.source == Src::AtcNdi) {
                    for (const LiveInputInfo &li : listLiveInputs())
                        if (li.kind == QLatin1String("NDI")) items.append({li.name, li.uri.section(':', 2, -1)});
                    first = QStringLiteral("(choose a source)");
                } else {
                    for (const decklink::Port &p : decklink::inputPorts()) items.append({p.name, p.id});
                }
                fillDevices(input, items, ts.input, first);
                auto *row = new QHBoxLayout;
                row->addWidget(input, 1);
                auto *rescan = new QPushButton(QStringLiteral("↻"));
                rescan->setFixedWidth(28);
                rescan->setToolTip(QStringLiteral("Look for the devices again (an NDI source takes a moment to be found)"));
                row->addWidget(rescan);
                tf->addRow(QStringLiteral("Input"), row);
                m_animate->attach(input, {QStringLiteral("tc/input")});
                trackLayoutRow(tf, row, {QStringLiteral("tc/input")});
                input->setEnabled(!m_locked);
                connect(rescan, &QPushButton::clicked, this, [this] {
                    QMetaObject::invokeMethod(this, &LayerInspector::rebuild, Qt::QueuedConnection);
                });
                connect(input, qOverload<int>(&QComboBox::activated), this, [apply, input](int) {
                    const QString v = input->currentData().toString();
                    apply(QStringLiteral("Timecode Input"), [v](TimecodeSettings &t) { t.input = v; });
                });
            }
            // Sent: to one of the composition's timecode outputs, whose patch (Composition tab) says where and so how
            auto *output = new QComboBox;
            output->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            output->setMinimumContentsLength(10);
            output->setToolTip(QStringLiteral("The timecode output this layer drives (one layer per output). Where it goes, "
                                              "and so in which protocol, is patched in the Composition tab: a MIDI output "
                                              "(MTC), an audio channel (LTC), a viewport (ATC in its NDI and DeckLink outputs)"));
            output->addItem(QStringLiteral("None"), QVariant::fromValue<quint64>(0));
            for (const TimecodeOutput &o : m_engine->timecodeOutputs()) {
                const int user = m_engine->timecodeOutputUser(o.id);
                const bool other = user >= 0 && user != m_layer;
                output->addItem(other ? QStringLiteral("%1 (from %2)").arg(o.name, layerNameOf(m_engine, user)) : o.name,
                                QVariant::fromValue<quint64>(o.id));
                if (other) // taken by another layer: not chosen here
                    if (auto *model = qobject_cast<QStandardItemModel *>(output->model()))
                        model->item(output->count() - 1)->setEnabled(false);
            }
            output->addItem(QStringLiteral("New Output…"), QVariant::fromValue<quint64>(~quint64(0)));
            output->setCurrentIndex(std::max(0, output->findData(QVariant::fromValue<quint64>(ts.output))));
            output->setEnabled(!m_locked);
            tf->addRow(QStringLiteral("Output"), output);
            trackRow(tf, output, {QStringLiteral("tc/output")});
            connect(output, qOverload<int>(&QComboBox::activated), this, [this, apply, output](int) {
                quint64 id = output->currentData().value<quint64>();
                if (id == ~quint64(0)) id = m_engine->addTimecodeOutput(layerNameOf(m_engine, m_layer));
                apply(QStringLiteral("Timecode Output"), [id](TimecodeSettings &t) { t.output = id; });
                QMetaObject::invokeMethod(this, &LayerInspector::rebuild, Qt::QueuedConnection);
            });

            // The free run's transport: its values (tc/*), held by snapshots and presets, undone as any value. Times are
            // shown as the timecode they give (offset included)
            if (internal && ts.internal == TimecodeSettings::Internal::FreeRun) {
                auto tcButton = [](const QIcon &icon, const QString &tip, bool checkable) {
                    auto *b = new QToolButton;
                    b->setIcon(icon);
                    b->setIconSize(QSize(18, 18));
                    b->setToolTip(tip);
                    b->setCheckable(checkable);
                    b->setAutoRaise(true);
                    b->setProperty("allowLocked", true); // the transport stays available on a locked layer, as a media's
                    b->setStyleSheet(QStringLiteral("QToolButton { padding:3px 7px; border-radius:3px; }"
                                                    "QToolButton:checked { background:%1; }"
                                                    "QToolButton:hover { background:#3a3a3f; }")
                                         .arg(theme::css()));
                    return b;
                };
                auto *playRow = new QHBoxLayout;
                playRow->setSpacing(2);
                auto *pause = tcButton(transportIcon(TransportIcon::Pause), QStringLiteral("Pause"), true);
                auto *play = tcButton(transportIcon(TransportIcon::PlayForward), QStringLiteral("Play"), true);
                auto *rewind = tcButton(transportIcon(TransportIcon::ToStart), QStringLiteral("Back to the in point"), false);
                auto *group = new QButtonGroup(box);
                group->setExclusive(true);
                group->addButton(pause, 0);
                group->addButton(play, 1);
                playRow->addWidget(pause);
                playRow->addWidget(play);
                playRow->addWidget(rewind);
                playRow->addSpacing(8);
                auto *position = new QLineEdit;
                position->setToolTip(QStringLiteral("Where it is: type a timecode to go there"));
                position->setProperty("allowLocked", true);
                playRow->addWidget(position, 1);
                tf->addRow(QStringLiteral("Transport"), playRow);
                m_animate->attach({pause, play}, {QStringLiteral("tc/play")});
                m_animate->attach({rewind, position}, {QStringLiteral("tc/position")});
                trackLayoutRow(tf, playRow, {QStringLiteral("tc/play"), QStringLiteral("tc/position")});

                auto *modeRow = new QHBoxLayout;
                modeRow->setSpacing(2);
                auto *modes = new QButtonGroup(box);
                modes->setExclusive(true);
                const struct { TcPlayMode mode; int icon; const char *tip; } kModes[] = {
                    {TcPlayMode::Loop, int(PlayMode::Loop), "Loop: from the in point again at the out point"},
                    {TcPlayMode::OneShot, int(PlayMode::OneShot), "One-shot: holds at the out point"},
                    {TcPlayMode::PingPong, int(PlayMode::PingPong), "Ping-pong: forwards, then backwards, and so on"},
                    {TcPlayMode::Stop, int(PlayMode::Stop), "Stop: plays once, then nothing is shown or sent"},
                };
                for (const auto &m : kModes) {
                    auto *b = tcButton(playModeIcon(m.icon), QString::fromUtf8(m.tip), true);
                    b->setProperty("allowLocked", false);
                    b->setEnabled(!m_locked);
                    modes->addButton(b, int(m.mode));
                    m_animate->attach(b, {QStringLiteral("tc/play_mode")});
                    modeRow->addWidget(b);
                }
                modeRow->addSpacing(8);
                auto *in = new QLineEdit, *out = new QLineEdit;
                in->setToolTip(QStringLiteral("In point: where it starts (and loops back to)"));
                out->setToolTip(QStringLiteral("Out point: where it ends, loops or turns back (empty: the end of the day)"));
                out->setPlaceholderText(QStringLiteral("End"));
                in->setEnabled(!m_locked);
                out->setEnabled(!m_locked);
                modeRow->addWidget(new QLabel(QStringLiteral("In")));
                modeRow->addWidget(in, 1);
                modeRow->addWidget(new QLabel(QStringLiteral("Out")));
                modeRow->addWidget(out, 1);
                tf->addRow(QStringLiteral("Mode"), modeRow);
                m_animate->attach(in, {QStringLiteral("tc/in")});
                m_animate->attach(out, {QStringLiteral("tc/out")});
                trackLayoutRow(tf, modeRow, {QStringLiteral("tc/play_mode"), QStringLiteral("tc/in"), QStringLiteral("tc/out")});

                // Seconds of transport ↔ the timecode shown (the offset is added)
                auto shown = [this](double secs) {
                    const std::optional<TimecodeSettings> cur = m_engine->layerTimecode(m_layer);
                    if (!cur) return QString();
                    const int64_t f = int64_t(std::floor(secs * Timecode::realFps(cur->rate) + 1e-6)) + cur->offset;
                    return Timecode::fromFrames(f, cur->rate).toString();
                };
                auto typed = [this](const QString &text) -> std::optional<double> {
                    const std::optional<TimecodeSettings> cur = m_engine->layerTimecode(m_layer);
                    if (!cur) return std::nullopt;
                    const std::optional<Timecode> t = Timecode::parse(text.trimmed(), cur->rate);
                    if (!t) return std::nullopt;
                    const int64_t day = Timecode::framesPerDay(cur->rate);
                    const int64_t f = ((t->toFrames() - cur->offset) % day + day) % day;
                    return double(f) / Timecode::realFps(cur->rate);
                };
                m_tcTransport = std::make_shared<TcTransport>();
                m_tcTransport->play = play;
                m_tcTransport->pause = pause;
                m_tcTransport->position = position;
                m_tcTransport->in = in;
                m_tcTransport->out = out;
                m_tcTransport->modes = modes;
                m_tcTransport->shown = shown;
                connect(group, &QButtonGroup::idClicked, this, [this](int id) {
                    cmd::setValue(m_undo, m_engine, m_layerId, QStringLiteral("tc/play"), id == 1,
                                  id == 1 ? QStringLiteral("Play Timecode") : QStringLiteral("Pause Timecode"));
                    refreshDynamic();
                });
                connect(rewind, &QToolButton::clicked, this, [this] {
                    if (std::shared_ptr<TimecodeClock> c = m_engine->layerTimecodeClock(m_layer))
                        cmd::setValue(m_undo, m_engine, m_layerId, QStringLiteral("tc/position"), c->inPoint(),
                                      QStringLiteral("Rewind Timecode"));
                    refreshDynamic();
                });
                connect(position, &QLineEdit::editingFinished, this, [this, position, typed] {
                    if (const std::optional<double> v = typed(position->text()))
                        cmd::setValue(m_undo, m_engine, m_layerId, QStringLiteral("tc/position"), *v,
                                      QStringLiteral("Seek Timecode"));
                    position->clearFocus();
                    refreshDynamic();
                });
                connect(modes, &QButtonGroup::idClicked, this, [this](int id) {
                    cmd::setValue(m_undo, m_engine, m_layerId, QStringLiteral("tc/play_mode"), tcPlayModeKey(TcPlayMode(id)),
                                  QStringLiteral("Change Timecode Play Mode"));
                });
                connect(in, &QLineEdit::editingFinished, this, [this, in, typed] {
                    if (const std::optional<double> v = typed(in->text()))
                        cmd::setValue(m_undo, m_engine, m_layerId, QStringLiteral("tc/in"), *v, QStringLiteral("Set Timecode In"));
                    in->clearFocus();
                    refreshDynamic();
                });
                connect(out, &QLineEdit::editingFinished, this, [this, out, typed] {
                    const QString text = out->text().trimmed();
                    const std::optional<double> v = text.isEmpty() ? std::optional<double>(-1.0) : typed(text);
                    if (v) cmd::setValue(m_undo, m_engine, m_layerId, QStringLiteral("tc/out"), *v, QStringLiteral("Set Timecode Out"));
                    out->clearFocus();
                    refreshDynamic();
                });
                refreshTimecodeTransport();
            }

            auto *status = new QLabel;
            status->setWordWrap(true);
            status->setStyleSheet("color:#9ad;");
            status->setText(m_engine->layerTimecodeStatus(m_layer));
            tf->addRow(QString(), status);
            m_timecodeStatus = status;
            for (QWidget *w : {static_cast<QWidget *>(source), static_cast<QWidget *>(clock), static_cast<QWidget *>(rate),
                               static_cast<QWidget *>(offset)})
                w->setEnabled(!m_locked);
            v->addWidget(box);
            connect(source, qOverload<int>(&QComboBox::activated), this, [this, apply](int k) {
                apply(QStringLiteral("Timecode Source"), [k](TimecodeSettings &t) { t.source = TimecodeSettings::Source(k); });
                QMetaObject::invokeMethod(this, &LayerInspector::rebuild, Qt::QueuedConnection); // its fields change
            });
            connect(clock, qOverload<int>(&QComboBox::activated), this, [this, apply](int k) {
                apply(QStringLiteral("Timecode Clock"), [k](TimecodeSettings &t) { t.internal = TimecodeSettings::Internal(k); });
                QMetaObject::invokeMethod(this, &LayerInspector::rebuild, Qt::QueuedConnection); // its transport, or none
            });
            connect(rate, qOverload<int>(&QComboBox::activated), this, [apply](int k) {
                apply(QStringLiteral("Timecode Rate"), [k](TimecodeSettings &t) { t.rate = TcRate(k); });
            });
            connect(offset, &QLineEdit::editingFinished, this, [this, apply, offset] {
                QString text = offset->text().trimmed();
                const bool minus = text.startsWith('-');
                if (minus) text.remove(0, 1);
                const std::optional<TimecodeSettings> cur = m_engine->layerTimecode(m_layer);
                if (!cur) return;
                const std::optional<Timecode> t = Timecode::parse(text, cur->rate);
                if (!t) { // shown again as it is
                    const Timecode o = Timecode::fromFrames(std::llabs(cur->offset), cur->rate);
                    offset->setText((cur->offset < 0 ? QStringLiteral("-") : QString()) + o.toString());
                    return;
                }
                const int64_t frames = minus ? -t->toFrames() : t->toFrames();
                apply(QStringLiteral("Timecode Offset"), [frames](TimecodeSettings &s) { s.offset = frames; });
            });
        } else {
            auto *editor = new QPlainTextEdit;
            editor->setPlainText(s.text.content);
            editor->setMinimumHeight(90);
            editor->setPlaceholderText(QStringLiteral("Type the text here"));
            editor->setToolTip(QStringLiteral("The text of this layer. A snapshot that holds another text types it (typewriter) over its fade."));
            editor->setReadOnly(m_locked);
            v->addWidget(editor);
            connect(editor, &QPlainTextEdit::textChanged, this, [this, editor, live] {
                live(QStringLiteral("Edit Text"), [this, editor] { m_engine->setLayerTextContent(m_layer, editor->toPlainText()); });
            });
        }

        auto *fmt = new QFormLayout;
        fmt->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

        auto *fontBox = new QFontComboBox;
        fontBox->setCurrentFont(QFont(s.text.font));
        fontBox->setEnabled(!m_locked);
        fmt->addRow(new ResetLabel(QStringLiteral("Font"), [fontBox, def] { fontBox->setCurrentFont(QFont(def.font)); }), fontBox);
        trackRow(fmt, fontBox, {QStringLiteral("text/font")});
        connect(fontBox, &QFontComboBox::currentFontChanged, this, [style](const QFont &f) {
            const QString family = f.family();
            style(QStringLiteral("Text Font"), [family](Layer &l) { l.text.font = family; });
        });

        auto *size = new IntBox;
        {
            const auto [lo, hi] = limitsOf(QStringLiteral("text/size"), 1, 1000);
            size->setRange(int(lo), int(hi));
        }
        size->setSuffix(QStringLiteral(" px"));
        size->setValue(s.text.size);
        size->setEnabled(!m_locked);
        auto *colorRow = new QHBoxLayout;
        colorRow->addWidget(size);
        const ColorSet setTextColor = [](Layer &l, const QColor &c) { l.text.color = c; };
        QPushButton *textColor = colorButton(s.text.color, QStringLiteral("Text Color"), setTextColor);
        colorRow->addWidget(textColor);
        colorRow->addStretch();
        fmt->addRow(new ResetLabel(QStringLiteral("Size / color"), [size, textColor, def, applyColor, setTextColor] {
                        size->setValue(def.size);
                        applyColor(textColor, def.color, QStringLiteral("Text Color"), setTextColor);
                    }),
                    colorRow);
        connect(size, QOverload<int>::of(&QSpinBox::valueChanged), this, [style](int px) {
            style(QStringLiteral("Text Size"), [px](Layer &l) { l.text.size = px; });
        });

        // Bold, italic, underline, strikethrough
        auto *styleRow = new QHBoxLayout;
        QList<ToggleButton *> styleButtons;
        struct Toggle { const char *label, *tip; bool on; bool TextSource::*field; };
        const Toggle toggles[] = {{"B", "Bold", s.text.bold, &TextSource::bold},
                                  {"I", "Italic", s.text.italic, &TextSource::italic},
                                  {"U", "Underline", s.text.underline, &TextSource::underline},
                                  {"S", "Strikethrough", s.text.strike, &TextSource::strike}};
        for (const Toggle &t : toggles) {
            auto *b = new ToggleButton(QString::fromLatin1(t.label));
            b->setToolTip(QString::fromLatin1(t.tip));
            b->setChecked(t.on);
            b->setEnabled(!m_locked);
            QFont f = b->font();
            f.setBold(t.label[0] == 'B');
            f.setItalic(t.label[0] == 'I');
            f.setUnderline(t.label[0] == 'U');
            f.setStrikeOut(t.label[0] == 'S');
            b->setFont(f);
            styleRow->addWidget(b);
            styleButtons << b;
            m_animate->attach(b, {QStringLiteral("text/") + (t.field == &TextSource::strike ? QStringLiteral("strike") : QString::fromLatin1(t.tip).toLower())});
            auto field = t.field;
            connect(b, &QToolButton::toggled, this, [style, field, tip = QString::fromLatin1(t.tip)](bool on) {
                style(tip, [field, on](Layer &l) { l.text.*field = on; });
            });
        }
        styleRow->addStretch();
        fmt->addRow(new ResetLabel(QStringLiteral("Style"), [styleButtons] { // plain
                        for (ToggleButton *b : styleButtons) b->setChecked(false);
                    }),
                    styleRow);

        // Alignment: left, center, right, justified; top, middle, bottom
        auto *hAlign = new QComboBox;
        hAlign->addItem(QStringLiteral("Left"), int(Qt::AlignLeft));
        hAlign->addItem(QStringLiteral("Center"), int(Qt::AlignHCenter));
        hAlign->addItem(QStringLiteral("Right"), int(Qt::AlignRight));
        hAlign->addItem(QStringLiteral("Justified"), int(Qt::AlignJustify));
        auto *vAlign = new QComboBox;
        vAlign->addItem(QStringLiteral("Top"), int(Qt::AlignTop));
        vAlign->addItem(QStringLiteral("Middle"), int(Qt::AlignVCenter));
        vAlign->addItem(QStringLiteral("Bottom"), int(Qt::AlignBottom));
        const int h0 = hAlign->findData(int(s.text.align & Qt::AlignHorizontal_Mask));
        const int v0 = vAlign->findData(int(s.text.align & Qt::AlignVertical_Mask));
        hAlign->setCurrentIndex(h0 >= 0 ? h0 : 0);
        vAlign->setCurrentIndex(v0 >= 0 ? v0 : 0);
        hAlign->setEnabled(!m_locked);
        vAlign->setEnabled(!m_locked);
        auto *alignRow = new QHBoxLayout;
        alignRow->addWidget(hAlign);
        alignRow->addWidget(vAlign);
        auto setAlign = [style, hAlign, vAlign] {
            const int a = hAlign->currentData().toInt() | vAlign->currentData().toInt();
            style(QStringLiteral("Text Alignment"), [a](Layer &l) { l.text.align = Qt::Alignment(a); });
        };
        fmt->addRow(new ResetLabel(QStringLiteral("Align"), [hAlign, vAlign, def, setAlign] {
                        hAlign->setCurrentIndex(std::max(0, hAlign->findData(int(def.align & Qt::AlignHorizontal_Mask))));
                        vAlign->setCurrentIndex(std::max(0, vAlign->findData(int(def.align & Qt::AlignVertical_Mask))));
                        setAlign();
                    }),
                    alignRow);
        m_animate->attach(hAlign, {QStringLiteral("text/align")});
        m_animate->attach(vAlign, {QStringLiteral("text/v_align")});
        trackLayoutRow(fmt, alignRow, {QStringLiteral("text/align"), QStringLiteral("text/v_align")});
        connect(hAlign, QOverload<int>::of(&QComboBox::activated), this, setAlign);
        connect(vAlign, QOverload<int>::of(&QComboBox::activated), this, setAlign);

        const auto lh = limitsOf(QStringLiteral("text/line_height"), 0.1, 10);
        auto *lineSp = spin(s.text.lineHeight, lh.first, lh.second, 0.05, 2, QStringLiteral(" ×"));
        lineSp->setToolTip(QStringLiteral("Space between the lines (1 = the font's own)"));
        fmt->addRow(new ResetLabel(QStringLiteral("Line spacing"), [lineSp, def] { lineSp->setValue(def.lineHeight); }), lineSp);
        connect(lineSp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [style](double x) {
            style(QStringLiteral("Line Spacing"), [x](Layer &l) { l.text.lineHeight = float(x); });
        });
        const auto ls = limitsOf(QStringLiteral("text/letter_spacing"), -200, 500);
        auto *letterSp = spin(s.text.letterSpacing, ls.first, ls.second, 0.5, 1, QStringLiteral(" px"));
        letterSp->setToolTip(QStringLiteral("Space added between the letters"));
        fmt->addRow(new ResetLabel(QStringLiteral("Letter spacing"), [letterSp, def] { letterSp->setValue(def.letterSpacing); }),
                    letterSp);
        connect(letterSp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [style](double x) {
            style(QStringLiteral("Letter Spacing"), [x](Layer &l) { l.text.letterSpacing = float(x); });
        });

        // Outline
        const auto ol = limitsOf(QStringLiteral("text/outline/width"), 0, 200);
        auto *outline = spin(s.text.outline, ol.first, ol.second, 0.5, 1, QStringLiteral(" px"));
        auto *outlineRow = new QHBoxLayout;
        outlineRow->addWidget(outline);
        const ColorSet setOutlineColor = [](Layer &l, const QColor &c) { l.text.outlineColor = c; };
        QPushButton *outlineColor = colorButton(s.text.outlineColor, QStringLiteral("Outline Color"), setOutlineColor);
        outlineRow->addWidget(outlineColor);
        outlineRow->addStretch();
        fmt->addRow(new ResetLabel(QStringLiteral("Outline"), [outline, outlineColor, def, applyColor, setOutlineColor] {
                        outline->setValue(def.outline);
                        applyColor(outlineColor, def.outlineColor, QStringLiteral("Outline Color"), setOutlineColor);
                    }),
                    outlineRow);
        connect(outline, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [style](double x) {
            style(QStringLiteral("Text Outline"), [x](Layer &l) { l.text.outline = float(x); });
        });

        for (auto [field, path] : std::initializer_list<std::pair<QWidget *, const char *>>{
                 {size, "text/size"}, {lineSp, "text/line_height"},
                 {letterSp, "text/letter_spacing"}, {outline, "text/outline/width"}})
            m_animate->attach(field, {QString::fromLatin1(path)});
        m_animate->attach(textColor, {"text/color/r", "text/color/g", "text/color/b", "text/color/a"});
        m_animate->attach(outlineColor, {"text/outline/color/r", "text/outline/color/g", "text/outline/color/b", "text/outline/color/a"});
        m_textColors.push_back({textColor, QStringLiteral("text/color")});
        m_textColors.push_back({outlineColor, QStringLiteral("text/outline/color")});
        m_animate->attach(fmt->labelForField(colorRow), {"text/size", "text/color/r", "text/color/g", "text/color/b", "text/color/a"});
        m_animate->attach(fmt->labelForField(lineSp), {QStringLiteral("text/line_height")});
        m_animate->attach(fmt->labelForField(letterSp), {QStringLiteral("text/letter_spacing")});
        m_animate->attach(fmt->labelForField(outlineRow), {"text/outline/width", "text/outline/color/r", "text/outline/color/g", "text/outline/color/b", "text/outline/color/a"});

        // Shadow
        auto *shadow = new FlagBox(QStringLiteral("Enable"));
        shadow->setChecked(s.text.shadow);
        shadow->setEnabled(!m_locked);
        auto *shadowRow = new QHBoxLayout;
        shadowRow->addWidget(shadow);
        const ColorSet setShadowColor = [](Layer &l, const QColor &c) { l.text.shadowColor = c; };
        QPushButton *shadowColor = colorButton(s.text.shadowColor, QStringLiteral("Shadow Color"), setShadowColor);
        shadowRow->addWidget(shadowColor);
        const auto sxl = limitsOf(QStringLiteral("text/shadow/x"), -2000, 2000);
        const auto syl = limitsOf(QStringLiteral("text/shadow/y"), -2000, 2000);
        auto *sx = spin(s.text.shadowX, sxl.first, sxl.second, 1, 0, QStringLiteral(" x"));
        auto *sy = spin(s.text.shadowY, syl.first, syl.second, 1, 0, QStringLiteral(" y"));
        shadowRow->addWidget(sx);
        shadowRow->addWidget(sy);
        fmt->addRow(new ResetLabel(QStringLiteral("Shadow"), [shadow, shadowColor, sx, sy, def, applyColor, setShadowColor] {
                        shadow->setChecked(def.shadow);
                        applyColor(shadowColor, def.shadowColor, QStringLiteral("Shadow Color"), setShadowColor);
                        sx->setValue(def.shadowX);
                        sy->setValue(def.shadowY);
                    }),
                    shadowRow);
        m_animate->attach(shadow, {QStringLiteral("text/shadow/enable")});
        m_animate->attach(sx, {QStringLiteral("text/shadow/x")});
        m_animate->attach(sy, {QStringLiteral("text/shadow/y")});
        m_animate->attach(shadowColor, {"text/shadow/color/r", "text/shadow/color/g", "text/shadow/color/b", "text/shadow/color/a"});
        m_textColors.push_back({shadowColor, QStringLiteral("text/shadow/color")});
        m_animate->attach(fmt->labelForField(shadowRow), {"text/shadow/x", "text/shadow/y", "text/shadow/color/r", "text/shadow/color/g", "text/shadow/color/b", "text/shadow/color/a"});
        connect(shadow, &QCheckBox::toggled, this, [style](bool on) {
            style(QStringLiteral("Text Shadow"), [on](Layer &l) { l.text.shadow = on; });
        });
        connect(sx, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [style](double x) {
            style(QStringLiteral("Text Shadow"), [x](Layer &l) { l.text.shadowX = float(x); });
        });
        connect(sy, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [style](double x) {
            style(QStringLiteral("Text Shadow"), [x](Layer &l) { l.text.shadowY = float(x); });
        });

        v->addLayout(fmt);
        v->addStretch();
        return g;
    }

    const bool media = (s.type == SourceType::Video && s.hasVideo) || (s.type == SourceType::Audio && s.hasAudio);
    if (media) {
        if (s.type == SourceType::Video) { // how the decoded frames reach the GPU: known once they come
            auto *facts = new QFormLayout;
            facts->setContentsMargins(0, 2, 0, 2);
            facts->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
            auto *k = new QLabel(QStringLiteral("Picture"));
            k->setStyleSheet("color:#8a8a8e;");
            m_pictureFact = new QLabel(QStringLiteral("…"));
            m_pictureFact->setTextInteractionFlags(Qt::TextSelectableByMouse);
            m_pictureFact->setToolTip(QStringLiteral("How the decoded frames reach the GPU. A pixel layout (yuv420p, nv12, "
                                                     "p010…) or HAP textures are converted by the GPU; \"converted on "
                                                     "the CPU\" or \"decoded on the CPU\" costs processor time."));
            facts->addRow(k, m_pictureFact);
            v->addLayout(facts);
        }
        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(4);
        grid->setColumnStretch(1, 1);
        auto rowLabel = [&](int row, const QString &text, std::function<void()> reset = {}) {
            QLabel *l = reset ? static_cast<QLabel *>(new ResetLabel(text, std::move(reset))) : new QLabel(text);
            l->setStyleSheet("color:#8a8a8e;");
            l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            grid->addWidget(l, row, 0);
            return l;
        };
        auto iconButton = [&](const QIcon &icon, const QString &tip, bool checkable) {
            auto *b = new QToolButton;
            b->setIcon(icon);
            b->setIconSize(QSize(18, 18));
            b->setToolTip(tip);
            b->setCheckable(checkable);
            b->setAutoRaise(true);
            b->setStyleSheet(QStringLiteral("QToolButton { padding:3px 7px; border-radius:3px; }"
                                            "QToolButton:checked { background:%1; }"
                                            "QToolButton:hover { background:#3a3a3f; }")
                                 .arg(theme::css()));
            return b;
        };

        // Play: direction and pause, then go to start and step one frame. Its label: the play state and "restart on
        // recall" in presets (right-click), as any value
        QLabel *playLabel = rowLabel(0, QStringLiteral("Play"));
        m_animate->attach(playLabel, {QStringLiteral("play"), QStringLiteral("restart")});
        auto *playRow = new QHBoxLayout;
        playRow->setSpacing(2);
        m_playButtons = new QButtonGroup(g);
        const struct { TransportIcon icon; const char *tip; } kPlay[] = {
            {TransportIcon::PlayBack, "Play backwards"},
            {TransportIcon::Pause, "Pause"},
            {TransportIcon::PlayForward, "Play"},
        };
        for (int i = 0; i < 3; ++i) {
            auto *b = iconButton(transportIcon(kPlay[i].icon), QString::fromUtf8(kPlay[i].tip), true);
            b->setProperty("allowLocked", true); // the transport stays available on a locked layer
            m_playButtons->addButton(b, i);
            playRow->addWidget(b);
        }
        m_playButtons->setExclusive(true);
        playRow->addSpacing(10);
        const double frame = 1.0 / std::max(1.0, s.fps > 0 ? s.fps : 25.0);
        const struct { TransportIcon icon; const char *tip; double step; } kStep[] = {
            {TransportIcon::ToStart, "Back to the in point", 0},
            {TransportIcon::StepBack, "One frame back", -1},
            {TransportIcon::StepForward, "One frame on", +1},
        };
        for (const auto &k : kStep) {
            auto *b = iconButton(transportIcon(k.icon), QString::fromUtf8(k.tip), false);
            b->setProperty("allowLocked", true);
            playRow->addWidget(b);
            const double step = k.step;
            connect(b, &QToolButton::clicked, this, [this, step, frame] {
                double to = 0;
                {
                    Engine::Lock lk(&m_engine->mutex());
                    Layer *l = m_engine->layer(m_layer);
                    if (!l) return;
                    to = step == 0 ? l->inPoint : l->position() + step * frame;
                }
                m_engine->seekLayer(m_layer, to);
                refreshDynamic();
            });
        }
        playRow->addStretch();
        grid->addLayout(playRow, 0, 1);

        // Mode: what happens at the end, then the in / out points at the playhead
        auto *modes = new QButtonGroup(g);
        QLabel *modeLabel = rowLabel(1, QStringLiteral("Mode"), [this, modes] { // the one new media get (Settings)
            const int def = int(m_engine->defaultPlayMode());
            if (QAbstractButton *b = modes->button(def)) b->setChecked(true);
            setValue(QStringLiteral("play_mode"), playModeKey(PlayMode(def)), QStringLiteral("Change Play Mode"));
            emit layerChanged();
        });
        m_animate->attach(modeLabel, {QStringLiteral("play_mode")});
        auto *modeRow = new QHBoxLayout;
        modeRow->setSpacing(2);
        modes->setExclusive(true);
        const struct { PlayMode mode; const char *tip; } kModes[] = {
            {PlayMode::Loop, "Loop: starts again from the beginning"},
            {PlayMode::OneShot, "One-shot: plays once and freezes on the last frame"},
            {PlayMode::PingPong, "Ping-pong: forwards, then backwards, and so on"},
            {PlayMode::Stop, "Stop: plays once, then goes black (and silent)"},
        };
        for (const auto &m : kModes) {
            auto *b = iconButton(playModeIcon(int(m.mode)), QString::fromUtf8(m.tip), true);
            b->setChecked(s.mode == m.mode);
            modes->addButton(b, int(m.mode));
            modeRow->addWidget(b);
        }
        modeRow->addSpacing(10);
        auto *markIn = iconButton(transportIcon(TransportIcon::MarkIn),
                                  QStringLiteral("In point at the playhead (I)"), false);
        auto *markOut = iconButton(transportIcon(TransportIcon::MarkOut),
                                   QStringLiteral("Out point at the playhead (O)"), false);
        modeRow->addWidget(markIn);
        modeRow->addWidget(markOut);
        modeRow->addStretch();
        grid->addLayout(modeRow, 1, 1);
        connect(modes, &QButtonGroup::idClicked, this, [this](int id) {
            setValue(QStringLiteral("play_mode"), playModeKey(PlayMode(id)), QStringLiteral("Change Play Mode"));
            emit layerChanged();
        });
        connect(markIn, &QToolButton::clicked, this, [this] { emit setInOutRequested(true); });
        connect(markOut, &QToolButton::clicked, this, [this] { emit setInOutRequested(false); });
        v->addLayout(grid);
        v->addWidget(separator());

        // Position and played range: one bar each, dragged or typed
        auto *bars = new QGridLayout;
        bars->setHorizontalSpacing(8);
        bars->setVerticalSpacing(4);
        bars->setColumnStretch(1, 1);
        auto barLabel = [&](int row, const QString &text, std::function<void()> reset) {
            auto *l = new ResetLabel(text, std::move(reset));
            l->setStyleSheet("color:#8a8a8e;");
            l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            bars->addWidget(l, row, 0);
            return l;
        };
        m_position = new SliderField;
        m_position->setRange(0, std::max(0.01, s.duration));
        m_position->setDecimals(2);
        m_position->setSuffix(QStringLiteral(" s"));
        m_position->setSingleStep(frame);
        m_position->setTicks(10);
        m_position->setSnaps({0});
        m_position->setProperty("allowLocked", true); // seeking is not an edit
        m_position->setToolTip(QStringLiteral("Playhead — drag the bar, type the time, or use the wheel"));
        barLabel(0, QStringLiteral("Position"), [this] { m_engine->seekLayer(m_layer, 0); });
        bars->addWidget(m_position, 0, 1);

        m_loop = new RangeField;
        m_loop->setRange(0, std::max(0.01, s.duration));
        m_loop->setDecimals(2);
        m_loop->setSuffix(QStringLiteral(" s"));
        m_loop->setValues(s.inPoint, s.outPoint < 0 ? s.duration : s.outPoint);
        m_loop->setToolTip(QStringLiteral("Played range: playback, loops and ping-pong stay between these two points"));
        const double duration0 = s.duration;
        QLabel *loopLabel = barLabel(1, QStringLiteral("Loop"), [this] {
            setValues({{"in", 0.0}, {"out", -1.0}}, QStringLiteral("Clear In / Out Points"));
            rebuild();
        });
        bars->addWidget(m_loop, 1, 1);
        // The in / out points in presets (right-click the bar or its name), as any value
        m_animate->attach({loopLabel, m_loop}, {QStringLiteral("in"), QStringLiteral("out")});
        v->addLayout(bars);

        // Playback is not a project edit: no undo on play, pause or seek.
        connect(m_playButtons, &QButtonGroup::idClicked, this, [this](int id) {
            double speed = 1;
            {
                Engine::Lock lk(&m_engine->mutex());
                Layer *l = m_engine->layer(m_layer);
                if (!l) return;
                speed = std::abs(l->speed) < 1e-6 ? 1.0 : std::abs(l->speed);
            }
            if (id == 1) {
                m_engine->setLayerPlaying(m_layer, false);
            } else {
                m_engine->setLayerSpeed(m_layer, id == 0 ? -speed : speed);
                m_engine->setLayerPlaying(m_layer, true);
            }
            refreshDynamic();
            emit layerChanged();
        });
        connect(m_position, &SliderField::valueEdited, this, [this](double t) { m_engine->seekLayer(m_layer, t); });
        connect(m_loop, &RangeField::edited, this, [this, duration0](bool low, double t) {
            if (low) setValue(QStringLiteral("in"), t, QStringLiteral("Set In Point"));
            else setValue(QStringLiteral("out"), t >= duration0 - 1e-6 ? -1.0 : t, QStringLiteral("Set Out Point"));
        });
    }

    m_synthFields.clear();
    {
        Engine::Lock lock(&m_engine->mutex());
        const Layer *layer = m_engine->layer(m_layer);
        if (layer && layer->audio && layer->audio->generator()) {
            auto *synth = layer->audio->generator();
            for (int i = 0; i < synth->parameterCount(); ++i) {
                if (!synth->external() && synth->noise() && i < AudioGenerator::Attack && i != AudioGenerator::Gain)
                    continue;
                if (!synth->external() &&
                    (synth->granular() ? (i == AudioGenerator::Ratio || i == AudioGenerator::Index)
                                       : (i >= AudioGenerator::GrainMs && i <= AudioGenerator::Position)))
                    continue;
                const auto &p = synth->parameter(i);
                const QString key = QStringLiteral("synth/") + p.key;
                auto *row = new QHBoxLayout;
                auto *label = new QLabel(QString::fromUtf8(p.label));
                auto *field = new SliderField;
                field->setRange(p.low, p.high);
                field->setDecimals(3);
                field->setValue(synth->value(i));
                row->addWidget(label);
                row->addWidget(field, 1);
                v->addLayout(row);
                m_synthFields.push_back({field, key});
                m_animate->attach({label, field}, {key});
                connect(field, &SliderField::valueEdited, this, [this, key](double value) {
                    setValue(key, value, QStringLiteral("Change synthesis parameter"));
                    emit layerChanged();
                });
            }
            if (synth->sonification()) { // it listens to a picture: which viewport's, and the color it sees
                std::vector<std::pair<quint64, QString>> viewports;
                for (int vi : m_engine->viewports())
                    if (const Layer *vl = m_engine->layer(vi)) viewports.push_back({vl->id, vl->name});
                auto *row = new QHBoxLayout;
                auto *label = new QLabel(QStringLiteral("Watches"));
                auto *pick = new QComboBox;
                pick->setObjectName(QStringLiteral("sonificationViewport"));
                pick->addItem(viewports.empty() ? QStringLiteral("The composition (no viewport)")
                                                : QStringLiteral("First viewport (%1)").arg(viewports.front().second),
                              QVariant::fromValue<qulonglong>(0));
                for (const auto &[id, name] : viewports) pick->addItem(name, QVariant::fromValue<qulonglong>(id));
                if (layer->sonificationViewport && pick->findData(QVariant::fromValue<qulonglong>(layer->sonificationViewport)) < 0)
                    pick->addItem(QStringLiteral("(viewport gone: the first one)"), QVariant::fromValue<qulonglong>(layer->sonificationViewport));
                pick->setCurrentIndex(std::max(0, pick->findData(QVariant::fromValue<qulonglong>(layer->sonificationViewport))));
                pick->setEnabled(!m_locked);
                pick->setToolTip(QStringLiteral("The viewport whose picture this instrument listens to: its average color, as the "
                                                "viewport outputs it (its own size, opacity and color space)."));
                auto *swatch = new QLabel;
                swatch->setObjectName(QStringLiteral("sonificationColor"));
                swatch->setFixedSize(28, 18);
                swatch->setToolTip(QStringLiteral("The average color of that viewport, as the instrument hears it"));
                m_sonificationSwatch = swatch;
                row->addWidget(label);
                row->addWidget(pick, 1);
                row->addWidget(swatch);
                v->addLayout(row);
                connect(pick, &QComboBox::activated, this, [this, pick](int k) {
                    const quint64 id = pick->itemData(k).toULongLong();
                    editSource(QStringLiteral("Change Sonification Viewport"),
                               [this, id] { m_engine->setLayerSonificationViewport(m_layer, id); });
                    emit layerChanged();
                });
            }
        }
    }

    if (media && s.hasAudio) {
        // Sound: layer volume (undoable), mute, level
        auto *row = new QHBoxLayout;
        row->setSpacing(8);
        auto *vol = new SliderField;
        vol->setRange(0, 200);
        vol->setDecimals(0);
        vol->setSuffix(QStringLiteral(" %"));
        vol->setSingleStep(5);
        vol->setTicks(8);
        vol->setSnaps({100});
        vol->setValue(s.volume * 100);
        m_volume = vol;
        vol->setToolTip(QStringLiteral("Layer volume (100% = original level). Hiding the layer also silences it."));
        auto *icon = new ResetLabel(QStringLiteral("Volume"), [vol] { vol->setValue(100); emit vol->valueEdited(100); });
        icon->setStyleSheet("color:#8a8a8e;");
        icon->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *mute = new FlagBox(QStringLiteral("Mute"));
        m_animate->attach(mute, {QStringLiteral("mute")});
        mute->setChecked(s.muted);
        row->addWidget(icon);
        row->addWidget(vol, 1);
        row->addWidget(mute);
        m_animate->attach({icon, vol}, {QStringLiteral("volume")});
        v->addLayout(row);
        m_meter = new LevelMeter(QStringLiteral("Layer output"));
        v->addWidget(m_meter);
        connect(vol, &SliderField::valueEdited, this, [this](double pct) {
            setValue(QStringLiteral("volume"), pct / 100.0, QStringLiteral("Change Volume"));
            emit layerChanged();
        });
        connect(mute, &QCheckBox::toggled, this, [this](bool on) {
            setValue(QStringLiteral("mute"), on, QStringLiteral("Toggle Mute"));
            emit layerChanged();
        });
    }

    if (s.type == SourceType::Isf && s.hasGenerator) {
        // Its inputs back to their defaults, at the top
        auto *reset = new QPushButton(QStringLiteral("Reset to Defaults"));
        reset->setToolTip(QStringLiteral("Every parameter of the shader back to its default value (one undo step)"));
        auto *resetRow = new QHBoxLayout;
        resetRow->addWidget(reset);
        resetRow->addStretch();
        v->addLayout(resetRow);
        auto *res = new QHBoxLayout;
        auto *w = new IntBox, *h = new IntBox;
        for (auto *sb : {w, h}) {
            sb->setRange(1, 16384);
            sb->setKeyboardTracking(false);
        }
        w->setValue(s.genW);
        h->setValue(s.genH);
        auto *fit = new QPushButton(QStringLiteral("= composition"));
        auto *reload = toolButton(QStringLiteral("⟳"), QStringLiteral("Reload shader from disk"));
        res->addWidget(new ResetLabel(QStringLiteral("Resolution"), [fit] { fit->click(); }));
        res->addWidget(w);
        res->addWidget(new QLabel(QStringLiteral("×")));
        res->addWidget(h);
        res->addWidget(fit);
        res->addWidget(reload);
        reload->setVisible(builtInIsfSource(s.sourcePath).isEmpty()); // a shader of the application has no file
        v->addLayout(res);
        auto apply = [this, w, h] { m_engine->setGeneratorSize(m_layer, w->value(), h->value()); };
        connect(w, qOverload<int>(&QSpinBox::valueChanged), this, apply);
        connect(h, qOverload<int>(&QSpinBox::valueChanged), this, apply);
        connect(fit, &QPushButton::clicked, this, [this, w, h] {
            const QSize c = m_engine->compositionSize();
            w->setValue(c.width());
            h->setValue(c.height());
        });
        connect(reload, &QToolButton::clicked, this, [this] {
            IsfInstance *gen;
            {
                Engine::Lock lk(&m_engine->mutex());
                Layer *ly = m_engine->layer(m_layer);
                gen = ly ? ly->generator.get() : nullptr;
            }
            m_engine->reloadIsf(gen);
            rebuild();
        });
        if (isLineGeneratorPath(s.sourcePath)) { // its lines: drawn in the preview, removed, given LED strips
            auto *lines = new QGroupBox(QStringLiteral("Lines"));
            auto *lv = new QVBoxLayout(lines);
            auto *count = new QLabel;
            const quint64 id = m_layerId;
            auto showCount = [this, count, id] {
                const auto ls = m_engine->layerLines(id);
                int segments = 0;
                for (const LinePath &l : ls) segments += l.segmentCount();
                count->setText(ls.empty() ? QStringLiteral("No line: draw one in the preview")
                                          : QStringLiteral("%1 line%2, %3 segment%4").arg(ls.size()).arg(ls.size() == 1 ? "" : "s")
                                                .arg(segments).arg(segments == 1 ? "" : "s"));
            };
            showCount();
            connect(m_engine, &Engine::layerLinesChanged, count, [showCount, id](quint64 layer) {
                if (layer == id) showCount();
            });
            auto *row = new QHBoxLayout;
            auto *draw = new QPushButton(QStringLiteral("Draw Line"));
            draw->setToolTip(QStringLiteral("Draw a line in the preview: click its points; a double-click, Enter or its first "
                                            "point again ends it; Backspace takes a point back; Esc cancels.\n"
                                            "Then drag its points, drag a segment to move the line, double-click a segment "
                                            "to add a point, Delete removes the selected point or line."));
            auto *clear = new QPushButton(QStringLiteral("Delete All"));
            row->addWidget(draw);
            row->addWidget(clear);
            row->addStretch();
            lv->addWidget(count);
            lv->addLayout(row);
            connect(draw, &QPushButton::clicked, this, &LayerInspector::lineDrawRequested);
            connect(clear, &QPushButton::clicked, this, [this, id] {
                const auto before = m_engine->layerLines(id);
                if (before.empty()) return;
                m_undo->push(new cmd::SetLines(m_engine, id, before, {}, QStringLiteral("Delete All Lines")));
            });
            // Pixel mapping: a strip on each line
            auto *strip = new QHBoxLayout;
            auto *type = new QComboBox;
            for (const FixtureType &t : builtInFixtureTypes())
                if (takesFixtureGrid(t)) type->addItem(t.name, t.id);
            type->setCurrentIndex(std::max(0, type->findData(QStringLiteral("rgb"))));
            auto *pixels = new IntBox;
            pixels->setRange(1, 4096);
            pixels->setValue(30);
            pixels->setSuffix(QStringLiteral(" px"));
            pixels->setToolTip(QStringLiteral("Pixels of each strip, spread along its line"));
            auto *add = new QPushButton(QStringLiteral("Fixtures on Lines"));
            add->setToolTip(QStringLiteral("A pixel mapping fixture on each line, its pixels along it, in DMX order from the "
                                           "line's first point. It follows the line when the line or the layer moves."));
            strip->addWidget(type);
            strip->addWidget(pixels);
            strip->addWidget(add, 1);
            lv->addLayout(strip);
            connect(add, &QPushButton::clicked, this, [this, type, pixels] {
                emit lineFixturesRequested(type->currentData().toString(), pixels->value());
            });
            v->addWidget(lines);
        }
        auto *params = new ParamPanel(m_engine, m_undo, m_layer, -1, ParamPanel::NoSpeed | ParamPanel::NoReset);
        params->setCoTargets(coTargets());
        connect(reset, &QPushButton::clicked, params, &ParamPanel::resetToDefaults);
        m_generatorParams = params;
        params->attachAnimate(m_animate);
        connect(params, &ParamPanel::rebuildRequested, this, &LayerInspector::rebuild, Qt::QueuedConnection);
        v->addWidget(params);
    }
    if (s.type == SourceType::None) {
        auto *hint = new QLabel(QStringLiteral("Nothing to set: the layer is empty."));
        hint->setStyleSheet("color:#888;");
        v->addWidget(hint);
    }
    return g;
}

// Part of the source picture used: preview with a rectangle whose sides are dragged, numeric fields in %
QWidget *LayerInspector::buildRoi(const LayerValues &s)
{
    auto *box = new QGroupBox;
    auto *v = new QVBoxLayout(box);
    auto *head = new QHBoxLayout;
    auto *title = new ResetLabel(QStringLiteral("<b>ROI</b>"), [this] {
        setRoi(Layer::fullRoi());
        rebuild();
    });
    title->setToolTip(QStringLiteral("Part of the source picture used by the layer — click to use the whole picture"));
    head->addWidget(title);
    head->addStretch();
    v->addLayout(head);
    m_roi = new RoiEditor;
    m_roi->setAspect(s.aspect);
    m_roi->setRoi(s.roi);
    v->addWidget(m_roi);
    m_animate->attach({title, m_roi.data()}, {"roi/left", "roi/top", "roi/right", "roi/bottom"});
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(1, 1);
    // Left / right and top / bottom are two ranges: one bar each, the bounds typed at either end
    RangeField *bars[2];
    const double bounds[2][2] = {{s.roi.left(), s.roi.right()}, {s.roi.top(), s.roi.bottom()}};
    static const char *kRows[] = {"Left · Right", "Top · Bottom"};
    for (int k = 0; k < 2; ++k) {
        auto *bar = new RangeField;
        bar->setRange(0, 100);
        bar->setDecimals(1);
        bar->setSuffix(QStringLiteral(" %"));
        bar->setValues(bounds[k][0] * 100, bounds[k][1] * 100);
        bars[k] = bar;
        auto *name = new ResetLabel(QString::fromUtf8(kRows[k]), [bar] {
            bar->setValues(0, 100);
            emit bar->editingFinished();
        });
        name->setStyleSheet("color:#8a8a8e;");
        name->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(name, k, 0);
        grid->addWidget(bar, k, 1);
        m_animate->attach({name, bar}, k == 0 ? QStringList{"roi/left", "roi/right"} : QStringList{"roi/top", "roi/bottom"});
    }
    v->addLayout(grid);
    auto apply = [this](const QRectF &r) {
        setRoi(r);
        emit layerChanged();
    };
    QPointer<RoiEditor> editor = m_roi;
    connect(m_roi, &RoiEditor::roiEdited, this, [apply, bars](const QRectF &r) {
        QSignalBlocker b0(bars[0]), b1(bars[1]);
        bars[0]->setValues(r.left() * 100, r.right() * 100);
        bars[1]->setValues(r.top() * 100, r.bottom() * 100);
        apply(r);
    });
    auto fromBars = [apply, bars, editor] {
        const double l = bars[0]->low() / 100, r = std::max(bars[0]->high() / 100, bars[0]->low() / 100 + 0.001);
        const double t = bars[1]->low() / 100, b = std::max(bars[1]->high() / 100, bars[1]->low() / 100 + 0.001);
        const QRectF rect(QPointF(l, t), QPointF(std::min(r, 1.0), std::min(b, 1.0)));
        if (editor) editor->setRoi(rect);
        apply(rect);
    };
    for (RangeField *bar : bars) {
        connect(bar, &RangeField::edited, this, [fromBars] { fromBars(); });
        connect(bar, &RangeField::editingFinished, this, [fromBars] { fromBars(); });
    }
    return box;
}

QWidget *LayerInspector::buildColor(const LayerValues &s)
{
    auto *g = new QWidget;
    auto *outer = new QVBoxLayout(g);
    outer->setContentsMargins(0, 0, 0, 0);
    // Switch of the whole section: the values are kept, they are simply not applied
    auto *colorOn = new FlagBox(QStringLiteral("Color"));
    m_animate->attach(colorOn, {QStringLiteral("color/enable")});
    colorOn->setChecked(s.color.enabled);
    colorOn->setStyleSheet("font-weight:bold;");
    colorOn->setToolTip(QStringLiteral("Apply the color of this layer.\nOff: every value is kept, the picture is left alone."));
    outer->addWidget(colorOn);
    auto *body = new QWidget; // everything the switch above turns off
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(0, 0, 0, 0);
    body->setEnabled(s.color.enabled);
    outer->addWidget(body);
    connect(colorOn, &QCheckBox::toggled, this, [this, body](bool on) {
        body->setEnabled(on);
        setValue(QStringLiteral("color/enable"), on, QStringLiteral("Switch Color"));
    });

    // Mask: where the whole section applies, from another layer's picture stretched over this one
    {
        std::vector<std::pair<quint64, QString>> masks; // layers that can mask it (no viewport, no feedback)
        {
            Engine::Lock lk(&m_engine->mutex());
            const Layer *self = m_engine->layer(m_layer);
            for (int k = 0; self && k < m_engine->layerCount(); ++k) {
                const Layer *o = m_engine->layer(k);
                if (!o || o->isViewport || o->id == self->id || m_engine->layerDependsOn(o->id, self->id)) continue;
                masks.push_back({o->id, o->isGroup ? o->name + QStringLiteral(" (group)") : o->name});
            }
        }
        auto *row = new QHBoxLayout;
        auto *label = new ResetLabel(QStringLiteral("Mask"), [this] { setProp(cmd::SetLayerProp::ColorMask, QVariant(qulonglong(0))); });
        auto *pick = new QComboBox;
        pick->addItem(QStringLiteral("None — everywhere"), QVariant(qulonglong(0)));
        int current = 0;
        for (const auto &[id, n] : masks) {
            pick->addItem(n, QVariant(qulonglong(id)));
            if (id == s.color.maskLayer) current = pick->count() - 1;
        }
        if (s.color.maskLayer && current == 0) { // a mask that cannot be chosen any more (gone): shown, not chosen
            pick->addItem(QStringLiteral("(gone)"), QVariant(qulonglong(s.color.maskLayer)));
            current = pick->count() - 1;
        }
        pick->setCurrentIndex(current);
        pick->setToolTip(QStringLiteral("Where the color applies: fully where the chosen layer's picture is white, not at "
                                        "all where it is black or transparent, in proportion in between. That picture is "
                                        "stretched over this layer's, after its ROI. The layer can stay hidden."));
        auto *inv = new FlagBox(QStringLiteral("Invert"));
        m_animate->attach(inv, {QStringLiteral("mask/invert")});
        inv->setChecked(s.color.maskInvert);
        inv->setEnabled(s.color.maskLayer != 0);
        row->addWidget(label);
        row->addWidget(pick, 1);
        row->addWidget(inv);
        v->addLayout(row);
        connect(pick, &QComboBox::activated, this, [this, pick, inv](int i) {
            const quint64 id = pick->itemData(i).toULongLong();
            QString err;
            const QVariant before = cmd::SetLayerProp::read(m_engine, m_layerId, cmd::SetLayerProp::ColorMask);
            if (!m_engine->setColorMask(m_layer, id, inv->isChecked(), &err)) { // refused: feedback
                if (!err.isEmpty()) QMessageBox::warning(this, QStringLiteral("Mask"), err);
            } else if (before.toULongLong() != id) {
                m_undo->push(new cmd::SetLayerProp(m_engine, m_layerId, cmd::SetLayerProp::ColorMask, before, QVariant(qulonglong(id))));
            }
            QMetaObject::invokeMethod(this, &LayerInspector::rebuild, Qt::QueuedConnection);
        });
        connect(inv, &QCheckBox::toggled, this, [this](bool on) { setValue(QStringLiteral("mask/invert"), on, QStringLiteral("Invert Color Mask")); });
    }

    // Balance first, as in DaVinci Resolve: temperature (blue / yellow) and tint (green / magenta)
    {
        auto *box = new QGroupBox;
        auto *grid = new QGridLayout(box);
        grid->addWidget(new QLabel(QStringLiteral("<b>Balance</b>")), 0, 0, 1, 4);
        struct Def {
            const char *name, *path;
            double range, value;
            QGradientStops gradient;
            QPointer<SliderField> *field;
            bool on;
        } defs[] = {{"Temp", "temp", ColorAdjust::kTempRange, s.color.temp,
                     {{0.0, QColor("#3a7bff")}, {0.5, QColor("#888888")}, {1.0, QColor("#ffd23a")}},
                     &m_temp, s.color.tempOn},
                    {"Tint", "tint", ColorAdjust::kTintRange, s.color.tint,
                     {{0.0, QColor("#2fd04a")}, {0.5, QColor("#888888")}, {1.0, QColor("#e03ce0")}},
                     &m_tint, s.color.tintOn}};
        int row = 1;
        for (const Def &d : defs) {
            auto *bar = new SliderField;
            bar->setRange(-d.range, d.range);
            bar->setDecimals(d.range >= 1000 ? 0 : 1);
            bar->setSingleStep(d.range / 100);
            bar->setSnaps({0});
            bar->setGradient(d.gradient);
            bar->setValue(d.value);
            *d.field = bar;
            const QString path = QString::fromLatin1(d.path), title = QString::fromUtf8(d.name);
            auto *on = new FlagBox;
            on->setChecked(d.on);
            m_animate->attach(on, {QString::fromLatin1(d.path) + QStringLiteral("/enable")});
            on->setToolTip(QStringLiteral("Apply %1 (the value is kept either way)").arg(QString::fromUtf8(d.name)));
            connect(on, &QCheckBox::toggled, this,
                    [this, path, title](bool v) { setValue(path + "/enable", v, QStringLiteral("Switch ") + title); });
            grid->addWidget(on, row, 0);
            auto *name = new ResetLabel(QString::fromUtf8(d.name), [bar] {
                bar->setValue(0);
                emit bar->valueEdited(0);
            });
            grid->addWidget(name, row, 1);
            grid->addWidget(bar, row, 2, 1, 2);
            m_animate->attach({name, bar}, {QString::fromLatin1(d.path)});
            connect(bar, &SliderField::valueEdited, this,
                    [this, path, title](double x) { setValue(path, x, QStringLiteral("Change ") + title); });
            ++row;
        }
        grid->setColumnStretch(2, 1);
        v->addWidget(box);
    }

    auto toColor = [](const float c[3]) { return QColor::fromRgbF(c[0], c[1], c[2]); };
    m_colorAdd = new ColorEditor(QStringLiteral("Add"), Qt::black);
    m_colorAdd->setSettingsKey(QStringLiteral("ui/colorPickerMode/layer/%1/add").arg(m_layerId));
    m_colorAdd->setColor(toColor(s.color.add));
    m_colorAdd->setToolTip(QStringLiteral("Color added to the picture, as light (black: nothing added)"));
    m_colorRemove = new ColorEditor(QStringLiteral("Remove"), Qt::black);
    m_colorRemove->setSettingsKey(QStringLiteral("ui/colorPickerMode/layer/%1/remove").arg(m_layerId));
    m_colorRemove->setColor(toColor(s.color.remove));
    m_colorRemove->setToolTip(QStringLiteral("Color removed from the picture, as a filter (black: nothing removed)"));
    for (ColorEditor *ed : {m_colorAdd.data(), m_colorRemove.data()}) {
        auto *frame = new QGroupBox;
        auto *fv = new QVBoxLayout(frame);
        fv->addWidget(ed);
        v->addWidget(frame);
    }
    m_animate->attach(m_colorAdd, {"add/r", "add/g", "add/b"});
    m_animate->attach(m_colorRemove, {"remove/r", "remove/g", "remove/b"});
    m_colorAdd->setSwitch(true, s.color.addOn, QStringLiteral("Apply the added color (it is kept either way)"));
    m_colorRemove->setSwitch(true, s.color.removeOn, QStringLiteral("Apply the removed color (it is kept either way)"));
    auto rgb = [](const QString &key, const QColor &c) {
        return QJsonObject{{key + "/r", c.redF()}, {key + "/g", c.greenF()}, {key + "/b", c.blueF()}};
    };
    connect(m_colorAdd, &ColorEditor::colorEdited, this,
            [this, rgb](const QColor &c) { setValues(rgb("add", c), QStringLiteral("Change Added Color")); });
    connect(m_colorRemove, &ColorEditor::colorEdited, this,
            [this, rgb](const QColor &c) { setValues(rgb("remove", c), QStringLiteral("Change Removed Color")); });
    connect(m_colorAdd, &ColorEditor::switchToggled, this,
            [this](bool on) { setValue(QStringLiteral("add/enable"), on, QStringLiteral("Switch Added Color")); });
    connect(m_colorRemove, &ColorEditor::switchToggled, this,
            [this](bool on) { setValue(QStringLiteral("remove/enable"), on, QStringLiteral("Switch Removed Color")); });
    return g;
}

QWidget *LayerInspector::buildCompositing(const LayerValues &s)
{
    auto *g = new QWidget; // titled by its sub-tab
    auto *form = new QFormLayout(g);

    auto *opacity = new SliderField;
    opacity->setObjectName(QStringLiteral("inspectorOpacity"));
    opacity->setRange(0, 100);
    opacity->setDecimals(0);
    opacity->setSuffix(QStringLiteral(" %"));
    opacity->setSingleStep(1);
    opacity->setTicks(10);
    opacity->setSnaps({100});
    opacity->setValue(s.opacity * 100);
    m_opacity = opacity;
    auto *opacityLabel = new ResetLabel(QStringLiteral("Opacity"), [this, opacity] {
        opacity->setValue(100);
        setValue(QStringLiteral("opacity"), 1.0, QStringLiteral("Change Opacity"));
    });
    form->addRow(opacityLabel, opacity);
    m_animate->attach({opacityLabel, opacity}, {QStringLiteral("opacity")});
    connect(opacity, &SliderField::valueEdited, this, [this](double v) { setValue(QStringLiteral("opacity"), v / 100.0, QStringLiteral("Change Opacity")); });
    if (s.isViewport) return g; // drawn onto nothing: no blend, no routing

    auto *blend = new QComboBox;
    for (BlendMode m : kBlendModes) blend->addItem(blendModeName(m), int(m));
    blend->setCurrentIndex(blend->findData(int(s.blend)));
    auto *blendLabel = new ResetLabel(QStringLiteral("Blend Mode"), [blend] { blend->setCurrentIndex(0); });
    form->addRow(blendLabel, blend);
    m_animate->attach({blendLabel, blend}, {QStringLiteral("blend_mode")});
    connect(blend, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, blend](int i) {
                setValue(QStringLiteral("blend_mode"), blendModeKey(BlendMode(blend->itemData(i).toInt())),
                         QStringLiteral("Change Blend Mode"));
            });

    return g;
}

// The viewports it appears in: chosen at the top of the list (a group for everything inside it)
QWidget *LayerInspector::buildViewports(const LayerValues &s)
{
    // Viewports it appears in: chosen at the top of the list (a group for everything inside it)
    auto *routes = new QWidget;
    auto *rv = new QVBoxLayout(routes);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(4);
    auto *intro = new QLabel(QStringLiteral("How much of it each viewport shows (0 hides it). A click on a name shows it fully."));
    intro->setWordWrap(true);
    intro->setStyleSheet("color:#888; font-size:11px;");
    rv->addWidget(intro);
    if (!s.routedBy.isEmpty()) {
        auto *note = new QLabel(QStringLiteral("Routed by its group “%1”.").arg(s.routedBy.toHtmlEscaped()));
        note->setStyleSheet("color:#999;");
        note->setWordWrap(true);
        rv->addWidget(note);
    }
    m_routeFields.clear();
    for (const auto &r : s.routes) {
        auto *row = new QWidget;
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        auto *sl = new SliderField;
        auto *name = new ResetLabel(r.name, [sl] { // fully shown
            sl->setValue(100);
            emit sl->valueEdited(100);
        });
        name->setMinimumWidth(70);
        sl->setRange(0, 100);
        sl->setDecimals(0);
        sl->setSuffix(QStringLiteral(" %"));
        sl->setSingleStep(1);
        sl->setTicks(10);
        sl->setSnaps({0, 100});
        sl->setValue(r.opacity * 100);
        sl->setEnabled(s.routedBy.isEmpty());
        sl->setToolTip(QStringLiteral("How much of it the viewport “%1” shows (0 hides it)").arg(r.name));
        rl->addWidget(name);
        rl->addWidget(sl, 1);
        rv->addWidget(row);
        const quint64 vp = r.id;
        auto current = std::make_shared<float>(r.opacity);
        m_routeFields.push_back({r.id, sl, current});
        connect(sl, &SliderField::valueEdited, this, [this, vp, current, vname = r.name](double v) {
            if (m_engine->isLocked(m_layer)) return;
            const float after = float(v / 100.0);
            m_undo->push(new cmd::SetOpacityIn(m_engine, m_layerId, vp, *current, after,
                                               QStringLiteral("Opacity in %1").arg(vname)));
            *current = after;
        });
    }
    rv->addStretch();
    return routes;
}


QWidget *LayerInspector::buildMapping(const LayerValues &s)
{
    auto *g = new QWidget; // titled by its sub-tab
    auto *v = new QVBoxLayout(g);

    // Position (center, composition pixels) and scale (% of the composition) of the whole mapped layer
    {
        const QSize comp = m_engine->compositionSize();
        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(6);
        auto spin = [](double lo, double hi, const QString &suffix, int decimals) {
            auto *b = new NumberBox;
            b->setRange(lo, hi);
            b->setDecimals(decimals);
            b->setSuffix(suffix);
            b->setKeyboardTracking(false);
            b->setAccelerated(true);
            return b;
        };
        // Their limits are the parameters' (Parameter.h)
        auto spinOf = [this, spin](const QString &path, const QString &suffix, int decimals) {
            const auto [lo, hi] = limitsOf(path, -100000, 100000);
            return spin(lo, hi, suffix, decimals);
        };
        m_posX = spinOf(QStringLiteral("position/x"), QStringLiteral(" px"), 1);
        m_posY = spinOf(QStringLiteral("position/y"), QStringLiteral(" px"), 1);
        m_sizePx = s.isViewport;
        m_scaleX = m_sizePx ? spinOf(QStringLiteral("width"), QStringLiteral(" px"), 1)
                            : spinOf(QStringLiteral("scale/x"), QStringLiteral(" %"), 2);
        m_scaleY = m_sizePx ? spinOf(QStringLiteral("height"), QStringLiteral(" px"), 1)
                            : spinOf(QStringLiteral("scale/y"), QStringLiteral(" %"), 2);
        m_rotation = spin(-360, 360, QStringLiteral("°"), 1);
        m_posX->setToolTip(QStringLiteral("Horizontal position of the layer's center, in composition pixels"));
        m_posY->setToolTip(QStringLiteral("Vertical position of the layer's center, in composition pixels"));
        m_scaleX->setToolTip(m_sizePx ? QStringLiteral("Width of the region this viewport shows, in composition pixels")
                                      : QStringLiteral("Width of the layer, in % of the composition width"));
        m_scaleY->setToolTip(m_sizePx ? QStringLiteral("Height of the region this viewport shows, in composition pixels")
                                      : QStringLiteral("Height of the layer, in % of the composition height"));
        m_rotation->setToolTip(QStringLiteral("Rotation of the layer, in degrees"));
        auto *link = new QToolButton;
        link->setCheckable(true);
        link->setChecked(m_scaleLinked);
        link->setText(QStringLiteral("⛓"));
        link->setToolTip(QStringLiteral("Linked: width and height keep the aspect ratio. Click to unlink them."));
        link->setStyleSheet(QStringLiteral("QToolButton:checked { background:%1; color:%2; }")
                                .arg(theme::css(), theme::onAccent().name()));
        auto *posLabel = new ResetLabel(QStringLiteral("Position"), [this, comp] {
            m_posX->setValue(comp.width() / 2.0); // centered
            m_posY->setValue(comp.height() / 2.0);
        });
        auto *scaleLabel = new ResetLabel(m_sizePx ? QStringLiteral("Size") : QStringLiteral("Scale"), [this, comp] {
            const bool linked = m_scaleLinked;
            m_scaleLinked = false;
            m_scaleX->setValue(m_sizePx ? comp.width() : 100.0); // the whole composition
            m_scaleY->setValue(m_sizePx ? comp.height() : 100.0);
            m_scaleLinked = linked;
        });
        grid->addWidget(posLabel, 0, 0);
        grid->addWidget(new QLabel(QStringLiteral("X")), 0, 1);
        grid->addWidget(m_posX, 0, 2);
        grid->addWidget(new QLabel(QStringLiteral("Y")), 0, 4);
        grid->addWidget(m_posY, 0, 5);
        grid->addWidget(scaleLabel, 1, 0);
        grid->addWidget(new QLabel(m_sizePx ? QStringLiteral("W") : QStringLiteral("X")), 1, 1);
        grid->addWidget(m_scaleX, 1, 2);
        grid->addWidget(link, 1, 3);
        grid->addWidget(new QLabel(m_sizePx ? QStringLiteral("H") : QStringLiteral("Y")), 1, 4);
        grid->addWidget(m_scaleY, 1, 5);
        {
            m_rotation->setWrapping(true);
            m_rotation->setRange(-180, 180);
            auto *rotLabel = new ResetLabel(QStringLiteral("Rotation"), [this] { m_rotation->setValue(0); });
            m_animate->attach(rotLabel, {"rotation"});
            grid->addWidget(rotLabel, 2, 0);
            grid->addWidget(m_rotation, 2, 2);
        }
        m_pivotX = spinOf(QStringLiteral("pivot/x"), QStringLiteral(" %"), 1);
        m_pivotY = spinOf(QStringLiteral("pivot/y"), QStringLiteral(" %"), 1);
        m_pivotX->setToolTip(QStringLiteral("Horizontal center of the rotation, in % of the layer (50: its middle)"));
        m_pivotY->setToolTip(QStringLiteral("Vertical center of the rotation, in % of the layer (50: its middle)"));
        auto *pivotLabel = new ResetLabel(QStringLiteral("Pivot"), [this] {
            editMapping(QStringLiteral("Pivot"), [](Mapping &m) {
                m.pivot = QPointF(0.5, 0.5); // the middle of the picture
                ++m.revision;
            });
            refreshSpatial();
        });
        grid->addWidget(pivotLabel, 3, 0);
        grid->addWidget(new QLabel(QStringLiteral("X")), 3, 1);
        grid->addWidget(m_pivotX, 3, 2);
        grid->addWidget(new QLabel(QStringLiteral("Y")), 3, 4);
        grid->addWidget(m_pivotY, 3, 5);
        {
            const QString sx = m_sizePx ? QStringLiteral("width") : QStringLiteral("scale/x");
            const QString sy = m_sizePx ? QStringLiteral("height") : QStringLiteral("scale/y");
            m_animate->attach(posLabel, {"position/x", "position/y"});
            m_animate->attach(m_posX, {"position/x"});
            m_animate->attach(m_posY, {"position/y"});
            m_animate->attach(scaleLabel, {sx, sy});
            m_animate->attach(m_scaleX, {sx});
            m_animate->attach(m_scaleY, {sy});
            m_animate->attach(m_rotation, {"rotation"});
            m_animate->attach(pivotLabel, {"pivot/x", "pivot/y"});
            m_animate->attach(m_pivotX, {"pivot/x"});
            m_animate->attach(m_pivotY, {"pivot/y"});
        }
        grid->setColumnStretch(2, 1);
        grid->setColumnStretch(5, 1);
        v->addLayout(grid);
        refreshSpatial();
        // The fields are the mapping's own values (as its parameters show them: position and pivot in composition
        // pixels, the size in % of the composition, or in pixels for a viewport, the rotation in degrees)
        if (m_rotation) {
            connect(m_rotation, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double deg) {
                editMapping(QStringLiteral("Rotation"), [&](Mapping &m) {
                    m.rotation = deg;
                    ++m.revision;
                }, true);
                refreshSpatial();
            });
        }

        connect(link, &QToolButton::toggled, this, [this](bool on) { m_scaleLinked = on; });
        auto setPivot = [this](bool x, double v) {
            editMapping(QStringLiteral("Pivot"), [&](Mapping &m) {
                (x ? m.pivot.rx() : m.pivot.ry()) = v / 100.0;
                ++m.revision;
            }, true);
        };
        connect(m_pivotX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [setPivot](double v) { setPivot(true, v); });
        connect(m_pivotY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [setPivot](double v) { setPivot(false, v); });
        auto setPosition = [this, comp](bool x, double v) {
            editMapping(QStringLiteral("Position"), [&](Mapping &m) {
                (x ? m.position.rx() : m.position.ry()) = v / (x ? comp.width() : comp.height());
                ++m.revision;
            }, true);
        };
        connect(m_posX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [setPosition](double v) { setPosition(true, v); });
        connect(m_posY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [setPosition](double v) { setPosition(false, v); });
        auto scale = [this, comp](double sx, double sy, bool fromX) {
            // in the composition's units: a fraction of its width / height
            const double unitX = m_sizePx ? comp.width() : 100.0, unitY = m_sizePx ? comp.height() : 100.0;
            editMapping(m_sizePx ? QStringLiteral("Size") : QStringLiteral("Scale"), [&](Mapping &m) {
                double w = sx / unitX, h = sy / unitY;
                const QSizeF was = m.size;
                if (m_scaleLinked) { // the other side follows, keeping the aspect ratio
                    if (fromX && std::abs(was.width()) > 1e-12) h = was.height() * w / was.width();
                    if (!fromX && std::abs(was.height()) > 1e-12) w = was.width() * h / was.height();
                } else if (fromX) {
                    h = was.height();
                } else {
                    w = was.width();
                }
                const double least = m_sizePx ? 1.0 / std::max(1, std::min(comp.width(), comp.height())) : 0.001;
                m.size = QSizeF(std::max(least, w), std::max(least, h));
                ++m.revision;
            }, true);
            refreshSpatial();
        };
        connect(m_scaleX, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [scale, this](double x) { scale(x, m_scaleY->value(), true); });
        connect(m_scaleY, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [scale, this](double y) { scale(m_scaleX->value(), y, false); });
    }

    if (s.isViewport) {
        // A viewport is a rectangle of the composition: no corners, no mesh (the projector's warp comes later)
        auto *actions = new QHBoxLayout;
        auto *pixels = new QPushButton(QStringLiteral("Pixel for Pixel"));
        pixels->setToolTip(QStringLiteral("One viewport pixel for one composition pixel: the region takes the "
                                          "viewport's size, around its center"));
        auto *whole = new QPushButton(QStringLiteral("Whole Composition"));
        actions->addWidget(pixels);
        actions->addWidget(whole);
        actions->addStretch();
        v->addLayout(actions);
        auto *hint = new QLabel(QStringLiteral("The region of the composition this viewport shows. "
                                               "In the preview, drag its frame to move it."));
        hint->setWordWrap(true);
        hint->setStyleSheet("color:#888; font-size:11px;");
        v->addWidget(hint);
        const QSize vs = s.vpSize;
        connect(pixels, &QPushButton::clicked, this, [this, vs] {
            const QSize c = m_engine->compositionSize();
            editMapping(QStringLiteral("Pixel for Pixel"), [&](Mapping &m) {
                Mapping::Rect r = m.rect(c);
                r.w = vs.width();
                r.h = vs.height();
                m.setRect(r, c);
            });
            refreshSpatial();
        });
        connect(whole, &QPushButton::clicked, this, [this] {
            editMapping(QStringLiteral("Whole Composition"), [](Mapping &m) { m.resetCorners(); });
            refreshSpatial();
        });
        return g;
    }

    auto *modeRow = new QHBoxLayout;
    auto *corners = new QRadioButton(QStringLiteral("Corners"));
    auto *mesh = new QRadioButton(QStringLiteral("Mesh"));
    (s.meshMode ? mesh : corners)->setChecked(true);
    auto *modes = new QButtonGroup(g);
    modes->addButton(corners, 0);
    modes->addButton(mesh, 1);
    auto *cols = new IntBox, *rows = new IntBox;
    cols->setRange(2, 32);
    rows->setRange(2, 32);
    cols->setValue(s.cols);
    rows->setValue(s.rows);
    cols->setToolTip(QStringLiteral("Point columns"));
    rows->setToolTip(QStringLiteral("Point rows"));
    auto *apply = new QPushButton(QStringLiteral("Apply"));
    apply->setToolTip(QStringLiteral("Change the mesh density (resets the warp)"));
    modeRow->addWidget(corners);
    modeRow->addWidget(mesh);
    modeRow->addStretch();
    modeRow->addWidget(cols);
    modeRow->addWidget(new QLabel(QStringLiteral("×")));
    modeRow->addWidget(rows);
    modeRow->addWidget(apply);
    v->addLayout(modeRow);

    auto *actions = new QHBoxLayout;
    auto *full = new QPushButton(QStringLiteral("Full Frame"));
    auto *ratio = new QPushButton(QStringLiteral("Source Aspect"));
    auto *resetMesh = new QPushButton(QStringLiteral("Flatten Mesh"));
    actions->addWidget(full);
    actions->addWidget(ratio);
    actions->addWidget(resetMesh);
    v->addLayout(actions);

    auto *hint = new QLabel(QStringLiteral("Drag: move · Shift: fine · Arrows: 1 px (Shift: 10 px) · "
                                           "Tab: next handle · Esc: deselect\n"
                                           "Several points: Ctrl/⌘+click to add, Ctrl/⌘+drag a rectangle, "
                                           "Ctrl/⌘+A for all; they move together.\n"
                                           "Preview: mouse wheel or pinch to zoom (finer moves), middle button or "
                                           "Alt/⌥ + drag to pan, Fit to see everything."));
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#888; font-size:11px;");
    v->addWidget(hint);

    connect(modes, &QButtonGroup::idClicked, this, [this](int id) {
        editMapping(id == 1 ? QStringLiteral("Mesh Mode") : QStringLiteral("Corners Mode"),
                    [id](Mapping &m) { m.meshMode = id == 1; });
    });
    connect(apply, &QPushButton::clicked, this, [this, cols, rows] {
        const int c = cols->value(), r = rows->value(); // read before the question: rebuilt meanwhile, they are gone
        const quint64 layer = m_layerId;
        bool deformed = false;
        for (const QPointF &o : cmd::SetMapping::read(m_engine, m_layer).offsets) deformed |= !o.isNull();
        if (deformed && QMessageBox::question(this, QStringLiteral("Mesh"),
                                              QStringLiteral("Changing the density clears the current warp "
                                                             "(undoable with Ctrl+Z). Continue?"))
                            != QMessageBox::Yes)
            return;
        if (m_layerId != layer) return; // another layer shown meanwhile
        editMapping(QStringLiteral("Mesh Density"), [c, r](Mapping &m) {
            m.resetMesh(c, r);
            m.meshMode = true;
        });
        rebuild();
    });
    connect(full, &QPushButton::clicked, this,
            [this] { editMapping(QStringLiteral("Full Frame"), [](Mapping &m) { m.resetCorners(); }); });
    connect(ratio, &QPushButton::clicked, this, [this] {
        int sw, sh;
        {
            Engine::Lock lk(&m_engine->mutex());
            Layer *ly = m_engine->layer(m_layer);
            if (!ly) return;
            sw = ly->sourceWidth();
            sh = ly->sourceHeight();
        }
        if (sh <= 0) return;
        const QSize c = m_engine->compositionSize();
        editMapping(QStringLiteral("Source Aspect"),
                    [=](Mapping &m) { m.fitAspect(double(sw) / sh, double(c.width()) / c.height()); });
    });
    connect(resetMesh, &QPushButton::clicked, this, [this] {
        editMapping(QStringLiteral("Flatten Mesh"), [](Mapping &m) { m.resetMesh(m.cols, m.rows); });
    });

    return g;
}

QWidget *LayerInspector::buildEffects(const LayerValues &s)
{
    auto *panel = new FxChainPanel(QStringLiteral("FX")); // titled by its sub-tab

    // General switch of the chain
    auto *all = panel->masterSwitch();
    m_animate->attach(all, {QStringLiteral("fx/enable")});
    all->setChecked(s.effectsEnabled);
    connect(all, &QCheckBox::toggled, this, [this](bool on) {
        setValue(QStringLiteral("fx/enable"), on, QStringLiteral("Enable / Disable FX"));
        emit layerChanged();
    });

    panel->addButton()->setToolTip(QStringLiteral("Search the ISF library and add an FX to the chain"));
    connect(panel, &FxChainPanel::addRequested, this, [this](QWidget *anchor) {
        QList<SearchPicker::Item> items;
        for (const IsfEntry &e : m_engine->library().filters())
            items.append({e.name, e.categories.value(0), e.description, e.path});
        auto *picker = new SearchPicker(items, this);
        connect(picker, &SearchPicker::picked, this, [this](const QString &p) {
            if (m_engine->isLocked(m_layer)) return;
            const QJsonArray before = m_engine->effectsJson(m_layer);
            const auto beforeAnimations = m_engine->captureAnimationTargets(m_engine->layerId(m_layer));
            QString err;
            const int fx = m_engine->addEffect(m_layer, p, &err);
            if (fx < 0) { // refused: the chain is unchanged, nothing to undo
                QMessageBox::warning(this, QStringLiteral("Effect"),
                                     QFileInfo(p).fileName() + QStringLiteral(": ") + err.section('\n', 0, 0));
                return;
            }
            m_selectedEffect = fx;
            m_undo->push(new cmd::SetEffects(m_engine, m_layer, before,
                                             QStringLiteral("Add FX %1").arg(QFileInfo(p).completeBaseName()), beforeAnimations));
            QMetaObject::invokeMethod(this, &LayerInspector::rebuild, Qt::QueuedConnection);
        });
        picker->popup(anchor->mapToGlobal(QPoint(0, anchor->height())));
    });
    auto *rescan = toolButton(QStringLiteral("↻"), QStringLiteral("Rescan ISF Library (shaders added in the Finder)"));
    connect(rescan, &QToolButton::clicked, this, &LayerInspector::rescanLibraryRequested);
    panel->addLeading(rescan);
    auto *reload = toolButton(QStringLiteral("⟳"), QStringLiteral("Reload shader from disk"));
    panel->addTrailing(reload);

    std::vector<FxChainPanel::Row> rows;
    for (const auto &fx : s.effects) {
        FxChainPanel::Row row;
        row.label = fx.name;
        row.suffix = fx.masked ? QStringLiteral("  ◐") : QString();
        row.tooltip = fx.masked ? QStringLiteral("Through a mask") : QString();
        row.enabled = fx.enabled;
        row.warning = !fx.valid;
        rows.push_back(row);
    }
    const int count = int(rows.size());
    m_selectedEffect = count ? std::clamp(m_selectedEffect, 0, count - 1) : 0;
    panel->setRows(rows, m_selectedEffect, s.effectsEnabled);
    reload->setEnabled(count > 0);
    if (!count) return panel;

    connect(panel, &FxChainPanel::toggled, this, [this](int r, bool on) {
        editEffects(on ? QStringLiteral("Enable FX") : QStringLiteral("Disable FX"), [this, r, on] {
            Engine::Lock lk(&m_engine->mutex());
            Layer *ly = m_engine->layer(m_layer);
            if (ly && r >= 0 && r < int(ly->effects.size())) ly->effects[size_t(r)]->enabled = on;
        });
    });
    connect(panel, &FxChainPanel::currentChanged, this, [this](int r) {
        if (r != m_selectedEffect) {
            m_selectedEffect = r;
            showEffectParams();
        }
    });
    connect(panel, &FxChainPanel::removeRequested, this, [this](int) {
        editEffects(QStringLiteral("Remove FX"), [this] { m_engine->removeEffect(m_layer, m_selectedEffect); });
        rebuild();
    });
    connect(panel, &FxChainPanel::moveRequested, this, [this](int from, int to) {
        editEffects(QStringLiteral("Reorder FX"), [this, from, to] { m_engine->moveEffect(m_layer, from, to); });
        m_selectedEffect = to;
        rebuild();
    });
    connect(reload, &QToolButton::clicked, this, [this] {
        IsfInstance *fx;
        {
            Engine::Lock lk(&m_engine->mutex());
            fx = cmd::resolveIsf(m_engine, m_layer, m_selectedEffect);
        }
        m_engine->reloadIsf(fx);
        rebuild();
    });

    // Parameters of the selected effect: their own container, refilled in place
    m_fxDetail = panel->detail();
    showEffectParams();
    return panel;
}

void LayerInspector::showEffectParams()
{
    if (!m_fxDetail) return;
    auto *lay = static_cast<QVBoxLayout *>(m_fxDetail->layout());
    clearLayout(lay); // the Speed and Mask rows are layouts of their own
    QString name, error;
    bool valid = false;
    quint64 maskId = 0, selfId = 0;
    bool invert = false, maskPre = false;
    std::vector<std::pair<quint64, QString>> masks; // layers that can mask it (no viewport, no feedback)
    {
        Engine::Lock lk(&m_engine->mutex());
        Layer *l = m_engine->layer(m_layer);
        if (!l || l->effects.empty()) return;
        m_selectedEffect = std::clamp(m_selectedEffect, 0, int(l->effects.size()) - 1);
        const IsfInstance *fx = l->effects[size_t(m_selectedEffect)].get();
        name = fx->name();
        error = fx->error();
        valid = fx->isValid();
        maskId = fx->maskLayer;
        invert = fx->maskInvert;
        maskPre = fx->maskPreFx;
        selfId = l->id;
        for (int k = 0; k < m_engine->layerCount(); ++k) {
            const Layer *o = m_engine->layer(k);
            if (!o || o->isViewport || o->id == selfId || m_engine->layerDependsOn(o->id, selfId)) continue;
            masks.push_back({o->id, o->isGroup ? o->name + QStringLiteral(" (group)") : o->name});
        }
    }
    lay->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(name.toHtmlEscaped())));
    if (!error.isEmpty()) lay->addWidget(errorLabel(error));

    // Its speed (the pace of its TIME), above its mask
    if (valid) {
        const QString path = m_engine->shaderPrefix(m_layerId, m_selectedEffect) + QStringLiteral("speed");
        auto *speed = new SliderField;
        speed->setRange(0, 10);
        speed->setDecimals(2);
        speed->setSingleStep(0.05);
        speed->setTicks(10);
        speed->setSnaps({1});
        speed->setValue(m_engine->parameterValue(m_layerId, path).toDouble(1.0));
        speed->setToolTip(QStringLiteral("Speed of the FX's time (1 = normal, 0 = stopped)"));
        m_fxSpeed = speed;
        m_fxSpeedPath = path;
        auto *label = new ResetLabel(QStringLiteral("Speed"), [this, speed, path] {
            speed->setValue(1);
            setValue(path, 1.0, QStringLiteral("Change Speed"));
        });
        connect(speed, &SliderField::valueEdited, this,
                [this, path](double x) { setValue(path, x, QStringLiteral("Change Speed")); });
        m_animate->attach({label, speed}, {path});
        auto *row = new QHBoxLayout;
        row->addWidget(label);
        row->addWidget(speed, 1);
        lay->addLayout(row);
    }

    // Mask: where the effect applies, from another layer's picture stretched over this one
    {
        auto *row = new QHBoxLayout;
        auto *label = new ResetLabel(QStringLiteral("Mask"), [this] {
            const int k = m_selectedEffect;
            editEffects(QStringLiteral("Remove FX Mask"), [this, k] { m_engine->setEffectMask(m_layer, k, 0, false); });
            QMetaObject::invokeMethod(this, &LayerInspector::rebuild, Qt::QueuedConnection); // the list shows ◐
        });
        auto *pick = new QComboBox;
        pick->addItem(QStringLiteral("None — everywhere"), QVariant(qulonglong(0)));
        int current = 0;
        for (const auto &[id, n] : masks) {
            pick->addItem(n, QVariant(qulonglong(id)));
            if (id == maskId) current = pick->count() - 1;
        }
        if (maskId && current == 0) { // a mask that cannot be chosen any more (gone): shown, not chosen
            pick->addItem(QStringLiteral("(gone)"), QVariant(qulonglong(maskId)));
            current = pick->count() - 1;
        }
        pick->setCurrentIndex(current);
        pick->setToolTip(QStringLiteral("Where this FX applies: fully where the chosen layer's picture is white, not at "
                                        "all where it is black or transparent, in proportion in between. That picture is "
                                        "stretched over this layer's, before the mapping. The layer can stay hidden."));
        auto *inv = new FlagBox(QStringLiteral("Invert"));
        inv->setChecked(invert);
        inv->setEnabled(maskId != 0);
        row->addWidget(label);
        row->addWidget(pick, 1);
        row->addWidget(inv);
        auto *tap = new QComboBox;
        tap->addItem(QStringLiteral("Pre-FX"), true);
        tap->addItem(QStringLiteral("Post-FX"), false);
        tap->setCurrentIndex(maskPre ? 0 : 1);
        tap->setEnabled(maskId != 0);
        tap->setToolTip(QStringLiteral("The mask's picture: before or after the mask layer's own FX"));
        row->addWidget(tap);
        lay->addLayout(row);
        connect(tap, &QComboBox::activated, this, [this, tap](int i) {
            const bool pre = tap->itemData(i).toBool();
            const int k = m_selectedEffect;
            editEffects(QStringLiteral("Set FX Mask Tap"), [&] { m_engine->setEffectMaskTap(m_layer, k, pre); });
        });
        connect(pick, &QComboBox::activated, this, [this, pick, inv](int i) {
            const quint64 id = pick->itemData(i).toULongLong();
            const int k = m_selectedEffect;
            QString err;
            bool ok = true;
            editEffects(id ? QStringLiteral("Set FX Mask") : QStringLiteral("Remove FX Mask"),
                        [&] { ok = m_engine->setEffectMask(m_layer, k, id, inv->isChecked(), &err); });
            if (!ok && !err.isEmpty()) QMessageBox::warning(this, QStringLiteral("Mask"), err);
            QMetaObject::invokeMethod(this, &LayerInspector::rebuild, Qt::QueuedConnection);
        });
        connect(inv, &QCheckBox::toggled, this, [this, pick](bool on) {
            const quint64 id = pick->currentData().toULongLong();
            const int k = m_selectedEffect;
            editEffects(QStringLiteral("Invert FX Mask"), [&] { m_engine->setEffectMask(m_layer, k, id, on); });
        });
    }
    if (valid) {
        lay->addWidget(accentSeparator());
        auto *params = new ParamPanel(m_engine, m_undo, m_layer, m_selectedEffect, ParamPanel::NoSpeed);
        params->setCoTargets(coTargets());
        m_effectParams = params;
        params->attachAnimate(m_animate);
        connect(params, &ParamPanel::rebuildRequested, this, &LayerInspector::rebuild, Qt::QueuedConnection);
        lay->addWidget(params);
    }
    lockInputs(m_fxDetail, m_locked);
}

void LayerInspector::refreshSpatial()
{
    if (!m_posX) return;
    Mapping m;
    const QSize comp = m_engine->compositionSize();
    {
        Engine::Lock lk(&m_engine->mutex());
        Layer *l = m_engine->layer(m_layer);
        if (!l) return;
        m = l->mapping;
    }
    const double unitX = m_sizePx ? comp.width() : 100.0, unitY = m_sizePx ? comp.height() : 100.0;
    QDoubleSpinBox *boxes[7] = {m_posX, m_posY, m_scaleX, m_scaleY, m_pivotX, m_pivotY, m_rotation};
    const double all[7] = {m.position.x() * comp.width(), m.position.y() * comp.height(), m.size.width() * unitX,
                           m.size.height() * unitY, m.pivot.x() * 100.0, m.pivot.y() * 100.0, std::remainder(m.rotation, 360.0)};
    for (int k = 0; k < 7; ++k) {
        if (!boxes[k] || boxes[k]->hasFocus()) continue; // being edited
        if (std::abs(boxes[k]->value() - all[k]) < 1e-6) continue;
        QSignalBlocker blk(boxes[k]);
        boxes[k]->setValue(all[k]);
    }
}

void LayerInspector::refreshMasks()
{
    if (m_maskPanel) m_maskPanel->refresh();
}

void LayerInspector::selectMask(int index)
{
    m_maskIndex = index;
    if (m_maskPanel) m_maskPanel->select(index);
}

void LayerInspector::refreshTimecodeTransport()
{
    const std::shared_ptr<TcTransport> t = m_tcTransport;
    if (!t || !t->play) return;
    const std::shared_ptr<TimecodeClock> c = m_engine->layerTimecodeClock(m_layer);
    if (!c) return;
    const bool playing = c->playing();
    if (t->play->isChecked() != playing) t->play->setChecked(playing);
    if (t->pause->isChecked() == playing) t->pause->setChecked(!playing);
    if (!t->position->hasFocus()) t->position->setText(c->display());
    if (!t->in->hasFocus()) t->in->setText(t->shown(c->inPoint()));
    if (!t->out->hasFocus()) t->out->setText(c->outPoint() < 0 ? QString() : t->shown(c->outPoint()));
    if (QAbstractButton *b = t->modes->button(int(c->playMode())); b && !b->isChecked()) b->setChecked(true);
}

void LayerInspector::refreshClock()
{
    const std::shared_ptr<ClockUi> u = m_clockUi;
    if (!u) return;
    const std::shared_ptr<ClockGenerator> c = m_engine->layerClock(m_layer);
    if (!c) return;
    if (u->play && u->pause) {
        const bool running = c->running();
        if (u->play->isChecked() != running) u->play->setChecked(running);
        if (u->pause->isChecked() == running) u->pause->setChecked(!running);
    }
    if (u->shown) {
        const QString shown = c->display();
        if (u->shown->text() != shown) u->shown->setText(shown);
        const QString css = c->overtime() ? QStringLiteral("color:%1;").arg(c->settings().overtimeColor.name()) : QString();
        if (u->shown->property("css").toString() != css) {
            u->shown->setProperty("css", css);
            u->shown->setStyleSheet(QStringLiteral("font-family:monospace; font-size:16px; font-weight:bold; ") + css);
        }
    }
    if (u->status) u->status->setText(c->status());
}

void LayerInspector::refreshDynamic()
{
    refreshPicker(); // a layer renamed or moved by something else
    for (const auto &[field, key] : m_synthFields) {
        if (field && !field->isDragging() && !field->hasFocus()) {
            const QSignalBlocker block(field);
            field->setValue(m_engine->parameterValue(m_layerId, key).toDouble());
        }
    }
    for (const auto *fields : {&m_audioFXFields, &m_audioFxDetail})
        for (const auto &[field, key] : *fields) {
            if (!field || field->hasFocus())
                continue;
            const auto value = m_engine->parameterValue(m_layerId, key);
            const QSignalBlocker blocker(field);
            if (auto *slider = qobject_cast<SliderField *>(field.data())) {
                if (!slider->isDragging())
                    slider->setValue(value.toDouble());
            } else if (auto *flag = qobject_cast<QCheckBox *>(field.data()))
                flag->setChecked(value.toBool());
        }
    refreshAudioMeters();
    if (m_toxStatus) {
        {
            Engine::Lock lock(&m_engine->mutex());
            const Layer *l = m_engine->layer(m_engine->indexOfId(m_layerId));
            if (l && l->toxPlayer) m_toxStatus->setText(l->toxPlayer->status());
        }
        if (m_layoutKey != layoutKey() && !m_toxRebuildQueued) {
            m_toxRebuildQueued = true;
            QTimer::singleShot(0, this, [this] { m_toxRebuildQueued = false; rebuild(); });
            return;
        }
        for (const auto &[field, path] : m_toxFields) {
            if (!field || field->hasFocus() || field->isAncestorOf(QApplication::focusWidget())) continue;
            const QJsonValue value = m_engine->parameterValue(m_layerId, path);
            const QSignalBlocker block(field);
            if (auto *n = qobject_cast<QDoubleSpinBox *>(field.data())) n->setValue(value.toDouble());
            else if (auto *b = qobject_cast<QCheckBox *>(field.data())) b->setChecked(value.toBool());
            else if (auto *t = qobject_cast<QLineEdit *>(field.data())) t->setText(value.toString());
            else if (auto *c = qobject_cast<QComboBox *>(field.data()); c && !c->view()->isVisible()) c->setCurrentIndex(c->findData(value.toString()));
        }
        for (const auto &[field, control] : m_toxColors) {
            if (!field || field->isAncestorOf(QApplication::focusWidget())) continue;
            auto value = [this, c = control](int k) { return std::clamp(m_engine->parameterValue(m_layerId, c.path(k)).toDouble(), 0.0, 1.0); };
            if (auto *picker = qobject_cast<ColorPicker *>(field.data()))
                picker->setColor(QColor::fromRgbF(float(value(0)), float(value(1)), float(value(2)), control.count == 4 ? float(value(3)) : 1.f));
        }
    }

    if (m_sourceColorSpace || m_sourceColorStatus) {
        QString space, status;
        {
            Engine::Lock lock(&m_engine->mutex());
            if (const Layer *l = m_engine->layer(m_engine->indexOfId(m_layerId))) {
                space = l->sourceColorSpace;
                status = sourceColorStatus(*l);
            }
        }
        if (m_sourceColorSpace && !m_sourceColorSpace->view()->isVisible()) {
            const QSignalBlocker blocker(m_sourceColorSpace);
            m_sourceColorSpace->setCurrentIndex(m_sourceColorSpace->findData(space));
        }
        if (m_sourceColorStatus) m_sourceColorStatus->setText(status);
    }
    // The fields show the values as they move (a snapshot's fade), unless Settings keeps them still until it is over
    const bool follow = SettingsPanel::followFades() || !m_engine->isFading();
    if (follow) for (const auto &[button, path] : m_textColors) {
        if (!button) continue;
        std::array<double, 3> rgb;
        if (!m_engine->animColorValue(m_layerId, path, &rgb)) continue;
        const QColor color = QColor::fromRgbF(rgb[0], rgb[1], rgb[2], m_engine->parameterValue(m_layerId, path + "/a").toDouble(1));
        if (button->property("color").value<QColor>() == color) continue;
        button->setProperty("color", color);
        button->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #666; border-radius: 2px;").arg(color.name()));
    }
    if (follow) refreshSpatial();
    if (m_streamState) { // a live stream's state, as its decoder reports it
        QString text;
        {
            Engine::Lock lock(&m_engine->mutex());
            const Layer *l = m_engine->layer(m_engine->indexOfId(m_layerId));
            if (l && l->video && l->video->isLive()) {
                const VideoDecoder::LiveStatus st = l->video->liveStatus();
                switch (st.state) {
                case VideoDecoder::LiveState::Connecting: text = QStringLiteral("Connecting…"); break;
                case VideoDecoder::LiveState::Live: {
                    const QString codec = l->video->codecName();
                    text = QStringLiteral("Live") + (l->srcWidth > 0 ? QStringLiteral(" · %1×%2").arg(l->srcWidth).arg(l->srcHeight) : QString()) +
                           (codec.isEmpty() ? QString() : QStringLiteral(" · ") + codec);
                    break;
                }
                case VideoDecoder::LiveState::Reconnecting:
                    text = QStringLiteral("Reconnecting (attempt %1)").arg(st.attempts + 1) +
                           (st.reason.isEmpty() ? QString() : QStringLiteral(": ") + st.reason);
                    break;
                }
            }
        }
        if (m_streamState->text() != text) m_streamState->setText(text);
    }
    if (m_output) m_output->refreshStatus(); // mode set by ⌘F or a closed window, publishing states
    if (m_timecodeStatus) m_timecodeStatus->setText(m_engine->layerTimecodeStatus(m_layer));
    refreshTimecodeTransport();
    refreshClock();
    // ROI preview: the source picture, read back by the render thread while the editor is shown
    const bool wantPreview = m_roi && m_roi->isVisible();
    if (wantPreview) {
        m_engine->requestSourcePreview(m_layerId, 480);
        quint64 id = 0;
        const QImage img = m_engine->sourcePreview(&id);
        if (id == m_layerId && !img.isNull()) m_roi->setImage(img);
    } else if (m_previewing) {
        m_engine->requestSourcePreview(0);
    }
    m_previewing = wantPreview;
    // Values changed elsewhere (OSC, undo, a snapshot) follow in the fields
    if (follow) {
        QRectF roi;
        QColor add, remove;
        double temp = 0, tint = 0;
        unsigned masksRev = 0;
        {
            Engine::Lock lk(&m_engine->mutex());
            Layer *l = m_engine->layer(m_layer);
            if (!l) return;
            roi = l->roi;
            masksRev = l->masksRevision;
            add = QColor::fromRgbF(l->color.add[0], l->color.add[1], l->color.add[2]);
            remove = QColor::fromRgbF(l->color.remove[0], l->color.remove[1], l->color.remove[2]);
            temp = l->color.temp;
            tint = l->color.tint;
        }
        for (auto [field, value] : {std::pair{m_temp.data(), temp}, std::pair{m_tint.data(), tint}})
            if (field && !field->isDragging() && std::abs(field->value() - value) > 1e-3) field->setValue(value);
        if (m_roi) m_roi->setRoi(roi);
        if (m_colorAdd) m_colorAdd->setColor(add);
        if (m_colorRemove) m_colorRemove->setColor(remove);
        if (masksRev != m_masksRev) { // a snapshot (or OSC) changed them: the list and the fields follow
            m_masksRev = masksRev;
            refreshMasks();
        }
    }
    if (follow) {
        float opacity = 1, volume = 1;
        std::vector<float> routes;
        {
            Engine::Lock lk(&m_engine->mutex());
            Layer *l = m_engine->layer(m_layer);
            if (!l) return;
            opacity = l->opacity;
            volume = l->volume;
            for (const RouteField &r : m_routeFields) routes.push_back(l->opacityIn(r.viewport));
        }
        auto show = [](SliderField *f, double value) {
            if (f && !f->isDragging() && std::abs(f->value() - value) > 1e-3) f->setValue(value);
        };
        show(m_opacity, opacity * 100.0);
        show(m_volume, volume * 100.0);
        if (m_generatorParams) m_generatorParams->refresh();
        if (m_genSpeed) {
            double gs = 1;
            {
                Engine::Lock lk(&m_engine->mutex());
                Layer *l = m_engine->layer(m_layer);
                if (l && l->generator) gs = l->generator->speed;
            }
            show(m_genSpeed, gs);
        }
        if (m_effectParams) m_effectParams->refresh();
        if (m_fxSpeed) show(m_fxSpeed, m_engine->parameterValue(m_layerId, m_fxSpeedPath).toDouble(1.0));
        for (size_t k = 0; k < m_routeFields.size(); ++k) {
            const RouteField &r = m_routeFields[k];
            if (!r.field || r.field->isDragging()) continue;
            *r.current = routes[k];
            show(r.field, routes[k] * 100.0);
        }
    }
    bool playing;
    double d, p, in, out, speed;
    {
        Engine::Lock lk(&m_engine->mutex());
        Layer *l = m_engine->layer(m_layer);
        if (!l || (l->type != SourceType::Video && l->type != SourceType::Audio)) return;
        in = l->inPoint;
        out = l->outPoint;
        playing = l->playing;
        speed = l->speed;
        d = l->duration();
        p = l->position();
        if (l->video && m_pictureFact && l->frame.layout && m_pictureFact->text() != l->frame.layout->description)
            m_pictureFact->setText(l->frame.layout->description);
    }
    // The transport follows what the engine does (play from a key, OSC, the end of a one-shot)
    if (m_playButtons) {
        const int on = !playing ? 1 : (speed < 0 ? 0 : 2);
        if (QAbstractButton *b = m_playButtons->button(on); b && !b->isChecked()) b->setChecked(true);
    }
    if (m_position && !m_position->isDragging()) {
        m_position->setRange(0, std::max(0.01, d));
        m_position->setValue(p);
    }
    if (!follow) return;
    if (m_speed && !m_speed->isDragging() && std::abs(m_speed->value() - speed * 100) > 0.5)
        m_speed->setValue(speed * 100);
    if (m_loop && !m_loop->isDragging()) {
        m_loop->setRange(0, std::max(0.01, d));
        m_loop->setValues(in, out < 0 ? d : out);
    }
}


QString LayerInspector::layoutKey() const
{
    Engine::Lock lock(&m_engine->mutex());
    const Layer *layer = m_engine->layer(m_layer);
    if (!layer) return {};
    QStringList key{QString::number(layer->id), QString::number(int(layer->type)), layer->sourcePath,
        QString::number(layer->isGroup), QString::number(layer->isViewport), QString::number(m_engine->isLocked(m_layer)),
        QString::number(bool(layer->timecode)), QString::number(layer->sourceLayer), QString::number(int(layer->sourceTap)), QString::number(layer->psdLayer),
        layer->videoSource.pattern, QString::number(layer->videoSource.fps), QString::number(layer->videoSource.live),
        QString::number(layer->videoSource.rtspTcp)};
    if (layer->timecode) key << QString::fromUtf8(QJsonDocument(layer->timecode->toJson()).toJson(QJsonDocument::Compact));
    if (layer->toxPlayer) key << QString::number(layer->toxPlayer->revision());
    if (layer->audioEffects)
        key << QString::number(layer->audioEffects->revision());
    for (const auto &effect : layer->effects) key << effect->path();
    if (layer->clockGen) { // a Countdown, Stopwatch or Clock: its fields show its settings (changed by a recall: made again)
        const ClockSettings c = layer->clockGen->settings();
        key << clockKindKey(c.kind) << QString::number(int(c.target)) << QString::number(c.duration, 'g', 12)
            << c.targetTime.toString() << QString::number(c.offset, 'g', 12) << c.format << c.customFormat
            << QString::number(c.fps) << QString::number(int(c.atZero)) << c.zeroText << c.overtimeColor.name(QColor::HexArgb)
            << QString::number(c.seconds) << QString::number(c.date) << c.zone;
    }
    return key.join(QChar(0x1f));
}

// The levels of the sound, where it is watched: the Source tab (the layer's output), the Audio FX tab (the source, the
// selected FX, the output). Read from the audio thread's atomics, a few times a second.
void LayerInspector::refreshAudioMeters()
{
    if (!m_meter && !m_audioSourceMeter && !m_audioOutputMeter && !m_audioFxMeter && !m_sonificationSwatch)
        return;
    std::shared_ptr<AudioStream> stream;
    std::shared_ptr<AudioEffect> effect;
    {
        Engine::Lock lock(&m_engine->mutex());
        const Layer *l = m_engine->layer(m_engine->indexOfId(m_layerId));
        if (l) {
            stream = l->audio;
            if (l->audioEffects) {
                const auto effects = l->audioEffects->effects();
                if (m_selectedAudioFx >= 0 && m_selectedAudioFx < int(effects.size())) effect = effects[size_t(m_selectedAudioFx)];
            }
        }
    }
    auto show = [](LevelMeter *meter, const AudioMeter *level) {
        if (meter) meter->setLevels(level ? level->peak(0) : 0.f, level ? level->peak(1) : 0.f);
    };
    show(m_meter, stream ? &stream->outputMeter() : nullptr);
    show(m_audioSourceMeter, stream ? &stream->sourceMeter() : nullptr);
    show(m_audioOutputMeter, stream ? &stream->outputMeter() : nullptr);
    show(m_audioFxMeter, effect ? &effect->meter() : nullptr);
    if (m_sonificationSwatch && stream && stream->generator()) {
        const auto rgb = stream->generator()->color();
        const QColor c = QColor::fromRgbF(std::clamp(rgb[0], 0.f, 1.f), std::clamp(rgb[1], 0.f, 1.f), std::clamp(rgb[2], 0.f, 1.f));
        m_sonificationSwatch->setStyleSheet(QStringLiteral("background:%1; border:1px solid #555;").arg(c.name()));
    }
}

void LayerInspector::refreshAfterRecall()
{
    if (m_layoutKey != layoutKey()) rebuild();
    else refreshDynamic();
}

namespace {
class SetAudioEffectsCommand final : public QUndoCommand {
    Engine *engine;
    quint64 layer;
    QJsonArray before, after;
    bool first = true;
    void apply(const QJsonArray &state)
    {
        const int index = engine->indexOfId(layer);
        QString error;
        if (index >= 0 && !engine->setAudioEffectsJson(index, state, &error, true))
            qWarning("Audio FX undo: %s", qPrintable(error));
    }

public:
    SetAudioEffectsCommand(Engine *e, quint64 id, QJsonArray old, QJsonArray next, const QString &text)
        : QUndoCommand(text), engine(e), layer(id), before(std::move(old)), after(std::move(next))
    {
    }
    void undo() override { apply(before); }
    void redo() override
    {
        if (first) {
            first = false;
            return;
        }
        apply(after);
    }
};
} // namespace
void LayerInspector::editAudioEffects(const QJsonArray &after, const QString &text)
{
    const int index = m_engine->indexOfId(m_layerId);
    if (index < 0)
        return;
    const auto before = m_engine->audioEffectsJson(index);
    QString error;
    if (!m_engine->setAudioEffectsJson(index, after, &error)) {
        QMessageBox::warning(this, QStringLiteral("Audio FX"), error);
        return;
    }
    m_undo->push(new SetAudioEffectsCommand(m_engine, m_layerId, before, m_engine->audioEffectsJson(index), text));
    QTimer::singleShot(0, this, [this] {
        rebuild();
        emit layerChanged();
    });
}
// The Audio FX tab: the same mechanism as the FX tab of the pictures (a list with a switch on each row, Add…, − ▲ ▼, the
// parameters of the selected FX below), with the level of the sound where it is worth watching: what comes in (the
// source), what leaves the selected FX, what leaves the layer (after the chain and its fader).
QWidget *LayerInspector::buildAudioEffects()
{
    auto *panel = new FxChainPanel(QStringLiteral("Audio FX"));
    panel->setObjectName(QStringLiteral("audioFXPanel"));
    m_audioChain = panel;
    m_audioFXFields.clear();
    m_audioFxDetail.clear();
    panel->setMaximum(16);
    panel->setEmptyText(QStringLiteral("No Audio FX. They are applied in list order, before the layer's volume."));

    bool chainOn = true;
    std::vector<std::shared_ptr<AudioEffect>> effects;
    {
        Engine::Lock lock(&m_engine->mutex());
        const auto *layer = m_engine->layer(m_engine->indexOfId(m_layerId));
        if (layer && layer->audioEffects) {
            effects = layer->audioEffects->effects();
            chainOn = layer->audioEffects->enabled();
        }
    }

    auto *master = panel->masterSwitch();
    master->setChecked(chainOn);
    m_animate->attach(master, {QStringLiteral("audio_fx/enable")});
    m_audioFXFields.push_back({master, QStringLiteral("audio_fx/enable")});
    connect(master, &QCheckBox::toggled, this, [this](bool on) {
        setValue(QStringLiteral("audio_fx/enable"), on, QStringLiteral("Enable / Disable Audio FX"));
        emit layerChanged();
    });

    auto addMeter = [this](QVBoxLayout *into, const QString &name, QPointer<LevelMeter> *keep) {
        auto *row = new QHBoxLayout;
        auto *label = new QLabel(name);
        label->setStyleSheet("color:#8a8a8e;");
        label->setMinimumWidth(40);
        auto *meter = new LevelMeter(name);
        *keep = meter;
        row->addWidget(label);
        row->addWidget(meter, 1);
        into->addLayout(row);
    };
    addMeter(panel->headerLayout(), QStringLiteral("Source"), &m_audioSourceMeter);
    addMeter(panel->footerLayout(), QStringLiteral("Output"), &m_audioOutputMeter);
    panel->addButton()->setText(QStringLiteral("Add Audio FX…"));
    panel->addButton()->setToolTip(QStringLiteral("Search the audio FX (Faust) and add one to the chain"));

    std::vector<FxChainPanel::Row> rows;
    for (const auto &effect : effects) {
        FxChainPanel::Row row;
        row.label = effect->name();
        row.enabled = effect->enabled();
        row.warning = !effect->error().isEmpty();
        row.tooltip = effect->error();
        rows.push_back(row);
    }
    m_selectedAudioFx = rows.empty() ? 0 : std::clamp(m_selectedAudioFx, 0, int(rows.size()) - 1);
    panel->setRows(rows, m_selectedAudioFx, chainOn);

    // The chain as it is saved, to edit: each request builds the next state and goes through one undo step
    auto state = [this] { return m_engine->audioEffectsJson(m_engine->indexOfId(m_layerId)); };
    connect(panel, &FxChainPanel::addRequested, this, [this, state](QWidget *anchor) {
        QList<SearchPicker::Item> items;
        for (const auto &preset : AudioEffect::presets)
            items.append({QString::fromUtf8(preset.label), QStringLiteral("Faust"),
                          QStringLiteral("Real-time stereo effect, written in Faust"), QStringLiteral("preset:") + QString::fromUtf8(preset.key)});
        items.append({QStringLiteral("Load Faust stereo FX (.dsp)…"), QStringLiteral("File"),
                      QStringLiteral("Your own Faust program: two inputs, two outputs; its sliders become parameters"),
                      QStringLiteral("file:")});
        auto *picker = new SearchPicker(items, this);
        connect(picker, &SearchPicker::picked, this, [this, state](const QString &what) {
            if (m_engine->isLocked(m_layer)) return;
            auto next = state();
            if (what == QLatin1String("file:")) {
                const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Faust Audio FX"), {},
                                                               QStringLiteral("Faust programs (*.dsp)"));
                if (path.isEmpty()) return;
                next.append(QJsonObject{{"kind", "faust"}, {"path", path}});
                m_selectedAudioFx = next.size() - 1;
                editAudioEffects(next, QStringLiteral("Load Faust Audio FX"));
                return;
            }
            next.append(QJsonObject{{"kind", what.mid(int(strlen("preset:")))}});
            m_selectedAudioFx = next.size() - 1;
            editAudioEffects(next, QStringLiteral("Add Audio FX"));
        });
        picker->popup(anchor->mapToGlobal(QPoint(0, anchor->height())));
    });
    connect(panel, &FxChainPanel::removeRequested, this, [this, state](int row) {
        auto next = state();
        if (row < 0 || row >= next.size()) return;
        next.removeAt(row);
        m_selectedAudioFx = std::max(0, std::min(row, int(next.size()) - 1));
        editAudioEffects(next, QStringLiteral("Remove Audio FX"));
    });
    connect(panel, &FxChainPanel::moveRequested, this, [this, state](int from, int to) {
        auto next = state();
        if (from < 0 || from >= next.size() || to < 0 || to >= next.size()) return;
        const auto entry = next.takeAt(from);
        next.insert(to, entry);
        m_selectedAudioFx = to;
        editAudioEffects(next, QStringLiteral("Reorder Audio FX"));
    });
    connect(panel, &FxChainPanel::toggled, this, [this](int row, bool on) {
        const auto effects = m_engine->audioEffects(m_layerId);
        if (row < 0 || row >= int(effects.size())) return;
        setValue(QStringLiteral("audio_fx/") + effects[size_t(row)]->id() + QStringLiteral("/enabled"), on,
                 on ? QStringLiteral("Enable Audio FX") : QStringLiteral("Bypass Audio FX"));
    });
    connect(panel, &FxChainPanel::currentChanged, this, [this](int row) {
        m_selectedAudioFx = row;
        showAudioEffectParams();
    });
    showAudioEffectParams();
    return panel;
}

// The parameters of the selected audio FX: swapped in place, the list is left alone (see FxChainPanel)
void LayerInspector::showAudioEffectParams()
{
    if (!m_audioChain)
        return;
    auto *lay = m_audioChain->detailLayout();
    clearLayout(lay);
    m_audioFxDetail.clear();
    m_audioFxMeter = nullptr;
    const auto effects = m_engine->audioEffects(m_layerId);
    if (effects.empty())
        return;
    m_selectedAudioFx = std::clamp(m_selectedAudioFx, 0, int(effects.size()) - 1);
    const auto &effect = effects[size_t(m_selectedAudioFx)];
    const QString prefix = QStringLiteral("audio_fx/") + effect->id() + QStringLiteral("/");

    lay->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(effect->name().toHtmlEscaped())));
    {
        auto *row = new QHBoxLayout;
        auto *label = new QLabel(QStringLiteral("Level"));
        label->setStyleSheet("color:#8a8a8e;");
        label->setMinimumWidth(40);
        auto *meter = new LevelMeter(effect->name());
        m_audioFxMeter = meter;
        row->addWidget(label);
        row->addWidget(meter, 1);
        lay->addLayout(row);
    }
    if (!effect->error().isEmpty())
        lay->addWidget(errorLabel(effect->error()));
    lay->addWidget(accentSeparator());

    auto number = [this, lay](const QString &label, const QString &key, double low, double high, double value,
                              double initial) {
        auto *row = new QHBoxLayout;
        auto *field = new SliderField;
        auto *name = new ResetLabel(label, [this, field, key, initial] {
            field->setValue(initial);
            setValue(key, initial, QStringLiteral("Reset Audio FX parameter"));
        });
        field->setRange(low, high);
        field->setDecimals(3);
        field->setValue(value);
        row->addWidget(name);
        row->addWidget(field, 1);
        lay->addLayout(row);
        m_audioFxDetail.push_back({field, key});
        m_animate->attach({name, field}, {key});
        connect(field, &SliderField::valueEdited, this, [this, key](double v) {
            setValue(key, v, QStringLiteral("Change Audio FX parameter"));
            emit layerChanged();
        });
    };
    number(QStringLiteral("Dry / Wet"), prefix + QStringLiteral("wet"), 0, 1, effect->wet(), 1);
    if (effect->error().isEmpty()) {
        const auto &controls = effect->parameters();
        for (size_t i = 0; i < controls.size(); ++i)
            number(controls[i].label, prefix + controls[i].key, controls[i].low, controls[i].high, effect->value(int(i)),
                   controls[i].initial);
    }
    lockInputs(m_audioChain->detail(), m_locked);
}
