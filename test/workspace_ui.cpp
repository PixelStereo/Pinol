#include "Engine.h"
#include "MediaBin.h"
#include <QFile>
#include "MainWindow.h"
#include "SequencePanel.h"
#include "SnapshotPanel.h"
#include "TimelinePanel.h"
#include "LayerInspector.h"
#include "PresetEditor.h"
#include "SettingsPanel.h"
#include "ColorPicker.h"
#include "ViewportOutput.h"
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QThread>
#include <QColorSpace>
#include <QComboBox>
#include "Commands.h"
#include "LayerTable.h"
#include "Widgets.h"
#include <QTableWidget>
#include <QAction>
#include <QHeaderView>
#include <QMouseEvent>
#include <QMenu>
#include <QMap>
#include <QUndoStack>
#include <QListWidget>
#include <QLabel>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QToolButton>
#include <QDir>
#include <QTabWidget>
#include <QPushButton>
#include <QApplication>
#include <QCheckBox>
#include <QMenuBar>
#include <QSettings>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QSurfaceFormat>
#include <QTabBar>
#include <QTemporaryDir>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <functional>
#include <utility>
int runColorParameterUiTests(Engine &engine, const QString &root);
int runXyParameterUiTests(Engine &engine, const QString &root);
int runClockPresetUiTests(Engine &engine);
int runLayerSourceUiTests(Engine &engine); // layer_source_ui.cpp
int runAudioFxUiTests(Engine &engine);     // audio_fx_ui.cpp
int runControlsUiTests(Engine &engine);    // controls_ui.cpp

static void mediaBinColumnTests(Engine &engine, const std::function<void(bool, const char *)> &check)
{
    QSettings settings;
    settings.beginGroup("mediaBin");
    QMap<QString, QVariant> saved;
    for (const QString &key : settings.allKeys()) saved.insert(key, settings.value(key));
    settings.remove("");
    QTemporaryDir files;
    const QString alpha = files.filePath("alpha.png"), bravo = files.filePath("bravo.png"), charlie = files.filePath("charlie.png");
    for (const auto &entry : {std::make_pair(alpha, 32), std::make_pair(bravo, 16), std::make_pair(charlie, 8)}) {
        QImage image(entry.second, entry.second, QImage::Format_RGBA8888);
        image.fill(Qt::gray);
        if (entry.first != charlie) image.setColorSpace(entry.first == alpha ? QColorSpace::DisplayP3 : QColorSpace::SRgb);
        check(image.save(entry.first), "create media-bin sorting fixture");
    }
    engine.addBinItems({alpha, bravo, charlie});
    QString error;
    for (int i = 0; i < 12; ++i)
        check(engine.setLayerImage(engine.addLayer(QStringLiteral("Use %1").arg(i)), i < 10 ? alpha : bravo, &error),
              "create different media usage counts");
    {
        MediaBin bin(&engine);
        bin.resize(1000, 500); bin.show(); QApplication::processEvents();
        auto *tree = bin.findChild<QTreeWidget *>("mediaBinTree");
        auto *columns = bin.findChild<QToolButton *>("mediaBinColumns");
        check(tree && columns && columns->menu(), "media bin exposes a Columns menu");
        if (tree && columns && columns->menu()) {
            const auto actions = columns->menu()->actions();
            check(actions.size() == 4 && actions[0]->text() == "File" && actions[0]->isChecked() && !actions[0]->isEnabled(),
                  "File is always visible and cannot be hidden in the Columns menu");
            auto itemOf = [tree](const QString &path) -> QTreeWidgetItem * {
                for (QTreeWidgetItemIterator it(tree); *it; ++it)
                    if ((*it)->data(0, Qt::UserRole + 1).toString() == path) return *it;
                return nullptr;
            };
            auto *a = itemOf(alpha), *b = itemOf(bravo), *c = itemOf(charlie);
            check(a && b && c, "sorting fixtures appear in the media bin");
            if (a && b && c && actions.size() == 4) {
                auto *group = a->parent();
                QList<QTreeWidgetItem *> groups;
                for (int i = 0; i < tree->topLevelItemCount(); ++i) groups << tree->topLevelItem(i);
                for (int i = 0; i < group->parent()->childCount(); ++i) groups << group->parent()->child(i);
                auto orderIs = [group](QTreeWidgetItem *first, QTreeWidgetItem *second, QTreeWidgetItem *third) {
                    return group->childCount() == 3 && group->child(0) == first && group->child(1) == second && group->child(2) == third;
                };
                tree->setCurrentItem(a);
                tree->sortItems(3, Qt::AscendingOrder);
                QElapsedTimer wait; wait.start();
                while (wait.elapsed() < 5000 && (a->text(3) == "…" || b->text(3) == "…" || c->text(3) == "…")) {
                    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
                    QThread::msleep(1);
                }
                check(a->text(3).contains("P3") && b->text(3).contains("sRGB") && c->text(3).contains("Unknown"),
                      "asynchronous media-bin color metadata arrives");
                check(tree->sortColumn() == 3 && group->child(0) == a && bin.selectedPath() == alpha,
                      "metadata updates preserve color sorting and selection");
                tree->sortItems(0, Qt::AscendingOrder);
                check(orderIs(a, b, c), "File sorts media by name");
                auto clickHeader = [tree](int column) {
                    auto *header = tree->header();
                    const QPointF pos(header->sectionViewportPosition(column) + header->sectionSize(column) / 2, header->height() / 2);
                    const QPointF global = header->viewport()->mapToGlobal(pos.toPoint());
                    QMouseEvent down(QEvent::MouseButtonPress, pos, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                    QMouseEvent up(QEvent::MouseButtonRelease, pos, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(header->viewport(), &down); QApplication::sendEvent(header->viewport(), &up);
                };
                clickHeader(0);
                check(tree->header()->sortIndicatorOrder() == Qt::DescendingOrder && orderIs(c, b, a),
                      "clicking a header reverses its sort order");
                a->setText(1, "Zulu"); b->setText(1, "Alpha"); c->setText(1, "Mike");
                tree->sortItems(1, Qt::AscendingOrder);
                check(orderIs(b, c, a), "Info sorts by the displayed description");
                for (int column = 0; column < 4; ++column) {
                    clickHeader(column);
                    check(tree->sortColumn() == column, "every column header selects its sort key");
                    bin.refresh();
                    check(tree->sortColumn() == column, "refresh preserves the selected sort key");
                    check(tree->topLevelItemCount() == 2 && tree->topLevelItem(0) == groups[0] && tree->topLevelItem(1) == groups[1],
                          "sorting keeps the Video and Audio roots in place");
                    for (int i = 0; i < group->parent()->childCount(); ++i)
                        check(group->parent()->child(i) == groups[2 + i], "sorting keeps media type groups in place");
                }
                tree->sortItems(2, Qt::AscendingOrder);
                check(orderIs(c, b, a), "Layers sorts zero, two and ten numerically");
                tree->sortItems(2, Qt::DescendingOrder); bin.refresh();
                check(orderIs(a, b, c) && tree->header()->sortIndicatorOrder() == Qt::DescendingOrder,
                      "descending numeric sorting survives a usage refresh");
                actions[2]->trigger();
                check(tree->isColumnHidden(2) && tree->sortColumn() == 0, "hiding the active sort column returns to File");
                actions[1]->trigger(); actions[3]->trigger();
                check(!tree->isColumnHidden(0) && tree->isColumnHidden(1) && tree->isColumnHidden(2) && tree->isColumnHidden(3),
                      "all optional columns can be hidden independently");
                actions[2]->trigger(); tree->sortItems(2, Qt::DescendingOrder);
                MediaBin reopened(&engine);
                auto *restored = reopened.findChild<QTreeWidget *>("mediaBinTree");
                check(restored && !restored->isColumnHidden(0) && restored->isColumnHidden(1) &&
                          !restored->isColumnHidden(2) && restored->isColumnHidden(3) && restored->sortColumn() == 2 &&
                          restored->header()->sortIndicatorOrder() == Qt::DescendingOrder,
                      "a new Media Bin restores column visibility and sorting");
            }
        }
    }
    engine.newProject();
    settings.remove("");
    for (auto it = saved.cbegin(); it != saved.cend(); ++it) settings.setValue(it.key(), it.value());
}

static void nestedGroupTests(Engine &engine, const std::function<void(bool, const char *)> &check)
{
    engine.newProject();
    const auto outer = engine.layerId(engine.addGroup("Outer", engine.layerCount()));
    const auto inner = engine.layerId(engine.addGroup("Inner", engine.layerCount()));
    const auto source = engine.layerId(engine.addLayer("Source", engine.layerCount()));
    const auto linked = engine.layerId(engine.addLayer("Linked", engine.layerCount()));
    const auto external = engine.layerId(engine.addLayer("External mask", engine.layerCount()));
    engine.setLayerText(engine.indexOfId(source)); engine.setLayerText(engine.indexOfId(external));
    auto structure = engine.structure();
    for (auto &node : structure) {
        if (node.id == inner) node.parent = outer;
        if (node.id == source || node.id == linked) node.parent = inner;
    }
    engine.setStructure(structure);
    check(engine.setLayerSourceLayer(engine.indexOfId(linked), source, LayerTap::PostFx), "create an internal group reference");
    LayerMatte matte; matte.layer = source;
    check(engine.setLayerMatte(engine.indexOfId(linked), matte), "create an internal matte reference");
    check(engine.setColorMask(engine.indexOfId(linked), external, false), "create an external mask reference");
    {
        MainWindow window(&engine); window.setQuiet(true); window.setAutosaveEnabled(false);
        window.show(); QApplication::processEvents();
        auto *table = window.findChild<LayerTable *>();
        auto *undo = window.findChild<QUndoStack *>();
        check(table && undo, "nested group UI fixture is available");
        if (table && undo) {
            const int before = engine.layerCount();
            table->setCurrentRow(engine.indexOfId(outer)); table->duplicateClicked();
            check(engine.layerCount() == before + 4, "duplicate includes all descendants of nested groups");
            const auto copy = engine.layerId(table->currentRow());
            const auto descendants = tree::descendants(engine.structure(), copy);
            check(descendants.size() == 3, "the copied nested structure is intact");
            if (descendants.size() == 3) {
                const auto *copiedLink = engine.layer(engine.indexOfId(descendants[2]));
                check(copiedLink && copiedLink->sourceLayer == descendants[1] && copiedLink->matte.layer == descendants[1] &&
                      copiedLink->color.maskLayer == external,
                      "copy redirects internal source/matte references and preserves external references");
            }
            undo->undo();
            check(engine.layerCount() == before && engine.structure() == structure, "Undo removes every duplicated descendant");
            undo->redo(); check(engine.layerCount() == before + 4, "Redo recreates the whole nested group");
            undo->undo();
            table->setCurrentRow(engine.indexOfId(outer)); table->removeClicked();
            check(engine.indexOfId(outer) < 0 && engine.indexOfId(inner) < 0 && engine.indexOfId(source) < 0 &&
                  engine.indexOfId(linked) < 0 && engine.indexOfId(external) >= 0,
                  "Delete removes the whole nested group and leaves unrelated layers alone");
            undo->undo(); check(engine.structure() == structure, "Undo restores all nested group members and parents");
            undo->setClean();
        }
    }
    engine.newProject();
}

static void systemIsfVisibilityTests(Engine &engine, const std::function<void(bool, const char *)> &check)
{
    const QStringList names{"showSystemIsfGenerators", "showSystemIsfFX", "showSystemIsfTransitions"};
    const QStringList keys{"isf/showSystemGenerators", "isf/showSystemFX", "isf/showSystemTransitions"};
    {
        MainWindow window(&engine); window.setQuiet(true); window.setAutosaveEnabled(false);
        auto *settings = window.findChild<SettingsPanel *>();
        auto *transition = settings ? settings->findChild<QComboBox *>("snapshotTransition") : nullptr;
        check(transition != nullptr, "default snapshot transition is accessible");
        if (transition) {
            const QString current = "/missing/system/HiddenTransition.fs";
            settings->setTransitions({}, current);
            check(transition->currentData().toString() == current && transition->currentText() == "HiddenTransition",
                  "hiding a transition keeps the current selection accurately displayed");
        }
        bool menu = false; // the ISF library's settings are in Settings › ISF, not in the menu bar
        for (QAction *a : window.menuBar()->actions()) menu = menu || a->text().contains("ISF");
        check(!menu, "no ISF Library menu in the menu bar");
        check(settings && settings->findChild<QWidget *>("isfFolders") && settings->findChild<QWidget *>("addIsfFolder") &&
                  settings->findChild<QWidget *>("rescanIsf") && settings->findChild<QWidget *>("forgetIsfFolders"),
              "the ISF tab holds the folders, Add, Rescan and Forget");
        for (int i = 0; i < names.size(); ++i) {
            auto *action = settings ? settings->findChild<QCheckBox *>(names[i]) : nullptr;
            check(action && action->isChecked(), "system shader categories default to visible");
            if (action) action->setChecked(false);
            check(!QSettings().value(keys[i], true).toBool(), "system shader visibility is saved immediately");
        }
        const auto visibility = engine.library().systemVisibility();
        check(!visibility.generators && !visibility.filters && !visibility.transitions, "all three preferences reach the library");
    }
    {
        MainWindow reopened(&engine); reopened.setQuiet(true); reopened.setAutosaveEnabled(false);
        auto *reopenedSettings = reopened.findChild<SettingsPanel *>();
        for (const auto &name : names) {
            auto *action = reopenedSettings ? reopenedSettings->findChild<QCheckBox *>(name) : nullptr;
            check(action && !action->isChecked(), "reopening restores hidden system shader categories");
            if (action) action->setChecked(true);
        }
        const auto visibility = engine.library().systemVisibility();
        check(visibility.generators && visibility.filters && visibility.transitions, "all categories can be shown again");
    }
    for (const auto &key : keys) QSettings().remove(key);
}

int main(int argc, char **argv)
{
    QSurfaceFormat format; format.setVersion(3, 3); format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    app.setOrganizationName("NyxMapTests"); app.setApplicationName("Workspace");
    Engine engine; QString error;
    if (!engine.initialize(&error)) return 1;
    engine.newProject();
    // Shared by workspace_ui (also under Valgrind), selection_ui (--selection) and color_parameter_ui (--color-animation).
    bool xy = false, selection = false, colorAnimation = false, clockPresets = false, systemIsf = false, layerSource = false, audioFx = false, controlsUi = false;
    QString capture;
    for (int k = 1; k < argc; ++k) {
        const QString arg = QString::fromLocal8Bit(argv[k]);
        if (arg == QLatin1String("--xy")) xy = true;
        else if (arg == QLatin1String("--selection")) selection = true;
        else if (arg == QLatin1String("--color-animation")) colorAnimation = true;
        else if (arg == QLatin1String("--clock-presets")) clockPresets = true;
        else if (arg == QLatin1String("--layer-source")) layerSource = true;
        else if (arg == QLatin1String("--audio-fx")) audioFx = true;
        else if (arg == QLatin1String("--controls")) controlsUi = true;
        else if (arg == QLatin1String("--system-isf")) systemIsf = true;
        else capture = arg;
    }
    if (xy) {
        const int failures = runXyParameterUiTests(engine, QStringLiteral(TEST_DIR));
        engine.shutdown();
        return failures ? 1 : 0;
    }
    if (controlsUi) {
        const int failures = runControlsUiTests(engine);
        engine.shutdown();
        return failures ? 1 : 0;
    }
    if (audioFx) {
        const int failures = runAudioFxUiTests(engine);
        engine.shutdown();
        return failures ? 1 : 0;
    }
    if (layerSource) {
        const int failures = runLayerSourceUiTests(engine);
        engine.shutdown();
        return failures ? 1 : 0;
    }
    if (clockPresets) {
        const int failures = runClockPresetUiTests(engine);
        engine.shutdown();
        return failures ? 1 : 0;
    }
    if (colorAnimation) {
        const int failures = runColorParameterUiTests(engine, QStringLiteral(TEST_DIR));
        engine.shutdown();
        return failures ? 1 : 0;
    }
    int failures = 0;
    auto check = [&](bool ok, const char *reason) {
        if (!ok) { ++failures; std::printf("FAIL %s: %s\n", selection ? "selection_ui" : "workspace_ui", reason); }
    };
    if (systemIsf) {
        systemIsfVisibilityTests(engine, check);
        engine.shutdown();
        return failures ? 1 : 0;
    }
    if (!selection) {
        mediaBinColumnTests(engine, check);
        nestedGroupTests(engine, check);
        MainWindow window(&engine); window.setAutosaveEnabled(false); window.setQuiet(true);
        window.show(); app.processEvents();
        auto *defaultColorView = window.findChild<QComboBox *>("defaultColorPickerView");
        check(defaultColorView && defaultColorView->currentData().toInt() == int(ColorPicker::HSL), "Settings defaults to HSL");
        if (defaultColorView) {
            ColorPicker unchosen, chosen;
            chosen.setView(ColorPicker::Palette);
            defaultColorView->setCurrentIndex(defaultColorView->findData(int(ColorPicker::RGB)));
            check(unchosen.view() == ColorPicker::RGB && chosen.view() == ColorPicker::Palette,
                  "Settings changes reach unchosen pickers and preserve local choices");
            defaultColorView->setCurrentIndex(defaultColorView->findData(int(ColorPicker::HSL)));
        }
        auto *tabs = window.findChild<QTabBar *>("showWorkspaceTabs");
        auto *pages = window.findChild<QStackedWidget *>("showWorkspacePages");
        auto *lists = window.findChild<QStackedWidget *>("snapshotPresetLists");
        auto *inspector = window.findChild<QStackedWidget *>("snapshotPresetInspector");
        auto *sequences = window.findChild<SequenceWindow *>();
        auto *timelines = window.findChild<TimelineWindow *>();
        check(tabs && pages && lists && inspector && sequences && timelines, "all docked workspace controls present");
        // Regressions: pane minima and long status strings used to force the
        // native window wider than a screen, preventing macOS half-screen tiling.
        {
            const QSize original = window.size();
            const QRect available = window.screen()->availableGeometry();
            check(available.adjusted(-2, -2, 2, 2).contains(window.frameGeometry()),
                  "initial window including its title bar fits the available screen");
            auto *mainTabs = window.findChild<QTabWidget *>("mainInspectorTabs");
            auto *libraryArea = window.findChild<QScrollArea *>("mainLibraryScroll");
            auto *libraryTabs = libraryArea ? qobject_cast<QTabWidget *>(libraryArea->widget()) : nullptr;
            const QSize halfScreen(std::max(320, available.width() / 2 - 16), std::max(320, available.height() - 80));
            for (const QSize &compact : {halfScreen, QSize(640, 480), QSize(480, 360)}) {
                window.resize(compact);
                app.processEvents();
                for (int mode : {0, 1, 2}) {
                    if (tabs) tabs->setCurrentIndex(mode);
                    if (mainTabs) mainTabs->setCurrentIndex(mode);
                    if (libraryTabs) libraryTabs->setCurrentIndex(mode);
                    app.processEvents();
                    check(window.width() <= compact.width() && window.height() <= compact.height(),
                          "switching workspace and inspector pages preserves a compact window size");
                }
            }
            if (auto *status = window.findChild<QLabel *>("mainStatus")) {
                const QString before = status->text();
                status->setText(QString(1000, QLatin1Char('W')));
                app.processEvents();
                check(window.width() <= 480 && window.height() <= 360,
                      "long output status cannot increase the window minimum size");
                status->setText(before);
            }
            auto *editor = window.findChild<QScrollArea *>("showEditorScroll");
            check(editor && editor->horizontalScrollBar()->maximum() > 0,
                  "wide show controls remain reachable through horizontal scrolling");
            if (editor) {
                editor->horizontalScrollBar()->setValue(editor->horizontalScrollBar()->maximum());
                check(editor->horizontalScrollBar()->value() == editor->horizontalScrollBar()->maximum(),
                      "the right edge of a wide editor is reachable in a tiled window");
            }
            // A saved window from a larger/disconnected screen must also recover.
            QWidget saved;
            saved.setGeometry(10000, 10000, 9000, 9000);
            window.hide();
            check(window.restoreGeometry(saved.saveGeometry()), "restore saved geometry from a larger screen");
            window.show();
            app.processEvents();
            check(window.screen()->availableGeometry().adjusted(-2, -2, 2, 2).contains(window.frameGeometry()),
                  "oversized restored geometry is brought back inside the available screen");
            window.resize(original);
            if (tabs) tabs->setCurrentIndex(0);
            if (mainTabs) mainTabs->setCurrentIndex(0);
            if (libraryTabs) libraryTabs->setCurrentIndex(1);
            app.processEvents();
        }
        if (tabs && pages && lists && inspector && sequences && timelines) {
            check(!sequences->isWindow() && !timelines->isWindow(), "editors embedded rather than floating");
            check(tabs->count() == 3 && tabs->tabText(0) == "Snapshots" && tabs->tabText(1) == "Presets" && tabs->tabText(2) == "Timelines", "three named workspace tabs");
            auto *library = window.findChild<QWidget *>("showLibraryColumn");
            check(library && tabs->parentWidget() == library, "tabs belong only to the library column");
            const QPoint inspectorOrigin = pages->mapTo(&window, QPoint());
            for (int tab : {0, 1, 2, 0, 2, 1}) {
                tabs->setCurrentIndex(tab); app.processEvents();
                check(pages->mapTo(&window, QPoint()) == inspectorOrigin, "inspection area stays in the same position");
                check(sequences->isVisible(), "sequence editor stays visible in all workspace modes");
                check(pages->currentIndex() == (tab == 2 ? 1 : 0), "workspace page follows tab");
                if (tab < 2) check(lists->currentIndex() == tab && inspector->currentIndex() == tab, "list and inspector follow Snapshots/Presets tab");
                else check(timelines->editor()->isVisible(), "timeline editor replaces inspector in timeline tab");
            }
            check(sequences->mapTo(&window, QPoint()).x() < tabs->mapTo(&window, QPoint()).x(), "sequences sit to the left of the tabbed workspace");
            auto *viewportList = window.findChild<QListWidget *>("viewportList");
            auto *viewportInspector = window.findChild<LayerInspector *>("viewportInspector");
            auto *mainTabs = window.findChild<QTabWidget *>("mainInspectorTabs");
            check(mainTabs && mainTabs->tabText(1) == "Viewports" && mainTabs->tabText(2) == "Composition", "Viewports sits between Layer and Composition");
            check(viewportList && viewportList->currentRow() == 0 && viewportInspector && viewportInspector->layerIndex() == engine.viewports().front(), "first viewport selected by default");
            {
                const quint64 viewport = engine.layerId(engine.viewports().front());
                QUndoStack colorUndo;
                ViewportOutputPanel panel(&engine, viewport, &colorUndo);
                auto *input = panel.findChild<QComboBox *>("outputDecode");
                auto *output = panel.findChild<QComboBox *>("outputEncode");
                auto *exposure = panel.findChild<QDoubleSpinBox *>("outputExposure");
                auto *reset = panel.findChild<QPushButton *>("resetOutputColor");
                check(input && output && exposure && reset, "viewport output exposes named input/output spaces and exposure");
                if (input && output && exposure && reset) {
                    output->setCurrentIndex(output->findData("displayp3"));
                    output->activated(output->currentIndex());
                    exposure->setValue(1);
                    check(engine.parameterValue(viewport,"output/space") == "displayp3" &&
                          engine.parameterValue(viewport,"output/exposure").toDouble() == 1,
                          "output color widgets write viewport parameters");
                    reset->click();
                    check(engine.parameterValue(viewport,"output/space") == "same" && exposure->value() == 0,
                          "one reset restores neutral output and refreshes the widgets");
                    colorUndo.undo();
                    panel.refreshStatus();
                    check(output->currentData() == "displayp3" && exposure->value() == 1,
                          "Undo restores color controls as well as the rendered settings");
                    colorUndo.undo(); colorUndo.undo();
                }
            }
            auto *update = window.findChild<QPushButton *>("snapshotUpdate");
            check(update && lists->isAncestorOf(update), "snapshot Update is in library toolbar");
            auto *remove = window.findChild<QPushButton *>("presetDelete");
            check(remove && lists->isAncestorOf(remove) && remove->text() == "−", "preset minus is in library toolbar");
            // Media Bin commands must target a source layer even after inspecting a viewport.
            auto *bin = window.findChild<MediaBin *>();
            QTemporaryDir shaderDir;
            const auto shaderPath = shaderDir.filePath("generator.fs");
            QFile shader(shaderPath);
            check(shader.open(QIODevice::WriteOnly), "create ISF fixture");
            shader.write("/*{\"ISFVSN\":\"2\",\"INPUTS\":[]}*/\nvoid main(){gl_FragColor=vec4(1.0);}"); shader.close();
            if (bin && mainTabs) {
                const int before = engine.layerCount();
                mainTabs->setCurrentIndex(1);
                bin->loadIntoNewLayerRequested(shaderPath);
                check(engine.layerCount() == before + 1, "ISF new-layer action creates a layer from Viewports tab");
                int loaded = -1;
                for (int i = 0; i < engine.layerCount(); ++i)
                    if (engine.layer(i)->sourcePath == shaderPath) loaded = i;
                check(loaded >= 0 && engine.layer(loaded)->type == SourceType::Isf, "new layer loads ISF generator");
                check(mainTabs->currentIndex() == 0, "source layer becomes selected");
                if (loaded >= 0) {
                    engine.setLayerText(loaded);
                    mainTabs->setCurrentIndex(1);
                    bin->useAsSourceRequested(shaderPath);
                    check(engine.layer(loaded)->type == SourceType::Isf, "selected-layer action replaces source with ISF");
                }
                // Media Bin filters: by type, used / unused; the rebuild button; Replace… for an ISF generator
                auto *tree = bin->findChild<QTreeWidget *>();
                check(tree && tree->columnCount() == 4 && tree->headerItem()->text(3) == "Color space",
                      "media bin has an explicit source color-space column");
                auto *type = bin->findChild<QComboBox *>("typeFilter");
                auto *usage = bin->findChild<QComboBox *>("usageFilter");
                auto *rebuild = bin->findChild<QToolButton *>("rebuild");
                check(tree && type && usage && rebuild, "media bin has type, usage filters and a rebuild button");
                auto itemOf = [&](const QString &path) -> QTreeWidgetItem * {
                    for (QTreeWidgetItemIterator it(tree); *it; ++it)
                        if ((*it)->data(0, Qt::UserRole + 1).toString() == QDir::cleanPath(path)) return *it;
                    return nullptr;
                };
                auto shown = [](QTreeWidgetItem *it) {
                    for (; it; it = it->parent()) if (it->isHidden()) return false;
                    return true;
                };
                if (tree && type && usage && rebuild && loaded >= 0) {
                    bin->refresh();
                    {
                        QTemporaryDir profiles;
                        const QString tagged = profiles.path() + "/tagged.png", untagged = profiles.path() + "/untagged.png";
                        QImage image(16,16,QImage::Format_RGBA64);
                        image.fill(Qt::gray);
                        check(image.save(untagged), "create untagged media-bin fixture");
                        image.setColorSpace(QColorSpace::DisplayP3);
                        check(image.save(tagged), "create Display P3 media-bin fixture");
                        engine.addBinItems({tagged,untagged}); bin->refresh();
                        QElapsedTimer wait; wait.start();
                        while(wait.elapsed()<5000) {
                            // Deliver queued probe results without repainting the whole workspace.
                            // Under Valgrind, unrelated render timers can consume the entire deadline
                            // in one processEvents() call, before the second result is delivered.
                            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
                            auto *a=itemOf(tagged), *b=itemOf(untagged);
                            if(a && b && a->text(3)!="…" && b->text(3)!="…") break;
                            QThread::msleep(1);
                        }
                        auto *a=itemOf(tagged), *b=itemOf(untagged);
                        if (!a || !b || a->text(3) == "…" || b->text(3) == "…")
                            std::printf("Media-bin probe timeout after %lld ms: tagged=%s, untagged=%s\n",
                                        static_cast<long long>(wait.elapsed()),
                                        a ? qPrintable(a->text(3)) : "missing", b ? qPrintable(b->text(3)) : "missing");
                        check(a && a->text(3).contains("P3") && a->toolTip(3).contains("primaries:"),
                              "media-bin async probe displays the embedded source profile and detailed primaries");
                        check(b && b->text(3).contains("Unknown"), "media-bin async probe keeps untagged source space unknown");
                        engine.removeBinItem(tagged); engine.removeBinItem(untagged); bin->refresh();
                    }
                    QTreeWidgetItem *gen = itemOf(shaderPath);
                    check(gen && shown(gen), "used ISF generator listed in the media bin");
                    usage->setCurrentIndex(2); // Unused
                    check(gen && !shown(gen), "Unused hides a generator a layer uses");
                    usage->setCurrentIndex(1); // Used
                    check(gen && shown(gen), "Used shows it");
                    // Two roots, Video and Audio; the kind filter picks Files, Generators, Inputs or ISF in them
                    QTreeWidgetItem *videoRoot = tree->topLevelItem(0), *audioRoot = tree->topLevelItem(1), *videos = nullptr, *isfTop = nullptr;
                    check(tree->topLevelItemCount() == 2 && videoRoot->text(0) == "Video" && audioRoot->text(0) == "Audio",
                          "the media bin has a Video root and an Audio root");
                    for (int c = 0; c < videoRoot->childCount(); ++c) {
                        if (videoRoot->child(c)->text(0).startsWith("Videos")) videos = videoRoot->child(c);
                        if (videoRoot->child(c)->text(0) == "ISF") isfTop = videoRoot->child(c);
                    }
                    auto *kind = bin->findChild<QComboBox *>("kindFilter");
                    check(videos && isfTop && kind, "Video holds Videos and ISF; the bin has a kind filter");
                    if (videos && isfTop && kind) {
                        usage->setCurrentIndex(0); // the use filter hides what is empty: not what these checks are about
                        kind->setCurrentIndex(kind->findText("ISF"));
                        check(videos->isHidden() && !isfTop->isHidden() && shown(gen), "kind filter ISF shows only ISF");
                        kind->setCurrentIndex(kind->findText("Files"));
                        check(!videos->isHidden() && isfTop->isHidden(), "kind filter Files hides the ISF");
                        kind->setCurrentIndex(0);
                        type->setCurrentIndex(type->findText("Audio"));
                        check(videoRoot->isHidden() && !audioRoot->isHidden(), "media filter Audio hides the Video root");
                        type->setCurrentIndex(type->findText("Video"));
                        check(!videoRoot->isHidden() && audioRoot->isHidden(), "media filter Video hides the Audio root");
                    }
                    type->setCurrentIndex(0);
                    usage->setCurrentIndex(0);
                    rebuild->click();
                    app.processEvents();
                    check(itemOf(shaderPath) && shown(itemOf(shaderPath)), "rebuild keeps the bin's items");
                    // Replace: the layer takes the other generator, one undo step brings the first back
                    const QString other = shaderDir.filePath("other.fs");
                    QFile o(other);
                    check(o.open(QIODevice::WriteOnly), "create second ISF fixture");
                    o.write("/*{\"ISFVSN\":\"2\",\"INPUTS\":[]}*/\nvoid main(){gl_FragColor=vec4(0.5);}"); o.close();
                    const quint64 id = engine.layerId(loaded);
                    bin->relinkRequested(QDir::cleanPath(shaderPath), other);
                    const int at = engine.indexOfId(id);
                    check(at >= 0 && engine.layer(at)->type == SourceType::Isf && engine.layer(at)->sourcePath == QDir::cleanPath(other),
                          "Replace swaps the ISF generator of the layers using it");
                    if (auto *u = window.findChild<QUndoStack *>()) u->undo();
                    check(at >= 0 && engine.layer(engine.indexOfId(id))->sourcePath == QDir::cleanPath(shaderPath), "Replace is undone");
                }
            }
            // A complete Spatial tab captures corner values and mesh in one undoable edit.
            auto *presets = PresetEditor::instance();
            auto *undo = window.findChild<QUndoStack *>();
            const int surface = engine.addLayer("Geometry surface");
            const quint64 surfaceId = engine.layerId(surface);
            Engine::Snapshot reference; reference.name = "Target cue"; reference.layers = engine.captureLayers();
            reference.composition = engine.captureComposition();
            const int snapshotIndex = engine.addSnapshot(reference);
            const quint64 snapshotId = engine.snapshot(snapshotIndex).id;
            auto *snapshotList = window.findChild<QTreeWidget *>("snapshotList");
            auto *snapshotRow = snapshotList && snapshotList->topLevelItemCount() ? snapshotList->topLevelItem(0) : nullptr;
            engine.recallSnapshot(snapshotIndex);
            check(snapshotRow && snapshotList->topLevelItem(0) == snapshotRow, "snapshot recall preserves library rows and parameter editors");
            auto *snapshotEditor = PresetEditor::instance();
            snapshotEditor->selectSnapshot(snapshotId);
            engine.setParameter(surfaceId, "opacity", .37);
            QMenu valueMenu;
            snapshotEditor->fillMenu(&valueMenu, surfaceId, {"opacity"});
            QAction *selectedUpdate = nullptr;
            QMenu *snapshotChoices = nullptr;
            for (auto *action : valueMenu.findChildren<QAction *>()) {
                if (action->text() == "Update in Selected Snapshot (Target cue)") selectedUpdate = action;
                if (action->text() == "Update in Snapshot") snapshotChoices = action->menu();
            }
            check(selectedUpdate && snapshotChoices && !snapshotChoices->actions().empty(), "snapshot update actions include selected name and target list");
            if (selectedUpdate) {
                const auto original = engine.snapshot(snapshotIndex).layers;
                selectedUpdate->trigger();
                const auto updated = engine.snapshot(snapshotIndex).layers;
                bool changed = false;
                for (int i = 0; i < updated.size(); ++i) {
                    auto a = original[i].toObject(), b = updated[i].toObject();
                    if (b.value("id").toString().toULongLong() != surfaceId) { check(a == b, "snapshot update preserves other layers"); continue; }
                    auto params = b.value("params").toObject();
                    check(std::abs(params.value("opacity").toDouble() - .37) < 1e-6, "selected snapshot receives live parameter value");
                    auto oldParams = a.value("params").toObject(); oldParams["opacity"] = params.value("opacity"); a["params"] = oldParams;
                    check(a == b, "snapshot update preserves every unrelated field"); changed = true;
                }
                check(changed, "target layer found in updated snapshot");
                window.findChild<QUndoStack *>()->undo();
                check(engine.snapshot(snapshotIndex).layers == original, "partial snapshot update is undoable");
            }

            engine.layer(surface)->mapping.resetMesh(5, 3);
            engine.layer(surface)->mapping.offsets[2] = QPointF(.1, -.2);
            ++engine.layer(surface)->mapping.revision;
            QMenu category;
            presets->fillCategoryMenu(&category, surfaceId, "Spatial");
            check(!category.actions().empty(), "Spatial tab offers preset capture");
            for (auto *action : category.findChildren<QAction *>())
                if (action->text() == "Add to New Preset") { action->trigger(); break; }
            const quint64 presetId = presets->selected();
            int presetIndex = engine.indexOfPreset(presetId);
            check(presetIndex >= 0 && engine.preset(presetIndex).geometry.contains(QString::number(surfaceId)), "Spatial capture includes mesh");
            check(presetIndex >= 0 && engine.preset(presetIndex).indexOf(surfaceId, "corner/top_left/x") >= 0, "Spatial capture includes four-corner geometry");
            if (presetIndex >= 0 && undo) {
                auto value = engine.preset(presetIndex).values.front();
                cmd::editPresets(undo, &engine, "Delay", [&](std::vector<Engine::Preset> &all) { all[size_t(presetIndex)].values.front().delay = 2; });
                check(engine.preset(presetIndex).values.front().delay == 2, "delay-only preset edit is retained");
                undo->undo(); check(engine.preset(presetIndex).values.front().delay == value.delay, "delay-only edit can be undone");
                engine.layer(surface)->mapping.resetMesh(7, 7);
                undo->push(new cmd::RecallPreset(&engine, presetId));
                check(engine.layer(surface)->mapping.cols == 5, "preset PLAY restores stored mesh");
                undo->undo(); check(engine.layer(surface)->mapping.cols == 7, "undo restores previous mesh");
                undo->redo(); check(engine.layer(surface)->mapping.cols == 5, "redo restores stored mesh again");
                Interaction driver = Interaction::made(InteractionSource::Osc);
                driver.layer = surfaceId; driver.param = "opacity"; driver.name = "Surface fader";
                const quint64 driverId = engine.addInteraction(driver);
                presets->addInteraction(driverId, false);
                check(engine.preset(engine.indexOfPreset(presetId)).interactions.size() == 1, "interaction-only edit is retained in selected preset");
                undo->undo(); check(engine.preset(engine.indexOfPreset(presetId)).interactions.empty(), "interaction capture is undoable");
                // Undo must cancel a delayed value; it must never return after its deadline.
                Engine::Preset delayed; delayed.name = "Delayed test"; delayed.fade = 0;
                auto target = engine.presetValue(surfaceId, "opacity"); target.value = .1; target.delay = 1;
                delayed.values = {target}; const int di = engine.addPreset(delayed);
                engine.setParameter(surfaceId, "opacity", .8);
                undo->push(new cmd::RecallPreset(&engine, engine.preset(di).id)); undo->undo();
                engine.advanceFades(2);
                check(std::abs(engine.parameterValue(surfaceId, "opacity").toDouble() - .8) < .01, "undone delayed recall stays canceled after deadline");
            }
            tabs->setCurrentIndex(0); app.processEvents();
            if (!capture.isEmpty()) check(window.grab().save(capture), "save workspace capture");
        }
        // The controller is a direct MainWindow child and may die before the reparented lists.
        delete window.findChild<PresetEditor *>();
        if (lists) {
            if (auto *presetList = lists->widget(1)->findChild<QTreeWidget *>())
                presetList->setCurrentItem(nullptr);
        }
        app.processEvents();
        window.hide();
    }
    // An opened project may put another layer at the index shown: the inspector follows the layer, not the index
    if (selection) {
        engine.newProject();
        const QString solid = QStringLiteral(TEST_DIR) + "/../isf/generators/SolidColor.fs";
        engine.addViewport("Front", QSize(640, 360));
        for (const char *name : {"Under", "Over"}) engine.setLayerIsf(engine.addLayer(name), solid, &error);
        QTemporaryDir dir;
        const QString project = dir.path() + "/reopen.nyxmap";
        check(engine.saveProject(project, {}, &error), "save project to reopen");
        MainWindow window(&engine); window.setAutosaveEnabled(false); window.setQuiet(true);
        window.show(); app.processEvents();
        check(window.openProject(project, false), "reopen project");
        app.processEvents();
        LayerInspector *inspector = nullptr;
        for (LayerInspector *candidate : window.findChildren<LayerInspector *>())
            if (candidate->objectName() != "viewportInspector") inspector = candidate;
        check(inspector && inspector->layerIndex() >= 0 &&
                  inspector->layerId() == engine.layerId(inspector->layerIndex()),
              "inspector edits the layer it shows after a project is opened");
        if (auto *opacity = inspector ? inspector->findChild<SliderField *>("inspectorOpacity") : nullptr) {
            emit opacity->valueEdited(30);
            check(std::abs(engine.parameterValue(engine.layerId(inspector->layerIndex()), "opacity").toDouble() - .3) < 1e-6,
                  "inspector edit after opening a project reaches the layer");
        }
        window.hide();
    }
    // A multiple selection: the inspector's edits, the list's opacity bars and Paste Attributes reach all of it
    if (selection) {
        engine.newProject();
        const QString solid = QStringLiteral(TEST_DIR) + "/../isf/generators/SolidColor.fs";
        QList<quint64> ids;
        for (const char *name : {"A", "B", "C"}) {
            const int i = engine.addLayer(name);
            engine.setLayerIsf(i, solid, &error);
            ids << engine.layerId(i);
        }
        MainWindow window(&engine); window.setAutosaveEnabled(false); window.setQuiet(true);
        window.show(); app.processEvents();
        auto *table = qobject_cast<QTableWidget *>(window.findChild<LayerTable *>()->table());
        auto *undo = window.findChild<QUndoStack *>();
        LayerInspector *inspector = nullptr;
        for (LayerInspector *candidate : window.findChildren<LayerInspector *>())
            if (candidate->objectName() != "viewportInspector") inspector = candidate;
        auto opacityOf = [&](quint64 id) { return engine.parameterValue(id, "opacity").toDouble(); };
        check(table && undo && inspector, "layer list, undo stack and inspector present");
        if (table && undo && inspector) {
            const int a = engine.indexOfId(ids[0]), b = engine.indexOfId(ids[1]), c = engine.indexOfId(ids[2]);
            table->clearSelection();
            table->setCurrentCell(a, 3);
            table->selectionModel()->select(table->model()->index(c, 0),
                                            QItemSelectionModel::Select | QItemSelectionModel::Rows);
            app.processEvents();
            check(inspector->coTargets() == QList<quint64>{ids[2]}, "inspector knows the other selected layer");
            auto *note = inspector->findChild<QLabel *>("multiSelectionNote");
            check(note && note->isVisible(), "inspector says the edit applies to the whole selection");
            auto *sourceSpace = inspector->findChild<QComboBox *>("sourceColorSpace");
            check(sourceSpace && sourceSpace->findData("displayp3") >= 0, "source interpretation control is present");
            if (sourceSpace) {
                const int p3 = sourceSpace->findData("displayp3");
                sourceSpace->setCurrentIndex(p3);
                QMetaObject::invokeMethod(sourceSpace, "activated", Qt::DirectConnection, Q_ARG(int, p3));
                check(engine.parameterValue(ids[0], "source/color_space") == "displayp3" &&
                      engine.parameterValue(ids[2], "source/color_space") == "displayp3" &&
                      engine.parameterValue(ids[1], "source/color_space") == "auto",
                      "source declaration reaches the selection only");
                undo->undo();
                check(engine.parameterValue(ids[0], "source/color_space") == "auto" &&
                      engine.parameterValue(ids[2], "source/color_space") == "auto", "one Undo restores source interpretation");
                app.processEvents();
                undo->clear(); // the next scenario starts with no redo branch
            }
            auto *opacity = inspector->findChild<SliderField *>("inspectorOpacity");
            check(opacity != nullptr, "inspector opacity field present");
            if (opacity) {
                const int steps = undo->count();
                emit opacity->valueEdited(40);
                check(std::abs(opacityOf(ids[0]) - .4) < 1e-6 && std::abs(opacityOf(ids[2]) - .4) < 1e-6,
                      "inspector opacity edit reaches every selected layer");
                check(std::abs(opacityOf(ids[1]) - 1) < 1e-6, "an unselected layer keeps its value");
                check(undo->count() == steps + 1, "one undo step for the whole selection");
                undo->undo();
                check(std::abs(opacityOf(ids[0]) - 1) < 1e-6 && std::abs(opacityOf(ids[2]) - 1) < 1e-6,
                      "undo restores every selected layer");
            }
            // The list's own opacity bar on a selected row
            if (auto *cell = table->cellWidget(c, 5); auto *bar = cell ? cell->findChild<SliderField *>() : nullptr) {
                emit bar->valueEdited(25);
                check(std::abs(opacityOf(ids[0]) - .25) < 1e-6 && std::abs(opacityOf(ids[2]) - .25) < 1e-6,
                      "list opacity bar of a selected row sets the whole selection");
                undo->undo();
            } else {
                check(false, "list opacity bar is the application's SliderField");
            }
            // Paste Attributes onto the selection: B's compositing copied onto A and C
            engine.setParameter(ids[1], "opacity", .6);
            QAction *copy = nullptr, *pasteAll = nullptr;
            for (QAction *act : window.findChildren<QAction *>()) {
                if (act->text() == "Copy Attributes") copy = act;
                if (act->text() == "Paste All Attributes") pasteAll = act;
            }
            check(copy && pasteAll, "Copy / Paste All Attributes actions present");
            if (copy && pasteAll) {
                table->clearSelection();
                table->setCurrentCell(b, 3); app.processEvents();
                copy->trigger();
                table->clearSelection();
                table->setCurrentCell(a, 3);
                table->selectionModel()->select(table->model()->index(c, 0),
                                                QItemSelectionModel::Select | QItemSelectionModel::Rows);
                app.processEvents();
                pasteAll->trigger();
                check(std::abs(opacityOf(ids[0]) - .6) < 1e-6 && std::abs(opacityOf(ids[2]) - .6) < 1e-6,
                      "paste reaches every selected layer");
            }
            // Viewports: several selected in their list, pasted onto together and edited together
            auto *tabs = window.findChild<QTabWidget *>("mainInspectorTabs");
            auto *viewportList = window.findChild<QListWidget *>("viewportList");
            auto *viewportInspector = window.findChild<LayerInspector *>("viewportInspector");
            const quint64 v1 = engine.layerId(engine.addViewport("V1")), v2 = engine.layerId(engine.addViewport("V2")),
                          v3 = engine.layerId(engine.addViewport("V3"));
            if (tabs) tabs->setCurrentIndex(1);
            app.processEvents();
            check(viewportList && viewportList->selectionMode() == QAbstractItemView::ExtendedSelection,
                  "viewport list takes a multiple selection");
            if (tabs && viewportList && viewportInspector && copy && pasteAll) {
                auto itemOf = [&](quint64 id) -> QListWidgetItem * {
                    for (int k = 0; k < viewportList->count(); ++k)
                        if (viewportList->item(k)->data(Qt::UserRole).toULongLong() == id) return viewportList->item(k);
                    return nullptr;
                };
                engine.setParameter(v1, "opacity", .3);
                viewportList->setCurrentItem(itemOf(v1)); app.processEvents();
                copy->trigger();
                viewportList->setCurrentItem(itemOf(v2));
                if (QListWidgetItem *item = itemOf(v3)) item->setSelected(true);
                app.processEvents();
                check(viewportInspector->coTargets() == QList<quint64>{v3}, "viewport inspector knows the selection");
                pasteAll->trigger();
                check(std::abs(opacityOf(v2) - .3) < 1e-6 && std::abs(opacityOf(v3) - .3) < 1e-6,
                      "paste reaches every selected viewport");
                if (auto *vo = viewportInspector->findChild<SliderField *>("inspectorOpacity")) {
                    emit vo->valueEdited(70);
                    check(std::abs(opacityOf(v2) - .7) < 1e-6 && std::abs(opacityOf(v3) - .7) < 1e-6 &&
                              std::abs(opacityOf(v1) - .3) < 1e-6,
                          "viewport inspector edit reaches every selected viewport only");
                }
            }
        }
        window.hide();
    }
    engine.shutdown();
    return failures ? 1 : 0;
}
