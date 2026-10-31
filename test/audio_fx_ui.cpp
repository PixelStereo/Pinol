// The Audio FX tab: the FX tab's mechanism (a list with a switch per row, Add, − ▲ ▼, the parameters of the selected FX)
// for the sound of a layer, each edit one undo step; and the Media Bin's Audio root (Sounds, Generators, Inputs).
#include "AudioCapture.h"
#include "AudioEffects.h"
#include "Engine.h"
#include "FxChainPanel.h"
#include "LayerInspector.h"
#include "MediaBin.h"
#include "Widgets.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoStack>
#include <cstdio>

int runAudioFxUiTests(Engine &engine)
{
    int failures = 0;
    auto check = [&](bool ok, const char *reason) {
        if (!ok) { ++failures; std::printf("FAIL audio_fx_ui: %s\n", reason); }
    };
    auto pump = [] {
        for (int k = 0; k < 3; ++k) {
            qApp->processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    };
    engine.setFadesManual(true);
    QTemporaryDir dir;
    const QString recipe = dir.filePath("instrument.nyxaudio");
    {
        QFile f(recipe);
        if (!f.open(QIODevice::WriteOnly)) { check(false, "recipe fixture"); return failures; }
        f.write("{\"version\":1,\"type\":\"fm\"}");
    }
    QString error;
    const int index = engine.addLayer("Instrument");
    const quint64 id = engine.layerId(index);
    check(engine.setLayerAudio(index, recipe, &error), "load a Faust instrument as a layer");
    check(!engine.setLayerAudio(index, audioInputUri("No such input device"), &error) && !error.isEmpty(),
          "an input that is not there is refused with a reason, the layer keeps its sound");
    check(!engine.setLayerFile(index, liveInputPath(audioInputUri("No such input device")), &error),
          "a dragged audio input is routed to the sound loader");
    check(engine.layer(index)->type == SourceType::Audio && AudioGenerator::isRecipe(engine.layer(index)->sourcePath), "still the instrument");

    QUndoStack undo;
    LayerInspector inspector(&engine, &undo);
    inspector.resize(700, 1400);
    inspector.setLayer(engine.indexOfId(id));
    inspector.show();
    inspector.rebuild();
    pump();

    QTabWidget *tabs = nullptr;
    for (auto *t : inspector.findChildren<QTabWidget *>("layerCategoryTabs")) tabs = t;
    int audioTab = -1, fxTab = -1;
    for (int k = 0; tabs && k < tabs->count(); ++k) {
        if (tabs->tabText(k) == "Audio FX") audioTab = k;
        if (tabs->tabText(k) == "FX") fxTab = k;
    }
    check(audioTab >= 0 && tabs->isTabEnabled(audioTab) && tabs->isTabVisible(audioTab), "a sound layer has an Audio FX tab");
    check(fxTab < 0 || !tabs->isTabVisible(fxTab), "no picture FX tab on a sound layer");
    if (audioTab < 0) return failures;
    tabs->setCurrentIndex(audioTab);
    pump();
    auto panel = [&]() -> FxChainPanel * {
        for (auto *p : inspector.findChildren<FxChainPanel *>("audioFXPanel")) return p;
        return nullptr;
    };
    FxChainPanel *fx = panel();
    check(fx && fx->count() == 0 && !fx->removeButton()->isEnabled() && fx->addButton()->isEnabled(), "an empty chain: Add only");
    check(fx && fx->masterSwitch() && fx->masterSwitch()->isChecked(), "the chain starts on");

    // The chain, as the engine holds it (what Add… builds)
    auto names = [&] {
        QStringList out;
        for (const auto &e : engine.audioEffects(id)) out << e->kind();
        return out;
    };
    check(engine.setAudioEffectsJson(engine.indexOfId(id), QJsonArray{QJsonObject{{"kind", "lowpass"}}, QJsonObject{{"kind", "delay"}},
                                                                    QJsonObject{{"kind", "reverb"}}}, &error),
          "three FX");
    inspector.rebuild();
    pump();
    fx = panel();
    check(fx && fx->count() == 3 && fx->list()->item(0)->text() == "Low-pass filter" && fx->list()->item(1)->text() == "Delay", "the list shows the chain in order");
    check(fx && fx->selected() == 0 && !fx->upButton()->isEnabled() && fx->downButton()->isEnabled(), "first FX selected: no Up");

    // ▼ : one undo step
    emit fx->moveRequested(0, 1);
    pump();
    check(names() == QStringList({"delay", "lowpass", "reverb"}), "▼ moves the FX later in the chain");
    undo.undo();
    pump();
    check(names() == QStringList({"lowpass", "delay", "reverb"}), "one undo brings it back");
    undo.redo();
    pump();
    check(names() == QStringList({"delay", "lowpass", "reverb"}), "and redo again");
    undo.undo();
    pump();

    // The row's switch bypasses one FX
    fx = panel();
    emit fx->toggled(1, false);
    pump();
    check(!engine.audioEffects(id)[1]->enabled() && engine.audioEffects(id)[0]->enabled(), "a row's switch bypasses that FX only");
    undo.undo();
    pump();
    check(engine.audioEffects(id)[1]->enabled(), "bypass is undone");

    // The master switch
    fx = panel();
    fx->masterSwitch()->setChecked(false);
    pump();
    bool chainOn = true;
    {
        Engine::Lock lk(&engine.mutex());
        chainOn = engine.layer(engine.indexOfId(id))->audioEffects->enabled();
    }
    check(!chainOn, "the master switch turns the chain off");
    undo.undo();
    pump();
    {
        Engine::Lock lk(&engine.mutex());
        chainOn = engine.layer(engine.indexOfId(id))->audioEffects->enabled();
    }
    check(chainOn, "and back on with undo");

    // Remove the selected one
    fx = panel();
    emit fx->removeRequested(2);
    pump();
    check(names() == QStringList({"lowpass", "delay"}), "− removes the FX");
    undo.undo();
    pump();
    check(names() == QStringList({"lowpass", "delay", "reverb"}), "and undo puts it back with its settings");

    // The parameters of the selected FX are swapped in place; the levels are there
    fx = panel();
    if (const QByteArray shots = qgetenv("NYXMAP_TEST_SHOTS"); !shots.isEmpty()) // pictures of the tab, for a look
        inspector.grab().save(QString::fromLocal8Bit(shots) + "/audio-fx-tab.png");
    check(!inspector.findChildren<LevelMeter *>().isEmpty(), "the tab shows levels");
    int meters = 0;
    for (auto *m : inspector.findChildren<LevelMeter *>()) if (m->isVisible()) ++meters;
    check(meters >= 3, "source, selected FX and output levels");
    LevelMeter probe("t");
    probe.setLevels(1.f, .0f);
    check(probe.left() == 1.f && probe.right() == 0.f && LevelMeter::position(0) > LevelMeter::position(-18) &&
              LevelMeter::position(-60) == 0 && LevelMeter::position(40) == 1,
          "the meter's scale runs from -60 to +6 dBFS");

    // A Video Sonification instrument chooses the viewport it listens to (one undo step)
    {
        const QString visual = dir.filePath("visual.nyxaudio");
        {
            QFile f(visual);
            if (!f.open(QIODevice::WriteOnly)) { check(false, "visual recipe"); return failures; }
            f.write("{\"version\":1,\"type\":\"video\"}");
        }
        const int listener = engine.addLayer("Listener");
        const quint64 listenerId = engine.layerId(listener);
        check(engine.setLayerAudio(engine.indexOfId(listenerId), visual, &error), "load the sonification instrument");
        const int second = engine.addViewport("Second", QSize(64, 64));
        const quint64 secondId = engine.layerId(second);
        inspector.setLayer(engine.indexOfId(listenerId));
        inspector.rebuild();
        pump();
        QComboBox *pick = nullptr;
        for (auto *c : inspector.findChildren<QComboBox *>("sonificationViewport")) pick = c;
        check(pick && pick->count() >= 3 && pick->currentIndex() == 0, "the instrument offers its viewports, the first by default");
        const int k = pick ? pick->findData(QVariant::fromValue<qulonglong>(secondId)) : -1;
        check(k > 0, "the second viewport is offered");
        if (pick && k > 0) {
            pick->setCurrentIndex(k);
            emit pick->activated(k);
            pump();
            check(engine.layerSonificationViewport(engine.indexOfId(listenerId)) == secondId, "the choice reaches the engine");
            undo.undo();
            pump();
            check(engine.layerSonificationViewport(engine.indexOfId(listenerId)) == 0, "and is undone");
        }
        inspector.setLayer(engine.indexOfId(id));
        inspector.rebuild();
        pump();
    }

    // The menu at the top of the inspector: every layer, group and viewport; picking one asks for it, ◀ ▶ step
    {
        const int grouped = engine.addLayer("In the group");
        const quint64 member = engine.layerId(grouped);
        const int g = engine.addGroup("The Group");
        const quint64 groupId = engine.layerId(g);
        LayerTree tree = engine.structure();
        for (TreeNode &n : tree)
            if (n.id == member) n.parent = groupId;
        engine.setStructure(tree);
        inspector.setLayer(engine.indexOfId(id));
        inspector.rebuild();
        pump();
        QComboBox *picker = nullptr;
        for (auto *c : inspector.findChildren<QComboBox *>("layerPicker")) picker = c;
        check(picker != nullptr, "the inspector has a menu at its top");
        if (picker) {
            check(picker->count() == engine.layerCount(), "it lists every layer, group and viewport");
            check(picker->itemText(picker->currentIndex()).contains("Instrument") && picker->itemData(picker->currentIndex()).toULongLong() == id,
                  "its current item is the layer inspected");
            bool viewportsLast = true, seenViewport = false;
            for (int k = 0; k < picker->count(); ++k) {
                const bool isViewport = picker->itemText(k).contains("◫");
                if (seenViewport && !isViewport) viewportsLast = false;
                seenViewport |= isViewport;
            }
            check(viewportsLast, "the viewports come last");
            bool indented = false;
            for (int k = 0; k < picker->count(); ++k)
                if (picker->itemText(k).contains("In the group")) indented = picker->itemText(k).startsWith(QChar(0x2003));
            check(indented, "a group's member is indented under it");
            quint64 asked = 0;
            QObject::connect(&inspector, &LayerInspector::layerPickRequested, &inspector, [&](quint64 picked) { asked = picked; });
            int other = picker->currentIndex() == 0 ? 1 : 0;
            emit picker->activated(other);
            check(asked == picker->itemData(other).toULongLong(), "picking an item asks for that layer");
            asked = 0;
            emit picker->activated(picker->currentIndex());
            check(asked == 0, "picking the one already shown asks for nothing");
            auto *next = inspector.findChild<QToolButton *>("layerPickerNext");
            auto *prev = inspector.findChild<QToolButton *>("layerPickerPrev");
            check(next && prev, "arrows to step");
            if (next && prev) {
                const int here = picker->currentIndex();
                if (next->isEnabled()) {
                    next->click();
                    check(asked == picker->itemData(here + 1).toULongLong(), "▶ asks for the one after");
                }
                if (prev->isEnabled()) {
                    prev->click();
                    check(asked == picker->itemData(here - 1).toULongLong(), "◀ asks for the one before");
                }
            }
            // A layer renamed by something else shows its new name
            {
                Engine::Lock lk(&engine.mutex());
                engine.layer(engine.indexOfId(id))->name = "Renamed elsewhere";
            }
            inspector.refreshDynamic();
            check(picker->itemText(picker->currentIndex()).contains("Renamed elsewhere"), "a rename elsewhere reaches the menu");
            {
                Engine::Lock lk(&engine.mutex());
                engine.layer(engine.indexOfId(id))->name = "Instrument";
            }
            inspector.refreshDynamic();
            if (const QByteArray shots = qgetenv("NYXMAP_TEST_SHOTS"); !shots.isEmpty()) inspector.grab().save(QString::fromLocal8Bit(shots) + "/inspector-picker.png");
        }
        engine.removeLayer(engine.indexOfId(groupId));
        engine.removeLayer(engine.indexOfId(member));
    }

    // Media Bin: Video and Audio roots
    {
        MediaBin bin(&engine);
        bin.resize(900, 500);
        bin.show();
        bin.refresh();
        pump();
        auto *tree = bin.findChild<QTreeWidget *>("mediaBinTree");
        if (const QByteArray shots = qgetenv("NYXMAP_TEST_SHOTS"); !shots.isEmpty())
            bin.grab().save(QString::fromLocal8Bit(shots) + "/media-bin.png");
        check(tree && tree->topLevelItemCount() == 2 && tree->topLevelItem(0)->text(0) == "Video" && tree->topLevelItem(1)->text(0) == "Audio",
              "the bin has a Video root and an Audio root");
        QTreeWidgetItem *audio = tree ? tree->topLevelItem(1) : nullptr, *generators = nullptr, *inputs = nullptr, *sounds = nullptr;
        for (int c = 0; audio && c < audio->childCount(); ++c) {
            const QString t = audio->child(c)->text(0);
            if (t.startsWith("Generators")) generators = audio->child(c);
            if (t.startsWith("Inputs")) inputs = audio->child(c);
            if (t.startsWith("Sounds")) sounds = audio->child(c);
        }
        check(generators && inputs && sounds, "Audio holds Sounds, Generators and Inputs");
        bool listed = false;
        for (int c = 0; generators && c < generators->childCount(); ++c)
            if (generators->child(c)->data(0, Qt::UserRole + 1).toString() == QDir::cleanPath(recipe)) {
                listed = true;
                check(generators->child(c)->text(2) == "1", "the instrument is listed with the layer that uses it");
            }
        check(listed, "an instrument in use is listed under Audio > Generators");
        check(sounds && sounds->childCount() == 0, "and not among the sound files");
        auto *kind = bin.findChild<QComboBox *>("kindFilter");
        auto *media = bin.findChild<QComboBox *>("typeFilter");
        check(kind && media, "kind and media filters");
        if (kind && media && audio && inputs && generators) {
            kind->setCurrentIndex(kind->findText("Inputs"));
            check(!inputs->isHidden() && generators->isHidden(), "kind Inputs shows the inputs of both roots only");
            kind->setCurrentIndex(0);
            media->setCurrentIndex(media->findText("Audio"));
            check(tree->topLevelItem(0)->isHidden() && !audio->isHidden(), "media Audio shows the Audio root only");
            media->setCurrentIndex(0);
        }
    }
    return failures;
}
