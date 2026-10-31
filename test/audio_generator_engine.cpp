#include "AudioStream.h"
#include "Engine.h"
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
static double mix(Engine &engine, int layer)
{
    double energy = 0;
    auto audio = engine.layer(layer)->audio;
    for (int block = 0; block < 100; ++block) {
        std::array<float, 960> samples{};
        audio->mix(samples.data(), 480, 0);
        if (block >= 90)
            for (float v : samples) {
                require(std::isfinite(v), "finite mixer output");
                energy += v * v;
            }
    }
    return energy;
}
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
    try {
        QTemporaryDir dir;
        require(dir.isValid(), "temporary directory");
        auto recipe = dir.filePath("visual.nyxaudio");
        {
            QFile file(recipe);
            require(file.open(QIODevice::WriteOnly), "write recipe");
            file.write("{\"version\":1,\"type\":\"video\"}");
        }
        std::cerr << "stage: new project\n";
        engine.newProject();
        engine.setCompositionSize(QSize(32, 32));
        int picture = engine.addLayer("Video picture");
        const auto pictureId = engine.layerId(picture);
        QImage image(32, 32, QImage::Format_RGB32);
        image.fill(Qt::black);
        const auto black = dir.filePath("black.png"), white = dir.filePath("white.png");
        require(image.save(black), "black image");
        image.fill(Qt::white);
        require(image.save(white), "white image");
        require(engine.setLayerImage(picture, black, &error), "load visual source");
        int sound = engine.addLayer("Sonification");
        const auto soundId = engine.layerId(sound);
        require(engine.setLayerAudio(sound, recipe, &error), "load live sonification");
        std::cerr << "stage: render frame\n";
        engine.renderFrame();
        std::cerr << "stage: black energy\n";
        require(mix(engine, sound) == 0, "black composition is silent");
        require(engine.setLayerImage(engine.indexOfId(pictureId), white, &error), "change visual source");
        std::cerr << "stage: render frame\n";
        engine.renderFrame();
        std::cerr << "stage: white energy\n";
        require(mix(engine, sound) > 0, "live rendered white composition drives audio");
        // The analysis is per viewport: a second viewport that does not show the picture sees black
        {
            const quint64 main = engine.mainViewportId();
            const int second = engine.addViewport("Second", QSize(32, 32));
            const quint64 secondId = engine.layerId(second);
            engine.setOpacityIn(engine.indexOfId(pictureId), secondId, 0);
            require(!engine.setLayerSonificationViewport(engine.indexOfId(soundId), pictureId), "a layer is not a viewport");
            require(engine.layerSonificationViewport(engine.indexOfId(soundId)) == 0, "the first viewport by default");
            require(engine.setLayerSonificationViewport(engine.indexOfId(soundId), secondId), "choose the second viewport");
            engine.renderFrame();
            engine.renderFrame();
            require(mix(engine, engine.indexOfId(soundId)) < 1e-9, "the viewport that shows black is silent (the smoothing of the color has died away)");
            require(engine.setLayerSonificationViewport(engine.indexOfId(soundId), main), "choose the first viewport by id");
            engine.renderFrame();
            engine.renderFrame();
            require(mix(engine, engine.indexOfId(soundId)) > 0, "the viewport that shows white sounds");
            require(engine.setLayerSonificationViewport(engine.indexOfId(soundId), secondId), "second again");
            const auto saved = engine.layerJson(engine.indexOfId(soundId));
            require(saved.value("sonification_viewport").toString().toULongLong() == secondId, "the choice is in the layer state");
            engine.setLayerSonificationViewport(engine.indexOfId(soundId), 0);
            engine.replaceLayerJson(engine.indexOfId(soundId), saved);
            require(engine.layerSonificationViewport(engine.indexOfId(soundId)) == secondId, "and restored with it");
            require(engine.setLayerSonificationViewport(engine.indexOfId(soundId), 0), "back to the first viewport");
            engine.renderFrame();
            engine.renderFrame();
            sound = engine.indexOfId(soundId); // a viewport was added above the layers
        }
        require(engine.setParameter(soundId, "synth/frequency", 800), "synthesis parameter address exposed");
        require(engine.setParameter(soundId, "synth/release", .25), "ADSR parameter exposed");
        std::cerr << "stage: capture parameters\n";
        const auto saved = engine.layerJson(sound);
        require(saved.value("params").toObject().value("synth/frequency").toDouble() == 800,
                "synthesis captured in layer state");
        engine.setParameter(soundId, "synth/frequency", 200);
        std::cerr << "stage: restore layer\n";
        engine.replaceLayerJson(sound, saved);
        const auto restored = engine.layerId(sound);
        require(engine.parameterValue(restored, "synth/frequency").toDouble() == 800,
                "synthesis survives source restore");
        const auto project = dir.filePath("audio.nyxmap");
        std::cerr << "stage: save project\n";
        require(engine.saveProject(project, {}, &error), "save project");
        std::cerr << "stage: load project\n";
        require(engine.loadProject(project, nullptr, &error), "reload project");
        require(engine.parameterValue(engine.layerId(sound), "synth/release").toDouble() == .25,
                "ADSR survives project reload");
#ifdef NYXMAP_FAUST_COMPILER
        const auto program = dir.filePath("custom.dsp");
        {
            QFile file(program);
            require(file.open(QIODevice::WriteOnly), "write custom DSP");
            file.write("process = hslider(\"Level\",0.1,0,1,0.001);");
        }
        require(engine.setLayerAudio(sound, program, &error), "load libfaust with OpenGL active");
        engine.renderFrame();
        require(mix(engine, sound) > 0, "custom DSP runs beside live OpenGL");
        require(engine.setParameter(engine.layerId(sound), "synth/level", .3), "custom widget parameter exposed");
#endif
        require(engine.setAudioEffectsJson(sound,
                                           {QJsonObject{{"id", "filter"}, {"kind", "lowpass"}},
                                            QJsonObject{{"id", "echo"}, {"kind", "delay"}}},
                                           &error),
                "attach layer audio FX");
        require(engine.setParameter(engine.layerId(sound), "audio_fx/filter/cutoff", 777),
                "FX controls exposed to interactions");
        require(engine.setParameter(engine.layerId(sound), "audio_fx/echo/wet", .2), "FX wet control");
        require(engine.saveProject(project, {}, &error), "save audio FX project");
        require(engine.loadProject(project, nullptr, &error), "restore audio FX project");
        require(engine.audioEffectsJson(sound).size() == 2, "audio FX order restored");
        require(engine.parameterValue(engine.layerId(sound), "audio_fx/filter/cutoff").toDouble() == 777,
                "FX parameter survives save/load");
        require(engine.setLayerAudio(sound, recipe, &error), "replace source with FX attached");
        require(engine.audioEffectsJson(sound).size() == 2, "FX survive source replacement");
        engine.renderFrame();
        require(std::isfinite(mix(engine, sound)), "FX run in the engine mixer");
        std::cerr << "stage: shutdown\n";
        engine.shutdown();
        std::cout << "Live GPU sonification, synth parameters and project restore passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "stage: shutdown\n";
        engine.shutdown();
        std::cerr << e.what() << '\n';
        return 1;
    }
}
