#include "MediaBin.h"
#include "AudioResources.h"
#include "Engine.h"
#include "ImageFile.h"
#include "LiveInput.h"
#include "Psd.h"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QSet>
#include <QTimer>
#include <functional>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QColorSpace>
#include "ColorInfo.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QToolButton>
#include <QImageReader>
#include <QLabel>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QThreadPool>
#include <QLineEdit>
#include <functional>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>

namespace {
// UsedRole: not removable (used, or always there); UsersRole: how many layers use it
enum Role { PathRole = Qt::UserRole + 1, MissingRole, UsedRole, IsfRole, CategoryRole, UsersRole, GroupOrderRole };

class BinItem : public QTreeWidgetItem
{
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &other) const override
    {
        const auto *tree = treeWidget();
        const int column = tree ? tree->sortColumn() : 0;
        const QVariant group = data(0, GroupOrderRole), otherGroup = other.data(0, GroupOrderRole);
        if (group.isValid() && otherGroup.isValid()) {
            // Sorting media must not shuffle their top-level type groups.
            return tree && tree->header()->sortIndicatorOrder() == Qt::DescendingOrder
                       ? group.toInt() > otherGroup.toInt() : group.toInt() < otherGroup.toInt();
        }
        if (column == 2) {
            const int count = data(0, UsersRole).toInt(), otherCount = other.data(0, UsersRole).toInt();
            if (count != otherCount) return count < otherCount;
        } else {
            const int compared = QString::localeAwareCompare(text(column), other.text(column));
            if (compared) return compared < 0;
        }
        const int name = QString::localeAwareCompare(text(0), other.text(0));
        return name ? name < 0 : data(0, PathRole).toString() < other.data(0, PathRole).toString();
    }
};

// Updating a sort key can move an item immediately. Batch updates keep child
// iteration safe and sort once after usage/metadata/discovery changes are complete.
class SortingPause
{
public:
    explicit SortingPause(QTreeWidget *tree) : m_tree(tree), m_enabled(tree->isSortingEnabled())
    { if (m_enabled) m_tree->setSortingEnabled(false); }
    ~SortingPause() { if (m_enabled) m_tree->setSortingEnabled(true); }
private:
    QTreeWidget *m_tree;
    bool m_enabled;
};

// Replace… works on files: media, and ISF generators (not Text, Timecode or the live inputs)
bool replaceable(const QTreeWidgetItem *it)
{
    const QString p = it->data(0, PathRole).toString();
    return !p.isEmpty() && !Engine::isStreamUrl(p) && (!it->data(0, IsfRole).toBool() || Engine::isIsfFile(p));
}

// Media Bin tree: drag items onto layers, drop files to import them.
class BinTree : public QTreeWidget
{
public:
    std::function<void(const QStringList &)> onDrop;

protected:
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override
    {
        auto *m = new QMimeData;
        QList<QUrl> urls;
        for (QTreeWidgetItem *it : items) {
            const QString p = it->data(0, PathRole).toString();
            // A stream as a (virtual) path, as the live inputs are: dropped onto a layer like a file
            if (!p.isEmpty() && !it->data(0, MissingRole).toBool())
                urls << QUrl::fromLocalFile(Engine::isStreamUrl(p) ? Engine::streamPath(p) : p);
        }
        m->setUrls(urls);
        return m;
    }
    QStringList mimeTypes() const override { return {QStringLiteral("text/uri-list")}; }
    Qt::DropActions supportedDropActions() const override { return Qt::CopyAction; }
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->source() == this || !e->mimeData()->hasUrls()) {
            e->ignore();
            return;
        }
        e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *e) override
    {
        if (e->source() == this) e->ignore();
        else e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override
    {
        QStringList paths;
        for (const QUrl &u : e->mimeData()->urls())
            if (u.isLocalFile()) paths << u.toLocalFile();
        if (onDrop) onDrop(paths);
        e->acceptProposedAction();
    }
};

QString fmtDuration(double s)
{
    const int t = int(s + 0.5);
    return t >= 3600 ? QStringLiteral("%1:%2:%3").arg(t / 3600).arg((t / 60) % 60, 2, 10, QChar('0')).arg(t % 60, 2, 10, QChar('0'))
                     : QStringLiteral("%1:%2").arg(t / 60, 2, 10, QChar('0')).arg(t % 60, 2, 10, QChar('0'));
}
} // namespace

MediaBin::MediaBin(Engine *engine, QWidget *parent) : QWidget(parent), m_engine(engine)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(8, 8, 4, 4);
    v->setSpacing(6);
    auto *head = new QHBoxLayout;
    head->addWidget(new QLabel(QStringLiteral("<b>Media Bin</b>")));
    head->addStretch();
    m_rebuild = new QToolButton;
    m_rebuild->setObjectName(QStringLiteral("rebuild"));
    m_rebuild->setText(QStringLiteral("⟳"));
    m_rebuild->setToolTip(QStringLiteral("Rebuild: read the files again (missing, changed), look for the live inputs and "
                                         "scan the ISF library folders again"));
    head->addWidget(m_rebuild);
    auto *import = new QPushButton(QStringLiteral("Import…"));
    import->setObjectName(QStringLiteral("mediaBinImport"));
    import->setToolTip(QStringLiteral("Add videos, images, sounds or ISF generators, an image sequence (a folder of "
                                      "numbered pictures) or a network stream to the Media Bin"));
    auto *importMenu = new QMenu(import);
    importMenu->addAction(QStringLiteral("Files…"), this, &MediaBin::importDialog);
    importMenu->addAction(QStringLiteral("Image Sequence (Folder)…"), this, &MediaBin::importSequenceDialog);
    importMenu->addAction(QStringLiteral("Add Stream URL…"), this, &MediaBin::addStreamDialog);
    import->setMenu(importMenu);
    head->addWidget(import);
    v->addLayout(head);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("Search…"));
    m_search->setClearButtonEnabled(true);
    m_search->setToolTip(QStringLiteral("Shows only the media and generators whose name contains this text"));
    auto *filters = new QHBoxLayout;
    filters->setSpacing(4);
    filters->addWidget(m_search, 1);
    m_type = new QComboBox;
    m_type->setObjectName(QStringLiteral("typeFilter"));
    m_type->addItems({QStringLiteral("Video + Audio"), QStringLiteral("Video"), QStringLiteral("Audio")});
    m_type->setToolTip(QStringLiteral("Shows the video, the audio, or both"));
    filters->addWidget(m_type);
    m_kind = new QComboBox;
    m_kind->setObjectName(QStringLiteral("kindFilter"));
    m_kind->addItems({QStringLiteral("All Kinds"), QStringLiteral("Files"), QStringLiteral("Generators"), QStringLiteral("Inputs"),
                      QStringLiteral("ISF")});
    m_kind->setToolTip(QStringLiteral("Files: videos, images and sounds; Generators; live Inputs; the ISF library"));
    filters->addWidget(m_kind);
    m_usage = new QComboBox;
    m_usage->setObjectName(QStringLiteral("usageFilter"));
    m_usage->addItems({QStringLiteral("All"), QStringLiteral("Used"), QStringLiteral("Unused")});
    m_usage->setToolTip(QStringLiteral("Used: only what the layers use; Unused: only what no layer uses"));
    filters->addWidget(m_usage);
    v->addLayout(filters);
    connect(m_search, &QLineEdit::textChanged, this, &MediaBin::applyFilter);
    connect(m_type, &QComboBox::currentIndexChanged, this, &MediaBin::applyFilter);
    connect(m_kind, &QComboBox::currentIndexChanged, this, &MediaBin::applyFilter);
    connect(m_usage, &QComboBox::currentIndexChanged, this, &MediaBin::applyFilter);

    auto *tree = new BinTree;
    m_tree = tree;
    m_tree->setObjectName(QStringLiteral("mediaBinTree"));
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({QStringLiteral("File"), QStringLiteral("Info"), QStringLiteral("Layers"), QStringLiteral("Color space")});
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDragDropMode(QAbstractItemView::DragDrop);
    m_tree->setDefaultDropAction(Qt::CopyAction);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setUniformRowHeights(true);
    m_tree->setIndentation(12); // ISF > Generators > category > shader: keep room for the names
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_tree->setColumnWidth(1, 120);
    m_tree->setColumnWidth(2, 56);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_tree->setColumnWidth(3, 170);
    auto *header = m_tree->header();
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    header->setToolTip(QStringLiteral("Click to sort; click again to reverse. Right-click to choose visible columns."));
    auto *columns = new QToolButton;
    columns->setObjectName(QStringLiteral("mediaBinColumns"));
    columns->setText(QStringLiteral("Columns"));
    columns->setToolTip(QStringLiteral("Choose visible columns; File is always shown"));
    columns->setPopupMode(QToolButton::InstantPopup);
    auto *columnMenu = new QMenu(columns);
    columns->setMenu(columnMenu);
    head->insertWidget(1, columns);
    const QStringList columnKeys{QStringLiteral("file"), QStringLiteral("info"), QStringLiteral("layers"), QStringLiteral("colorSpace")};
    QSettings preferences;
    for (int column = 0; column < m_tree->columnCount(); ++column) {
        const QString key = QStringLiteral("mediaBin/columns/") + columnKeys[column];
        const bool visible = column == 0 || preferences.value(key, true).toBool();
        m_tree->setColumnHidden(column, !visible);
        auto *action = columnMenu->addAction(m_tree->headerItem()->text(column));
        action->setCheckable(true);
        action->setChecked(visible);
        action->setEnabled(column != 0);
        if (column == 0) continue;
        connect(action, &QAction::toggled, this, [this, column, key](bool shown) {
            m_tree->setColumnHidden(column, !shown);
            QSettings().setValue(key, shown);
            if (!shown && m_tree->sortColumn() == column) m_tree->sortItems(0, Qt::AscendingOrder);
        });
    }
    connect(header, &QHeaderView::customContextMenuRequested, this, [header, columnMenu](const QPoint &pos) {
        columnMenu->exec(header->mapToGlobal(pos));
    });
    int sortColumn = preferences.value(QStringLiteral("mediaBin/sortColumn"), 0).toInt();
    auto sortOrder = preferences.value(QStringLiteral("mediaBin/sortOrder"), int(Qt::AscendingOrder)).toInt() == int(Qt::DescendingOrder)
                         ? Qt::DescendingOrder : Qt::AscendingOrder;
    if (sortColumn < 0 || sortColumn >= m_tree->columnCount() || m_tree->isColumnHidden(sortColumn)) {
        sortColumn = 0;
        sortOrder = Qt::AscendingOrder;
    }
    header->setSortIndicator(sortColumn, sortOrder);
    connect(header, &QHeaderView::sortIndicatorChanged, this, [](int column, Qt::SortOrder order) {
        QSettings settings;
        settings.setValue(QStringLiteral("mediaBin/sortColumn"), column);
        settings.setValue(QStringLiteral("mediaBin/sortOrder"), int(order));
    });
    m_tree->setTextElideMode(Qt::ElideMiddle);
    m_tree->setToolTip(QStringLiteral("Drag an item onto a layer (layer list, or the Source tab of the layer) to load it.\n"
                                      "ISF FX dragged onto a layer join its FX chain.\n"
                                      "Double-click: load into the selected layer."));
    tree->onDrop = [this](const QStringList &p) { importFiles(p); };
    m_videoRoot = new BinItem(m_tree, {QStringLiteral("Video")});
    m_audioRoot = new BinItem(m_tree, {QStringLiteral("Audio")});
    m_videos = new BinItem(m_videoRoot, {QStringLiteral("Videos")});
    m_images = new BinItem(m_videoRoot, {QStringLiteral("Images")});
    // The generators, built in, always there: Text, Timecode, Countdown, Stopwatch, Clock, Line
    m_generators = new BinItem(m_videoRoot, {QStringLiteral("Generators")});
    auto generator = [](QTreeWidgetItem *group, const QString &name, const QString &path) {
        auto *it = new BinItem(group);
        it->setData(0, PathRole, path);
        it->setData(0, IsfRole, true); // no file: not relinked
        it->setData(0, MissingRole, false);
        it->setData(0, UsedRole, true); // not removable
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        it->setText(0, name);
        it->setTextAlignment(2, Qt::AlignCenter);
        return it;
    };
    m_text = generator(m_generators, QStringLiteral("Text"), kTextGeneratorPath);
    m_timecode = generator(m_generators, QStringLiteral("Timecode"), kTimecodeGeneratorPath);
    for (ClockKind k : {ClockKind::Countdown, ClockKind::Stopwatch, ClockKind::Clock})
        m_clocks[int(k)] = generator(m_generators, clockKindName(k), clockGeneratorPath(k));
    m_line = generator(m_generators, QStringLiteral("Line"), kLineGeneratorPath);
    // Web Page: only when the application has a browser (Qt WebEngine)
    m_web = webPagesAvailable() ? generator(m_generators, QStringLiteral("Web Page"), kWebGeneratorPath) : nullptr;
    m_inputs = new BinItem(m_videoRoot, {QStringLiteral("Inputs")});
    m_isf = new BinItem(m_videoRoot, {QStringLiteral("ISF")});
    m_isfGenerators = new BinItem(m_isf, {QStringLiteral("Generators")});

    m_audios = new BinItem(m_audioRoot, {QStringLiteral("Sounds")});
    m_audioGenerators = new BinItem(m_audioRoot, {QStringLiteral("Generators")});
    const auto generatorFolder = AudioResources::generatorsFolder();
    const QDir audioGenerators(generatorFolder);
    if (!generatorFolder.isEmpty())
        for (const auto &[name, file] : {std::pair{QStringLiteral("FM"), QStringLiteral("FM")},
                                         std::pair{QStringLiteral("Granular"), QStringLiteral("Granular")},
                                         std::pair{QStringLiteral("White Noise"), QStringLiteral("White Noise")},
                                         std::pair{QStringLiteral("Pink Noise"), QStringLiteral("Pink Noise")},
                                         std::pair{QStringLiteral("Video Sonification"), QStringLiteral("Video")}}) {
            const QString path = audioGenerators.filePath(file + QStringLiteral(".nyxaudio"));
            if (QFileInfo::exists(path))
                generator(m_audioGenerators, name, path);
        }
    m_audioInputs = new BinItem(m_audioRoot, {QStringLiteral("Inputs")});

    int groupOrder = 0;
    const QList<QTreeWidgetItem *> groups{m_videoRoot, m_audioRoot, m_videos, m_images, m_generators, m_inputs, m_isf,
                                          m_isfGenerators, m_audios, m_audioGenerators, m_audioInputs};
    for (QTreeWidgetItem *group : groups) {
        group->setData(0, GroupOrderRole, groupOrder++);
        QFont f = group->font(0);
        f.setBold(true);
        group->setFont(0, f);
        group->setFlags(Qt::ItemIsEnabled);
        group->setExpanded(true);
        group->setFirstColumnSpanned(true);
    }
    v->addWidget(m_tree, 1);
    m_inputs->setToolTip(0, QStringLiteral("Cameras, screens and windows, DeckLink inputs, and the NDI, OMT, Syphon and Spout sources "
                                          "found on this machine and the network"));
    m_audioInputs->setToolTip(0, QStringLiteral("The sound inputs of this machine (microphones, sound card inputs): dragged onto a layer, "
                                                "a live sound with its volume, its Audio FX and its levels"));
    m_audioGenerators->setToolTip(0, QStringLiteral("Real-time instruments written in Faust (FM, granular, noise, the sound of the "
                                                    "picture), and your own .dsp programs"));
    auto *look = new QTimer(this); // the network's sources come and go
    look->setInterval(3000);
    connect(look, &QTimer::timeout, this, [this] {
        if (!isVisible()) return;
        refreshInputs();
        refreshAudioInputs();
    });
    look->start();

    auto *buttons = new QHBoxLayout;
    m_relink = new QPushButton(QStringLiteral("Replace…"));
    m_relink->setToolTip(QStringLiteral("Replace this file (media or ISF generator) with another everywhere it is used "
                                        "(moved file, new version)"));
    m_remove = new QPushButton(QStringLiteral("Remove"));
    m_remove->setToolTip(QStringLiteral("Remove from the Media Bin (only files not used by any layer)"));
#if defined(Q_OS_MACOS)
    m_reveal = new QPushButton(QStringLiteral("Finder"));
#elif defined(Q_OS_WIN)
    m_reveal = new QPushButton(QStringLiteral("Explorer"));
#else
    m_reveal = new QPushButton(QStringLiteral("Folder"));
#endif
    m_reveal->setToolTip(QStringLiteral("Show the file in its folder"));
    for (auto *b : {m_relink, m_remove, m_reveal}) buttons->addWidget(b);
    v->addLayout(buttons);
    m_summary = new QLabel;
    m_summary->setStyleSheet("color:#888; font-size:11px;");
    m_summary->setWordWrap(true);
    v->addWidget(m_summary);

    connect(m_rebuild, &QToolButton::clicked, this, &MediaBin::rebuild);
    connect(m_relink, &QPushButton::clicked, this, &MediaBin::relinkSelected);
    connect(m_remove, &QPushButton::clicked, this, &MediaBin::removeSelected);
    connect(m_reveal, &QPushButton::clicked, this, &MediaBin::revealSelected);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &MediaBin::updateButtons);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &MediaBin::contextMenu);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        const QString p = it->data(0, PathRole).toString();
        if (!p.isEmpty() && !it->data(0, MissingRole).toBool()) emit useAsSourceRequested(p);
    });
    refresh();
    m_tree->setSortingEnabled(true);
}

QString MediaBin::selectedPath() const
{
    const auto items = m_tree->selectedItems();
    return items.isEmpty() ? QString() : items.first()->data(0, PathRole).toString();
}

void MediaBin::refresh()
{
    const SortingPause sorting(m_tree);
    const auto usage = m_engine->mediaUsage();
    const QString selected = selectedPath();

    int nv = 0, ni = 0, na = 0, missing = 0;
    // Update in place: keep selection and scroll position.
    QHash<QString, QTreeWidgetItem *> existing;
    for (QTreeWidgetItem *cat : {m_videos, m_images, m_audios})
        for (int i = 0; i < cat->childCount(); ++i) existing.insert(cat->child(i)->data(0, PathRole).toString(), cat->child(i));
    QSet<QString> seen;
    for (const auto &r : usage) {
        if (r.audio && AudioGenerator::isRecipe(r.path)) continue; // an instrument: Audio > Generators (refreshAudioGenerators)
        QTreeWidgetItem *cat = r.video ? m_videos : r.audio ? m_audios : m_images;
        QTreeWidgetItem *it = existing.value(r.path);
        if (it && it->parent() != cat) {
            delete it;
            it = nullptr;
        }
        if (!it) {
            it = new BinItem(cat);
            it->setData(0, PathRole, r.path);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        }
        seen.insert(r.path);
        (r.video ? nv : r.audio ? na : ni)++;
        if (r.missing) ++missing;
        it->setData(0, MissingRole, r.missing);
        it->setData(0, UsedRole, !r.users.isEmpty());
        it->setData(0, UsersRole, int(r.users.size()));
        const bool stream = Engine::isStreamUrl(r.path);
        it->setText(0, stream ? Engine::streamName(r.path) : QFileInfo(r.path).fileName());
        if (stream) { // never probed: that would connect to it
            const bool rtsp = r.path.startsWith(QLatin1String("rtsp"), Qt::CaseInsensitive);
            it->setText(1, QStringLiteral("Stream · ") + r.path.section(QLatin1Char(':'), 0, 0).toUpper() +
                               (rtsp ? (m_engine->binStreamTcp(r.path) ? QStringLiteral(" (TCP)") : QStringLiteral(" (UDP)")) : QString()));
            it->setText(3, QStringLiteral("Unknown (live stream)"));
            it->setToolTip(1, QStringLiteral("Live network stream: connected when a layer plays it, again when it drops"));
        } else if (r.missing) {
            it->setText(1, QStringLiteral("missing"));
            it->setText(3, QStringLiteral("Unavailable"));
            m_info.remove(r.path);
        } else {
            if (!m_info.contains(r.path)) probe(r.path);
            const QString info = m_info.value(r.path, QStringLiteral("…"));
            it->setText(1, info.section('\t', 0, 0));
            it->setText(3, info.contains('\t') ? info.section('\t', 2, 2) : QStringLiteral("…"));
            it->setToolTip(3, info.section('\t', 1, 1));
            it->setToolTip(1, info.contains('\t') ? info.section('\t', 0, 0) + QStringLiteral(" · ") + info.section('\t', 1, 1) : info);
        }
        if (r.missing) it->setToolTip(1, QStringLiteral("File not found: ") + r.path);
        // Layer count; details are in the tooltip
        it->setText(2, r.users.isEmpty() ? QStringLiteral("—") : QString::number(r.users.size()));
        it->setTextAlignment(2, Qt::AlignCenter);
        const QString usedBy = r.users.isEmpty() ? QStringLiteral("Unused (imported to the Media Bin)")
                                                 : QStringLiteral("Used by:\n") + r.users.join('\n');
        it->setToolTip(0, r.path + QStringLiteral("\n\n") + usedBy);
        it->setToolTip(2, usedBy);
        const QColor fg = r.missing ? QColor(255, 110, 95) : (r.users.isEmpty() ? QColor(140, 140, 145) : QColor(225, 225, 228));
        for (int c = 0; c < 4; ++c) it->setForeground(c, fg);
    }
    for (auto e = existing.begin(); e != existing.end(); ++e)
        if (!seen.contains(e.key())) delete e.value();
    m_videos->setText(0, QStringLiteral("Videos (%1)").arg(nv));
    m_images->setText(0, QStringLiteral("Images (%1)").arg(ni));
    m_audios->setText(0, QStringLiteral("Sounds (%1)").arg(na));
    m_summary->setText(missing ? QStringLiteral("<span style='color:#ff6e5f'>%1 missing file(s): "
                                                "select them, then \"Replace…\"</span>").arg(missing)
                               : QStringLiteral("%1 file(s)").arg(nv + ni + na));
    refreshIsf();
    refreshText();
    refreshInputs();
    refreshAudioGenerators();
    refreshAudioInputs(true);
    if (!selected.isEmpty())
        for (QTreeWidgetItemIterator it(m_tree); *it; ++it) // ISF items are one level deeper (category)
            if ((*it)->data(0, PathRole).toString() == selected) {
                m_tree->setCurrentItem(*it);
                break;
            }
    updateButtons();
    applyFilter();
}

// ISF > Generators: the shaders of the library (bundled, system and added folders) and those used by layers.
// Category a generator is filed under: its first ISF category, "Generator" aside (they all are).
static QString isfCategory(const QStringList &categories)
{
    for (const QString &c : categories) {
        const QString t = c.trimmed();
        if (!t.isEmpty() && t.compare(QStringLiteral("Generator"), Qt::CaseInsensitive) != 0 &&
            t.compare(QStringLiteral("Generators"), Qt::CaseInsensitive) != 0)
            return t;
    }
    return QStringLiteral("Other");
}

// ISF > Generators > <category>: the shaders of the library (bundled, system and added folders) and those used
// by layers, filed by their ISF category.
void MediaBin::refreshIsf()
{
    const SortingPause sorting(m_tree);
    struct Gen {
        QString name, category, description;
        QStringList users;
        bool inLibrary = false, imported = false;
    };
    QMap<QString, Gen> gens;
    for (const IsfEntry &e : m_engine->library().generators())
        gens[QDir::cleanPath(e.path)] = {e.name, isfCategory(e.categories), e.description, {}, true};
    for (const QString &p : m_engine->binItems()) { // generators dropped into the Media Bin
        if (!Engine::isIsfFile(p)) continue;
        Gen &g = gens[QDir::cleanPath(p)];
        g.imported = true;
        if (g.name.isEmpty()) {
            const IsfInstance::Header h = IsfInstance::readHeader(p);
            g.name = QFileInfo(p).completeBaseName();
            g.category = isfCategory(h.categories);
            g.description = h.description;
        }
    }
    {
        Engine::Lock lk(&m_engine->mutex());
        for (int i = 0; i < m_engine->layerCount(); ++i) {
            const Layer *l = m_engine->layer(i);
            if (l->type != SourceType::Isf || l->sourcePath.isEmpty() || isLineGeneratorPath(l->sourcePath)) continue; // Line: under Generators
            Gen &g = gens[QDir::cleanPath(QFileInfo(l->sourcePath).absoluteFilePath())];
            if (g.name.isEmpty()) {
                const IsfInstance::Header h = IsfInstance::readHeader(l->sourcePath);
                g.name = QFileInfo(l->sourcePath).completeBaseName();
                g.category = isfCategory(h.categories);
                g.description = h.description;
            }
            g.users << l->name;
        }
    }
    // Existing items (kept: selection, expanded categories), by path and by category
    QHash<QString, QTreeWidgetItem *> existing, categories;
    for (int c = 0; c < m_isfGenerators->childCount(); ++c) {
        QTreeWidgetItem *cat = m_isfGenerators->child(c);
        categories.insert(cat->data(0, CategoryRole).toString(), cat);
        for (int i = 0; i < cat->childCount(); ++i) existing.insert(cat->child(i)->data(0, PathRole).toString(), cat->child(i));
    }
    QSet<QString> usedCategories;
    for (auto g = gens.cbegin(); g != gens.cend(); ++g) {
        QTreeWidgetItem *cat = categories.value(g->category);
        if (!cat) {
            cat = new BinItem(m_isfGenerators);
            cat->setData(0, CategoryRole, g->category);
            cat->setFlags(Qt::ItemIsEnabled);
            cat->setFirstColumnSpanned(true);
            cat->setExpanded(true);
            QFont f = cat->font(0);
            f.setItalic(true);
            cat->setFont(0, f);
            cat->setForeground(0, QColor(200, 200, 205));
            categories.insert(g->category, cat);
        }
        usedCategories.insert(g->category);
        QTreeWidgetItem *it = existing.take(g.key());
        if (it && it->parent() != cat) { // category changed
            delete it;
            it = nullptr;
        }
        if (!it) {
            it = new BinItem(cat);
            it->setData(0, PathRole, g.key());
            it->setData(0, IsfRole, true);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        }
        const bool missing = !QFileInfo::exists(g.key());
        it->setData(0, MissingRole, missing);
        it->setData(0, UsedRole, g->inLibrary || !g->users.isEmpty()); // library shaders stay; imported ones can be removed
        it->setData(0, UsersRole, int(g->users.size()));
        it->setText(0, g->name);
        it->setText(3, QStringLiteral("Unknown (shader RGB)"));
        it->setText(1, missing ? QStringLiteral("missing") : g->inLibrary ? QString() : g->imported ? QStringLiteral("imported") : QStringLiteral("used by project"));
        it->setText(2, g->users.isEmpty() ? QStringLiteral("—") : QString::number(g->users.size()));
        it->setTextAlignment(2, Qt::AlignCenter);
        const QString usedBy = g->users.isEmpty() ? QStringLiteral("Not used by any layer")
                                                  : QStringLiteral("Used by:\n") + g->users.join('\n');
        it->setToolTip(0, g.key() + (g->description.isEmpty() ? QString() : QStringLiteral("\n\n") + g->description) +
                              QStringLiteral("\n\n") + usedBy);
        it->setToolTip(1, g->description);
        it->setToolTip(2, usedBy);
        const QColor fg = missing ? QColor(255, 110, 95) : (g->users.isEmpty() ? QColor(170, 170, 175) : QColor(225, 225, 228));
        for (int c = 0; c < 4; ++c) it->setForeground(c, fg);
    }
    qDeleteAll(existing);
    for (auto c = categories.begin(); c != categories.end(); ++c)
        if (!usedCategories.contains(c.key())) delete c.value();
    // Opened at the first fill: an item expanded while still empty may stay folded
    if (!m_isfOpened && m_isfGenerators->childCount() > 0) {
        m_isfOpened = true;
        m_isf->setExpanded(true);
        m_isfGenerators->setExpanded(true);
        for (int c = 0; c < m_isfGenerators->childCount(); ++c) m_isfGenerators->child(c)->setExpanded(true);
    }
    int total = 0;
    for (int c = 0; c < m_isfGenerators->childCount(); ++c) {
        QTreeWidgetItem *cat = m_isfGenerators->child(c);
        cat->setText(0, QStringLiteral("%1 (%2)").arg(cat->data(0, CategoryRole).toString()).arg(cat->childCount()));
        total += cat->childCount();
    }
    m_isfGenerators->setText(0, QStringLiteral("Generators (%1)").arg(total));
}

// What an Inputs group shows: the inputs found now, plus those the layers use that are not there
namespace {
struct InputRow {
    QString uri, name, kind;
    bool present;
};

// Fills `group` with `rows` (the users of each by URI); false when it was already as it is (no flicker)
bool fillInputs(QTreeWidget *tree, QTreeWidgetItem *group, QStringList *signature, const std::vector<InputRow> &rows,
                const QHash<QString, QStringList> &users, const QString &title, const QString &dragTip)
{
    QStringList now;
    for (const InputRow &r : rows) now << r.uri + (r.present ? "+" : "-") + users.value(r.uri).join(',');
    if (now == *signature && group->childCount() == int(rows.size())) return false;
    const SortingPause sorting(tree);
    *signature = now;
    while (group->childCount()) delete group->takeChild(0);
    for (const InputRow &r : rows) {
        auto *it = new BinItem(group);
        it->setData(0, PathRole, liveInputPath(r.uri));
        it->setData(0, IsfRole, true); // no file: not relinked
        it->setData(0, MissingRole, !r.present);
        it->setData(0, UsedRole, true); // not removable: it is there or it is not
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        it->setText(0, r.name);
        it->setText(3, QStringLiteral("Unknown (live input)"));
        it->setText(1, r.present ? r.kind : r.kind + QStringLiteral(" — not found"));
        if (!r.present) it->setForeground(1, QColor(255, 110, 95));
        const QStringList u = users.value(r.uri);
        it->setData(0, UsersRole, int(u.size()));
        it->setText(2, u.isEmpty() ? QStringLiteral("—") : QString::number(u.size()));
        it->setTextAlignment(2, Qt::AlignCenter);
        it->setToolTip(0, QStringLiteral("%1 input — %2\n%3").arg(r.kind, dragTip, r.uri) +
                              (u.isEmpty() ? QString() : QStringLiteral("\nUsed by: ") + u.join(", ")));
    }
    group->setText(0, QStringLiteral("%1 (%2)").arg(title).arg(group->childCount()));
    return true;
}
} // namespace

// Video > Inputs: the cameras, the screens and windows, the DeckLink inputs, and the NDI, OMT, Syphon and Spout sources
// announced now (looked for again every few seconds while the Media Bin is shown), with the layers that use them
void MediaBin::refreshInputs()
{
    const std::vector<LiveInputInfo> found = Engine::liveInputs();
    QHash<QString, QStringList> users;
    QStringList used; // the inputs the layers use, there or not
    {
        Engine::Lock lk(&m_engine->mutex());
        for (int i = 0; i < m_engine->layerCount(); ++i) {
            const Layer *l = m_engine->layer(i);
            if (l->type != SourceType::Input) continue;
            users[l->sourcePath] << l->name;
            if (!used.contains(l->sourcePath)) used << l->sourcePath;
        }
    }
    std::vector<InputRow> rows;
    for (const LiveInputInfo &i : found) rows.push_back({i.uri, i.name, i.kind, true});
    for (const QString &u : used) {
        bool there = false;
        for (const InputRow &r : rows) there = there || r.uri == u;
        if (!there) rows.push_back({u, liveInputName(u), liveInputKind(u), false});
    }
    if (fillInputs(m_tree, m_inputs, &m_inputsSignature, rows, users, QStringLiteral("Inputs"), QStringLiteral("drag it onto a layer")))
        applyFilter();
}

// Audio > Inputs: the sound inputs of the machine and those the layers use. Listing them opens the audio system, so it is
// done every ten seconds, and at once when asked (`force`: a refresh after a change, the rebuild)
void MediaBin::refreshAudioInputs(bool force)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool list = force ? now - m_audioScanned > 1500 || m_audioScanned == 0 : now - m_audioScanned > 10000;
    if (list) {
        m_audioDevices.clear();
        for (const LiveInputInfo &i : Engine::audioInputs()) m_audioDevices << i.name;
        m_audioScanned = now;
    }
    QHash<QString, QStringList> users;
    QStringList used;
    {
        Engine::Lock lk(&m_engine->mutex());
        for (int i = 0; i < m_engine->layerCount(); ++i) {
            const Layer *l = m_engine->layer(i);
            const SourceType t = l->type != SourceType::None ? l->type : l->missingType;
            if (t != SourceType::Audio || !isAudioInputUri(l->sourcePath)) continue;
            users[l->sourcePath] << l->name;
            if (!used.contains(l->sourcePath)) used << l->sourcePath;
        }
    }
    std::vector<InputRow> rows;
    for (const QString &device : m_audioDevices) rows.push_back({audioInputUri(device), device, QStringLiteral("Audio"), true});
    for (const QString &u : used) {
        bool there = false;
        for (const InputRow &r : rows) there = there || r.uri == u;
        if (!there) rows.push_back({u, audioInputDevice(u).isEmpty() ? QStringLiteral("Default input") : audioInputDevice(u), QStringLiteral("Audio"), false});
    }
    if (fillInputs(m_tree, m_audioInputs, &m_audioInputsSignature, rows, users, QStringLiteral("Inputs"), QStringLiteral("drag it onto a layer")))
        applyFilter();
}

// Audio > Generators: the built-in instruments, and the Faust programs (.dsp) and recipes the project uses or has imported;
// the layers that use each
void MediaBin::refreshAudioGenerators()
{
    const SortingPause sorting(m_tree);
    QHash<QString, QStringList> users;
    QStringList custom;
    {
        Engine::Lock lk(&m_engine->mutex());
        for (int i = 0; i < m_engine->layerCount(); ++i) {
            const Layer *l = m_engine->layer(i);
            const SourceType t = l->type != SourceType::None ? l->type : l->missingType;
            if (t == SourceType::Audio && AudioGenerator::isRecipe(l->sourcePath)) users[l->sourcePath] << l->name;
        }
    }
    QStringList wanted;
    for (const QString &p : m_engine->binItems())
        if (AudioGenerator::isRecipe(p)) wanted << p;
    for (auto u = users.begin(); u != users.end(); ++u) wanted << u.key();
    wanted.removeDuplicates();
    QHash<QString, QTreeWidgetItem *> existing;
    for (int c = 0; c < m_audioGenerators->childCount(); ++c)
        existing.insert(m_audioGenerators->child(c)->data(0, PathRole).toString(), m_audioGenerators->child(c));
    const QString shipped = AudioResources::generatorsFolder();
    for (const QString &p : wanted) {
        const bool builtIn = !shipped.isEmpty() && QDir::cleanPath(QFileInfo(p).absolutePath()) == shipped;
        if (builtIn || existing.contains(p)) continue;
        auto *it = new BinItem(m_audioGenerators);
        it->setData(0, PathRole, p);
        it->setData(0, IsfRole, false); // a file: replaced, revealed
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        it->setText(0, QFileInfo(p).completeBaseName());
        it->setTextAlignment(2, Qt::AlignCenter);
        existing.insert(p, it);
    }
    for (int c = m_audioGenerators->childCount() - 1; c >= 0; --c) {
        QTreeWidgetItem *it = m_audioGenerators->child(c);
        const QString p = it->data(0, PathRole).toString();
        const bool builtIn = !shipped.isEmpty() && QDir::cleanPath(QFileInfo(p).absolutePath()) == shipped;
        if (!builtIn && !wanted.contains(p)) {
            delete m_audioGenerators->takeChild(c);
            continue;
        }
        const QStringList u = users.value(p);
        const bool missing = !QFileInfo::exists(p);
        it->setData(0, MissingRole, missing);
        it->setData(0, UsersRole, int(u.size()));
        it->setData(0, UsedRole, builtIn || !u.isEmpty());
        it->setText(1, missing ? QStringLiteral("missing") : builtIn ? QString() : QStringLiteral("Faust"));
        it->setText(2, u.isEmpty() ? QStringLiteral("—") : QString::number(u.size()));
        it->setText(3, QStringLiteral("Unspecified (generated)"));
        const QString usedBy = u.isEmpty() ? QStringLiteral("Not used by any layer") : QStringLiteral("Used by:\n") + u.join('\n');
        it->setToolTip(0, (builtIn ? QStringLiteral("Real-time Faust instrument") : p) + QStringLiteral("\n\n") + usedBy);
        it->setToolTip(2, usedBy);
        const QColor fg = missing ? QColor(255, 110, 95) : QColor(225, 225, 228);
        for (int col = 0; col < 4; ++col) it->setForeground(col, fg);
    }
    m_audioGenerators->setText(0, QStringLiteral("Generators (%1)").arg(m_audioGenerators->childCount()));
}

// The generators: the layers that use each
void MediaBin::refreshText()
{
    const SortingPause sorting(m_tree);
    QStringList texts, timecodes, webs, lines, clocks[3];
    {
        Engine::Lock lk(&m_engine->mutex());
        for (int i = 0; i < m_engine->layerCount(); ++i) {
            const Layer *l = m_engine->layer(i);
            if (l->type == SourceType::Text) (l->clockGen ? clocks[int(l->clockGen->kind())] : l->timecode ? timecodes : texts) << l->name;
            if (l->type == SourceType::Web) webs << l->name;
            if (l->type == SourceType::Isf && isLineGeneratorPath(l->sourcePath)) lines << l->name;
        }
    }
    auto show = [](QTreeWidgetItem *it, const QStringList &users, const QString &what) {
        it->setData(0, UsersRole, int(users.size()));
        it->setText(2, users.isEmpty() ? QStringLiteral("—") : QString::number(users.size()));
        const QString usedBy = users.isEmpty() ? QStringLiteral("Not used by any layer") : QStringLiteral("Used by:\n") + users.join('\n');
        it->setToolTip(0, what + QStringLiteral("\n\n") + usedBy);
        it->setToolTip(2, usedBy);
        it->setText(3, QStringLiteral("Unspecified (generated)"));
    };
    show(m_text, texts, QStringLiteral("Text generator: type a text; a snapshot that holds another text types it (typewriter)"));
    show(m_timecode, timecodes,
         QStringLiteral("Timecode generator: shows a timecode, from its own clock (time of day, free run) or received "
                        "(LTC, MTC, ATC), and can send it (LTC, MTC)"));
    show(m_clocks[int(ClockKind::Countdown)], clocks[int(ClockKind::Countdown)],
         QStringLiteral("Countdown generator: counts down from a duration, or to a time of day, to zero; then holds, shows "
                        "a text, or goes on into overtime in a color of its own. Start, Pause, Reset from its Source tab, "
                        "OSC, interactions, snapshots and sequences"));
    show(m_clocks[int(ClockKind::Stopwatch)], clocks[int(ClockKind::Stopwatch)],
         QStringLiteral("Chronometer generator: counts up from zero, or from a time of its own. Start, Pause, Reset from its "
                        "Source tab, OSC, interactions, snapshots and sequences"));
    show(m_clocks[int(ClockKind::Clock)], clocks[int(ClockKind::Clock)],
         QStringLiteral("Clock generator: the time of day, 24 h or 12 h, with or without its seconds and its date, in the "
                        "system's time zone or another one"));
    show(m_line, lines,
         QStringLiteral("Line generator: parallel lines, rays from a centre or rings around it; their count, width, "
                        "softness, angle, speed, phase, mirrored, waved or dashed, in a color over a background of their own"));
    if (m_web)
        show(m_web, webs,
             QStringLiteral("Web Page generator: a web page (an address, or a local HTML file) rendered at its own size and "
                            "frame rate, its background transparent if asked; its sound is muted"));
    m_inputs->setText(0, QStringLiteral("Inputs (%1)").arg(m_inputs->childCount()));
}

// A Photoshop file as a layer reads it: its size and how many layers it has to choose from (empty when it cannot)
static QString psdInfo(const QString &path)
{
    QSize canvas;
    const QVector<PsdLayerInfo> layers = isPsdFile(path) ? psdLayers(path, nullptr, &canvas) : QVector<PsdLayerInfo>();
    if (!canvas.isValid()) return {};
    const auto count = std::count_if(layers.begin(), layers.end(), [](const PsdLayerInfo &l) { return !l.group; });
    return QStringLiteral("%1×%2 · PSD, %3 layer%4\tAll its layers, or one of them chosen in the layer's Source tab")
        .arg(canvas.width())
        .arg(canvas.height())
        .arg(count)
        .arg(count == 1 ? QString() : QStringLiteral("s"));
}

void MediaBin::probe(const QString &path)
{
    if (Engine::isToxFile(path)) {
        m_info[path] = QStringLiteral("TouchDesigner component\tGPU TOP source\tUnspecified (generated)");
        return;
    }

    if (m_probing.value(path)) return;
    m_probing[path] = true;
    QPointer<MediaBin> self(this);
    const bool image = Engine::isImageFile(path);
    // Read metadata off the UI thread (files on network drives, etc.)
    QThreadPool::globalInstance()->start([self, path, image] {
        QString info;
        QString color = QStringLiteral("Unknown");
        if (QFileInfo(path).isDir()) { // an image sequence: its pictures as a clip (25 fps unless a layer says)
            Engine::SequenceInfo seq;
            VideoSource source;
            VideoDecoder::Info vi;
            if (Engine::detectImageSequence(path, &seq)) {
                source.pattern = seq.pattern;
                source.start = seq.start;
            }
            if (source.isSequence() && VideoDecoder::probe(path, &vi, nullptr, source)) {
                info = QStringLiteral("Sequence · %1 frames · %2×%3 · %4 fps\t%5, from %6 · %7")
                           .arg(vi.frames).arg(vi.width).arg(vi.height).arg(vi.fps, 0, 'g', 4)
                           .arg(seq.pattern).arg(seq.start).arg(vi.codec);
                color = vi.colorSpaceSummary();
            } else {
                info = QStringLiteral("no image sequence");
            }
        }
        AudioStream::Info ai;
        const bool sound = !image && !QFileInfo(path).isDir() && AudioStream::probe(path, &ai);
        const QString soundText = sound ? QStringLiteral("%1 %2 kHz %3")
                                              .arg(ai.codec)
                                              .arg(ai.sampleRate / 1000.0, 0, 'g', 3)
                                              .arg(ai.channels == 1 ? QStringLiteral("mono")
                                                   : ai.channels == 2 ? QStringLiteral("stereo")
                                                                      : QStringLiteral("%1 ch").arg(ai.channels))
                                        : QString();
        if (!info.isEmpty()) {
            // an image sequence, above
        } else if (!image) {
            VideoDecoder::Info vi;
            if (Engine::isAudioFile(path) || !VideoDecoder::probe(path, &vi)) {
                // Sound only
                if (sound) color = QStringLiteral("Not applicable");
                info = sound ? (ai.duration > 0 ? fmtDuration(ai.duration) + QStringLiteral(" · ") : QString()) +
                                   QStringLiteral("%1 kHz").arg(ai.sampleRate / 1000.0, 0, 'g', 3) + QStringLiteral("\t") +
                                   soundText
                             : QStringLiteral("unreadable");
            } else {
                // Short column (resolution · duration); details are in the tooltip
                info = QStringLiteral("%1×%2").arg(vi.width).arg(vi.height);
                if (vi.duration > 0) info += QStringLiteral(" · ") + fmtDuration(vi.duration);
                QStringList more;
                if (vi.fps > 0) more << QStringLiteral("%1 fps").arg(vi.fps, 0, 'g', 4);
                if (!vi.codec.isEmpty()) more << vi.codec;
                more << vi.colorDescription();
                color = vi.colorSpaceSummary();
                more << (sound ? QStringLiteral("sound: ") + soundText : QStringLiteral("no sound"));
                if (vi.bitDepth > 0) info += QStringLiteral(" · %1-bit").arg(vi.bitDepth);
                if (!vi.matrix.isEmpty()) info += QStringLiteral(" · ") + vi.matrix;
                info += QStringLiteral("\t") + more.join(QStringLiteral(" · "));
            }
        } else if (const QString psd = psdInfo(path); !psd.isEmpty()) {
            info = psd;
        } else if (isSvgFile(path)) { // drawn at the composition's size when a layer shows it
            const QSize s = svgRenderSize(path, 0);
            info = s.isValid() ? QStringLiteral("%1×%2 · SVG\tvector drawing, rendered at least as large as the "
                                                "composition, transparent background").arg(s.width()).arg(s.height())
                               : QStringLiteral("unreadable");
            color = QStringLiteral("sRGB");
        } else {
            QImageReader r(path);
            const QSize s = r.size();
            info = s.isValid() ? QStringLiteral("%1×%2 · %3").arg(s.width()).arg(s.height()).arg(QString::fromLatin1(r.format()))
                               : QStringLiteral("unreadable");
            if (s.isValid()) {
                // Its profile from a 1-pixel read (JPEG decodes scaled; other formats may decode in full before
                // scaling, hence the size limit below)
                const QImage::Format fmt = r.imageFormat();
                QStringList tags;
                if (fmt != QImage::Format_Invalid) tags << imageSourceDepth(fmt);
                if (qint64(s.width()) * s.height() <= 64LL * 1024 * 1024) {
                    r.setScaledSize(QSize(1, 1));
                    const QImage sample = r.read();
                    const QColorSpace space = sample.colorSpace();
                    tags << imageColorDescription(space);
                    color = imageColorSpaceName(space);
                } else {
                    tags << QStringLiteral("color profile: not probed (large image)");
                    color = QStringLiteral("Not probed (large image)");
                }
                info += QStringLiteral("\t") + tags.join(QStringLiteral(" · "));
            } else if (const QImage img = readImageFile(path); !img.isNull()) {
                // No Qt plugin for it (OpenEXR, Photoshop, TIFF on Linux): FFmpeg decodes it, as the layer will
                info = QStringLiteral("%1×%2 · FFmpeg\t%3 · %4")
                           .arg(img.width()).arg(img.height())
                           .arg(imageSourceDepth(img.format()), imageColorDescription(img.colorSpace()));
                color = imageColorSpaceName(img.colorSpace());
            }
        }
        // Three fields: short info, tooltip details, explicit source color tags.
        if (!info.contains('\t')) info += '\t';
        info += '\t' + color;
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, path, info] {
                if (!self) return;
                self->m_info[path] = info;
                self->m_probing.remove(path);
                self->refresh();
            },
            Qt::QueuedConnection);
    });
}

void MediaBin::updateButtons()
{
    bool any = false, removable = false;
    for (QTreeWidgetItem *it : m_tree->selectedItems()) {
        if (it->data(0, PathRole).toString().isEmpty()) continue;
        any = true;
        removable |= !it->data(0, UsedRole).toBool();
    }
    m_relink->setEnabled(m_tree->selectedItems().size() == 1 && replaceable(m_tree->selectedItems().first()));
    m_remove->setEnabled(removable);
    m_reveal->setEnabled(any && !m_tree->selectedItems().first()->data(0, MissingRole).toBool() &&
                         !Engine::isStreamUrl(m_tree->selectedItems().first()->data(0, PathRole).toString()));
}

static bool isMedia(const QString &p) { return Engine::isToxFile(p) || Engine::isVideoFile(p) || Engine::isImageFile(p) || Engine::isAudioFile(p); }

void MediaBin::importFiles(const QStringList &paths)
{
    QStringList ok, refused;
    for (const QString &p : paths) {
        QFileInfo fi(p);
        if (const QString url = Engine::streamUrlOfPath(p); !url.isEmpty()) { // a stream (its transport kept)
            if (!m_engine->binItems().contains(url)) m_engine->addBinStream(url, true);
            ok << url;
        } else if (fi.isDir() && Engine::isSequenceFolder(p)) {
            // A folder of numbered pictures: one image sequence, played as a clip
            ok << QDir::cleanPath(fi.absoluteFilePath());
        } else if (fi.isDir()) {
            // A dropped folder: import its videos, images and audio files (top level only)
            for (const QFileInfo &f : QDir(p).entryInfoList(QDir::Files, QDir::Name))
                if (isMedia(f.filePath()) || (Engine::isIsfFile(f.filePath()) && IsfInstance::readHeader(f.filePath()).ok &&
                                              !IsfInstance::readHeader(f.filePath()).isFilter &&
                                              !IsfInstance::readHeader(f.filePath()).isTransition))
                    ok << f.absoluteFilePath();
        } else if (isMedia(p)) {
            ok << fi.absoluteFilePath();
        } else if (Engine::isIsfFile(p)) { // an ISF generator from the Finder
            const IsfInstance::Header h = IsfInstance::readHeader(p);
            if (h.ok && !h.isFilter && !h.isTransition) ok << fi.absoluteFilePath();
            else refused << fi.fileName();
        }
    }
    if (!refused.isEmpty())
        QMessageBox::information(this, QStringLiteral("Media Bin"),
                                 QStringLiteral("Not a generator (effects and transitions are added from a layer's FX tab):\n") +
                                     refused.join('\n'));
    if (ok.isEmpty()) return;
    m_engine->addBinItems(ok);
    refresh();
    emit binEdited();
}

void MediaBin::importDialog()
{
    QSettings s;
    QStringList ext;
    for (const QString &e : Engine::videoExtensions() + Engine::imageExtensions() + Engine::audioExtensions()) ext << "*." + e;
    ext << "*.fs" << "*.tox";
    const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Import to Media Bin"),
                                                            s.value("dirs/video").toString(),
                                                            QStringLiteral("Videos, Images, Audio, ISF and TouchDesigner components (%1);;All Files (*)").arg(ext.join(' ')));
    if (files.isEmpty()) return;
    s.setValue("dirs/video", QFileInfo(files.first()).absolutePath());
    importFiles(files);
}

void MediaBin::importSequenceDialog()
{
    QSettings s;
    const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Import Image Sequence (a folder of numbered pictures)"),
                                                             s.value("dirs/video").toString());
    if (folder.isEmpty()) return;
    s.setValue("dirs/video", QFileInfo(folder).absolutePath());
    if (!Engine::isSequenceFolder(folder)) {
        QMessageBox::information(this, QStringLiteral("Image Sequence"),
                                 QStringLiteral("No image sequence in %1: a sequence is a folder whose pictures (%2) are "
                                                "mostly one numbered series (name0001.exr, name0002.exr…).")
                                     .arg(QDir::toNativeSeparators(folder), Engine::sequenceExtensions().join(QStringLiteral(", "))));
        return;
    }
    importFiles({folder});
}

void MediaBin::addStreamDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Add Stream URL"));
    auto *form = new QFormLayout(&dialog);
    auto *url = new QLineEdit;
    url->setObjectName(QStringLiteral("streamUrl"));
    url->setPlaceholderText(QStringLiteral("rtsp://camera.local/stream"));
    url->setMinimumWidth(360);
    url->setToolTip(QStringLiteral("rtsp://, rtmp://, srt://, udp://, rtp://, http(s)://… (HLS .m3u8 or a progressive "
                                   "stream). Played live: no duration, no seeking; connected again when it drops."));
    auto *tcp = new QCheckBox(QStringLiteral("TCP for RTSP"));
    tcp->setChecked(true);
    tcp->setToolTip(QStringLiteral("RTSP over TCP: through firewalls and without lost packets. Off: UDP, lower latency "
                                   "on a clean network"));
    form->addRow(QStringLiteral("URL"), url);
    form->addRow(QString(), tcp);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, url] {
        if (Engine::isStreamUrl(url->text().trimmed())) dialog.accept();
        else
            QMessageBox::information(&dialog, QStringLiteral("Add Stream URL"),
                                     QStringLiteral("Not a stream URL: rtsp://, rtmp://, srt://, udp://, rtp://, tcp:// or http(s)://"));
    });
    if (dialog.exec() != QDialog::Accepted) return;
    m_engine->addBinStream(url->text().trimmed(), tcp->isChecked());
    refresh();
    emit binEdited();
}

void MediaBin::relinkSelected()
{
    const QString from = selectedPath();
    if (from.isEmpty()) return;
    QString start = QFileInfo(from).absolutePath();
    if (!QFileInfo(start).isDir()) start = QSettings().value("dirs/video").toString();
    // A generator by another generator; a media file by any media (a video by an image…) or a generator
    QStringList ext;
    if (!Engine::isIsfFile(from))
        for (const QString &e : Engine::videoExtensions() + Engine::imageExtensions() + Engine::audioExtensions()) ext << "*." + e;
    ext << QStringLiteral("*.fs") << QStringLiteral("*.frag") << QStringLiteral("*.tox");
    const QString to = QFileDialog::getOpenFileName(this, QStringLiteral("Replace \"%1\" with…").arg(QFileInfo(from).fileName()),
                                                    start + "/" + QFileInfo(from).fileName(),
                                                    (Engine::isIsfFile(from) ? QStringLiteral("ISF generators (%1)")
                                                                             : QStringLiteral("Media, ISF and TouchDesigner components (%1)"))
                                                            .arg(ext.join(' ')) +
                                                        QStringLiteral(";;All Files (*)"));
    if (to.isEmpty() || to == from) return;
    emit relinkRequested(from, QFileInfo(to).absoluteFilePath());
}

void MediaBin::removeSelected()
{
    for (QTreeWidgetItem *it : m_tree->selectedItems())
        if (!it->data(0, UsedRole).toBool()) m_engine->removeBinItem(it->data(0, PathRole).toString());
    refresh();
    emit binEdited();
}

void MediaBin::revealSelected()
{
    const QString p = selectedPath();
    if (p.isEmpty()) return;
#if defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), p});
#elif defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer"), {QStringLiteral("/select,"), QDir::toNativeSeparators(p)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(p).absolutePath()));
#endif
}

void MediaBin::contextMenu(const QPoint &pos)
{
    QTreeWidgetItem *it = m_tree->itemAt(pos);
    const QString p = it ? it->data(0, PathRole).toString() : QString();
    QMenu menu(this);
    if (!p.isEmpty()) {
        const bool missing = it->data(0, MissingRole).toBool();
        const bool isf = it->data(0, IsfRole).toBool();
        QAction *a = menu.addAction(QStringLiteral("Load into Selected Layer"), this, [this, p] { emit useAsSourceRequested(p); });
        a->setEnabled(!missing);
        menu.addAction(QStringLiteral("Load into New Layer"), this, [this, p] { emit loadIntoNewLayerRequested(p); })->setEnabled(!missing);
        QMenu *into = menu.addMenu(QStringLiteral("Load into Layer"));
        {
            // Every layer of the composition, groups shown (disabled: they have no source) with their layers indented
            Engine::Lock lk(&m_engine->mutex());
            for (int i = 0; i < m_engine->layerCount(); ++i) {
                Layer *l = m_engine->layer(i);
                if (!l || l->isViewport) continue;
                int depth = 0;
                for (quint64 up = l->parent; up; ++depth) {
                    const Layer *g = m_engine->layer(m_engine->indexOfId(up));
                    up = g ? g->parent : 0;
                }
                const QString label = QStringLiteral("      ").repeated(depth) + l->name;
                QAction *la = into->addAction(label, this, [this, i, p] { emit loadIntoLayerRequested(i, p); });
                la->setEnabled(!l->isGroup && !m_engine->isLocked(i));
            }
        }
        into->setEnabled(!missing && !into->isEmpty());
        menu.addSeparator();
        if (isf) menu.addAction(QStringLiteral("Reload ISF"), this, [this, p] { emit reloadIsfRequested(p); })->setEnabled(!missing);
        menu.addAction(QStringLiteral("Replace…"), this, [this, it] {
                m_tree->setCurrentItem(it);
                relinkSelected();
            })->setEnabled(replaceable(it));
        menu.addAction(m_reveal->text(), this, &MediaBin::revealSelected)->setEnabled(!missing);
        menu.addAction(QStringLiteral("Remove from Media Bin"), this, &MediaBin::removeSelected)->setEnabled(!it->data(0, UsedRole).toBool());
        menu.addSeparator();
    }
    menu.addAction(QStringLiteral("Import…"), this, &MediaBin::importDialog);
    menu.addAction(QStringLiteral("Import Image Sequence (Folder)…"), this, &MediaBin::importSequenceDialog);
    menu.addAction(QStringLiteral("Add Stream URL…"), this, &MediaBin::addStreamDialog);
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void MediaBin::rebuild()
{
    const SortingPause sorting(m_tree);
    // Read again: what the files are now (a file found again, a new version), the inputs there, the library's shaders
    m_info.clear();
    for (auto p = m_probing.begin(); p != m_probing.end();) p = p.value() ? std::next(p) : m_probing.erase(p);
    m_inputsSignature.clear();
    m_audioInputsSignature.clear();
    m_audioScanned = 0;
    for (QTreeWidgetItem *cat : {m_videos, m_images, m_audios})
        for (int i = 0; i < cat->childCount(); ++i) cat->child(i)->setText(1, QStringLiteral("…"));
    emit rebuildRequested(); // the owner scans the ISF library again, then refreshes the bin
    refresh();
}

void MediaBin::applyFilter()
{
    const QString f = m_search ? m_search->text().trimmed() : QString();
    const int usage = m_usage ? m_usage->currentIndex() : 0; // 0 all, 1 used, 2 unused
    const int media = m_type ? m_type->currentIndex() : 0;   // 0 both, 1 video, 2 audio
    const int kind = m_kind ? m_kind->currentIndex() : 0;    // 0 all, 1 files, 2 generators, 3 inputs, 4 ISF
    const bool filtering = !f.isEmpty() || usage != 0;
    // Leaves (media, shaders) match by name and use; a category stays visible if one of its items does
    std::function<bool(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *it) -> bool {
        if (it->childCount() == 0 && (it->data(0, PathRole).isValid() || it->parent())) {
            const bool item = it->data(0, PathRole).isValid(); // not an empty category
            const bool show = item ? (f.isEmpty() || it->text(0).contains(f, Qt::CaseInsensitive) ||
                                      QFileInfo(it->data(0, PathRole).toString()).fileName().contains(f, Qt::CaseInsensitive)) &&
                                         (usage == 0 || (usage == 1) == (it->data(0, UsersRole).toInt() > 0))
                                   : !filtering;
            it->setHidden(!show);
            return show;
        }
        bool any = false;
        for (int c = 0; c < it->childCount(); ++c) any |= visit(it->child(c));
        it->setHidden(filtering && !any);
        if (filtering && any) it->setExpanded(true);
        return any || !filtering;
    };
    const QList<QTreeWidgetItem *> files{m_videos, m_images, m_audios}, generators{m_generators, m_audioGenerators},
        inputs{m_inputs, m_audioInputs};
    auto kindMatches = [&](const QTreeWidgetItem *group) {
        return kind == 0 || (kind == 1 && files.contains(group)) || (kind == 2 && generators.contains(group)) ||
               (kind == 3 && inputs.contains(group)) || (kind == 4 && group == m_isf);
    };
    for (QTreeWidgetItem *root : {m_videoRoot, m_audioRoot}) {
        bool any = false;
        for (int c = 0; c < root->childCount(); ++c) {
            QTreeWidgetItem *group = root->child(c);
            const bool shown = visit(group);
            // The chosen kind is shown even empty; the other kinds are not
            group->setHidden(!kindMatches(group) || (!shown && filtering));
            any |= !group->isHidden();
        }
        const bool mediaMatches = media == 0 || (media == 1) == (root == m_videoRoot);
        root->setHidden(!mediaMatches || (!any && (filtering || kind != 0)));
    }
}
