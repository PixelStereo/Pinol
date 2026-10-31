#include "Engine.h"
#include "StoredContent.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>
static void check(bool ok, const char *why)
{
    if (!ok)
        throw std::runtime_error(why);
}
static bool near(double a, double b) { return std::abs(a - b) < 1e-5; }
int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    Engine engine;
    QString error;
    if (!engine.initialize(&error)) {
        std::cerr << error.toStdString();
        return 1;
    }
    engine.setFadesManual(true);
    try {
        using namespace StoredContent;
        const int layer = engine.addLayer("First");
        const auto id = engine.layerId(layer);
        const int other = engine.addLayer("Other");
        const auto otherId = engine.layerId(other);
        check(engine.setLayerText(engine.indexOfId(id)) && engine.setLayerText(engine.indexOfId(otherId)),
              "text fixtures");
        engine.setParameter(id, "enable", true);
        engine.setParameter(otherId, "enable", true);
        engine.setParameter(id, "opacity", .25);
        engine.setParameter(id, "rotation", .35);
        Engine::Snapshot original;
        original.fade = 0;
        original.layers = engine.captureLayers();
        original.composition = engine.captureComposition();
        const std::vector<Selection> opacity{{Owner::Layer, QString::number(id), {"params", "opacity"}}};
        auto rows = original.layers;
        for (int i = 0; i < rows.size(); ++i)
            if (rows[i].toObject().value("id").toString() == QString::number(id)) {
                auto row = rows[i].toObject();
                row["timing"] = QJsonObject{{"opacity", 2}, {"color", 3}};
                row["delay"] = QJsonObject{{"opacity", .4}};
                row["easing"] = QJsonObject{{"opacity", "linear"}};
                rows[i] = row;
            }
        original.layers = rows;
        auto fragment = select(original, opacity);
        check(fragment.layers.size() == 1, "only selected owner copied");
        const auto stored = fragment.layers[0].toObject();
        check(stored.value("params").toObject().size() == 1 && !stored.contains("source"),
              "partial clipboard does not capture source or other parameters");
        check(stored.value("timing").toObject().size() == 1, "only applicable timing copied");
        Engine::Snapshot decoded;
        check(decode(encode(fragment), &decoded) && decoded.layers == fragment.layers, "typed clipboard roundtrip");
        check(!decode("{}", &decoded), "unrelated clipboard rejected");
        auto preset = toPreset(fragment);
        check(preset.values.size() == 1 && preset.values[0].layer == id && near(preset.values[0].time, 2) &&
                  near(preset.values[0].delay, .4) && preset.values[0].easing == "linear",
              "snapshot to preset keeps timing, delay and easing");
        auto from = fromPreset(preset);
        check(from.layers[0].toObject().value("params").toObject().value("opacity").toDouble() == .25,
              "preset to snapshot keeps stored value");
        auto visibility = toPreset(select(original, {{Owner::Layer, QString::number(id), {"enable"}}}));
        check(visibility.values.size() == 1 && visibility.values[0].time == 0, "copied visibility is an explicit cut");
        const int visibilityIndex = engine.addPreset(visibility);
        engine.setParameter(id, "enable", false);
        engine.recallPreset(visibilityIndex);
        check(engine.layer(engine.indexOfId(id))->enabled, "copied preset visibility applies");
        auto delayedVisibility = engine.preset(visibilityIndex);
        delayedVisibility.values[0].value = false;
        delayedVisibility.values[0].delay = .2;
        engine.setPreset(visibilityIndex, delayedVisibility);
        engine.recallPreset(visibilityIndex);
        check(engine.layer(engine.indexOfId(id))->enabled, "delayed visibility waits");
        engine.advanceFades(.2);
        check(!engine.layer(engine.indexOfId(id))->enabled, "delayed visibility dispatches");
        engine.setParameter(id, "enable", true);
        auto reduced = original;
        remove(reduced, opacity);
        check(reduced.partial, "removing content makes snapshot partial");
        // Remove all structural ownership as well; missing fields must not reset live state.
        remove(reduced, {{Owner::Layer, QString::number(id), {"source"}},
                         {Owner::Layer, QString::number(id), {"fx"}},
                         {Owner::Layer, QString::number(id), {"masks"}},
                         {Owner::Layer, QString::number(id), {"anims"}},
                         {Owner::Layer, QString::number(id), {"viewports"}},
                         {Owner::Layer, QString::number(otherId), {}}});
        const int snap = engine.addSnapshot(reduced);
        engine.setParameter(id, "opacity", .8);
        engine.setParameter(id, "rotation", .9);
        engine.setParameter(otherId, "opacity", .7);
        engine.recallSnapshot(snap);
        check(near(engine.parameterValue(id, "opacity").toDouble(), .8), "removed value stays live on recall");
        check(near(engine.parameterValue(id, "rotation").toDouble(), .35), "retained value still recalled");
        check(engine.layer(engine.indexOfId(otherId))->enabled &&
                  near(engine.parameterValue(otherId, "opacity").toDouble(), .7),
              "removed owner is not hidden by recall");
        QTemporaryDir directory;
        const auto path = directory.filePath("partial.nyxmap");
        check(engine.saveProject(path, {}, &error), "save partial snapshot");
        check(engine.loadProject(path, nullptr, &error), "load partial snapshot");
        check(engine.snapshot(0).partial, "partial semantics persisted");
        bool omitted = false;
        for (const auto &v : engine.snapshot(0).layers)
            if (v.toObject().value("id").toString() == QString::number(id))
                omitted = !v.toObject().contains("source") && !v.toObject().contains("fx");
        check(omitted, "project load does not recreate deleted structures");
        // Group dispatch: same GO, longest static duration, continuation after group.
        engine.newProject();
        engine.setFadesManual(true);
        Engine::Snapshot a, b, c;
        a.fade = .2;
        b.fade = .5;
        c.fade = 0;
        const int ai = engine.addSnapshot(a), bi = engine.addSnapshot(b), ci = engine.addSnapshot(c);
        const auto aid = engine.snapshot(ai).id, bid = engine.snapshot(bi).id, cid = engine.snapshot(ci).id;
        // Composition makes these non-empty recalls and gives them a measurable duration.
        for (int i : {ai, bi, ci}) {
            auto s = engine.snapshot(i);
            s.composition = {{"opacity", i == ai ? .2 : i == bi ? .5 : .9}};
            engine.setSnapshot(i, s);
        }
        Sequencer::Step first, second, last, group;
        first.snapshot = aid;
        second.snapshot = bid;
        last.snapshot = cid;
        group.actions = {first, second};
        group.preWait = .1;
        group.next = Sequencer::StepContinue::AutoFollow;
        group.postWait = .1;
        Sequencer::Sequence sequence;
        sequence.steps = {group, last};
        const int qi = engine.sequencer().add(sequence);
        engine.sequencer().setCurrent(qi);
        int fired = 0;
        auto connection = QObject::connect(&engine, &Engine::snapshotRecalled, [&](int) { ++fired; });
        engine.sequencer().go();
        check(fired == 0, "group respects common pre-wait");
        engine.sequencer().advance(.1);
        check(fired == 2 && engine.sequencer().position() == 0, "all group actions dispatched on one GO");
        engine.sequencer().advance(.49);
        check(engine.sequencer().position() == 0, "auto-follow waits for longest grouped action");
        engine.sequencer().advance(.12);
        check(engine.sequencer().position() == 1 && fired == 3, "group auto-follow reaches next cue once");
        auto json = engine.sequencer().toJson();
        check(json[qi].toObject().value("steps").toArray()[0].toObject().value("actions").toArray().size() == 2,
              "groups serialized");
        engine.sequencer().fromJson(json, qi);
        check(engine.sequencer().sequence(qi).steps[0].actions.size() == 2, "groups load");
        auto edit = engine.sequencer().sequence(qi);
        edit.editMode = true;
        engine.sequencer().setSequence(qi, edit);
        engine.sequencer().setPosition(-1);
        fired = 0;
        engine.sequencer().go();
        check(fired == 2, "edit mode manual GO auditions entire group immediately");
        engine.sequencer().advance(10);
        check(engine.sequencer().position() == 0 && fired == 2, "edit mode suppresses auto-follow");
        check(engine.sequencer().toJson()[qi].toObject().value("edit_mode").toBool(), "edit mode saved");
        const int clockLayer = engine.addLayer("Clock");
        engine.layer(clockLayer)->type = SourceType::Text;
        const auto clockId = engine.layerId(clockLayer);
        engine.setParameter(clockId, "opacity", 0.0);
        Sequencer::Sequence clocked;
        clocked.name = "Clocked";
        clocked.clockLayer = clockId;
        clocked.clockParam = "opacity";
        first.at = .2;
        second.at = .5;
        clocked.steps = {first, second};
        const int clockIndex = engine.sequencer().add(clocked);
        engine.renderFrame();
        clocked = engine.sequencer().sequence(clockIndex);
        clocked.editMode = true;
        engine.sequencer().setSequence(clockIndex, clocked);
        engine.setParameter(clockId, "opacity", .3);
        engine.renderFrame();
        check(engine.sequencer().positionOf(clockIndex) == -1, "muted timecode does not trigger cues");
        clocked.editMode = false;
        engine.sequencer().setSequence(clockIndex, clocked);
        engine.renderFrame();
        check(engine.sequencer().positionOf(clockIndex) == -1, "unmute does not chase crossed cues");
        engine.setParameter(clockId, "opacity", .6);
        engine.renderFrame();
        check(engine.sequencer().positionOf(clockIndex) == 1, "future timecode cues resume normally");
        Animation animation;
        animation.duration = 1;
        animation.loop = AnimLoop::Once;
        const int animationIndex = engine.addAnimation(animation);
        const auto animationId = engine.animation(animationIndex).id;
        Sequencer::Step play;
        play.timeline = animationId;
        Sequencer::Step timed;
        timed.actions = {first, play};
        timed.next = Sequencer::StepContinue::AutoFollow;
        Sequencer::Sequence timedSequence;
        timedSequence.steps = {timed, last};
        const int timedIndex = engine.sequencer().add(timedSequence);
        engine.sequencer().setCurrent(timedIndex);
        engine.sequencer().go();
        engine.advanceFades(.3);
        engine.sequencer().advance(.3);
        check(engine.sequencer().positionOf(timedIndex) == 0, "group waits for finite timeline");
        engine.controlAnimation(animationId, AnimAction::Speed, 0);
        engine.advanceFades(.5);
        engine.sequencer().advance(.5);
        check(engine.sequencer().positionOf(timedIndex) == 0, "frozen group timeline holds auto-follow");
        engine.controlAnimation(animationId, AnimAction::Speed, 2);
        engine.advanceFades(.34);
        engine.sequencer().advance(.34);
        check(engine.sequencer().positionOf(timedIndex) == 0, "group tracks actual resumed playback");
        engine.advanceFades(.02);
        engine.sequencer().advance(.02);
        check(engine.sequencer().positionOf(timedIndex) == 1, "group continues once timeline completes");
        QObject::disconnect(connection);
        engine.shutdown();
        std::cout << "Stored-content transfer, deletion, partial recall, project persistence, action groups and edit "
                     "mode passed\n";
        return 0;
    } catch (const std::exception &e) {
        engine.shutdown();
        std::cerr << e.what() << '\n';
        return 1;
    }
}
