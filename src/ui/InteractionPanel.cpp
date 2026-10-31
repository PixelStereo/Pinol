#include "InteractionPanel.h"
#include "TimelinePanel.h"
#include "AudioSpectrum.h"
#include <QDoubleSpinBox>
#include "AnimEditor.h"
#include "PresetEditor.h"
#include "Osc.h"
#include "Engine.h"
#include "Widgets.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <cmath>

// ---------------------------------------------------------------------------
// Undo: the whole list before and after (edits of one drag merge into one step)
// ---------------------------------------------------------------------------
namespace {

class SetInteractions : public QUndoCommand
{
public:
    SetInteractions(Engine *e, std::vector<Interaction> before, std::vector<Interaction> after, const QString &text,
                    const QString &mergeKey)
        : QUndoCommand(text), m_e(e), m_before(std::move(before)), m_after(std::move(after)), m_key(mergeKey),
          m_time(QDateTime::currentMSecsSinceEpoch())
    {
    }
    void undo() override { m_e->setInteractions(m_before); }
    void redo() override { m_e->setInteractions(m_after); }
    int id() const override { return m_key.isEmpty() ? -1 : 0x1A7E; }
    bool mergeWith(const QUndoCommand *other) override
    {
        auto *o = static_cast<const SetInteractions *>(other);
        if (o->m_key != m_key || o->m_time - m_time > 1000) return false; // a new gesture: a step of its own
        m_after = o->m_after;
        m_time = o->m_time;
        return true;
    }

private:
    Engine *m_e;
    std::vector<Interaction> m_before, m_after;
    QString m_key;
    qint64 m_time;
};

constexpr char kMime[] = "application/x-nyxmap-interaction-settings";
const InteractionSource kKinds[] = {InteractionSource::Keyboard, InteractionSource::Midi,  InteractionSource::Osc,
                                    InteractionSource::Internal, InteractionSource::Audio, InteractionSource::Pixels,
                                    InteractionSource::Hands};

// A frame that tells when it is clicked (where no child takes the click): a filter selected
class ClickFrame : public QFrame
{
public:
    std::function<void()> pressed;

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (pressed && e->button() == Qt::LeftButton) pressed();
        QFrame::mousePressEvent(e);
    }
};

QLabel *note(const QString &t)
{
    auto *l = new QLabel(t);
    l->setWordWrap(true);
    l->setStyleSheet("color:#888; font-size:11px;");
    return l;
}

// A value of an interaction's chain: the application's number field (press and slide sideways; a click types it),
// with no limit of its own — a range, a filter's setting can be any value. `span`: what sliding across ~200 pixels covers
constexpr double kUnbounded = 1e6;
NumberBox *chainNumber(double value, double span, int decimals)
{
    auto *box = new NumberBox;
    box->setRange(-kUnbounded, kUnbounded);
    box->setDecimals(decimals);
    box->setSingleStep(std::max(std::pow(10.0, -decimals), std::abs(span) / 200.0));
    box->setKeyboardTracking(false); // typed: once entered
    box->setValue(value);
    return box;
}

QProgressBar *meter()
{
    auto *m = new QProgressBar;
    m->setRange(0, 1000);
    m->setTextVisible(false);
    m->setFixedHeight(6);
    m->setStyleSheet(QStringLiteral("QProgressBar { background:#1b1b1d; border:none; } QProgressBar::chunk { background:%1; }")
                         .arg(theme::css()));
    return m;
}

// The chain alone (not the source, not the target): what Copy / Paste Settings carry
QJsonObject chainJson(const Interaction &it)
{
    QJsonObject o = it.toJson();
    for (const char *k : {"id", "name", "enabled", "source", "key", "port", "channel", "message", "number", "address",
                          "argument", "source_layer", "source_param", "audio_low", "audio_high", "audio_feature", "audio_input", "layer", "param", "xy_y"})
        o.remove(QLatin1String(k));
    o["mode"] = it.isAction() ? QStringLiteral("action") : QStringLiteral("mapping"); // pasted, a Mapping stays one
    return o;
}

void applyChain(Interaction &it, const QJsonObject &chain)
{
    it = Interaction::fromJson(Interaction::merged(it.toJson(), chain)); // a chain copied before: its filter and curve
}

} // namespace

// ---------------------------------------------------------------------------
// The panel
// ---------------------------------------------------------------------------
InteractionPanel::InteractionPanel(Engine *engine, QUndoStack *undo, QWidget *parent)
    : QWidget(parent), m_engine(engine), m_undo(undo)
{
    s_instance = this;
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(6);

    m_kinds = new QTabBar;
    m_kinds->setDrawBase(false);
    m_kinds->setExpanding(false);
    m_kinds->setStyleSheet(theme::tabStyle()); // as the inspector's sub-tabs
    connect(theme::notifier(), &theme::Notifier::changed, m_kinds, [this] { m_kinds->setStyleSheet(theme::tabStyle()); });
    for (InteractionSource k : kKinds) m_kinds->addTab(interactionSourceName(k));
    m_kinds->addTab(QStringLiteral("All"));
    m_kinds->setTabToolTip(3, QStringLiteral("A value of the show driven by another one, with no protocol in between"));
    v->addWidget(m_kinds);

    auto *bar = new QHBoxLayout;
    auto *addButton = new QPushButton(QStringLiteral("+"));
    addButton->setFixedWidth(32);
    addButton->setToolTip(QStringLiteral("A new interaction of this kind"));
    m_remove = new QPushButton(QStringLiteral("−"));
    m_remove->setFixedWidth(32);
    m_remove->setToolTip(QStringLiteral("Remove the selected interactions"));
    m_learn = new QToolButton;
    m_learn->setObjectName(QStringLiteral("interactionLearn"));
    m_learn->setText(QStringLiteral("Learn"));
    m_learn->setCheckable(true);
    // Listening shows: red, as a recording, and says so
    m_learn->setStyleSheet(QStringLiteral("QToolButton { padding:2px 8px; } "
                                          "QToolButton:checked { background:#c62828; color:white; font-weight:bold; "
                                          "border:1px solid #ff6f60; border-radius:3px; }"));
    m_learn->setToolTip(QStringLiteral("Move a controller, press a key or send an OSC message: the selected interaction "
                                       "takes it as its source (none selected: a new one is made)"));
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("Search…"));
    m_search->setClearButtonEnabled(true);
    bar->addWidget(addButton);
    bar->addWidget(m_remove);
    bar->addWidget(m_learn);
    bar->addWidget(m_search, 1);
    v->addLayout(bar);

    m_list = new QTreeWidget;
    m_list->setObjectName(QStringLiteral("interactionList"));
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        auto *item = m_list->itemAt(point);
        if (!item) return;
        QMenu menu(this);
        if (auto *editor = PresetEditor::instance()) editor->fillInteractionMenu(&menu, item->data(0, Qt::UserRole).toULongLong());
        if (!menu.isEmpty()) menu.exec(m_list->viewport()->mapToGlobal(point));
    });
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Source"), QStringLiteral("Target")});
    m_list->setRootIsDecorated(false);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_list->setColumnWidth(0, 110);
    m_list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed); // its name, in place

    auto *settingsBox = new QWidget;
    auto *sv = new QVBoxLayout(settingsBox);
    sv->setContentsMargins(0, 0, 0, 0);
    auto *head = new QHBoxLayout;
    m_settingsTitle = new QLabel(QStringLiteral("Settings"));
    m_settingsTitle->setStyleSheet("font-weight:bold;");
    auto *copy = new QPushButton(QStringLiteral("Copy Settings"));
    copy->setToolTip(QStringLiteral("Its chain (ranges, toggle, filters, master, pick-up), not its source or target"));
    m_copy = copy;
    m_paste = new QPushButton(QStringLiteral("Paste Settings"));
    m_paste->setToolTip(QStringLiteral("The chain copied, onto the selected interactions (their sources and targets stay)"));
    head->addWidget(m_settingsTitle, 1);
    head->addWidget(copy);
    head->addWidget(m_paste);
    sv->addLayout(head);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_settings = new QWidget;
    m_settingsLayout = new QVBoxLayout(m_settings);
    m_settingsLayout->setContentsMargins(0, 0, 4, 0);
    scroll->setWidget(m_settings);
    m_scroll = scroll;
    sv->addWidget(scroll, 1);

    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(m_list);
    split->addWidget(settingsBox);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 3);
    split->setSizes({180, 540}); // the settings get the room
    v->addWidget(split, 1);

    connect(m_kinds, &QTabBar::currentChanged, this, [this] {
        const bool audio = shownKind() == InteractionSource::Audio || shownKind() == InteractionSource::Pixels ||
                           shownKind() == InteractionSource::Hands;
        if (audio)
            m_learn->setChecked(false);
        m_learn->setEnabled(!audio);
        rebuildList();
    });
    connect(m_search, &QLineEdit::textChanged, this, [this] { rebuildList(); });
    connect(addButton, &QPushButton::clicked, this, [this, addButton] {
        if (const std::optional<InteractionSource> k = shownKind()) {
            add(*k);
            return;
        }
        QMenu menu;
        for (InteractionSource k : kKinds) menu.addAction(interactionSourceName(k), this, [this, k] { add(k); });
        menu.exec(addButton->mapToGlobal(QPoint(0, addButton->height())));
    });
    connect(m_remove, &QPushButton::clicked, this, &InteractionPanel::removeSelected);
    connect(m_learn, &QToolButton::toggled, this, [this](bool on) {
        m_learn->setText(on ? QStringLiteral("● Learning…") : QStringLiteral("Learn"));
        m_engine->setInteractionLearn(on, on ? m_learnKind : std::nullopt);
        if (!on) m_learnKind.reset(); // the next Learn: of any kind
    });
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, [this] {
        // The list made again (after each edit) empties then selects again: not a choice. Its settings stay, or a
        // curve dragged would be replaced under the mouse (rebuildList shows another one's settings when it changed)
        if (m_filling) return;
        m_remove->setEnabled(!m_list->selectedItems().isEmpty());
        if (selectedId() != m_shown) rebuildSettings();
    });
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int col) {
        if (m_filling || col != 0) return;
        const quint64 id = item->data(0, Qt::UserRole).toULongLong();
        for (Interaction it : m_engine->interactions()) {
            if (it.id == id && !item->text(0).trimmed().isEmpty() && it.name != item->text(0).trimmed()) { // renamed
                it.name = item->text(0).trimmed();
                commit(it, QStringLiteral("Rename Interaction"));
                return;
            }
            if (it.id == id && it.enabled != (item->checkState(0) == Qt::Checked)) {
                it.enabled = item->checkState(0) == Qt::Checked;
                commit(it, it.enabled ? QStringLiteral("Enable Interaction") : QStringLiteral("Disable Interaction"));
            }
        }
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        const std::optional<Interaction> it = selected();
        if (!it) return;
        auto *mime = new QMimeData;
        const QByteArray json = QJsonDocument(chainJson(m_xyAxis == 1 && !it->xyInputs.empty() ? it->xyInputs.front() : *it)).toJson(QJsonDocument::Compact);
        mime->setData(kMime, json);
        mime->setText(QString::fromUtf8(json));
        QApplication::clipboard()->setMimeData(mime);
    });
    connect(m_paste, &QPushButton::clicked, this, [this] {
        const QMimeData *mime = QApplication::clipboard()->mimeData();
        if (!mime || !mime->hasFormat(kMime)) return;
        const QJsonObject chain = QJsonDocument::fromJson(mime->data(kMime)).object();
        QSet<quint64> ids;
        for (QTreeWidgetItem *item : m_list->selectedItems()) ids.insert(item->data(0, Qt::UserRole).toULongLong());
        std::vector<Interaction> list = m_engine->interactions();
        for (Interaction &it : list)
            if (ids.contains(it.id)) applyChain(m_xyAxis == 1 && !it.xyInputs.empty() ? it.xyInputs.front() : it, chain);
        commitList(list, QStringLiteral("Paste Interaction Settings"));
        rebuildSettings();
    });
    connect(m_engine, &Engine::interactionsChanged, this, [this] {
        rebuildList();
        if (!m_committing) rebuildSettings(); // undone, opened: the settings shown again
    });
    m_valuesTimer = new QTimer(this);
    m_valuesTimer->setSingleShot(true);
    m_valuesTimer->setInterval(120);
    connect(m_valuesTimer, &QTimer::timeout, this, [this] {
        if (QApplication::mouseButtons() != Qt::NoButton) { // not under a drag: once let go
            m_valuesTimer->start();
            return;
        }
        rebuildList();
        rebuildSettings();
    });
    connect(m_engine, &Engine::interactionValuesChanged, this, [this] {
        if (!m_valuesTimer->isActive()) m_valuesTimer->start();
    });
    connect(m_engine, &Engine::layersChanged, this, [this] { // a layer renamed or gone: the targets' names
        QTimer::singleShot(0, this, [this] { rebuildList(); });
    });

    m_timer = new QTimer(this);
    m_timer->setInterval(50);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (!isVisible()) return;
        refreshLevels();
        pollLearn();
    });
    m_timer->start();
    rebuildList();
    rebuildSettings();
}

std::optional<InteractionSource> InteractionPanel::shownKind() const
{
    const int i = m_kinds->currentIndex();
    if (i >= 0 && i < kInteractionSourceCount) return kKinds[i];
    return std::nullopt;
}

QString InteractionPanel::keyName(int key, Qt::KeyboardModifiers mods)
{
    mods &= Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    return QKeySequence(QKeyCombination(mods, Qt::Key(key))).toString(QKeySequence::PortableText);
}

void InteractionPanel::keyEvent(int key, Qt::KeyboardModifiers mods, bool pressed)
{
    if (key == 0 || key == Qt::Key_unknown || key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt ||
        key == Qt::Key_Meta)
        return;
    InteractionEvent e;
    e.source = InteractionSource::Keyboard;
    if (pressed) {
        e.address = keyName(key, mods);
        m_pressed.insert(key, e.address);
    } else {
        e.address = m_pressed.take(key); // let go: the name it was pressed with
        if (e.address.isEmpty()) e.address = keyName(key, mods);
    }
    e.value = pressed ? 1.0 : 0.0;
    m_engine->feedInteraction(e);
}

quint64 InteractionPanel::selectedId() const
{
    const QList<QTreeWidgetItem *> sel = m_list->selectedItems();
    QTreeWidgetItem *item = m_list->currentItem();
    if (!item || !sel.contains(item)) item = sel.isEmpty() ? nullptr : sel.first();
    return item ? item->data(0, Qt::UserRole).toULongLong() : 0;
}

std::optional<Interaction> InteractionPanel::selected() const
{
    const quint64 id = selectedId();
    for (const Interaction &it : m_engine->interactions())
        if (it.id == id) return it;
    return std::nullopt;
}

QString InteractionPanel::targetLabel(quint64 layer, const QString &param) const
{
    if (param.isEmpty()) return QStringLiteral("(no target)");
    QString owner = QStringLiteral("Composition");
    if (layer) {
        Engine::Lock lk(&m_engine->mutex());
        const int i = m_engine->indexOfId(layer);
        const Layer *l = i >= 0 ? m_engine->layer(i) : nullptr;
        owner = l ? l->name : QStringLiteral("(layer gone)");
    }
    ParamInfo info;
    if (m_engine->parameterInfo(layer, param, &info))
        return owner + QStringLiteral(" › ") + info.label;
    // Complete ISF colors are grouped under the parent of their R/G/B/A
    // scalar parameters. Keep old interaction targets readable too.
    if (m_engine->parameterInfo(layer, param + QStringLiteral("/r"), &info)) {
        const QString label = info.label.endsWith(QStringLiteral(" R")) ? info.label.chopped(2) : info.label;
        return owner + QStringLiteral(" › ") + label;
    }
    for (const auto &point : m_engine->animatablePoints(layer))
        if (point.path == param) return owner + QStringLiteral(" › ") + point.label;
    return owner + QStringLiteral(" › ") + param;
}

QString InteractionPanel::describe(const Interaction &it) const
{
    switch (it.source) {
    case InteractionSource::Keyboard: return it.key.isEmpty() ? QStringLiteral("Key (none)") : QStringLiteral("Key %1").arg(it.key);
    case InteractionSource::Midi: {
        QString s = QStringLiteral("MIDI %1 %2").arg(it.midiChannel ? QStringLiteral("ch %1").arg(it.midiChannel) : QStringLiteral("any ch"),
                                                     midiMessageName(it.midiMessage));
        if (it.midiMessage != MidiMessageKind::PitchBend) s += QStringLiteral(" %1").arg(it.midiNumber);
        if (!it.midiPort.isEmpty()) s += QStringLiteral(" · ") + it.midiPort;
        return s;
    }
    case InteractionSource::Osc:
        return it.oscAddress.isEmpty() ? QStringLiteral("OSC (no address)")
                                       : QStringLiteral("OSC %1").arg(it.oscAddress) + (it.oscArgument ? QStringLiteral(" [%1]").arg(it.oscArgument) : QString());
    case InteractionSource::Internal: return targetLabel(it.sourceLayer, it.sourceParam);
    case InteractionSource::Pixels:
    case InteractionSource::Hands:
    case InteractionSource::Audio: {Interaction named=it;named.name.clear();return named.label();}
    }
    return QString();
}

void InteractionPanel::rebuildList()
{
    const quint64 keep = selectedId();
    QSet<quint64> keepAll;
    for (QTreeWidgetItem *item : m_list->selectedItems()) keepAll.insert(item->data(0, Qt::UserRole).toULongLong());
    m_filling = true;
    m_list->clear();
    const std::optional<InteractionSource> kind = shownKind();
    const QString search = m_search->text().trimmed();
    QTreeWidgetItem *current = nullptr;
    for (const Interaction &it : m_engine->interactions()) {
        if (kind && it.source != *kind) continue;
        const QString src = describe(it);
        const QString dst = it.isAction() ? QStringLiteral("Action · %1 if true · %2 if false").arg(it.onTrue.size()).arg(it.onFalse.size())
                                          : targetLabel(it.layer, it.param);
        if (!search.isEmpty() && !it.name.contains(search, Qt::CaseInsensitive) && !src.contains(search, Qt::CaseInsensitive) &&
            !dst.contains(search, Qt::CaseInsensitive))
            continue;
        auto *item = new QTreeWidgetItem(m_list, {it.name, kind ? src : interactionSourceName(it.source) + QStringLiteral(" · ") + src, dst});
        item->setData(0, Qt::UserRole, QVariant::fromValue<quint64>(it.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        item->setToolTip(0, QStringLiteral("On / off · its name (double-click to rename): its address in OSC "
                                           "(/interaction/<name>/…), in the presets and the snapshots"));
        item->setCheckState(0, it.enabled ? Qt::Checked : Qt::Unchecked);
        if (keepAll.contains(it.id)) item->setSelected(true);
        if (it.id == keep) current = item;
    }
    if (current) m_list->setCurrentItem(current, 0, QItemSelectionModel::NoUpdate);
    m_filling = false;
    m_remove->setEnabled(!m_list->selectedItems().isEmpty());
    if (selectedId() != m_shown) rebuildSettings();
}

void InteractionPanel::commitList(const std::vector<Interaction> &list, const QString &text)
{
    m_committing = true;
    m_undo->push(new SetInteractions(m_engine, m_engine->interactions(), list, text, QString()));
    m_committing = false;
    emit edited();
}

void InteractionPanel::commit(const Interaction &it, const QString &text, const QString &mergeKey)
{
    std::vector<Interaction> before = m_engine->interactions(), after = before;
    bool found = false;
    for (Interaction &x : after)
        if (x.id == it.id) {
            x = it;
            found = true;
        }
    if (!found) return;
    m_committing = true;
    m_undo->push(new SetInteractions(m_engine, before, after, text, mergeKey.isEmpty() ? QString() : mergeKey + QString::number(it.id)));
    m_committing = false;
    emit edited();
}

void InteractionPanel::add(InteractionSource kind)
{
    std::vector<Interaction> list = m_engine->interactions();
    Interaction it = Interaction::made(kind);
    // An id now, so that it is selected (the same one when redone)
    quint64 id = 1;
    for (const Interaction &x : list) id = std::max(id, x.id + 1);
    it.id = id;
    list.push_back(it);
    commitList(list, QStringLiteral("Add Interaction"));
    if (shownKind() && *shownKind() != kind) m_kinds->setCurrentIndex(int(kind));
    rebuildList();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (m_list->topLevelItem(i)->data(0, Qt::UserRole).toULongLong() == id) {
            m_list->clearSelection();
            m_list->setCurrentItem(m_list->topLevelItem(i));
        }
}

void InteractionPanel::removeSelected()
{
    QSet<quint64> ids;
    for (QTreeWidgetItem *item : m_list->selectedItems()) ids.insert(item->data(0, Qt::UserRole).toULongLong());
    if (ids.isEmpty()) return;
    std::vector<Interaction> list = m_engine->interactions();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Interaction &x) { return ids.contains(x.id); }), list.end());
    commitList(list, ids.size() > 1 ? QStringLiteral("Remove Interactions") : QStringLiteral("Remove Interaction"));
}

void InteractionPanel::refreshLevels()
{
    if (!m_engine->sampleInteractionLevels(&m_levelSample)) return;
    auto sampled = [this](quint64 id) -> const Engine::InteractionLevel * {
        const auto found = std::lower_bound(m_levelSample.begin(), m_levelSample.end(), id,
            [](const Engine::InteractionLevel &level, quint64 target) { return level.id < target; });
        return found != m_levelSample.end() && found->id == id ? &*found : nullptr;
    };
    const auto *value = sampled(m_shown);
    const bool y = m_xyAxis == 1 && value && value->hasY;
    if (m_lane) m_lane->setPlayhead(value ? (y ? value->yCurveInput : value->curveInput) : 0, value != nullptr);
    auto place = [](double v, const std::pair<double, double> &span) { // within the meter's range: 0..1000
        const double w = span.second - span.first;
        return int(std::lround(std::clamp(std::abs(w) < 1e-12 ? 0.0 : (v - span.first) / w, 0.0, 1.0) * 1000));
    };
    if (m_inMeter) m_inMeter->setValue(value ? place(y ? value->yInput : value->input, m_inSpan) : 0);
    if (!m_stageMeters.empty()) { // each filter's output
        static const std::vector<double> none;
        const std::vector<double> &stages = !value ? none : y ? value->yStages : value->stages;
        for (size_t k = 0; k < m_stageMeters.size(); ++k) {
            const StageMeter &m = m_stageMeters[k];
            const bool known = k < stages.size();
            m.bar->setValue(known ? place(stages[k], m.span) : 0);
            const QString text = known ? QString::number(stages[k], 'g', 5) : QStringLiteral("—");
            if (m.number->text() != text) m.number->setText(text);
        }
    }
    if (m_outMeter) m_outMeter->setValue(value ? place(y ? value->yOutput : value->output, m_outSpan) : 0);
    if (m_actionState) {
        const QString state = !value || value->met < 0 ? QStringLiteral("— (no value yet)")
                            : value->met ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
        if (m_actionState->text() != state) m_actionState->setText(state);
    }
    if (m_copy) m_copy->setEnabled(m_shown != 0);
    if (m_paste) m_paste->setEnabled(QApplication::clipboard()->mimeData() && QApplication::clipboard()->mimeData()->hasFormat(kMime) &&
                                     !m_list->selectedItems().isEmpty());
}

void InteractionPanel::pollLearn()
{
    if (!m_learn->isChecked()) return;
    std::vector<InteractionEvent> heard = m_engine->takeLearnedInteractions();
    if (heard.empty()) return;
    m_learn->setChecked(false); // learning stops: what came in is chosen from
    InteractionEvent chosen = heard.front();
    if (heard.size() > 1) { // several moved together: which one?
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Learn"));
        auto *v = new QVBoxLayout(&dialog);
        v->addWidget(new QLabel(QStringLiteral("Several sources came in at the same time. Which one?")));
        auto *list = new QListWidget;
        for (const InteractionEvent &e : heard) list->addItem(e.describe() + (e.port.isEmpty() ? QString() : QStringLiteral(" · ") + e.port));
        list->setCurrentRow(0);
        v->addWidget(list);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        v->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
        if (dialog.exec() != QDialog::Accepted || list->currentRow() < 0) return;
        chosen = heard[size_t(list->currentRow())];
    }
    applyLearned(chosen);
}

void InteractionPanel::applyLearned(const InteractionEvent &ev)
{
    const InteractionEvent *e = &ev;
    std::optional<Interaction> sel = selected();
    Interaction it = sel ? *sel : Interaction::made(e->source);
    Interaction &input = sel && m_xyAxis == 1 && !it.xyInputs.empty() ? it.xyInputs.front() : it;
    if (sel && input.source != e->source) input.takeover = Interaction::made(e->source).takeover; // that kind's
    input.source = e->source;
    switch (e->source) {
    case InteractionSource::Keyboard: input.key = e->address; break;
    case InteractionSource::Midi:
        input.midiPort = e->port;
        input.midiChannel = e->channel;
        input.midiMessage = e->message;
        input.midiNumber = e->number;
        break;
    case InteractionSource::Osc:
        input.oscAddress = e->address;
        input.oscArgument = e->argument;
        break;
    case InteractionSource::Pixels:
    case InteractionSource::Hands:
    case InteractionSource::Audio:
    case InteractionSource::Internal: break;
    }
    if (m_xyAxis == 0 && !it.xyInputs.empty() && e->source == InteractionSource::Osc &&
        it.xyInputs.front().source == InteractionSource::Osc && it.xyInputs.front().oscAddress.isEmpty()) {
        it.xyInputs.front().oscAddress = e->address;
        it.xyInputs.front().oscArgument = std::min(63, e->argument + 1);
    }
    if (sel) {
        commit(it, QStringLiteral("Learn Interaction Source"));
        if (shownKind() && *shownKind() != it.source) m_kinds->setCurrentIndex(int(it.source));
    } else {
        std::vector<Interaction> list = m_engine->interactions();
        quint64 id = 1;
        for (const Interaction &x : list) id = std::max(id, x.id + 1);
        it.id = id;
        list.push_back(it);
        commitList(list, QStringLiteral("Learn Interaction"));
        if (shownKind() && *shownKind() != it.source) m_kinds->setCurrentIndex(int(it.source));
        select(id);
    }
    rebuildList();
    select(it.id);
    rebuildSettings();
}

void InteractionPanel::select(quint64 id)
{
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (m_list->topLevelItem(i)->data(0, Qt::UserRole).toULongLong() == id) {
            m_list->clearSelection();
            m_list->setCurrentItem(m_list->topLevelItem(i));
        }
}

// From a value anywhere (its right-click): a new interaction driving it, its source learnt (of that kind), or chosen
// in its settings (Internal)
void InteractionPanel::learnFor(quint64 layer, const QString &path, InteractionSource kind)
{
    std::vector<Interaction> list = m_engine->interactions();
    Interaction it = Interaction::made(kind);
    quint64 id = 1;
    for (const Interaction &x : list) id = std::max(id, x.id + 1);
    it.id = id;
    m_engine->configureInteractionTarget(it, layer, path);
    m_xyAxis = 0;
    list.push_back(it);
    commitList(list, QStringLiteral("New Interaction"));
    emit showRequested();
    m_kinds->setCurrentIndex(int(kind));
    rebuildList();
    select(id);
    rebuildSettings();
    if (kind != InteractionSource::Internal && kind != InteractionSource::Audio && kind != InteractionSource::Pixels &&
        kind != InteractionSource::Hands) {
        m_learnKind = kind;
        m_learn->setChecked(true); // the next source of that kind becomes its
    }
}

InteractionPanel *InteractionPanel::s_instance = nullptr;

InteractionPanel::~InteractionPanel()
{
    if (s_instance == this) s_instance = nullptr;
}

void InteractionPanel::fillValueMenu(QMenu *menu, Engine *e, quint64 layer, const QStringList &paths)
{
    if (paths.isEmpty()) return;
    std::vector<ParamInfo> params;
    for (const auto &path : paths) {
        ParamInfo info;
        if (e->parameterInfo(layer, path, &info)) params.push_back(info);
    }
    for (const auto &point : e->animatablePoints(layer))
        if (paths.contains(point.path) || (paths.contains(point.path + "/x") && paths.contains(point.path + "/y")))
            params.insert(params.begin(), point);
    if (params.empty()) return;
    if (auto *panel = instance()) {
        QMenu *animate = nullptr;
        for (auto *action : menu->actions()) if (action->text() == QLatin1String("Animate")) animate = action->menu();
        QMenu *interaction = nullptr;
        for (const auto &param : params) {
            if (!(param.access & ParamInfo::Write) || param.type == ParamInfo::Type::Text) continue;
            if (!interaction) {
                if (!animate) animate = menu->addMenu(QStringLiteral("Animate"));
                interaction = animate->addMenu(QStringLiteral("Interaction"));
                for (InteractionSource kind : kKinds) {
                    if (params.size() == 1)
                        interaction->addAction(interactionSourceName(kind), panel, [panel, layer, path = param.path, kind] {
                            panel->learnFor(layer, path, kind);
                        });
                    else interaction->addMenu(interactionSourceName(kind));
                }
            }
            if (params.size() > 1) for (int i = 0; i < interaction->actions().size(); ++i) {
                const auto kind = kKinds[i];
                interaction->actions()[i]->menu()->addAction(param.name(), panel, [panel, layer, path = param.path, kind] {
                    panel->learnFor(layer, path, kind);
                });
            }
        }
    }
    QMenu *addresses = params.size() > 1 ? menu->addMenu(QStringLiteral("Copy OSC Address")) : menu;
    for (const auto &param : params) {
        const QString addr = OscNamespace::addressOf(e, layer, param.path);
        if (addr.isEmpty()) continue;
        auto *action = addresses->addAction(params.size() > 1 ? param.name() : QStringLiteral("Copy OSC Address"),
                                            menu, [addr] { QApplication::clipboard()->setText(addr); });
        action->setToolTip(addr);
    }
}

// ---------------------------------------------------------------------------
// The settings of the one selected
// ---------------------------------------------------------------------------
void InteractionPanel::attachPreset(std::initializer_list<QWidget *> ws, const QStringList &paths)
{
    PresetEditor *pe = PresetEditor::instance();
    if (!pe || !m_shown) return;
    QStringList full;
    for (const QString &p : paths) {
        const bool y = m_xyAxis == 1 && p != QLatin1String("name") && p != QLatin1String("target");
        full << QStringLiteral("interaction/%1/%2%3").arg(m_shown).arg(y ? QStringLiteral("xy/y/") : QString()).arg(p);
    }
    for (QWidget *w : ws) {
        if (!w) continue;
        pe->track(w, 0, full); // Edit mode: underlined, its name puts it in the preset or takes it out
        w->setProperty("presetPaths", full);
        w->installEventFilter(this);
        for (QWidget *c : w->findChildren<QWidget *>()) c->installEventFilter(this);
    }
}

bool InteractionPanel::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() != QEvent::ContextMenu) return QWidget::eventFilter(o, e);
    auto *w = qobject_cast<QWidget *>(o);
    while (w && !w->property("presetPaths").isValid()) w = w->parentWidget();
    PresetEditor *pe = PresetEditor::instance();
    if (!w || !pe) return QWidget::eventFilter(o, e);
    auto *menu = new QMenu(w);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    pe->fillMenu(menu, 0, w->property("presetPaths").toStringList()); // Add to New Preset / to Selected Preset
    TimelineWindow::fillParameterMenu(menu, m_engine, m_undo, 0, w->property("presetPaths").toStringList(), w);
    fillValueMenu(menu, m_engine, 0, w->property("presetPaths").toStringList());
    if (menu->isEmpty()) {
        delete menu;
        return QWidget::eventFilter(o, e);
    }
    menu->popup(static_cast<QContextMenuEvent *>(e)->globalPos());
    return true;
}

void InteractionPanel::fillByOwner(QMenu *menu, const std::function<void(QMenu *, quint64)> &fill)
{
    fill(menu->addMenu(QStringLiteral("Composition")), 0);
    Engine::Lock lk(&m_engine->mutex());
    for (int i = 0; i < m_engine->layerCount(); ++i)
        if (const Layer *l = m_engine->layer(i); l && !l->isGroup) fill(menu->addMenu(l->name), l->id);
}

std::pair<double, double> InteractionPanel::sourceSpan(const Interaction &it) const
{
    ParamInfo info;
    if (it.source == InteractionSource::Internal && m_engine->parameterInfo(it.sourceLayer, it.sourceParam, &info)) {
        if (info.type == ParamInfo::Type::Choice) return {0.0, double(std::max(1, int(info.choices.size()) - 1))};
        if (info.isNumber()) return {info.min, info.max};
    }
    return {0.0, 1.0};
}

std::pair<double, double> InteractionPanel::targetSpan(const Interaction &root, int axis) const
{
    ParamInfo info;
    const QString path = root.xyInputs.empty() ? root.param : root.param + (axis ? "/y" : "/x");
    if (!m_engine->parameterInfo(root.layer, path, &info)) return {0.0, 1.0};
    if (info.type == ParamInfo::Type::Choice) return {0.0, double(std::max(1, int(info.choices.size()) - 1))};
    return info.isNumber() ? std::pair{info.min, info.max} : std::pair{0.0, 1.0};
}

void InteractionPanel::rebuildSettings()
{
    const std::optional<Interaction> cur = selected();
    if (!cur || cur->id != m_shown || cur->xyInputs.empty()) m_xyAxis = 0;
    m_shown = cur ? cur->id : 0;
    m_inMeter = m_outMeter = nullptr;
    m_actionState = nullptr;
    m_stageMeters.clear();
    m_lane = nullptr;
    while (QLayoutItem *item = m_settingsLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) { w->hide(); w->deleteLater(); }
        delete item;
    }
    if (!cur) {
        m_settingsTitle->setText(QStringLiteral("Settings"));
        m_settingsLayout->addWidget(note(shownKind()==InteractionSource::Audio ? QStringLiteral("Add Audio (+), select a frequency band, then choose the parameter to drive.") : QStringLiteral("Add an interaction (+), or press Learn and move a controller, "
                                                        "press a key or send an OSC message.")));
        m_settingsLayout->addStretch();
        return;
    }
    const Interaction root = *cur;
    Interaction it = m_xyAxis == 1 ? root.xyInputs.front() : root;
    it.id = root.id;
    it.name = root.name;
    it.layer = root.layer;
    it.param = root.param;
    m_settingsTitle->setText(QStringLiteral("Settings · %1").arg(interactionSourceName(it.source)));
    const quint64 id = it.id;
    // An edit of this one: read again as it is now (another edit may have come), changed, committed
    auto editRoot = [this, id](const QString &text, const std::function<void(Interaction &)> &fn, const QString &merge = QString()) {
        for (Interaction x : m_engine->interactions())
            if (x.id == id) {
                fn(x);
                commit(x, text, merge);
            }
    };

    const int axis = m_xyAxis;
    auto edit = [editRoot, axis](const QString &text, const std::function<void(Interaction &)> &fn, const QString &merge = QString()) {
        editRoot(text, [axis, fn](Interaction &x) {
            if (axis == 1 && !x.xyInputs.empty()) fn(x.xyInputs.front());
            else fn(x);
        }, merge.isEmpty() ? merge : merge + QStringLiteral("/axis%1").arg(axis));
    };

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *formBox = new QWidget;
    formBox->setLayout(form);
    m_settingsLayout->addWidget(formBox);

    auto *name = new QLineEdit(it.name);
    {
        Interaction unnamed = it;
        unnamed.name.clear();
        name->setPlaceholderText(describe(unnamed));
    }
    form->addRow(QStringLiteral("Name"), name);
    attachPreset({name}, {QStringLiteral("name")});
    attachPreset({m_settingsTitle}, {QStringLiteral("source/kind")});
    connect(name, &QLineEdit::editingFinished, this, [editRoot, name] {
        editRoot(QStringLiteral("Rename Interaction"), [name](Interaction &x) { x.name = name->text().trimmed(); });
    });

    // Mapping: the source drives the target through the chain · Action: tests on the source's value, and what is done
    // when they become true, false
    if (root.xyInputs.empty()) {
        auto *mode = new QComboBox;
        mode->setObjectName(QStringLiteral("interactionMode"));
        mode->addItems({QStringLiteral("Mapping"), QStringLiteral("Action")});
        mode->setCurrentIndex(int(it.mode));
        mode->setToolTip(QStringLiteral("Mapping: the source drives a value of the show through the chain below\n"
                                        "Action: when the source's value meets its conditions, it does the actions "
                                        "\"if true\"; when it no longer does, the actions \"if false\""));
        form->addRow(QStringLiteral("Mode"), mode);
        connect(mode, qOverload<int>(&QComboBox::activated), this, [this, editRoot](int m) {
            editRoot(QStringLiteral("Change Interaction Mode"), [m](Interaction &x) {
                x.mode = InteractionMode(m);
                if (x.isAction() && x.conditions.empty()) x.conditions = {InteractionCondition{}};
            });
            rebuildSettings();
            rebuildList();
        });
    }

    if (!root.xyInputs.empty()) {
        auto *axisBox = new QComboBox;
        axisBox->setObjectName(QStringLiteral("xyInteractionAxis"));
        axisBox->addItems({QStringLiteral("X input and mapping"), QStringLiteral("Y input and mapping")});
        axisBox->setCurrentIndex(m_xyAxis);
        form->addRow(QStringLiteral("XY component"), axisBox);
        connect(axisBox, qOverload<int>(&QComboBox::activated), this, [this](int axis) {
            m_xyAxis = axis;
            rebuildSettings();
        });
        auto *kind = new QComboBox;
        kind->setObjectName(QStringLiteral("xyInteractionSource"));
        for (int k = 0; k < kInteractionSourceCount; ++k) kind->addItem(interactionSourceName(InteractionSource(k)));
        kind->setCurrentIndex(int(it.source));
        form->addRow(QStringLiteral("Input type"), kind);
        attachPreset({kind}, {QStringLiteral("source/kind")});
        connect(kind, qOverload<int>(&QComboBox::activated), this, [this, edit](int k) {
            edit(QStringLiteral("Change XY Input Type"), [k](Interaction &x) { x.source = InteractionSource(k); });
            rebuildSettings();
            rebuildList();
        });
        form->addRow(note(QStringLiteral("Each axis has its own input and mapping. For one OSC XY message, use the same address with arguments 0 and 1.")));
    }
    // The source
    // What the source gives and the target takes, as they usually go: a Remap added from + starts with them, the
    // In / Out meters show them
    const auto srcSpan = sourceSpan(it), tgtSpan = targetSpan(root, m_xyAxis);
    const double srcLo = srcSpan.first, srcHi = srcSpan.second, tgtLo = tgtSpan.first, tgtHi = tgtSpan.second;
    m_inSpan = {srcLo, srcHi};
    m_outSpan = {tgtLo, tgtHi};
    switch (it.source) {
    case InteractionSource::Keyboard: {
        auto *key = new QKeySequenceEdit(QKeySequence::fromString(it.key, QKeySequence::PortableText));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        key->setMaximumSequenceLength(1);
#endif
        key->setToolTip(QStringLiteral("The key (with its modifiers): pressed 1, let go 0"));
        form->addRow(QStringLiteral("Key"), key);
        attachPreset({key}, {QStringLiteral("source/key")});
        connect(key, &QKeySequenceEdit::editingFinished, this, [edit, key] {
            const QKeySequence seq = key->keySequence();
            const QString k = seq.isEmpty() ? QString() : QKeySequence(seq[0]).toString(QKeySequence::PortableText); // one key
            edit(QStringLiteral("Change Interaction Key"), [k](Interaction &x) { x.key = k; });
        });
        break;
    }
    case InteractionSource::Midi: {
        auto *port = new QComboBox;
        port->addItem(QStringLiteral("Any input"), QString());
        for (const QString &p : Engine::midiInputPorts()) port->addItem(p, p);
        int at = port->findData(it.midiPort);
        if (at < 0) {
            port->addItem(QStringLiteral("%1 (not found)").arg(it.midiPort), it.midiPort);
            at = port->count() - 1;
        }
        port->setCurrentIndex(at);
        port->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        port->setMinimumContentsLength(8);
        form->addRow(QStringLiteral("Input"), port);
        attachPreset({port}, {QStringLiteral("source/midi/input")});
        auto *row = new QHBoxLayout;
        auto *channel = new QComboBox;
        channel->addItem(QStringLiteral("Any channel"), 0);
        for (int c = 1; c <= 16; ++c) channel->addItem(QStringLiteral("Channel %1").arg(c), c);
        channel->setCurrentIndex(channel->findData(it.midiChannel));
        auto *message = new QComboBox;
        for (int k = 0; k < 4; ++k) message->addItem(midiMessageName(MidiMessageKind(k)), k);
        message->setCurrentIndex(int(it.midiMessage));
        auto *number = new IntBox;
        number->setRange(0, 127);
        number->setValue(it.midiNumber);
        number->setEnabled(it.midiMessage != MidiMessageKind::PitchBend);
        number->setToolTip(QStringLiteral("The controller, the note or the program"));
        row->addWidget(channel, 1);
        row->addWidget(message, 1);
        row->addWidget(number);
        form->addRow(QStringLiteral("Message"), row);
        attachPreset({channel}, {QStringLiteral("source/midi/channel")});
        attachPreset({message}, {QStringLiteral("source/midi/message")});
        attachPreset({number}, {QStringLiteral("source/midi/number")});
        connect(port, qOverload<int>(&QComboBox::activated), this, [edit, port](int) {
            const QString p = port->currentData().toString();
            edit(QStringLiteral("Change Interaction Input"), [p](Interaction &x) { x.midiPort = p; });
        });
        connect(channel, qOverload<int>(&QComboBox::activated), this, [edit, channel](int) {
            const int c = channel->currentData().toInt();
            edit(QStringLiteral("Change Interaction Channel"), [c](Interaction &x) { x.midiChannel = c; });
        });
        connect(message, qOverload<int>(&QComboBox::activated), this, [this, edit](int k) {
            edit(QStringLiteral("Change Interaction Message"), [k](Interaction &x) { x.midiMessage = MidiMessageKind(k); });
            rebuildSettings();
        });
        connect(number, qOverload<int>(&QSpinBox::valueChanged), this, [edit](int n) {
            edit(QStringLiteral("Change Interaction Number"), [n](Interaction &x) { x.midiNumber = n; }, QStringLiteral("midinumber"));
        });
        break;
    }
    case InteractionSource::Osc: {
        auto *address = new QLineEdit(it.oscAddress);
        address->setObjectName(QStringLiteral("interactionOscAddress"));
        address->setPlaceholderText(QStringLiteral("/fader/1"));
        auto *argument = new IntBox;
        argument->setObjectName(QStringLiteral("interactionOscArgument"));
        argument->setRange(0, 63);
        argument->setValue(it.oscArgument);
        argument->setPrefix(QStringLiteral("arg "));
        argument->setToolTip(QStringLiteral("Which value of the message (the first: 0)"));
        auto *row = new QHBoxLayout;
        row->addWidget(address, 1);
        row->addWidget(argument);
        form->addRow(QStringLiteral("Address"), row);
        attachPreset({address}, {QStringLiteral("source/osc/address")});
        attachPreset({argument}, {QStringLiteral("source/osc/argument")});
        connect(address, &QLineEdit::editingFinished, this, [edit, address] {
            const QString a = address->text().trimmed();
            edit(QStringLiteral("Change Interaction Address"), [a](Interaction &x) { x.oscAddress = a; });
        });
        connect(argument, qOverload<int>(&QSpinBox::valueChanged), this, [edit](int a) {
            edit(QStringLiteral("Change Interaction Argument"), [a](Interaction &x) { x.oscArgument = a; }, QStringLiteral("oscarg"));
        });
        break;
    }
    case InteractionSource::Audio: {
        form->addRow(note(QStringLiteral("1 · Select the sound, then 2 · Map it to a parameter below")));
        auto *input=new QComboBox;input->setObjectName("audioInput");
        input->addItems({QStringLiteral("Program mix"),QStringLiteral("Live input")});input->setCurrentIndex(it.audioInput?1:0);
        input->setToolTip(QStringLiteral("Program mix: every playing audio layer, before master volume. Live input: the sound card input chosen in Settings › Audio Input"));
        form->addRow(QStringLiteral("Audio input"),input);
        connect(input,qOverload<int>(&QComboBox::activated),this,[this,edit](int k){edit(QStringLiteral("Change Audio Input"),[k](Interaction &x){x.audioInput=k==1;});rebuildSettings();});
        attachPreset({input},{QStringLiteral("source/audio/input")});
        if(it.audioInput){
            AudioInput &live=m_engine->audioInput();
            form->addRow(QString(),note(live.isRunning()?QStringLiteral("Listening to %1").arg(live.deviceName())
                                                         :QStringLiteral("No live input open: choose one in Settings › Audio Input")));
        }
        auto *feature=new QComboBox;feature->setObjectName("audioFeature");
        feature->addItems({QStringLiteral("Selected frequency band"),QStringLiteral("Overall level"),QStringLiteral("Attacks / onset"),QStringLiteral("Hits (a trigger)")});feature->setToolTip(QStringLiteral("Hits: a trigger at each hit (a kick, a snare) in the frequency region, at the hit's velocity — feed it an Envelope filter for a flash or an impact"));feature->setCurrentIndex(int(it.audioFeature));
        form->addRow(QStringLiteral("Measure"),feature);
        connect(feature,qOverload<int>(&QComboBox::activated),this,[this,edit](int k){edit(QStringLiteral("Change Audio Measure"),[k](Interaction &x){x.audioFeature=Interaction::AudioFeature(k);});rebuildSettings();});
        attachPreset({feature},{QStringLiteral("source/audio/feature")});
        if(it.audioFeature==Interaction::AudioFeature::Band||it.audioFeature==Interaction::AudioFeature::Hit){
            Engine *engine=m_engine;const bool live=it.audioInput;
            auto *spectrum=new AudioSpectrum([engine,live]{return live?engine->audioInput().spectrum():engine->audioOutput().spectrum();});spectrum->setBand(it.audioLow,it.audioHigh);
            form->addRow(spectrum);
            auto *band=new QWidget;auto *row=new QHBoxLayout(band);row->setContentsMargins(0,0,0,0);
            auto *low=new QDoubleSpinBox;auto *high=new QDoubleSpinBox;
            low->setObjectName("audioLowHz");high->setObjectName("audioHighHz");
            for(auto *f:{low,high}){f->setRange(20,20000);f->setDecimals(0);f->setSuffix(QStringLiteral(" Hz"));f->setKeyboardTracking(false);}
            low->setValue(it.audioLow);high->setValue(it.audioHigh);row->addWidget(low);row->addWidget(new QLabel(QStringLiteral("to")));row->addWidget(high);
            form->addRow(QStringLiteral("Frequency band"),band);
            auto setBand=[edit,low,high,spectrum](double lo,double hi){lo=std::clamp(lo,20.,19999.);hi=std::clamp(hi,lo+1.,20000.);QSignalBlocker a(low),b(high);low->setValue(lo);high->setValue(hi);spectrum->setBand(lo,hi);edit(QStringLiteral("Change Audio Band"),[lo,hi](Interaction &x){x.audioLow=lo;x.audioHigh=hi;},QStringLiteral("audio_band"));};
            connect(spectrum,&AudioSpectrum::bandEdited,this,setBand);
            connect(low,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[setBand,high](double v){setBand(v,std::max(v+1.,high->value()));});
            connect(high,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[setBand,low](double v){setBand(std::min(low->value(),v-1.),v);});
            attachPreset({low},{QStringLiteral("source/audio/low")});attachPreset({high},{QStringLiteral("source/audio/high")});
            auto *regions=new QComboBox;regions->addItem(QStringLiteral("Choose a region…"));
            regions->addItems({QStringLiteral("Sub · 20–80 Hz"),QStringLiteral("Bass · 80–250 Hz"),QStringLiteral("Body · 250–2000 Hz"),QStringLiteral("Presence · 2000–6000 Hz"),QStringLiteral("Air · 6000–20000 Hz"),QStringLiteral("Full spectrum")});
            form->addRow(QStringLiteral("Regions"),regions);
            connect(regions,qOverload<int>(&QComboBox::activated),this,[setBand](int k){const double bands[][2]={{20,20000},{20,80},{80,250},{250,2000},{2000,6000},{6000,20000},{20,20000}};if(k>=0&&k<7)setBand(bands[k][0],bands[k][1]);});
        }
        if(it.audioFeature==Interaction::AudioFeature::Hit){
            // How faint a hit it hears, and how soon after one it may hear the next (a kick's tail is not a second kick)
            auto *sens=new SliderField;sens->setObjectName("audioHitSensitivity");sens->setRange(0,100);sens->setDecimals(0);sens->setSuffix(QStringLiteral(" %"));sens->setValue(it.audioSensitivity*100);
            sens->setToolTip(QStringLiteral("How faint a hit it hears: high catches soft hits (and some noise), low only the hard ones. The detector learns the usual level of the sound by itself."));
            form->addRow(QStringLiteral("Sensitivity"),sens);
            connect(sens,&SliderField::valueEdited,this,[edit](double v){edit(QStringLiteral("Change Hit Sensitivity"),[v](Interaction &x){x.audioSensitivity=std::clamp(v/100.,0.,1.);},QStringLiteral("audio_hit_sensitivity"));});
            attachPreset({sens},{QStringLiteral("source/audio/sensitivity")});
            auto *gap=new SliderField;gap->setObjectName("audioHitGap");gap->setRange(40,1000);gap->setDecimals(0);gap->setSuffix(QStringLiteral(" ms"));gap->setValue(it.audioGap*1000);
            gap->setToolTip(QStringLiteral("The shortest time between two hits. 80 ms follows fast patterns; raise it to ignore a kick's rebound or a hi-hat's ring."));
            form->addRow(QStringLiteral("Minimum gap"),gap);
            connect(gap,&SliderField::valueEdited,this,[edit](double v){edit(QStringLiteral("Change Hit Gap"),[v](Interaction &x){x.audioGap=std::clamp(v/1000.,0.04,2.);},QStringLiteral("audio_hit_gap"));});
            attachPreset({gap},{QStringLiteral("source/audio/gap")});
            form->addRow(QString(),note(QStringLiteral("A pulse of 40 ms at the hit's velocity: add an Envelope filter for a flash or an impact.")));
        }
        break;
    }
    case InteractionSource::Pixels:
    case InteractionSource::Hands: {
        const bool hands = it.source == InteractionSource::Hands;
        form->addRow(note(QStringLiteral("1 · Choose an image layer (camera, video, mask or composition), then 2 · Map "
                                         "its analysis below. Masks are separate Motion Difference FX.")));
        auto *source = new QComboBox;
        source->setObjectName(QStringLiteral("visionSource"));
        source->addItem(QStringLiteral("Composition"), QStringLiteral("0"));
        {
            Engine::Lock lock(&m_engine->mutex());
            for (int i = 0; i < m_engine->layerCount(); ++i) {
                const auto *l = m_engine->layer(i);
                if (l)
                    source->addItem(l->name, QString::number(l->id));
            }
        }
        int selected = source->findData(QString::number(it.pixels.layer));
        if (selected < 0) {
            source->addItem(QStringLiteral("Missing layer %1").arg(it.pixels.layer), QString::number(it.pixels.layer));
            selected = source->count() - 1;
        }
        source->setCurrentIndex(selected);
        form->addRow(QStringLiteral("Image source"), source);
        connect(source, qOverload<int>(&QComboBox::activated), this, [this, edit, source](int index) {
            const auto id = source->itemData(index).toString().toULongLong();
            edit(QStringLiteral("Change Image Analysis Source"), [id](Interaction &x) { x.pixels.layer = id; });
            rebuildSettings();
        });
        attachPreset({source}, {QStringLiteral("source/vision/layer")});
        auto *tap = new QComboBox;
        tap->setObjectName(QStringLiteral("visionTap"));
        tap->addItems({QStringLiteral("Source"), QStringLiteral("Before FX"), QStringLiteral("After FX (mask)")});
        tap->setCurrentIndex(it.pixels.tap);
        form->addRow(QStringLiteral("Image tap"), tap);
        tap->setEnabled(it.pixels.layer != 0);
        connect(tap, qOverload<int>(&QComboBox::activated), this, [this, edit](int value) {
            edit(QStringLiteral("Change Analysis Tap"), [value](Interaction &x) { x.pixels.tap = value; });
            rebuildSettings();
        });
        attachPreset({tap}, {QStringLiteral("source/vision/tap")});
        if (!hands) {
            auto *channel = new QComboBox;
            channel->setObjectName(QStringLiteral("pixelChannel"));
            channel->addItems(PixelAnalysis::channelNames());
            channel->setCurrentIndex(int(it.pixels.channel));
            form->addRow(QStringLiteral("Channel"), channel);
            connect(channel, qOverload<int>(&QComboBox::activated), this, [edit](int value) {
                edit(QStringLiteral("Change Pixel Channel"),
                     [value](Interaction &x) { x.pixels.channel = PixelAnalysis::Channel(value); });
            });
            attachPreset({channel}, {QStringLiteral("source/pixels/channel")});
            auto *feature = new QComboBox;
            feature->setObjectName(QStringLiteral("pixelFeature"));
            feature->addItems(PixelAnalysis::featureNames());
            feature->setCurrentIndex(int(it.pixelFeature));
            form->addRow(QStringLiteral("Measure"), feature);
            connect(feature, qOverload<int>(&QComboBox::activated), this, [edit](int value) {
                edit(QStringLiteral("Change Pixel Measure"),
                     [value](Interaction &x) { x.pixelFeature = PixelAnalysis::Feature(value); });
            });
            attachPreset({feature}, {QStringLiteral("source/pixels/feature")});
            auto *threshold = new QDoubleSpinBox;
            threshold->setRange(0, 1);
            threshold->setDecimals(3);
            threshold->setSingleStep(.01);
            threshold->setValue(it.pixels.threshold);
            threshold->setKeyboardTracking(false);
            form->addRow(QStringLiteral("Active threshold"), threshold);
            connect(threshold, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [edit](double value) {
                edit(
                    QStringLiteral("Change Pixel Threshold"), [value](Interaction &x) { x.pixels.threshold = value; },
                    QStringLiteral("pixel_threshold"));
            });
            attachPreset({threshold}, {QStringLiteral("source/pixels/threshold")});
            const char *labels[] = {"x", "y", "width", "height"};
            const double values[] = {it.pixels.roi.x(), it.pixels.roi.y(), it.pixels.roi.width(),
                                     it.pixels.roi.height()};
            for (int c = 0; c < 4; ++c) {
                auto *number = new QDoubleSpinBox;
                number->setObjectName(QStringLiteral("pixelROI_") + labels[c]);
                number->setRange(0, 1);
                number->setDecimals(3);
                number->setSingleStep(.01);
                number->setValue(values[c]);
                number->setKeyboardTracking(false);
                form->addRow(QStringLiteral("ROI ") + labels[c], number);
                connect(number, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [edit, c](double value) {
                    edit(
                        QStringLiteral("Change Pixel ROI"),
                        [value, c](Interaction &x) {
                            const auto r = x.pixels.roi;
                            x.pixels.roi = QRectF(c == 0 ? value : r.x(), c == 1 ? value : r.y(),
                                                  c == 2 ? value : r.width(), c == 3 ? value : r.height())
                                               .intersected(QRectF(0, 0, 1, 1));
                        },
                        QStringLiteral("pixel_roi"));
                });
                attachPreset({number}, {QStringLiteral("source/pixels/roi/") + labels[c]});
            }
        } else {
            auto *hand = new QComboBox;
            hand->setObjectName(QStringLiteral("trackedHand"));
            hand->addItems({QStringLiteral("Tracked hand 1"), QStringLiteral("Tracked hand 2")});
            hand->setCurrentIndex(it.handIndex);
            form->addRow(QStringLiteral("Hand"), hand);
            connect(hand, qOverload<int>(&QComboBox::activated), this, [edit](int value) {
                edit(QStringLiteral("Change Tracked Hand"), [value](Interaction &x) { x.handIndex = value; });
            });
            attachPreset({hand}, {QStringLiteral("source/hands/index")});
            auto *feature = new QComboBox;
            feature->setObjectName(QStringLiteral("handFeature"));
            feature->addItems(HandTracking::featureNames());
            feature->setCurrentIndex(int(it.handFeature));
            form->addRow(QStringLiteral("Measure"), feature);
            connect(feature, qOverload<int>(&QComboBox::activated), this, [edit](int value) {
                edit(QStringLiteral("Change Hand Measure"),
                     [value](Interaction &x) { x.handFeature = HandTracking::Feature(value); });
            });
            attachPreset({feature}, {QStringLiteral("source/hands/feature")});
            auto *confidence = new QDoubleSpinBox;
            confidence->setRange(.5, .99);
            confidence->setDecimals(2);
            confidence->setSingleStep(.05);
            confidence->setValue(it.handConfidence);
            confidence->setKeyboardTracking(false);
            form->addRow(QStringLiteral("Min confidence"), confidence);
            connect(confidence, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [edit](double value) {
                edit(QStringLiteral("Change Hand Confidence"), [value](Interaction &x) { x.handConfidence = value; });
            });
            attachPreset({confidence}, {QStringLiteral("source/hands/confidence")});
            auto *models = new QPushButton(QStringLiteral("Choose hand models folder…"));
            form->addRow(models);
            connect(models, &QPushButton::clicked, this, [this] {
                const auto directory = QFileDialog::getExistingDirectory(
                    this, QStringLiteral("OpenCV Zoo hand model directory"), m_engine->handModelsDirectory());
                if (!directory.isEmpty()) {
                    m_engine->setHandModelsDirectory(directory);
                    rebuildSettings();
                }
            });
            form->addRow(note(QStringLiteral("Local OpenCV DNN / MediaPipe: two verified ONNX models. Download with "
                                             "scripts/download_hand_models.py; no images leave this computer.")));
        }
        auto *status = new QLabel;
        status->setWordWrap(true);
        status->setObjectName(QStringLiteral("visionStatus"));
        form->addRow(status);
        auto *preview = new QLabel;
        preview->setObjectName(QStringLiteral("visionPreview"));
        preview->setMinimumHeight(80);
        preview->setMaximumHeight(200);
        preview->setAlignment(Qt::AlignCenter);
        form->addRow(preview);
        auto *timer = new QTimer(status);
        const auto settings = it.pixels;
        auto refresh = [this, status, preview, settings, hands] {
            auto current = settings;
            if (const auto root = this->selected()) {
                const auto &input = m_xyAxis == 1 && !root->xyInputs.empty() ? root->xyInputs.front() : *root;
                current = input.pixels;
            }
            status->setText(m_engine->visionStatus(current, hands));
            auto image = m_engine->visionPreview(current, hands);
            if (!image.isNull()) {
                if (!hands && current.roi != QRectF(0, 0, 1, 1)) {
                    QPainter painter(&image);
                    painter.setPen(QPen(Qt::yellow, 1));
                    const auto r = current.roi;
                    painter.drawRect(QRectF(r.x() * image.width(), r.y() * image.height(), r.width() * image.width(),
                                            r.height() * image.height()));
                }
                preview->setPixmap(
                    QPixmap::fromImage(image).scaled(280, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            } else
                preview->clear();
        };
        connect(timer, &QTimer::timeout, status, refresh);
        timer->start(150);
        refresh();
        break;
    }
    case InteractionSource::Internal: {
        auto *source = new QToolButton;
        source->setText(targetLabel(it.sourceLayer, it.sourceParam));
        source->setPopupMode(QToolButton::InstantPopup);
        source->setToolButtonStyle(Qt::ToolButtonTextOnly);
        source->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        source->setToolTip(QStringLiteral("The value of the show it follows"));
        auto *menu = new QMenu(source);
        source->setMenu(menu);
        connect(menu, &QMenu::aboutToShow, this, [this, menu, edit] {
            menu->clear();
            auto fill = [&](QMenu *into, quint64 layer) {
                std::vector<ParamInfo> params;
                for (const ParamInfo &p : m_engine->parameters(layer))
                    if (!p.path.startsWith(QStringLiteral("audio/")) && (p.access & ParamInfo::Read) &&
                        (p.isNumber() || p.type == ParamInfo::Type::Bool || p.type == ParamInfo::Type::Choice))
                        params.push_back(p);
                addParamMenu(into, params, [this, edit, layer](QMenu *m, const ParamInfo &p, const QString &text) {
                    m->addAction(text, this, [this, edit, layer, p] {
                        edit(QStringLiteral("Change Interaction Source"), [layer, p](Interaction &x) {
                            x.sourceLayer = layer;
                            x.sourceParam = p.path; // a switch follows as 0 / 1, a choice as its index
                        });
                        rebuildSettings();
                    });
                });
            };
            fillByOwner(menu, fill);
        });
        form->addRow(QStringLiteral("Follows"), source);
        attachPreset({source}, {QStringLiteral("source/internal")});
        break;
    }
    }

    if (it.isAction() && root.xyInputs.empty()) {
        buildActionSettings(form, it, editRoot);
        m_settingsLayout->addStretch();
        return;
    }

    // The target
    auto *target = new QToolButton;
    target->setText(targetLabel(it.layer, it.param));
    target->setPopupMode(QToolButton::InstantPopup);
    target->setToolButtonStyle(Qt::ToolButtonTextOnly);
    target->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    target->setToolTip(QStringLiteral("The value of the show it drives: a number, a switch, a choice or an action"));
    auto *targetMenu = new QMenu(target);
    target->setMenu(targetMenu);
    connect(targetMenu, &QMenu::aboutToShow, this, [this, targetMenu, editRoot] {
        targetMenu->clear();
        auto fill = [&](QMenu *into, quint64 layer) {
            std::vector<ParamInfo> params;
            const std::vector<ParamInfo> available = m_engine->parameters(layer);
            QSet<QString> writable;
            for (const ParamInfo &p : available) {
                if (!(p.access & ParamInfo::Write) || p.type == ParamInfo::Type::Text) continue;
                writable.insert(p.path);
                params.push_back(p);
            }
            // Add one selectable composite target above its individually
            // addressable channels (e.g. param/color as well as /r, /g, /b).
            for (const ParamInfo &p : available) {
                if (!p.path.endsWith(QStringLiteral("/r"))) continue;
                const QString base = p.path.chopped(2);
                if (!writable.contains(base + QStringLiteral("/r")) ||
                    !writable.contains(base + QStringLiteral("/g")) ||
                    !writable.contains(base + QStringLiteral("/b")) ||
                    writable.contains(base)) continue;
                ParamInfo color = p;
                color.path = base;
                if (color.label.endsWith(QStringLiteral(" R"))) color.label.chop(2);
                color.animatable = false;
                params.push_back(color);
            }
            const auto points = m_engine->animatablePoints(layer);
            params.insert(params.end(), points.begin(), points.end());
            addParamMenu(into, params, [this, editRoot, layer](QMenu *m, const ParamInfo &p, const QString &text) {
                m->addAction(text, this, [this, editRoot, layer, p] {
                    editRoot(QStringLiteral("Change Interaction Target"), [this, layer, p](Interaction &x) {
                        m_engine->configureInteractionTarget(x, layer, p.path);
                    });
                    m_xyAxis = 0;
                    rebuildSettings();
                    rebuildList();
                });
            });
        };
        fillByOwner(targetMenu, fill);
    });
    form->addRow(QStringLiteral("Target"), target);
    attachPreset({target}, {QStringLiteral("target")});

    // Whole-color targets expose an explicit alpha opt-in. By default a
    // Chataigne RGBA packet changes RGB but keeps the layer's existing alpha.
    // Individual /a scalar targets remain freely mappable as before.
    ParamInfo colorPart;
    const bool wholeColor = m_engine->parameterInfo(it.layer, it.param + QStringLiteral("/r"), &colorPart) &&
                            m_engine->parameterInfo(it.layer, it.param + QStringLiteral("/g"), &colorPart) &&
                            m_engine->parameterInfo(it.layer, it.param + QStringLiteral("/b"), &colorPart) &&
                            m_engine->parameterInfo(it.layer, it.param + QStringLiteral("/a"), &colorPart);
    if (wholeColor) {
        auto *mapAlpha = new FlagBox(QStringLiteral("Map alpha (RGBA)"));
        mapAlpha->setChecked(it.colorMapAlpha);
        mapAlpha->setToolTip(QStringLiteral("Off: preserve the target's opacity (recommended). "
                                           "On: use the fourth incoming OSC component as alpha, including zero."));
        form->addRow(QString(), mapAlpha);
        attachPreset({mapAlpha}, {QStringLiteral("color/map_alpha")});
        connect(mapAlpha, &QCheckBox::toggled, this, [edit](bool on) {
            edit(QStringLiteral("Change Color Alpha Mapping"), [on](Interaction &x) { x.colorMapAlpha = on; });
        });
    }

    // What it does now
    m_inMeter = meter();
    m_outMeter = meter();
    m_inMeter->setToolTip(QStringLiteral("The source, within its usual range"));
    m_outMeter->setToolTip(QStringLiteral("The result, after the filters and the master level, within the target's range"));
    auto *meters = new QVBoxLayout;
    meters->setSpacing(3);
    meters->addWidget(m_inMeter);
    meters->addWidget(m_outMeter);
    form->addRow(QStringLiteral("In / Out"), meters);

    // The chain
    auto *master = new SliderField;
    master->setRange(0, 100);
    master->setDecimals(0);
    master->setSuffix(QStringLiteral(" %"));
    master->setSingleStep(1);
    master->setTicks(10);
    master->setSnaps({0, 100});
    master->setValue(it.master * 100);
    auto *masterLabel = new ResetLabel(QStringLiteral("Master Level"), [edit, master] {
        master->setValue(100);
        edit(QStringLiteral("Reset Master Level"), [](Interaction &x) { x.master = 1; });
    });
    form->addRow(masterLabel, master);
    attachPreset({masterLabel, master}, {QStringLiteral("master")});
    connect(master, &SliderField::valueEdited, this, [edit](double v) {
        edit(QStringLiteral("Change Master Level"), [v](Interaction &x) { x.master = v / 100.0; }, QStringLiteral("master"));
    });

    auto *takeover = new QComboBox;
    takeover->addItem(QStringLiteral("Pick-up"), int(Takeover::PickUp));
    takeover->addItem(QStringLiteral("Jump"), int(Takeover::Jump));
    takeover->setCurrentIndex(it.takeover == Takeover::PickUp ? 0 : 1);
    takeover->setToolTip(QStringLiteral("Pick-up: once the target was moved by something else (a snapshot, the interface), "
                                        "it waits for the controller to meet it · Jump: it sets it at once"));
    auto *takeoverLabel = new QLabel(QStringLiteral("Takeover"));
    form->addRow(takeoverLabel, takeover);
    attachPreset({takeoverLabel, takeover}, {QStringLiteral("takeover")});
    connect(takeover, qOverload<int>(&QComboBox::activated), this, [edit, takeover](int) {
        const Takeover t = Takeover(takeover->currentData().toInt());
        edit(QStringLiteral("Change Takeover"), [t](Interaction &x) { x.takeover = t; });
    });

    // Filters, in a frame, as a layer's effects: + adds one (of the kind chosen), − removes the one selected, ▲ ▼ move
    // it; each one on or off, its settings under its name, any value. A click on a filter selects it
    const int count = int(it.filters.size());
    m_selectedFilter = count ? std::clamp(m_selectedFilter, 0, count - 1) : -1;
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("filtersFrame"));
    frame->setStyleSheet(QStringLiteral("QFrame#filtersFrame { border:1px solid #3c3c43; border-radius:4px; }"));
    auto *fv = new QVBoxLayout(frame);
    fv->setContentsMargins(6, 4, 6, 6);
    fv->setSpacing(4);
    auto *bar = new QHBoxLayout;
    auto *filtersLabel = new QLabel(QStringLiteral("<b>Filters</b>"));
    filtersLabel->setToolTip(QStringLiteral("Applied one after the other, from the top, between the source range and the master level"));
    auto tool = [](const QString &text, const QString &tip, const char *name) {
        auto *b = new QToolButton;
        b->setObjectName(QString::fromLatin1(name));
        b->setText(text);
        b->setToolTip(tip);
        b->setMinimumWidth(26);
        return b;
    };
    auto *addFilter = tool(QStringLiteral("+"), QStringLiteral("Add a filter (at the end)"), "addFilter");
    auto *removeFilter = tool(QStringLiteral("−"), QStringLiteral("Remove the selected filter"), "removeFilter");
    auto *upFilter = tool(QStringLiteral("▲"), QStringLiteral("Move the selected filter up (applied earlier)"), "filterUp");
    auto *downFilter = tool(QStringLiteral("▼"), QStringLiteral("Move the selected filter down (applied later)"), "filterDown");
    removeFilter->setEnabled(m_selectedFilter >= 0);
    upFilter->setEnabled(m_selectedFilter > 0);
    downFilter->setEnabled(m_selectedFilter >= 0 && m_selectedFilter + 1 < count);
    addFilter->setPopupMode(QToolButton::InstantPopup);
    addFilter->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; width: 0; }")); // a plain +
    auto *addMenu = new QMenu(addFilter);
    addFilter->setMenu(addMenu);
    const InteractionFilter overTime[] = {InteractionFilter::Average, InteractionFilter::WeightedAverage, InteractionFilter::AttackRelease,
                                          InteractionFilter::Damper, InteractionFilter::PulseDetection, InteractionFilter::Delay,
                                          InteractionFilter::Speed, InteractionFilter::Envelope};
    const InteractionFilter onValue[] = {InteractionFilter::Remap, InteractionFilter::Curve, InteractionFilter::Math, InteractionFilter::Clamp,
                                         InteractionFilter::Steps, InteractionFilter::Threshold, InteractionFilter::Toggle};
    auto offer = [this, edit, addMenu, count, srcLo, srcHi, tgtLo, tgtHi](InteractionFilter kind) {
        addMenu->addAction(interactionFilterName(kind), this, [this, edit, kind, count, srcLo, srcHi, tgtLo, tgtHi] {
            m_revealCurve = kind == InteractionFilter::Curve;
            m_selectedFilter = count; // the new one
            // A Remap: from what the source gives to what the target takes, as they usually go
            const InteractionStage made = kind == InteractionFilter::Remap ? InteractionStage::remap(srcLo, srcHi, tgtLo, tgtHi)
                                                                           : InteractionStage::made(kind);
            edit(QStringLiteral("Add Filter"), [made](Interaction &x) { x.filters.push_back(made); });
            rebuildSettings();
        });
    };
    addMenu->addSection(QStringLiteral("On the value"));
    for (InteractionFilter k : onValue) offer(k);
    addMenu->addSection(QStringLiteral("Over time"));
    for (InteractionFilter k : overTime) offer(k);
    bar->addWidget(filtersLabel);
    bar->addStretch();
    for (QToolButton *b : {addFilter, removeFilter, upFilter, downFilter}) bar->addWidget(b);
    fv->addLayout(bar);
    attachPreset({filtersLabel}, {QStringLiteral("filters")});
    if (it.filters.empty()) fv->addWidget(note(QStringLiteral("None: the value goes straight to the master level.")));
    connect(removeFilter, &QToolButton::clicked, this, [this, edit] {
        const int k = m_selectedFilter;
        edit(QStringLiteral("Remove Filter"), [k](Interaction &x) {
            if (k >= 0 && k < int(x.filters.size())) x.filters.erase(x.filters.begin() + k);
        });
        rebuildSettings();
    });
    auto move = [this, edit](int by) {
        const int k = m_selectedFilter, to = k + by;
        edit(QStringLiteral("Move Filter"), [k, to](Interaction &x) {
            if (k >= 0 && to >= 0 && k < int(x.filters.size()) && to < int(x.filters.size())) std::swap(x.filters[size_t(k)], x.filters[size_t(to)]);
        });
        m_selectedFilter = to; // it stays selected
        rebuildSettings();
    };
    connect(upFilter, &QToolButton::clicked, this, [move] { move(-1); });
    connect(downFilter, &QToolButton::clicked, this, [move] { move(1); });

    auto editStage = [edit](size_t k, const QString &text, const std::function<void(InteractionStage &)> &fn, const QString &merge = QString()) {
        edit(text, [k, fn](Interaction &x) {
            if (k < x.filters.size()) fn(x.filters[k]);
        }, merge);
    };
    // The range each filter's meter shows: as the value goes there — the source's at first, a Remap's Out after it, 0..1
    // after what gives 0..1 (a curve, steps, a threshold, a toggle, a pulse, a speed), a Clamp's; otherwise as before
    std::pair<double, double> span{srcLo, srcHi};
    for (size_t k = 0; k < it.filters.size(); ++k) {
        const InteractionStage &f = it.filters[k];
        if (f.enabled) switch (f.kind) {
        case InteractionFilter::Remap: span = {std::min(f.c, f.d), std::max(f.c, f.d)}; break;
        case InteractionFilter::Clamp: span = {std::min(f.a, f.b), std::max(f.a, f.b)}; break;
        case InteractionFilter::Curve:
        case InteractionFilter::Steps:
        case InteractionFilter::Threshold:
        case InteractionFilter::Toggle:
        case InteractionFilter::PulseDetection:
        case InteractionFilter::Envelope:
        case InteractionFilter::Speed: span = {0.0, 1.0}; break;
        default: break;
        }
        const QString base = QStringLiteral("filters/%1/").arg(k + 1);
        const bool selected = int(k) == m_selectedFilter;
        auto *block = new ClickFrame;
        block->setObjectName(QStringLiteral("filterBlock%1").arg(k));
        block->setStyleSheet(selected ? QStringLiteral("QFrame#filterBlock%1 { background:#2e2e35; border-left:2px solid %2; }").arg(k).arg(theme::css())
                                      : QStringLiteral("QFrame#filterBlock%1 { border-left:2px solid transparent; }").arg(k));
        block->pressed = [this, k] {
            if (m_selectedFilter == int(k)) return;
            m_selectedFilter = int(k);
            QTimer::singleShot(0, this, [this] { rebuildSettings(); }); // after the click
        };
        auto *bf = new QFormLayout(block);
        bf->setContentsMargins(6, 3, 2, 3);
        bf->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        bf->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        auto *on = new FlagBox(QStringLiteral("%1 · %2").arg(k + 1).arg(interactionFilterName(f.kind)));
        on->setObjectName(QStringLiteral("filterOn%1").arg(k));
        on->setChecked(f.enabled);
        on->setToolTip(QStringLiteral("On / off (off: the value goes through unchanged)"));
        bf->addRow(on);
        attachPreset({on}, {base + QStringLiteral("enable")});
        {
            // What comes out of it now: a meter in its range, and the number
            auto *bar = meter();
            bar->setObjectName(QStringLiteral("filterMeter%1").arg(k));
            bar->setToolTip(QStringLiteral("What comes out of this filter (the bar: from %1 to %2)").arg(span.first).arg(span.second));
            auto *number = new QLabel(QStringLiteral("—"));
            number->setObjectName(QStringLiteral("filterValue%1").arg(k));
            number->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            number->setMinimumWidth(64);
            number->setStyleSheet(QStringLiteral("color:#999; font-size:11px;"));
            auto *row = new QHBoxLayout;
            row->addWidget(bar, 1);
            row->addWidget(number);
            bf->addRow(row);
            m_stageMeters.push_back({bar, number, span});
        }
        connect(on, &QCheckBox::toggled, this, [editStage, k](bool v) {
            editStage(k, QStringLiteral("Change Filter"), [v](InteractionStage &s) { s.enabled = v; });
        });

        if (f.kind == InteractionFilter::Math) {
            auto *op = new QComboBox;
            op->setObjectName(QStringLiteral("filterOp%1").arg(k));
            for (int o = 0; o < kMathOpCount; ++o) op->addItem(mathOpName(MathOp(o)), o);
            op->setCurrentIndex(int(f.op));
            op->setToolTip(QStringLiteral("x: the value coming in (0..1 after the source range, any value after another filter); a: the value below"));
            bf->addRow(QStringLiteral("Operation"), op);
            attachPreset({op}, {QStringLiteral("filters")});
            connect(op, qOverload<int>(&QComboBox::activated), this, [this, editStage, k](int o) {
                editStage(k, QStringLiteral("Change Filter"), [o](InteractionStage &s) { s.op = MathOp(o); });
                rebuildSettings();
            });
        }
        for (int which = 0; which < kFilterSettingCount; ++which) {
            const FilterSetting fs = interactionFilterSetting(f.kind, which);
            if (fs.name.isEmpty() || (f.kind == InteractionFilter::Math && !mathOpUsesValue(f.op))) continue;
            // (a Remap's In slides over the source's range, its Out over the target's)
            const double span = f.kind == InteractionFilter::Remap ? (which < 2 ? srcHi - srcLo : tgtHi - tgtLo) : fs.max - fs.min;
            auto *field = chainNumber(f.setting(which), span, f.kind == InteractionFilter::Steps ? 0 : 3);
            field->setObjectName(QStringLiteral("filter%1%2").arg(k).arg(QChar('a' + which)));
            field->setSuffix(fs.suffix);
            field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            // Reset: its default (a Remap: the source's or the target's end)
            const double def = f.kind != InteractionFilter::Remap ? fs.def : which == 0 ? srcLo : which == 1 ? srcHi : which == 2 ? tgtLo : tgtHi;
            auto *label = new ResetLabel(fs.name, [editStage, field, k, which, def] {
                const QSignalBlocker quiet(field);
                field->setValue(def);
                editStage(k, QStringLiteral("Reset Filter"), [which, def](InteractionStage &s) { s.setting(which) = def; });
            });
            bf->addRow(label, field);
            attachPreset({label, field}, {base + QChar('a' + which)});
            connect(field, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [editStage, k, which](double v) {
                editStage(k, QStringLiteral("Change Filter"), [which, v](InteractionStage &s) { s.setting(which) = v; },
                          QStringLiteral("filter%1/%2").arg(k).arg(which));
            });
        }
        if (f.kind == InteractionFilter::Curve) {
            // The animations' curve editor, its whole square shown (0..1 in, 0..1 out, a margin around); the first
            // curve's playhead where its input is now
            auto *straight = new QPushButton(QStringLiteral("Straight"));
            straight->setToolTip(QStringLiteral("Back to a straight line"));
            bf->addRow(QString(), straight);
            connect(straight, &QPushButton::clicked, this, [this, editStage, k] {
                editStage(k, QStringLiteral("Reset Curve"), [](InteractionStage &s) { s.curve = Interaction::straightCurve(); });
                rebuildSettings();
            });
            auto *lane = new CurveLane;
            lane->setObjectName(QStringLiteral("filterCurve%1").arg(k));
            lane->setTrack(f.curve, 1.0, 0.0, 1.0);
            lane->setView(-0.03, 1.03, -0.08, 1.08);
            lane->setFixedHeight(200);
            lane->setToolTip(QStringLiteral("Across: what comes in · up: what goes out. Click to add a key, drag it, "
                                            "double-click to remove it; right-click: its curve (easing, Bézier, hold)"));
            bf->addRow(lane);
            if (!m_lane) m_lane = lane;
            // Its right-click is its keys' own (their easing): only Edit mode (a click on its title) puts it in a preset
            if (PresetEditor *pe = PresetEditor::instance()) pe->track(lane, 0, {QStringLiteral("interaction/%1/filters").arg(it.id)});
            if (m_revealCurve && k + 1 == it.filters.size()) { // just added: shown
                m_revealCurve = false;
                QTimer::singleShot(60, this, [this] { // once laid out
                    if (!m_scroll) return;
                    m_settings->adjustSize();
                    m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
                });
            }
            connect(lane, &CurveLane::edited, this, [editStage, k](const AnimTrack &t, int gesture) {
                editStage(k, QStringLiteral("Edit Curve"), [t](InteractionStage &s) {
                    s.curve.keys = t.keys;
                    for (AnimKey &key : s.curve.keys) key.t = std::clamp(key.t, 0.0, 1.0);
                }, gesture ? QStringLiteral("curve%1/%2").arg(k).arg(gesture) : QString());
            });
        }
        fv->addWidget(block);
    }
    form->addRow(frame);
    m_settingsLayout->addStretch();
}

// ---------------------------------------------------------------------------
// Action mode: the conditions on the source's value, then what is done when they become true, and false
// ---------------------------------------------------------------------------
void InteractionPanel::buildActionSettings(QFormLayout *form, const Interaction &it, const EditFn &edit)
{
    m_inMeter = meter();
    m_inMeter->setToolTip(QStringLiteral("The source, within its source range"));
    form->addRow(QStringLiteral("Input"), m_inMeter);
    m_actionState = new QLabel(QStringLiteral("— (no value yet)"));
    m_actionState->setObjectName(QStringLiteral("actionState"));
    m_actionState->setToolTip(QStringLiteral("Whether the conditions hold for the source's last value"));
    form->addRow(QStringLiteral("Now"), m_actionState);

    auto *all = new QComboBox;
    all->setObjectName(QStringLiteral("actionMatch"));
    all->addItems({QStringLiteral("All conditions are true (and)"), QStringLiteral("Any condition is true (or)")});
    all->setCurrentIndex(it.conditionsAll ? 0 : 1);
    form->addRow(QStringLiteral("True when"), all);
    connect(all, qOverload<int>(&QComboBox::activated), this, [edit](int k) {
        edit(QStringLiteral("Change Action Conditions"), [k](Interaction &x) { x.conditionsAll = k == 0; }, QString());
    });

    // Each condition: the source's value, an operator, one value (two for a range), removed by −
    auto number = [](double v) { return chainNumber(v, 20, 3); }; // any value; sliding: 0.1 a pixel
    for (size_t i = 0; i < it.conditions.size(); ++i) {
        const InteractionCondition c = it.conditions[i];
        auto *row = new QWidget;
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto *op = new QComboBox;
        op->setObjectName(QStringLiteral("conditionOp%1").arg(i));
        for (int k = 0; k < kConditionOpCount; ++k) op->addItem(conditionOpName(ConditionOp(k)));
        op->setCurrentIndex(int(c.op));
        auto *a = number(c.a), *b = number(c.b);
        a->setObjectName(QStringLiteral("conditionA%1").arg(i));
        b->setObjectName(QStringLiteral("conditionB%1").arg(i));
        const bool range = c.op == ConditionOp::InRange || c.op == ConditionOp::OutOfRange;
        b->setVisible(range);
        auto *remove = new QToolButton;
        remove->setText(QStringLiteral("−"));
        remove->setToolTip(QStringLiteral("Remove this condition"));
        h->addWidget(op);
        h->addWidget(a, 1);
        h->addWidget(b, 1);
        h->addWidget(remove);
        form->addRow(i == 0 ? QStringLiteral("If value") : it.conditionsAll ? QStringLiteral("and") : QStringLiteral("or"), row);
        connect(op, qOverload<int>(&QComboBox::activated), this, [this, edit, i](int k) {
            edit(QStringLiteral("Change Action Condition"), [i, k](Interaction &x) {
                if (i < x.conditions.size()) x.conditions[i].op = ConditionOp(k);
            }, QString());
            rebuildSettings();
        });
        connect(a, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [edit, i](double v) {
            edit(QStringLiteral("Change Action Condition"), [i, v](Interaction &x) {
                if (i < x.conditions.size()) x.conditions[i].a = v;
            }, QStringLiteral("condition%1/a").arg(i));
        });
        connect(b, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [edit, i](double v) {
            edit(QStringLiteral("Change Action Condition"), [i, v](Interaction &x) {
                if (i < x.conditions.size()) x.conditions[i].b = v;
            }, QStringLiteral("condition%1/b").arg(i));
        });
        connect(remove, &QToolButton::clicked, this, [this, edit, i] {
            edit(QStringLiteral("Remove Action Condition"), [i](Interaction &x) {
                if (i < x.conditions.size()) x.conditions.erase(x.conditions.begin() + long(i));
            }, QString());
            rebuildSettings();
        });
    }
    auto *addCondition = new QPushButton(QStringLiteral("+ Condition"));
    addCondition->setObjectName(QStringLiteral("addCondition"));
    form->addRow(QString(), addCondition);
    connect(addCondition, &QPushButton::clicked, this, [this, edit] {
        edit(QStringLiteral("Add Action Condition"), [](Interaction &x) { x.conditions.push_back(InteractionCondition{}); }, QString());
        rebuildSettings();
    });
    if (it.conditions.empty()) form->addRow(QString(), note(QStringLiteral("No condition: true at each value the source gives.")));

    // What it does: each action a value of the show (a layer's, the composition's) and what it is set to
    auto actions = [&](bool whenTrue) {
        const std::vector<InteractionConsequence> &list = whenTrue ? it.onTrue : it.onFalse;
        const QString title = whenTrue ? QStringLiteral("If true") : QStringLiteral("If false");
        auto listOf = [whenTrue](Interaction &x) -> std::vector<InteractionConsequence> & { return whenTrue ? x.onTrue : x.onFalse; };
        auto *header = new QLabel(QStringLiteral("<b>%1</b>").arg(title));
        form->addRow(header);
        for (size_t i = 0; i < list.size(); ++i) {
            const InteractionConsequence c = list[i];
            auto *row = new QWidget;
            auto *h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            auto *target = new QToolButton;
            target->setObjectName(QStringLiteral("%1Target%2").arg(whenTrue ? "true" : "false").arg(i));
            target->setText(targetLabel(c.layer, c.param));
            target->setPopupMode(QToolButton::InstantPopup);
            target->setToolButtonStyle(Qt::ToolButtonTextOnly);
            target->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            target->setToolTip(QStringLiteral("The value of the show it sets: an action (fired), a switch, a choice, a number"));
            auto *menu = new QMenu(target);
            target->setMenu(menu);
            connect(menu, &QMenu::aboutToShow, this, [this, menu, edit, listOf, i] {
                menu->clear();
                auto fill = [&](QMenu *into, quint64 layer) {
                    std::vector<ParamInfo> params;
                    for (const ParamInfo &p : m_engine->parameters(layer))
                        if ((p.access & ParamInfo::Write) && p.type != ParamInfo::Type::Text) params.push_back(p);
                    addParamMenu(into, params, [this, edit, listOf, i, layer](QMenu *m, const ParamInfo &p, const QString &text) {
                        m->addAction(text, this, [this, edit, listOf, i, layer, p] {
                            // What it is set to at first: on, the first choice, the number as it is now
                            double v = 1;
                            if (p.type == ParamInfo::Type::Choice) v = 0;
                            else if (p.isNumber()) v = m_engine->parameterValue(layer, p.path).toDouble(p.defaultValue.toDouble());
                            edit(QStringLiteral("Change Action Target"), [listOf, i, layer, p, v](Interaction &x) {
                                auto &l = listOf(x);
                                if (i >= l.size()) return;
                                l[i].layer = layer; // its time and curve kept
                                l[i].param = p.path;
                                l[i].value = v;
                            }, QString());
                            rebuildSettings();
                        });
                    });
                };
                fillByOwner(menu, fill);
            });
            h->addWidget(target, 2);
            // What it is set to, by the kind of value
            auto setValue = [edit, listOf, i](double v, const QString &merge) {
                edit(QStringLiteral("Change Action Value"), [listOf, i, v](Interaction &x) {
                    auto &l = listOf(x);
                    if (i < l.size()) l[i].value = v;
                }, merge);
            };
            ParamInfo info;
            const bool known = !c.param.isEmpty() && m_engine->parameterInfo(c.layer, c.param, &info);
            if (known && (info.type == ParamInfo::Type::Bool || info.type == ParamInfo::Type::Choice)) {
                auto *value = new QComboBox;
                value->setObjectName(QStringLiteral("%1Value%2").arg(whenTrue ? "true" : "false").arg(i));
                if (info.type == ParamInfo::Type::Bool) value->addItems({QStringLiteral("Off"), QStringLiteral("On")});
                else value->addItems(info.choices);
                value->setCurrentIndex(std::clamp(int(std::lround(c.value)), 0, std::max(0, value->count() - 1)));
                h->addWidget(value, 1);
                connect(value, qOverload<int>(&QComboBox::activated), this, [setValue](int k) { setValue(k, QString()); });
            } else if (known && info.isNumber()) {
                auto *value = number(c.value);
                value->setObjectName(QStringLiteral("%1Value%2").arg(whenTrue ? "true" : "false").arg(i));
                value->setRange(info.lo, info.hi);
                value->setValue(c.value);
                h->addWidget(value, 1);
                connect(value, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [setValue, whenTrue, i](double v) {
                    setValue(v, QStringLiteral("%1value%2").arg(whenTrue ? "true" : "false").arg(i));
                });
            } else if (known) {
                h->addWidget(new QLabel(QStringLiteral("fires")));
            }
            auto *remove = new QToolButton;
            remove->setText(QStringLiteral("−"));
            remove->setToolTip(QStringLiteral("Remove this action"));
            h->addWidget(remove);
            form->addRow(QString(), row);
            // A number that goes through its values: in how long, and how (eased)
            if (known && info.isNumber() && info.ramp != ParamInfo::Ramp::Cut) {
                const QString side = whenTrue ? QStringLiteral("true") : QStringLiteral("false");
                auto *timing = new QWidget;
                auto *t = new QHBoxLayout(timing);
                t->setContentsMargins(0, 0, 0, 0);
                auto *time = chainNumber(c.time, 20, 3);
                time->setObjectName(QStringLiteral("%1Time%2").arg(side).arg(i));
                time->setRange(0, 600);
                time->setSuffix(QStringLiteral(" s"));
                time->setValue(c.time);
                time->setToolTip(QStringLiteral("How long it takes to get there, from where it is (0: at once)"));
                auto *curve = new QComboBox;
                curve->setObjectName(QStringLiteral("%1Curve%2").arg(side).arg(i));
                curve->addItems(Engine::easingNames());
                curve->setCurrentIndex(std::clamp(c.curve, 0, int(curve->count()) - 1));
                curve->setEnabled(c.time > 0);
                t->addWidget(new QLabel(QStringLiteral("over")));
                t->addWidget(time, 1);
                t->addWidget(curve, 1);
                form->addRow(QString(), timing);
                connect(time, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [edit, listOf, i, curve, side](double v) {
                    curve->setEnabled(v > 0);
                    edit(QStringLiteral("Change Action Time"), [listOf, i, v](Interaction &x) {
                        auto &l = listOf(x);
                        if (i < l.size()) l[i].time = v;
                    }, QStringLiteral("%1time%2").arg(side).arg(i));
                });
                connect(curve, qOverload<int>(&QComboBox::activated), this, [edit, listOf, i](int k) {
                    edit(QStringLiteral("Change Action Curve"), [listOf, i, k](Interaction &x) {
                        auto &l = listOf(x);
                        if (i < l.size()) l[i].curve = k;
                    }, QString());
                });
            }
            connect(remove, &QToolButton::clicked, this, [this, edit, listOf, i] {
                edit(QStringLiteral("Remove Action"), [listOf, i](Interaction &x) {
                    auto &l = listOf(x);
                    if (i < l.size()) l.erase(l.begin() + long(i));
                }, QString());
                rebuildSettings();
                rebuildList();
            });
        }
        auto *add = new QPushButton(QStringLiteral("+ Action %1").arg(whenTrue ? QStringLiteral("if true") : QStringLiteral("if false")));
        add->setObjectName(whenTrue ? QStringLiteral("addTrueAction") : QStringLiteral("addFalseAction"));
        form->addRow(QString(), add);
        connect(add, &QPushButton::clicked, this, [this, edit, listOf] {
            edit(QStringLiteral("Add Action"), [listOf](Interaction &x) { listOf(x).push_back(InteractionConsequence{}); }, QString());
            rebuildSettings();
            rebuildList();
        });
    };
    actions(true);
    actions(false);
}
