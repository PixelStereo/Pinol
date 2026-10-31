// The Controls tab: new controls from the menu, played, moved by something else, shaped, deleted — each edit one undo step.
#include "ControlsPanel.h"
#include "Engine.h"
#include "Widgets.h"
#include <QApplication>
#include <QComboBox>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QUndoStack>
#include <cstdio>

int runControlsUiTests(Engine &engine)
{
    int failures = 0;
    auto check = [&](bool ok, const char *reason) {
        if (!ok) { ++failures; std::printf("FAIL controls_ui: %s\n", reason); }
    };
    auto pump = [] {
        for (int k = 0; k < 3; ++k) {
            qApp->processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    };
    QUndoStack undo;
    ControlsPanel panel(&engine, &undo);
    panel.resize(420, 520);
    panel.show();
    pump();
    check(engine.controls().empty() && panel.findChildren<QWidget *>("controlRow").isEmpty(), "no control yet");
    auto *add = panel.findChild<QToolButton *>("controlsAdd");
    check(add && add->menu() && add->menu()->actions().size() == 5, "a menu of the five kinds");
    if (!add) return failures;
    for (int k = 0; k < 5; ++k) {
        add->menu()->actions().at(k)->trigger();
        pump();
    }
    const auto list = engine.controls();
    check(list.size() == 5 && list[0].kind == ShowControl::Kind::Slider && list[1].kind == ShowControl::Kind::Toggle &&
              list[2].kind == ShowControl::Kind::Choice && list[3].kind == ShowControl::Kind::Button && list[4].kind == ShowControl::Kind::Color,
          "one of each kind, in order");
    check(panel.findChildren<QWidget *>("controlRow").size() == 5, "five rows");
    check(undo.count() == 5, "each is one undo step");
    undo.undo();
    pump();
    check(engine.controls().size() == 4 && panel.findChildren<QWidget *>("controlRow").size() == 4, "undo takes the last one away, and its row");
    undo.redo();
    pump();
    check(engine.controls().size() == 5, "and redo brings it back");

    // Played from the panel
    auto *slider = panel.findChild<SliderField *>(QStringLiteral("control_%1").arg(list[0].key));
    check(slider != nullptr, "the slider has its field");
    if (slider) {
        emit slider->valueEdited(0.75);
        check(std::abs(engine.control(list[0].id)->value - 0.75) < 1e-9, "moving it sets the control");
    }
    auto *button = panel.findChild<QPushButton *>(QStringLiteral("control_%1").arg(list[3].key));
    if (button) {
        emit button->pressed();
        check(engine.control(list[3].id)->value == 1.0, "a button is 1 while held");
        emit button->released();
        check(engine.control(list[3].id)->value == 0.0, "and 0 when let go");
    } else {
        check(false, "the button has its field");
    }
    auto *selector = panel.findChild<QComboBox *>(QStringLiteral("control_%1").arg(list[2].key));
    if (selector) {
        selector->setCurrentIndex(2);
        emit selector->activated(2);
        check(engine.control(list[2].id)->value == 2.0, "a selector's choice");
    } else {
        check(false, "the selector has its field");
    }

    // Moved by something else (OSC, an interaction, a timeline): the panel follows
    engine.setControlValue(list[0].id, 0.25);
    panel.refreshValues();
    if (slider) check(std::abs(slider->value() - 0.25) < 1e-9, "the panel shows a value that something else set");

    // Moved in the list, one undo step
    {
        const QJsonArray before = engine.controlsJson();
        engine.moveControl(list[4].id, 0);
        pump();
        check(engine.controls().front().id == list[4].id, "moved");
        engine.setControlsJson(before);
        pump();
        check(engine.controls().front().id == list[0].id, "put back");
    }
    // Removing is undone with its value
    engine.setControlValue(list[0].id, 0.6);
    {
        const QJsonArray before = engine.controlsJson();
        engine.removeControl(list[0].id);
        pump();
        check(engine.controls().size() == 4, "deleted");
        engine.setControlsJson(before);
        pump();
        check(engine.controls().size() == 5 && std::abs(engine.controls().front().value - 0.6) < 1e-9, "restored with its value");
    }
    if (const QByteArray shots = qgetenv("NYXMAP_TEST_SHOTS"); !shots.isEmpty()) panel.grab().save(QString::fromLocal8Bit(shots) + "/controls.png");
    return failures;
}
