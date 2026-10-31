#include "Commands.h"
#include "Engine.h"
#include "PresetEditor.h"
#include "SequencePanel.h"
#include "SnapshotPanel.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QUndoStack>
#include <iostream>
#include <cmath>
#include <stdexcept>
static void check(bool ok, const char *why)
{
    if (!ok)
        throw std::runtime_error(why);
}
static void shortcut(QWidget *widget, const QKeySequence &key)
{
    for (auto *shortcut : widget->findChildren<QShortcut *>(QString(), Qt::FindDirectChildrenOnly))
        if (shortcut->key() == key) {
            emit shortcut->activated();
            return;
        }
    throw std::runtime_error("shortcut missing");
}
static QTreeWidgetItem *find(QTreeWidget *tree, const QString &tooltip)
{
    QTreeWidgetItemIterator it(tree);
    for (; *it; ++it)
        if ((*it)->toolTip(0) == tooltip)
            return *it;
    return nullptr;
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    Engine engine;
    QUndoStack undo;
    try {
        const int layer = engine.addLayer("Layer");
        engine.layer(layer)->type = SourceType::Text;
        const auto id = engine.layerId(layer);
        Engine::Snapshot first, second;
        first.name = "First";
        first.layers = engine.captureLayers();
        second.name = "Second";
        engine.addSnapshot(first);
        engine.addSnapshot(second);
        Engine::Preset preset;
        preset.name = "Preset";
        const auto pi = engine.addPreset(preset);
        PresetEditor editor(&engine, &undo);
        SnapshotPanel panel(&engine, &undo);
        panel.resize(1000, 650);
        panel.show();
        app.processEvents();
        auto *library = panel.listPanel()->findChild<QTreeWidget *>("snapshotList"),
             *values = panel.findChild<QTreeWidget *>("snapshotValues"),
             *presets = panel.listPanel()->findChild<QTreeWidget *>("presetList"),
             *presetValues = panel.findChild<QTreeWidget *>("presetValues");
        check(library && values && presets && presetValues, "stored-content trees present");
        library->setCurrentItem(library->topLevelItem(0));
        auto *opacity = find(values, "opacity");
        check(opacity, "stored opacity row");
        values->setCurrentItem(opacity);
        shortcut(values, QKeySequence(QKeySequence::Copy));
        library->setCurrentItem(library->topLevelItem(1));
        shortcut(values, QKeySequence(QKeySequence::Paste));
        check(engine.snapshot(1).partial && engine.snapshot(1).layers.size() == 1,
              "paste into empty snapshot is partial");
        check(engine.snapshot(1).layers[0].toObject().value("params").toObject().size() == 1,
              "only copied value pasted");
        undo.undo();
        check(engine.snapshot(1).layers.isEmpty(), "snapshot paste undoes");
        undo.redo();
        check(engine.snapshot(1).layers.size() == 1, "snapshot paste redoes");
        engine.setParameter(id, "opacity", .33);
        panel.findChild<QPushButton *>("snapshotUpdate")->click();
        check(engine.snapshot(1).layers[0].toObject().value("params").toObject().size() == 1 &&
                  std::abs(engine.snapshot(1).layers[0].toObject().value("params").toObject().value("opacity").toDouble() - .33) < 1e-5,
              "partial snapshot update keeps deleted contents out");
        undo.undo();
        panel.findChild<QPushButton *>("snapshotDuplicate")->click();
        check(engine.snapshotCount() == 3 && engine.snapshot(1).id != engine.snapshot(2).id,
              "snapshot duplicate has new identity");
        const auto duplicateId = engine.snapshot(2).id;
        undo.undo();
        check(engine.snapshotCount() == 2, "duplicate undo");
        undo.redo();
        check(engine.snapshot(2).id == duplicateId, "duplicate redo retains identity");
        presets->setCurrentItem(presets->topLevelItem(pi));
        panel.setMode(1);
        shortcut(presetValues, QKeySequence(QKeySequence::Paste));
        check(engine.preset(pi).indexOf(id, "opacity") >= 0, "copy snapshot value into preset");
        presetValues->setCurrentItem(presetValues->topLevelItem(0));
        shortcut(presetValues, QKeySequence(QKeySequence::Delete));
        check(engine.preset(pi).values.empty(), "delete preset owner removes stored contents");
        undo.undo();
        check(!engine.preset(pi).values.empty(), "preset removal undoes");
        panel.findChild<QPushButton *>("presetDuplicate")->click();
        check(engine.presetCount() == 2 && engine.preset(0).id != engine.preset(1).id,
              "preset duplicate has fresh identity");
        Engine::Preset visibility;
        visibility.values.push_back({id, "enable", true, 0, QString()});
        const int vi = engine.addPreset(visibility);
        engine.setParameter(id, "enable", false);
        undo.push(new cmd::RecallPreset(&engine, engine.preset(vi).id));
        check(engine.layer(engine.indexOfId(id))->enabled, "copied visibility can be recalled");
        undo.undo();
        check(!engine.layer(engine.indexOfId(id))->enabled, "visibility recall undo restores transient state");
        // Sequence multi-selection, groups, clipboard, and editing mode all operate through the actual UI.
        Sequencer::Step a, b, c;
        a.snapshot = engine.snapshot(0).id;
        a.at = .2;
        b.preset = engine.preset(0).id;
        c.text = "Third";
        Sequencer::Sequence sequence;
        sequence.steps = {a, b, c};
        const int qi = engine.sequencer().add(sequence);
        engine.sequencer().setCurrent(qi);
        SequenceWindow window(&engine, nullptr, &undo);
        window.show();
        auto *steps = window.findChild<QTableWidget *>("sequenceSteps");
        check(steps, "sequence table");
        steps->setCurrentCell(0, 1);
        window.findChild<QPushButton *>("sequenceDuplicateSteps")->click();
        auto edited = engine.sequencer().sequence(qi);
        check(edited.steps.size() == 4 && edited.steps[0].runtimeId != edited.steps[1].runtimeId &&
                  edited.steps[1].at < 0,
              "duplicate cue keeps fresh identity and clears duplicate timecode");
        undo.undo();
        check(engine.sequencer().sequence(qi).steps.size() == 3, "cue duplication undo");
        steps->clearSelection();
        steps->setRangeSelected(QTableWidgetSelectionRange(0, 0, 1, steps->columnCount() - 1), true);
        window.findChild<QPushButton *>("sequenceGroupSteps")->click();
        edited = engine.sequencer().sequence(qi);
        check(edited.steps.size() == 2 && edited.steps[0].actions.size() == 2, "selected cues become one action group");
        QMenu groupMenu;
        window.fillStepMenu(&groupMenu, 0);
        QAction *groupEditor = nullptr;
        for (auto *action : groupMenu.actions())
            if (action->text() == "Edit action group…")
                groupEditor = action;
        check(groupEditor, "action group editor menu");
        bool sawGroup = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (dialog) {
                auto *actions = dialog->findChild<QListWidget *>("cueGroupActions");
                sawGroup = actions && actions->count() == 2;
                dialog->reject();
            }
        });
        groupEditor->trigger();
        check(sawGroup && engine.sequencer().sequence(qi).steps[0].actions.size() == 2,
              "group editor previews actions and Cancel leaves definitions intact");
        undo.undo();
        check(engine.sequencer().sequence(qi).steps.size() == 3, "grouping undo");
        steps->setCurrentCell(0, 1);
        shortcut(steps, QKeySequence(QKeySequence::Copy));
        steps->setCurrentCell(2, 1);
        shortcut(steps, QKeySequence(QKeySequence::Paste));
        check(engine.sequencer().sequence(qi).steps.size() == 4, "cue clipboard paste");
        shortcut(steps, QKeySequence(QKeySequence::Delete));
        check(engine.sequencer().sequence(qi).steps.size() == 3, "cue keyboard delete");
        undo.undo();
        check(engine.sequencer().sequence(qi).steps.size() == 4, "cue delete undo");
        auto *editMode = window.findChild<QCheckBox *>("sequenceEditMode");
        check(editMode, "timecode edit control");
        editMode->setChecked(true);
        check(engine.sequencer().sequence(qi).editMode, "timecode edit control persists");
        undo.undo();
        check(!engine.sequencer().sequence(qi).editMode, "edit mode undo");
        std::cout << "Snapshot/preset transfer, removal, duplication, undo; sequence clipboard, duplicate, group and "
                     "edit controls passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
