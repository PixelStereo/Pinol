// The show's controls: sliders, switches, selectors, buttons, colors as parameters of the show, in the project, in OSC.
#include "Engine.h"
#include "Osc.h"
#include <QGuiApplication>
#include <QSurfaceFormat>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char *why)
{
    if (!ok) throw std::runtime_error(why);
}
static bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; }

int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    try {
        Engine engine;
        QString error;
        require(engine.initialize(&error), qPrintable(error));
        engine.newProject();
        QTemporaryDir dir;
        auto has = [&](const QString &path) {
            for (const ParamInfo &p : engine.parameters(0))
                if (p.path == path) return true;
            return false;
        };
        ShowControl slider;
        slider.name = "Master Level";
        slider.min = 0;
        slider.max = 200;
        slider.step = 1;
        slider.def = 100;
        slider.value = 100;
        slider.unit = " %";
        ShowControl toggle;
        toggle.name = "Strobe";
        toggle.kind = ShowControl::Kind::Toggle;
        ShowControl choice;
        choice.name = "Scene";
        choice.kind = ShowControl::Kind::Choice;
        choice.choices = {"Intro", "Verse", "Drop"};
        ShowControl button;
        button.name = "Flash";
        button.kind = ShowControl::Kind::Button;
        ShowControl color;
        color.name = "Wash";
        color.kind = ShowControl::Kind::Color;
        color.rgb = {1, 0.5, 0.25};
        const quint64 a = engine.addControl(slider), b = engine.addControl(toggle), c = engine.addControl(choice), d = engine.addControl(button),
                      e = engine.addControl(color);
        require(a && b && c && d && e && a != b, "five controls with their own ids");
        const auto list = engine.controls();
        require(list.size() == 5 && list[0].key == "master_level" && list[1].key == "strobe" && list[4].key == "wash", "keys from names");
        require(engine.addControl(slider) != 0 && engine.controls().back().key == "master_level_2", "a second one of the same name gets its own key");
        engine.removeControl(engine.controls().back().id);

        // Each is a parameter of the show
        for (const char *p : {"control/master_level", "control/strobe", "control/scene", "control/flash", "control/wash/r", "control/wash/g", "control/wash/b"})
            require(has(p), p);
        require(engine.setParameter(0, "control/master_level", 150), "set a slider by its address");
        require(near(engine.control(a)->value, 150), "the control holds it");
        require(engine.setParameter(0, "control/master_level", 900), "a value beyond the range is kept in it");
        require(near(engine.control(a)->value, 200), "clamped to the slider's range");
        require(engine.setParameter(0, "control/strobe", true) && engine.control(b)->value == 1.0, "a switch");
        require(engine.setParameter(0, "control/scene", "drop") && engine.control(c)->value == 2.0, "a selector by the name of a choice");
        require(!engine.setParameter(0, "control/scene", "nonsense"), "not a choice it has");
        require(engine.setParameter(0, "control/flash", true) && engine.control(d)->value == 1.0, "a button is held down");
        engine.setParameter(0, "control/flash", false);
        require(engine.control(d)->value == 0.0, "and let go");
        require(engine.setParameter(0, "control/wash/g", 0.9) && near(engine.control(e)->rgb[1], 0.9), "a color's channel");
        double read = 0;
        require(engine.setControlValue(a, 20) && engine.animParamValue(0, "control/master_level", &read) && near(read, 20), "set through the API, read as a parameter");
        require(!engine.setControlValue(e, 1), "a color has no single value");

        // Edit: the address stays when the name changes; the value is kept within a new range
        auto changed = *engine.control(a);
        changed.name = "Master";
        changed.max = 100;
        changed.value = 20;
        require(engine.updateControl(a, changed) && engine.control(a)->key == "master_level", "renamed, still at its address");
        require(has("control/master_level"), "and the parameter is still there");
        require(engine.moveControl(e, 0) && engine.controls().front().id == e, "moved");
        engine.moveControl(e, 4);
        require(engine.setParameter(0, "control/master_level", 80), "a control is a parameter that animations and interactions can drive");
        bool animatable = false;
        for (const ParamInfo &p : engine.parameters(0))
            if (p.path == "control/master_level") animatable = p.animatable;
        require(animatable, "a slider is animatable");

        // OSC: /control/<key>
        {
            OscServer osc(&engine);
            int status = 0;
            const auto tree = QJsonDocument::fromJson(osc.httpGet("/control", &status)).object();
            require(status == 200 && tree.value("CONTENTS").toObject().contains("master_level") &&
                        tree.value("CONTENTS").toObject().contains("wash"), "the controls are in the OSC namespace");
            const auto node = QJsonDocument::fromJson(osc.httpGet("/control/master_level", &status)).object();
            require(node.value("TYPE").toString() == "f" && near(node.value("RANGE").toArray().at(0).toObject().value("MAX").toDouble(), 100),
                    "a slider is a float with its range");
            osc::Message m;
            m.address = "/control/master_level";
            m.types = "f";
            m.args = {QVariant(42.0)};
            require(osc.handleMessage(m) && near(engine.control(a)->value, 42), "set it by OSC");
            m.address = "/control/scene";
            m.types = "i";
            m.args = {QVariant(2)};
            require(osc.handleMessage(m) && engine.control(c)->value == 1.0, "a selector by OSC: 1 is the first choice");
            m.address = "/control/wash";
            m.types = "fff";
            m.args = {QVariant(0.1), QVariant(0.2), QVariant(0.3)};
            require(osc.handleMessage(m) && near(engine.control(e)->rgb[2], 0.3), "a color by OSC");
            // The tree changes when a control is added
            ShowControl extra;
            extra.name = "Extra";
            engine.addControl(extra);
            const auto again = QJsonDocument::fromJson(osc.httpGet("/control", &status)).object();
            require(again.value("CONTENTS").toObject().contains("extra"), "a new control appears in the OSC tree");
            engine.removeControl(engine.controls().back().id);
        }

        // The hub: one control read by two interactions drives two layers at once (the second one inverted); a control is
        // also a target, driven by another value of the show
        {
            engine.setInteractions({});
            ShowControl fader;
            fader.name = "Fader";
            const quint64 fid = engine.addControl(fader);
            const quint64 la = engine.layerId(engine.addLayer("A")), lb = engine.layerId(engine.addLayer("B"));
            auto opacity = [&](quint64 id) { return engine.layer(engine.indexOfId(id))->opacity; };
            Interaction one = Interaction::made(InteractionSource::Internal);
            one.sourceLayer = 0;
            one.sourceParam = "control/fader";
            one.layer = la;
            one.param = "opacity";
            Interaction two = one;
            two.layer = lb;
            two.filters = {InteractionStage::remap(0, 1, 1, 0)};
            engine.addInteraction(one);
            engine.addInteraction(two);
            require(engine.setControlValue(fid, 0.3), "move the fader");
            engine.renderFrame();
            require(near(opacity(la), 0.3, 1e-4) && near(opacity(lb), 0.7, 1e-4), "one control drives two layers, one of them inverted");
            require(engine.setParameter(0, "control/fader", 0.9), "moved by address (OSC, a timeline)");
            engine.renderFrame();
            require(near(opacity(la), 0.9, 1e-4) && near(opacity(lb), 0.1, 1e-4), "and they follow");
            // A control as a target: a layer's opacity drives it
            engine.setInteractions({});
            Interaction back = Interaction::made(InteractionSource::Internal);
            back.sourceLayer = la;
            back.sourceParam = "opacity";
            back.layer = 0;
            back.param = "control/fader";
            back.takeover = Takeover::Jump;
            engine.addInteraction(back);
            require(engine.setParameter(la, "opacity", 0.45), "set a layer's opacity");
            engine.renderFrame();
            require(near(engine.control(fid)->value, 0.45, 1e-4), "an interaction drives a control");
            engine.setInteractions({});
            engine.removeControl(fid);
        }

        // The project keeps them, with their values
        const QString project = dir.filePath("controls.nyxmap");
        engine.setControlValue(a, 33);
        engine.setControlValue(b, 1);
        engine.setControlValue(c, 2);
        engine.setControlValue(d, 1);
        require(engine.saveProject(project, {}, &error), qPrintable(error));
        engine.newProject();
        require(engine.controls().empty() && !has("control/master_level"), "a new project has no controls");
        require(engine.loadProject(project, nullptr, &error), qPrintable(error));
        const auto loaded = engine.controls();
        require(loaded.size() == 5 && loaded[0].key == "master_level" && loaded[0].kind == ShowControl::Kind::Slider &&
                    near(loaded[0].value, 33) && near(loaded[0].max, 100) && loaded[0].unit == " %",
                "the slider is back with its range and value");
        require(loaded[1].value == 1.0 && loaded[2].value == 2.0 && loaded[2].choices.size() == 3, "the switch and the selector");
        require(loaded[3].value == 0.0, "a button comes back up");
        require(near(loaded[4].rgb[0], 0.1) && near(loaded[4].rgb[2], 0.3), "the color");
        require(has("control/master_level") && has("control/scene"), "and their parameters");
        // Hand-edited states: nothing breaks
        QJsonArray junk;
        junk.append(QJsonObject{{"id", "1"}, {"name", "A"}, {"key", "bad/key"}, {"kind", "slider"}, {"min", 5}, {"max", 1}, {"value", 99}});
        junk.append(QJsonObject{{"id", "1"}, {"name", "A"}, {"key", "bad/key"}, {"kind", "choice"}});
        junk.append(42);
        require(!engine.setControlsJson(junk, &error) && !error.isEmpty(), "an entry that is not an object is refused");
        junk.removeLast();
        require(engine.setControlsJson(junk, &error), "the rest is accepted");
        const auto kept = engine.controls();
        require(kept.size() == 2 && kept[0].id != kept[1].id && kept[0].key != kept[1].key && kept[0].key.indexOf('/') < 0, "ids and keys made unique, no slash in a key");
        require(kept[0].max > kept[0].min && kept[0].value <= kept[0].max && kept[1].choices.size() >= 2, "ranges and choices made valid");
        engine.shutdown();
        std::cout << "Show controls: parameters, OSC, project, edits passed\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
