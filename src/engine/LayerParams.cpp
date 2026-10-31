// The parameters a layer instantiates (Layer::parameters)
#include "EngineInternal.h"
#include "Osc.h"
#include "ColorInfo.h"

#include <QHash>
#include <cmath>

namespace {

QStringList blendKeys()
{
    QStringList k;
    for (BlendMode m : kBlendModes) k << blendModeKey(m);
    return k;
}

// What the parameters are made from: when it changes, they are made again. A hash, without allocating: it is
// computed on every look-up (each OSC message, each animated value, each frame of a fade)
quint64 keyOf(const Layer &l)
{
    quint64 h = 1469598103934665603ULL; // FNV-1a over 64-bit words
    auto mix = [&h](quint64 v) {
        h ^= v;
        h *= 1099511628211ULL;
    };
    const QSize c = l.compSize ? *l.compSize : QSize(1920, 1080);
    for (quint64 v :
         {quint64(l.type), quint64(l.hasTransport()), quint64(bool(l.audio)), quint64(l.isViewport),
          quint64(quintptr(l.audio.get())), quint64(l.isGroup), quint64(c.width()), quint64(c.height()),
          quint64(quintptr(l.generator.get())), quint64(l.effects.size()), quint64(std::llround(l.duration() * 1000)),
          quint64(l.hasPicture()), quint64(bool(l.tcClock)), quint64(quintptr(l.clockGen.get())),
          // the played range: the playhead's (an Internal interaction takes its range from it)
          quint64(std::llround(l.inPoint * 1000)), quint64(std::llround(l.outPoint * 1000))})
        mix(v);
    // An allocator can reuse a replaced generator's address. Its kind changes
    // which parameters exist even when the pointer and text source stay equal.
    if (l.clockGen) mix(quint64(l.clockGen->kind()));
    if (l.audioEffects)
        mix(l.audioEffects->revision());
    auto inputs = [&mix](const IsfInstance *i) {
        if (!i) return;
        mix(quint64(i->isValid()));
        for (const IsfInput &in : i->inputs()) mix(qHash(in.name) ^ (quint64(in.type) << 40));
    };
    mix(quint64(quintptr(l.toxPlayer.get())));
    if (l.toxPlayer) mix(l.toxPlayer->revision());
    inputs(l.generator.get());
    for (const auto &fx : l.effects) {
        mix(quint64(quintptr(fx.get())));
        mix(qHash(fx->name()));
        inputs(fx.get());
    }
    return h;
}

} // namespace

const std::vector<Parameter *> &Layer::parameters()
{
    const quint64 key = keyOf(*this);
    if (key == paramKey && !paramList.empty()) return paramList;
    paramKey = key;
    paramStore.clear();
    paramList.clear();
    using T = Parameter::Type;
    Layer *self = this;
    const QSize *comp = compSize;
    auto C = [comp] { return comp ? *comp : QSize(1920, 1080); };
    auto add = [this](const QString &path, const QString &label, T type) -> Parameter & {
        paramStore.push_back(std::make_unique<Parameter>(path, label, type));
        return *paramStore.back();
    };
    auto num = [&](const QString &path, const QString &label, double lo, double hi, double def, std::function<double()> get,
                   std::function<void(double)> set) -> Parameter & {
        return add(path, label, T::Float)
            .range(lo, hi)
            .byDefault(def)
            .bind([get] { return QVariant(get()); }, [set](const QVariant &v) { set(v.toDouble()); });
    };
    auto flag = [&](const QString &path, const QString &label, bool def, bool *field) -> Parameter & {
        return add(path, label, T::Bool).byDefault(def).bind([field] { return QVariant(*field); },
                                                             [field](const QVariant &v) { *field = v.toBool(); });
    };
    const bool picture = hasPicture();

    // On / off and solo, as the layer list's switches: driven by an interaction or a timeline, never saved in a
    // snapshot or a preset (a snapshot shows and hides its layers itself, with its fades)
    flag("enable", "Layer › Visible", true, &enabled).ramp(Parameter::Ramp::Cut).transient();
    flag("solo", "Layer › Solo", false, &solo).ramp(Parameter::Ramp::Cut).transient();

    if (picture) {
        num("opacity", "Opacity", 0, 1, 1.0, [self] { return double(self->opacity); },
            [self](double v) { self->opacity = float(v); });
        if (!isViewport)
            add("blend_mode", "Blend Mode", T::Choice)
                .choices(blendKeys())
                .byDefault(blendModeKey(BlendMode::Normal))
                .bind([self] { return QVariant(blendModeKey(self->blend)); },
                      [self](const QVariant &v) { self->blend = blendModeFromKey(v.toString()); });

        // Where it is (Mapping.h): the center of its rectangle, its size, its rotation about its pivot, the pins of its
        // corners — each one a value of its own, as the mapping stores it; a viewport's size is in pixels, a layer's in %
        // of the composition. The pivot and the pins are in % of the layer: they move with it
        Mapping *m = &mapping;
        auto touch = [m] { ++m->revision; };
        num("rotation", "Spatial › Rotation (°)", -180, 180, 0.0, [m] { return m->rotation; },
            [m, touch](double v) {
                m->rotation = v;
                touch();
            })
            .limits(-1e9, 1e9)
            .step(0.1)
            .ramp(Parameter::Ramp::Angle);
        for (int axis = 0; axis < 2; ++axis) { // the center of the rotation: where it is in the layer (moving it moves the layer)
            const bool x = axis == 0;
            num(x ? "pivot/x" : "pivot/y", x ? "Spatial › Pivot X (%)" : "Spatial › Pivot Y (%)", 0, 100, 50.0,
                [m, x] { return (x ? m->pivot.x() : m->pivot.y()) * 100.0; },
                [m, x, touch](double v) {
                    (x ? m->pivot.rx() : m->pivot.ry()) = v / 100.0;
                    touch();
                })
                .limits(-1000, 1000)
                .step(0.1).pointComponent(axis);
        }
        for (int axis = 0; axis < 2; ++axis) {
            const bool x = axis == 0;
            const double size = x ? C().width() : C().height();
            num(x ? "position/x" : "position/y", x ? "Spatial › Position X" : "Spatial › Position Y", -4 * size, 5 * size, size / 2,
                [m, C, x] { return x ? m->position.x() * C().width() : m->position.y() * C().height(); },
                [m, C, x, touch](double v) {
                    (x ? m->position.rx() : m->position.ry()) = v / (x ? C().width() : C().height());
                    touch();
                })
                .step(1).pointComponent(axis);
        }
        for (int axis = 0; axis < 2; ++axis) {
            const bool x = axis == 0;
            auto get = [m, x] { return x ? m->size.width() : m->size.height(); };
            auto put = [m, x, touch](double v) {
                if (x) m->size.setWidth(v);
                else m->size.setHeight(v);
                touch();
            };
            if (isViewport) // its rectangle, in pixels
                num(x ? "width" : "height", x ? "Spatial › Width" : "Spatial › Height", 1, 10000, x ? C().width() : C().height(),
                    [get, C, x] { return get() * (x ? C().width() : C().height()); },
                    [put, C, x](double v) { put(v / (x ? C().width() : C().height())); })
                    .limits(1, 100000)
                    .step(1);
            else // its size, in % of the composition
                num(x ? "scale/x" : "scale/y", x ? "Spatial › Scale X (%)" : "Spatial › Scale Y (%)", 0.1, 400, 100.0,
                    [get] { return get() * 100.0; }, [put](double v) { put(v / 100.0); })
                    .limits(0.1, 2000)
                    .step(0.1).pointComponent(axis);
        }
        if (!isViewport) { // a viewport is a rectangle: no pins, no mesh
            static const char *const kCorners[] = {"top_left", "top_right", "bottom_right", "bottom_left"};
            static const char *const kCornerNames[] = {"Top Left", "Top Right", "Bottom Right", "Bottom Left"};
            for (int k = 0; k < 4; ++k)
                for (int axis = 0; axis < 2; ++axis) { // how far the corner is pulled, in % of the layer
                    const bool x = axis == 0;
                    num(QStringLiteral("corner/%1/%2").arg(QLatin1String(kCorners[k]), x ? "x" : "y"),
                        QStringLiteral("Spatial › %1 %2 (%)").arg(QLatin1String(kCornerNames[k]), x ? "X" : "Y"), -100, 100, 0.0,
                        [m, k, x] { return (x ? m->pins[k].x() : m->pins[k].y()) * 100.0; },
                        [m, k, x, touch](double v) {
                            (x ? m->pins[k].rx() : m->pins[k].ry()) = v / 100.0;
                            touch();
                        })
                        .limits(-1000, 1000)
                        .step(0.01).pointComponent(axis);
                }
        }
        // ROI: the part of the source used (normalized, origin top left); its sides never cross
        static const char *const kRoi[] = {"left", "top", "right", "bottom"};
        static const char *const kRoiNames[] = {"Left", "Top", "Right", "Bottom"};
        for (int side = 0; side < 4; ++side)
            num(QStringLiteral("roi/") + kRoi[side], QStringLiteral("ROI › ") + kRoiNames[side], 0, 1, side < 2 ? 0.0 : 1.0,
                [self, side] {
                    const double v[4] = {self->roi.left(), self->roi.top(), self->roi.right(), self->roi.bottom()};
                    return v[side];
                },
                [self, side](double x) {
                    double v[4] = {self->roi.left(), self->roi.top(), self->roi.right(), self->roi.bottom()};
                    v[side] = x;
                    const double minSize = 0.002;
                    if (side == 0) v[0] = std::min(v[0], v[2] - minSize);
                    if (side == 2) v[2] = std::max(v[2], v[0] + minSize);
                    if (side == 1) v[1] = std::min(v[1], v[3] - minSize);
                    if (side == 3) v[3] = std::max(v[3], v[1] + minSize);
                    self->roi = QRectF(QPointF(v[0], v[1]), QPointF(v[2], v[3])) & Layer::fullRoi();
                })
                .step(0.001);

        // Color: the section's switch, the balance, the colors removed and added (each with its switch)
        flag("color/enable", "Color › Enable", true, &color.enabled);
        const double T_ = ColorAdjust::kTempRange, N = ColorAdjust::kTintRange;
        num("temp", "Color › Temperature", -T_, T_, 0.0, [self] { return double(self->color.temp); },
            [self](double v) { self->color.temp = float(v); })
            .step(1);
        flag("temp/enable", "Color › Temperature Enable", true, &color.tempOn);
        num("tint", "Color › Tint", -N, N, 0.0, [self] { return double(self->color.tint); },
            [self](double v) { self->color.tint = float(v); })
            .step(0.1);
        flag("tint/enable", "Color › Tint Enable", true, &color.tintOn);
        for (int which = 0; which < 2; ++which) {
            const QString key = which ? QStringLiteral("remove") : QStringLiteral("add");
            const QString name = which ? QStringLiteral("Remove") : QStringLiteral("Add");
            for (int c = 0; c < 3; ++c)
                num(key + "/" + QChar("rgb"[c]), QStringLiteral("Color › %1 %2").arg(name).arg(QChar("RGB"[c])), 0, 1, 0.0,
                    [self, which, c] { return double((which ? self->color.remove : self->color.add)[c]); },
                    [self, which, c](double v) { (which ? self->color.remove : self->color.add)[c] = float(v); }).colorComponent(c);
            flag(key + "/enable", QStringLiteral("Color › %1 Enable").arg(name), true, which ? &color.removeOn : &color.addOn);
        }
        flag("mask/invert", "Color › Invert Mask", false, &color.maskInvert);
        flag("fx/enable", "FX › Enable", true, &effectsEnabled);
    }

    if (!isViewport && !isGroup && type != SourceType::Layer && picture) {
        add("source/color_space", "Source › Color Space", T::Choice)
            .choices(sourceColorSpaceKeys()).byDefault("auto").animatable(false)
            .bind([self] { return QVariant(self->sourceColorSpace); },
                  [self](const QVariant &v) { self->sourceColorSpace = v.toString(); });
    }
    if (isViewport) {
        add("output/input_space", "Output › Rendered Input Space", T::Choice)
            .choices(outputColorSpaceKeys()).byDefault("srgb")
            .bind([self] { return QVariant(self->outputInputSpace); },
                  [self](const QVariant &v) { self->outputInputSpace = v.toString(); });
        auto targets = outputColorSpaceKeys();
        targets.prepend("same");
        add("output/space", "Output › Color Space", T::Choice)
            .choices(targets).byDefault("same")
            .bind([self] { return QVariant(self->outputSpace); },
                  [self](const QVariant &v) { self->outputSpace = v.toString(); });
        num("output/exposure", "Output › Exposure (EV)", -10, 10, 0,
            [self] { return self->outputExposure; }, [self](double v) { self->outputExposure = v; })
            .step(.01).animatable(false); // calibration belongs to the viewport, independent of cues
    }

    // Playback: the media's (backwards below 0: it turns round where it is), or the pace of the shader's TIME
    if (audio) {
        if (audioEffects)
            for (const auto &effect : audioEffects->effects()) {
                const QString prefix = QStringLiteral("audio_fx/") + effect->id() + QStringLiteral("/");
                const QString label = QStringLiteral("Audio FX › ") + effect->name() + QStringLiteral(" › ");
                add(prefix + QStringLiteral("enabled"), label + QStringLiteral("Enabled"), T::Bool)
                    .byDefault(true)
                    .bind([effect] { return QVariant(effect->enabled()); },
                          [effect](const QVariant &v) { effect->setEnabled(v.toBool()); });
                num(
                    prefix + QStringLiteral("wet"), label + QStringLiteral("Dry/Wet"), 0, 1, 1,
                    [effect] { return effect->wet(); }, [effect](double v) { effect->setWet(v); });
                const auto &controls = effect->parameters();
                for (size_t i = 0; i < controls.size(); ++i) {
                    const auto &p = controls[i];
                    num(
                        prefix + p.key, label + p.label, p.low, p.high, p.initial,
                        [effect, i] { return effect->value(int(i)); },
                        [effect, i](double v) { effect->setValue(int(i), v); });
                }
            }
        if (audioEffects)
            add(QStringLiteral("audio_fx/enable"), QStringLiteral("Audio FX › Enable"), T::Bool)
                .byDefault(true)
                .bind([chain = audioEffects] { return QVariant(chain->enabled()); },
                      [chain = audioEffects](const QVariant &v) { chain->setEnabled(v.toBool()); });
        if (auto *synth = audio->generator()) {
            for (int i = 0; i < synth->parameterCount(); ++i) {
                const auto &p = synth->parameter(i);
                num(
                    QStringLiteral("synth/") + p.key, QStringLiteral("Source › Synth › ") + p.label, p.low, p.high,
                    p.initial, [synth, i] { return synth->value(i); }, [synth, i](double v) { synth->setValue(i, v); });
            }
        }
        num("volume", "Source › Volume", 0, 2, 1.0, [self] { return double(self->volume); },
            [self](double v) { self->volume = float(v); });
        flag("mute", "Source › Mute", false, &muted);
    }
    if (hasTransport()) {
        num("speed", "Source › Speed", 0, 4, 1.0, [self] { return self->speed; }, [self](double v) { self->setSpeed(v); })
            .limits(-8, 8);
        add("play_mode", "Source › Play Mode", T::Choice)
            .choices({"oneshot", "loop", "pingpong", "stop"})
            .byDefault(playModeKey(PlayMode::Loop))
            .bind([self] { return QVariant(playModeKey(self->mode)); },
                  [self](const QVariant &v) { self->setPlayMode(playModeFromKey(v.toString())); });
        // The played range, in seconds (out −1: the end of the media, whatever its length); set at once, not faded or
        // animated (each change repositions the media)
        const double d = std::max(0.0, duration()), hi = d > 0 ? d : 1e9;
        num("in", "Source › In (s)", 0, hi, 0.0, [self] { return self->inPoint; },
            [self](double v) { self->setInOut(v, self->outPoint); })
            .step(0.01)
            .ramp(Parameter::Ramp::Cut)
            .animatable(false);
        num("out", "Source › Out (s)", -1, hi, -1.0, [self] { return self->outPoint; },
            [self](double v) { self->setInOut(self->inPoint, v < 0 ? -1.0 : v); })
            .step(0.01)
            .ramp(Parameter::Ramp::Cut)
            .animatable(false);
        // Played or paused: held by the snapshots and the presets like any value (set at once). After the in / out
        // points: a recall that also changes them plays (or pauses) in the new range
        add("play", "Source › Play", T::Bool)
            .ramp(Parameter::Ramp::Cut)
            .byDefault(true)
            .bind([self] { return QVariant(self->playing); },
                  [self](const QVariant &v) {
                      const bool on = v.toBool();
                      if (on && !self->playing && self->atEnd()) startMedia(*self); // played to the end: from the start
                      self->playing = on;
                      if (on) self->ended = false;
                  });
        // From the beginning: a snapshot or a preset that holds it on starts the media again at its in point (its out
        // point backwards) and plays it. Read back as off: it is an action the recall makes, not a state. Last of the
        // transport: after the play state, the in / out points and the play mode it may set as well
        add("restart", "Source › Restart on Recall", T::Bool)
            .ramp(Parameter::Ramp::Cut)
            .byDefault(false)
            .bind([] { return QVariant(false); },
                  [self](const QVariant &v) {
                      if (!v.toBool()) return;
                      self->playing = true;
                      self->ended = false;
                      startMedia(*self);
                  });
        // Back to the beginning (its out point backwards), played or paused as it was: an action (an event of a
        // timeline, OSC), not a value — a media played to its end in one shot starts again if it was playing
        add("rewind", "Source › Rewind", T::Trigger).bind(nullptr, [self](const QVariant &) {
            self->ended = false;
            startMedia(*self);
        });
        // To a time of the media (seconds), played or paused as it was: written by an interaction, an event of a
        // timeline, OSC; read back as Playhead below
        add("seek", "Source › Seek To (s)", T::Float)
            .range(0, hi)
            .ramp(Parameter::Ramp::Cut)
            .writeOnly()
            .bind(nullptr, [self](const QVariant &v) { reposition(*self, v.toDouble(), self->dir); });
        // Where it plays, read back for an Internal interaction to follow (never saved): in seconds of the media, and
        // from its in point (0) to its out point (1)
        // (its range: the played one, from the in point to the out point)
        const Timeline played = timeline();
        add("playhead", "Source › Playhead (s)", T::Float)
            .range(d > 0 ? played.lo() : 0, d > 0 ? played.hi() : 1)
            .readOnly()
            .bind([self] { return QVariant(self->position()); }, nullptr);
        add("progress", "Source › Progress", T::Float)
            .range(0, 1)
            .readOnly()
            .bind([self] {
                const Timeline t = self->timeline();
                const double span = t.hi() - t.lo();
                return QVariant(span > 0 ? std::clamp((self->position() - t.lo()) / span, 0.0, 1.0) : 0.0);
            }, nullptr);
    }

    // A Timecode generator's transport (its free run): played, paused, sought, its in / out points and play mode.
    // Values in seconds of timecode; set at once (each one repositions the clock). In a snapshot, the position of a
    // clock that plays there and plays already is left alone: it goes on (Engine::applyParams)
    if (tcClock) {
        // Keep the registry stable across configuration changes. Transport bindings resolve
        // the current clock so a recall can configure it and seek it in the same pass.
        auto clock = [self] { return self->tcClock; };
        if (engine && timecode) {
            auto config = [&](const QString &field, const QString &label, T type, const QVariant &def) -> Parameter & {
                return add("tc/" + field, "Timecode › " + label, type).byDefault(def)
                    .ramp(Parameter::Ramp::Cut).animatable(false)
                    .bind([self, field] { return self->timecode->toJson().value(field).toVariant(); },
                          [self, field](const QVariant &value) {
                              QJsonObject settings = self->timecode->toJson();
                              settings[field] = QJsonValue::fromVariant(value);
                              const int row = t_layerBuild.layer == self ? kBuildSlot : self->engine->indexOfId(self->id);
                              self->engine->setLayerTimecode(row, TimecodeSettings::fromJson(settings));
                          });
            };
            config("source", "Source", T::Choice, "internal").choices({"internal", "ltc", "mtc", "atc_ndi", "atc_decklink"});
            config("internal", "Clock", T::Choice, "time_of_day").choices({"time_of_day", "free_run"});
            config("rate", "Rate", T::Choice, "25").choices({"24", "25", "29.97df", "30"});
            config("offset", "Offset (frames)", T::Int, 0).range(-2592000, 2592000);
            config("input", "Input", T::Text, QString());
            config("output", "Output", T::Text, QStringLiteral("0"));
        }
        const double day = 24.0 * 3600.0;
        num("tc/in", "Timecode › In (s)", 0, day, 0.0, [clock] { return clock()->inPoint(); }, [clock](double v) { clock()->setInOut(v, clock()->outPoint()); })
            .step(0.04)
            .ramp(Parameter::Ramp::Cut)
            .animatable(false);
        num("tc/out", "Timecode › Out (s)", -1, day, -1.0, [clock] { return clock()->outPoint(); },
            [clock](double v) { clock()->setInOut(clock()->inPoint(), v < 0 ? -1.0 : v); })
            .step(0.04)
            .ramp(Parameter::Ramp::Cut)
            .animatable(false);
        add("tc/play_mode", "Timecode › Play Mode", T::Choice)
            .choices({"oneshot", "loop", "pingpong", "stop"})
            .byDefault(QStringLiteral("loop"))
            .bind([clock] { return QVariant(tcPlayModeKey(clock()->playMode())); },
                  [clock](const QVariant &v) { clock()->setPlayMode(tcPlayModeFromKey(v.toString())); });
        num("tc/position", "Timecode › Position (s)", 0, day, 0.0, [clock] { return clock()->position(); }, [clock](double v) { clock()->seek(v); })
            .step(0.04)
            .ramp(Parameter::Ramp::Cut)
            .animatable(false);
        add("tc/play", "Timecode › Play", T::Bool)
            .ramp(Parameter::Ramp::Cut)
            .byDefault(true)
            .bind([clock] { return QVariant(clock()->playing()); }, [clock](const QVariant &v) { clock()->setPlaying(v.toBool()); });
        add("tc/rewind", "Timecode › Rewind", T::Trigger).bind(nullptr, [clock](const QVariant &) { clock()->rewind(); });
    }

    // A Countdown, Stopwatch or Clock generator: its settings, then its transport (countdown, stopwatch). Set at once,
    // not animated. In a snapshot, the elapsed time of one that runs there and runs already is left alone: it goes on
    // (Engine::applyParams). Its value and whether a countdown reached zero are read (an interaction can follow them)
    if (std::shared_ptr<ClockGenerator> c = clockGen) {
        ClockGenerator *k = c.get(); // the parameters are made again when the generator changes
        const ClockKind kind = k->kind();
        const ClockSettings def = ClockSettings::defaults(kind);
        auto setting = [&](const QString &path, const QString &label, T type, const QVariant &byDef,
                           std::function<QVariant(const ClockSettings &)> get,
                           std::function<void(ClockSettings &, const QVariant &)> set) -> Parameter & {
            return add(path, label, type)
                .ramp(Parameter::Ramp::Cut)
                .byDefault(byDef)
                .bind([k, get] { return get(k->settings()); },
                      [k, set](const QVariant &v) { k->update([&](ClockSettings &s) { set(s, v); }); });
        };
        auto secs = [&](const QString &path, const QString &label, double lo, double hi, double llo, double lhi, double byDef,
                        double ClockSettings::*m) -> Parameter & {
            return setting(path, label, T::Float, byDef, [m](const ClockSettings &s) { return QVariant(s.*m); },
                           [m](ClockSettings &s, const QVariant &v) { s.*m = v.toDouble(); })
                .range(lo, hi)
                .limits(llo, lhi)
                .step(0.1)
                .animatable(false);
        };
        auto text = [&](const QString &path, const QString &label, const QString &byDef, QString ClockSettings::*m) -> Parameter & {
            return setting(path, label, T::Text, byDef, [m](const ClockSettings &s) { return QVariant(s.*m); },
                           [m](ClockSettings &s, const QVariant &v) { s.*m = v.toString(); });
        };
        const double day = 24.0 * 3600.0, most = 100 * day;
        const QString name = clockKindName(kind);
        if (kind == ClockKind::Countdown) {
            setting("clock/target", name + " › Target", T::Choice, QStringLiteral("duration"),
                    [](const ClockSettings &s) {
                        return QVariant(s.target == ClockSettings::Target::TimeOfDay ? QStringLiteral("time_of_day")
                                                                                      : QStringLiteral("duration"));
                    },
                    [](ClockSettings &s, const QVariant &v) {
                        s.target = v.toString() == QLatin1String("time_of_day") ? ClockSettings::Target::TimeOfDay
                                                                                 : ClockSettings::Target::Duration;
                    })
                .choices({"duration", "time_of_day"});
            secs("clock/duration", name + " › Duration (s)", 0, day, 0, most, def.duration, &ClockSettings::duration).step(1);
            setting("clock/target_time", name + " › Target Time", T::Text, def.targetTime.toString(QStringLiteral("HH:mm:ss")),
                    [](const ClockSettings &s) { return QVariant(s.targetTime.toString(QStringLiteral("HH:mm:ss"))); },
                    [](ClockSettings &s, const QVariant &v) {
                        const QTime t = ClockGenerator::parseTime(v.toString());
                        if (t.isValid()) s.targetTime = t;
                    });
        }
        if (kind == ClockKind::Stopwatch)
            secs("clock/offset", name + " › Start At (s)", 0, day, -most, most, def.offset, &ClockSettings::offset).step(1);
        setting("clock/format", name + " › Format", T::Choice, def.format, [](const ClockSettings &s) { return QVariant(s.format); },
                [](ClockSettings &s, const QVariant &v) { s.format = v.toString(); })
            .choices(kind == ClockKind::Clock ? ClockGenerator::clockFormats() : ClockGenerator::durationFormats());
        text("clock/custom_format", name + " › Custom Format", QString(), &ClockSettings::customFormat);
        if (kind != ClockKind::Clock)
            setting("clock/fps", name + " › Frames per Second", T::Int, def.fps, [](const ClockSettings &s) { return QVariant(s.fps); },
                    [](ClockSettings &s, const QVariant &v) { s.fps = std::clamp(v.toInt(), 1, 1000); })
                .range(1, 120)
                .limits(1, 1000);
        if (kind == ClockKind::Countdown) {
            static const char *const kZero[] = {"stop", "text", "overtime"};
            setting("clock/at_zero", name + " › At Zero", T::Choice, QStringLiteral("stop"),
                    [](const ClockSettings &s) { return QVariant(QString::fromLatin1(kZero[int(s.atZero)])); },
                    [](ClockSettings &s, const QVariant &v) {
                        for (int z = 0; z < 3; ++z)
                            if (v.toString() == QLatin1String(kZero[z])) s.atZero = ClockSettings::AtZero(z);
                    })
                .choices({"stop", "text", "overtime"});
            text("clock/zero_text", name + " › Text at Zero", def.zeroText, &ClockSettings::zeroText);
            for (int q = 0; q < 4; ++q) {
                const QColor &dc = def.overtimeColor;
                const double d = q == 0 ? dc.redF() : q == 1 ? dc.greenF() : q == 2 ? dc.blueF() : dc.alphaF();
                setting(QStringLiteral("clock/overtime_color/") + QChar("rgba"[q]), name + " › Overtime Color " + QChar("RGBA"[q]),
                        T::Float, d,
                        [q](const ClockSettings &s) {
                            const QColor &x = s.overtimeColor;
                            return QVariant(q == 0 ? x.redF() : q == 1 ? x.greenF() : q == 2 ? x.blueF() : x.alphaF());
                        },
                        [q](ClockSettings &s, const QVariant &v) {
                            QColor &x = s.overtimeColor;
                            const float f = float(std::clamp(v.toDouble(), 0.0, 1.0));
                            if (q == 0) x.setRedF(f);
                            else if (q == 1) x.setGreenF(f);
                            else if (q == 2) x.setBlueF(f);
                            else x.setAlphaF(f);
                        })
                    .range(0, 1)
                    .colorComponent(q);
            }
        }
        if (kind == ClockKind::Clock) {
            setting("clock/seconds", name + " › Seconds", T::Bool, def.seconds, [](const ClockSettings &s) { return QVariant(s.seconds); },
                    [](ClockSettings &s, const QVariant &v) { s.seconds = v.toBool(); });
            setting("clock/date", name + " › Date", T::Bool, def.date, [](const ClockSettings &s) { return QVariant(s.date); },
                    [](ClockSettings &s, const QVariant &v) { s.date = v.toBool(); });
        }
        if (kind != ClockKind::Stopwatch) text("clock/zone", name + " › Time Zone", QString(), &ClockSettings::zone);
        if (k->hasTransport()) {
            // Where it is, then whether it runs: a recall that also sets where it is starts (or holds) it from there
            num("clock/elapsed", name + " › Elapsed (s)", 0, day, 0.0, [k] { return k->elapsed(); }, [k](double v) { k->setElapsed(v); })
                .limits(0, most)
                .step(0.1)
                .ramp(Parameter::Ramp::Cut)
                .animatable(false);
            add("clock/play", name + " › Run", T::Bool)
                .ramp(Parameter::Ramp::Cut)
                .byDefault(false)
                .bind([k] { return QVariant(k->running()); }, [k](const QVariant &v) { k->setRunning(v.toBool()); });
            // From its start, running: a snapshot or a preset that holds it on starts it again (read back as off: an
            // action of the recall, as a media's restart)
            add("clock/restart", name + " › Restart on Recall", T::Bool)
                .ramp(Parameter::Ramp::Cut)
                .byDefault(false)
                .bind([] { return QVariant(false); },
                      [k](const QVariant &v) {
                          if (v.toBool()) k->restart();
                      });
            add("clock/start", name + " › Start", T::Trigger).bind(nullptr, [k](const QVariant &) { k->start(); });
            add("clock/pause", name + " › Pause", T::Trigger).bind(nullptr, [k](const QVariant &) { k->pause(); });
            add("clock/reset", name + " › Reset", T::Trigger).bind(nullptr, [k](const QVariant &) { k->reset(); });
        }
        add("clock/value", name + " › Value (s)", T::Float)
            .range(kind == ClockKind::Countdown ? -day : 0, day)
            .limits(-most, most)
            .readOnly()
            .bind([k] { return QVariant(k->value()); }, nullptr);
        if (kind == ClockKind::Countdown)
            add("clock/zero", name + " › Zero Reached", T::Bool).readOnly().bind([k] { return QVariant(k->finished()); }, nullptr);
    }

    // Text generator
    if (isText()) {
        TextSource *t = &text;
        auto textNum = [&](const QString &path, const QString &label, double lo, double hi, double llo, double lhi, double def,
                           float TextSource::*m) -> Parameter & {
            return num(path, label, lo, hi, def, [t, m] { return m ? double(t->*m) : double(t->size); },
                       [t, m](double v) {
                           if (m) t->*m = float(v);
                           else t->size = int(std::lround(v));
                       })
                .limits(llo, lhi);
        };
        const TextSource def;
        add("text/content", "Text › Content", T::Text)
            .ramp(Parameter::Ramp::Typed)
            .byDefault(QString())
            .bind([t] { return QVariant(t->content); }, [t](const QVariant &v) { t->content = v.toString(); });
        add("text/font", "Text › Font", T::Text).byDefault(def.font).bind([t] { return QVariant(t->font); }, [t](const QVariant &v) {
            t->font = v.toString().isEmpty() ? QStringLiteral("Arial") : v.toString();
        });
        static const char *const kH[] = {"left", "center", "right", "justify"};
        static const Qt::AlignmentFlag kHF[] = {Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight, Qt::AlignJustify};
        static const char *const kV[] = {"top", "middle", "bottom"};
        static const Qt::AlignmentFlag kVF[] = {Qt::AlignTop, Qt::AlignVCenter, Qt::AlignBottom};
        add("text/align", "Text › Align", T::Choice)
            .choices({"left", "center", "right", "justify"})
            .byDefault(QStringLiteral("center"))
            .bind(
                [t] {
                    for (int k = 0; k < 4; ++k)
                        if (t->align & kHF[k]) return QVariant(QString::fromLatin1(kH[k]));
                    return QVariant(QStringLiteral("left"));
                },
                [t](const QVariant &v) {
                    for (int k = 0; k < 4; ++k)
                        if (v.toString() == QLatin1String(kH[k])) t->align = Qt::Alignment((t->align & Qt::AlignVertical_Mask) | kHF[k]);
                });
        add("text/v_align", "Text › Vertical Align", T::Choice)
            .choices({"top", "middle", "bottom"})
            .byDefault(QStringLiteral("middle"))
            .bind(
                [t] {
                    for (int k = 0; k < 3; ++k)
                        if (t->align & kVF[k]) return QVariant(QString::fromLatin1(kV[k]));
                    return QVariant(QStringLiteral("top"));
                },
                [t](const QVariant &v) {
                    for (int k = 0; k < 3; ++k)
                        if (v.toString() == QLatin1String(kV[k])) t->align = Qt::Alignment((t->align & Qt::AlignHorizontal_Mask) | kVF[k]);
                });
        flag("text/bold", "Text › Bold", def.bold, &text.bold);
        flag("text/italic", "Text › Italic", def.italic, &text.italic);
        flag("text/underline", "Text › Underline", def.underline, &text.underline);
        flag("text/strike", "Text › Strikethrough", def.strike, &text.strike);
        for (int axis = 0; axis < 2; ++axis) { // the size of its picture
            int TextSource::*f = axis ? &TextSource::height : &TextSource::width;
            add(axis ? "text/height" : "text/width", axis ? "Text › Height (px)" : "Text › Width (px)", T::Int)
                .range(1, 16384)
                .ramp(Parameter::Ramp::Cut)
                .byDefault(axis ? def.height : def.width)
                .bind([t, f] { return QVariant(t->*f); }, [t, f](const QVariant &v) { t->*f = std::clamp(v.toInt(), 1, 16384); });
        }
        textNum("text/size", "Text › Size (px)", 1, 400, 1, 1000, def.size, nullptr).step(1);
        textNum("text/line_height", "Text › Line Height", 0.5, 3, 0.1, 10, def.lineHeight, &TextSource::lineHeight);
        textNum("text/letter_spacing", "Text › Letter Spacing (px)", -20, 100, -200, 500, def.letterSpacing, &TextSource::letterSpacing)
            .step(0.5);
        textNum("text/outline/width", "Text › Outline (px)", 0, 40, 0, 200, def.outline, &TextSource::outline).step(0.5);
        flag("text/shadow/enable", "Text › Shadow", def.shadow, &text.shadow);
        textNum("text/shadow/x", "Text › Shadow X (px)", -100, 100, -2000, 2000, def.shadowX, &TextSource::shadowX).step(1);
        textNum("text/shadow/y", "Text › Shadow Y (px)", -100, 100, -2000, 2000, def.shadowY, &TextSource::shadowY).step(1);
        struct Col {
            const char *path, *label;
            QColor TextSource::*member;
            QColor def;
        };
        for (const Col &c : {Col{"text/color", "Text › Color", &TextSource::color, def.color},
                             Col{"text/outline/color", "Text › Outline Color", &TextSource::outlineColor, def.outlineColor},
                             Col{"text/shadow/color", "Text › Shadow Color", &TextSource::shadowColor, def.shadowColor}})
            for (int k = 0; k < 4; ++k) {
                QColor TextSource::*m = c.member;
                const double d = k == 0 ? c.def.redF() : k == 1 ? c.def.greenF() : k == 2 ? c.def.blueF() : c.def.alphaF();
                num(QString::fromLatin1(c.path) + "/" + QChar("rgba"[k]), QString::fromUtf8(c.label) + " " + QChar("RGBA"[k]), 0, 1, d,
                    [t, m, k] {
                        const QColor &q = t->*m;
                        return k == 0 ? q.redF() : k == 1 ? q.greenF() : k == 2 ? q.blueF() : q.alphaF();
                    },
                    [t, m, k](double v) {
                        QColor &q = t->*m;
                        if (k == 0) q.setRedF(float(v));
                        else if (k == 1) q.setGreenF(float(v));
                        else if (k == 2) q.setBlueF(float(v));
                        else q.setAlphaF(float(v));
                    }).colorComponent(k);
            }
    }

    if (type == SourceType::Tox && toxPlayer) {
        flag("tox/play", "TouchDesigner › Play", true, &tox.playing)
            .bind([self] { return QVariant(self->tox.playing); }, [self](const QVariant &v) { self->tox.playing = v.toBool(); self->applyTox(); });
        num("tox/fps", "TouchDesigner › Frame Rate", 1, 120, 30, [self] { return self->tox.fps; },
            [self](double v) { self->tox.fps = v; self->applyTox(); }).animatable(false);
        add("tox/output", "TouchDesigner › Output TOP", T::Text).byDefault(QString()).bind(
            [self] { return QVariant(self->tox.output); },
            [self](const QVariant &v) { self->tox.output = v.toString(); self->applyTox(); });
        add("tox/reload", "TouchDesigner › Reload", T::Trigger).bind(nullptr, [self](const QVariant &) { self->toxPlayer->reload(); });
        for (const ToxControl &c : toxPlayer->controls()) {
            for (int k = 0; k < c.count; ++k) {
                Parameter &p = add(c.path(k), "TouchDesigner › " + c.label + (c.count > 1 ? " " + c.path(k).section('/', -1).toUpper() : QString()), c.type);
                if (c.type == T::Trigger) { p.bind(nullptr, [self, c](const QVariant &) { self->toxPlayer->pulse(c.id); }); continue; }
                const double lo = k < int(c.minimum.size()) ? c.minimum[size_t(k)] : 0;
                const double hi = k < int(c.maximum.size()) ? c.maximum[size_t(k)] : 1;
                p.range(lo, std::max(lo, hi)).byDefault(c.value(ToxSettings(), k))
                    .bind([self, c, k] { return c.value(self->tox, k); },
                          [self, c, k](const QVariant &v) { c.setValue(self->tox, k, v); self->applyTox(); });
                if (c.color && k < 4) p.colorComponent(k);
            }
        }
    }

    // Web Page generator (WebPage.h): each change goes to its page at once (a new URL loads it; the others keep it)
    if (type == SourceType::Web) {
        WebSource *w = &web;
        const WebSource def;
        add("web/url", "Web › URL", T::Text)
            .byDefault(QString())
            .bind([w] { return QVariant(w->url); },
                  [self](const QVariant &v) {
                      self->web.url = v.toString();
                      self->applyWeb();
                  });
        for (int axis = 0; axis < 2; ++axis) { // the size of its picture: the composition's by default
            int WebSource::*f = axis ? &WebSource::height : &WebSource::width;
            add(axis ? "web/height" : "web/width", axis ? "Web › Height (px)" : "Web › Width (px)", T::Int)
                .range(16, 7680)
                .limits(1, WebSource::kMaxSize)
                .ramp(Parameter::Ramp::Cut)
                .animatable(false)
                .byDefault([C, axis] { return QVariant(axis ? C().height() : C().width()); })
                .bind([w, f] { return QVariant(w->*f); },
                      [self, f](const QVariant &v) {
                          self->web.*f = v.toInt();
                          self->applyWeb();
                          self->srcWidth = self->web.width;
                          self->srcHeight = self->web.height;
                      });
        }
        num("web/fps", "Web › Frame Rate (fps)", WebSource::kMinFps, WebSource::kMaxFps, def.fps, [w] { return w->fps; },
            [self](double v) {
                self->web.fps = v;
                self->applyWeb();
            })
            .step(1)
            .ramp(Parameter::Ramp::Cut)
            .animatable(false);
        num("web/zoom", "Web › Zoom", WebSource::kMinZoom, WebSource::kMaxZoom, def.zoom, [w] { return w->zoom; },
            [self](double v) {
                self->web.zoom = v;
                self->applyWeb();
            })
            .step(0.05)
            .ramp(Parameter::Ramp::Cut)
            .animatable(false);
        add("web/transparent", "Web › Transparent Background", T::Bool)
            .byDefault(def.transparent)
            .bind([w] { return QVariant(w->transparent); },
                  [self](const QVariant &v) {
                      self->web.transparent = v.toBool();
                      self->applyWeb();
                  });
        add("web/reload", "Web › Reload", T::Trigger).bind(nullptr, [self](const QVariant &) {
            if (self->webPage) self->webPage->reload();
        });
    }

    // The shaders: the generator's at the layer's top (its speed: the pace of its TIME), each effect under fx/<fx>/
    auto shader = [&](const IsfInstance &inst, const QString &prefix, const QString &title, bool effect) {
        for (Parameter p : inst.parameters(title)) {
            if (!effect && (p.path() == "enable" || p.path() == "mask/invert")) continue; // an effect's
            if (!effect && p.path() == "speed" && hasTransport()) continue;              // the media's speed instead
            paramStore.push_back(std::make_unique<Parameter>(p.prefixed(prefix)));
        }
    };
    if (generator && generator->isValid() && !hasTransport()) shader(*generator, QString(), QStringLiteral("Source"), false);
    QStringList names;
    for (const auto &x : effects) names << x->name();
    const QStringList segs = osc::uniqueSegments(names);
    for (size_t k = 0; k < effects.size(); ++k)
        shader(*effects[k], QStringLiteral("fx/%1/").arg(segs[int(k)]), QStringLiteral("FX › %1").arg(effects[k]->name()), true);

    paramIndex.clear();
    for (auto &p : paramStore) {
        paramList.push_back(p.get());
        paramIndex.insert(p->path(), p.get());
    }
    // Values read from a file for parameters it did not have then (its media was missing): theirs now
    if (!extraParams.isEmpty()) parametersFromJson(paramList, QJsonObject(extraParams), &extraParams);
    return paramList;
}

Parameter *Layer::parameter(const QString &path)
{
    parameters();
    return paramIndex.value(path);
}

void Layer::setSpeed(double v)
{
    if (v == speed) return;
    const int d = v < 0 ? -1 : v > 0 ? 1 : dir;
    const double pos = position();
    speed = v;
    if (d != dir && hasTransport()) reposition(*this, pos, d); // the other way from the same place
}

void Layer::setPlayMode(PlayMode m)
{
    if (m == mode) return; // as it is (a Stop layer that ended stays so)
    const double pos = position();
    mode = m;
    ended = false;
    if (hasTransport()) reposition(*this, pos, dir); // same place, new mode
}

void Layer::setInOut(double in, double out)
{
    const double d = duration();
    in = std::max(0.0, in);
    if (d > 0) {
        in = std::min(in, d);
        if (out >= 0) out = std::clamp(out, 0.0, d);
        if (out >= d - 1e-6) out = -1; // up to the end: follows the media
    }
    if (out >= 0 && out < in + 0.01) out = in + 0.01; // at least a few milliseconds
    if (std::abs(inPoint - in) < 1e-9 && std::abs(outPoint - out) < 1e-9) return;
    const double pos = position();
    inPoint = in;
    outPoint = out;
    if (hasTransport()) reposition(*this, pos, dir); // clamped into the new range
}
