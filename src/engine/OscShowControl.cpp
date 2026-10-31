#include "Osc.h"
#include "Engine.h"
#include "SequenceFeedback.h"
#include <QJsonDocument>
#include <cmath>

namespace {
QString json(const QJsonObject &value) { return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)); }
QString idText(quint64 id) { return QString::number(id); }
bool trigger(const QVariantList &args) { return args.isEmpty() || !args[0].isValid() || args[0].toDouble() != 0; }
double finiteTime(double seconds) { return std::isfinite(seconds) ? seconds : -1; }

QJsonObject catalog(Engine *e)
{
    Engine::Lock lock(&e->mutex());
    QJsonArray snapshots, presets, timelines, sequences, layers;
    auto item = [](quint64 id, int index, const QString &name) {
        return QJsonObject{{"id", idText(id)}, {"index", index + 1}, {"name", name}};
    };
    for (int k = 0; k < e->layerCount(); ++k) {
        const Layer *layer = e->layer(k);
        auto entry = item(layer->id, k, layer->name);
        entry["parent"] = idText(layer->parent);
        entry["group"] = layer->isGroup;
        entry["viewport"] = layer->isViewport;
        entry["path"] = "/show/layer/by-id/" + idText(layer->id);
        layers.append(entry);
    }
    for (int k = 0; k < e->snapshotCount(); ++k) {
        const auto value = e->snapshot(k);
        snapshots.append(item(value.id, k, value.name));
    }
    for (int k = 0; k < e->presetCount(); ++k) {
        const auto value = e->preset(k);
        presets.append(item(value.id, k, value.name));
    }
    for (int k = 0; k < e->animationCount(); ++k) {
        const auto value = e->animation(k);
        auto entry = item(value.id, k, value.name);
        entry["duration"] = value.duration;
        timelines.append(entry);
    }
    auto &sq = e->sequencer();
    for (int k = 0; k < sq.count(); ++k) {
        const auto value = sq.sequence(k);
        auto entry = item(sq.sequenceId(k), k, value.name);
        entry["loop"] = value.loop;
        entry["edit_mode"] = value.editMode;
        QJsonArray cues;
        for (size_t n = 0; n < value.steps.size(); ++n) {
            const auto &step = value.steps[n];
            QString kind = "empty", targetName;
            quint64 target = 0;
            int index = -1;
            if (!step.actions.empty()) {
                kind = "group";
                targetName = QStringLiteral("%1 actions").arg(step.actions.size());
            } else if (step.sequence) {
                kind = "sequence"; target = step.sequence; index = sq.indexOfSequence(target);
                if (index >= 0) targetName = sq.sequence(index).name;
            } else if (step.preset) {
                kind = "preset"; target = step.preset; index = e->indexOfPreset(target);
                if (index >= 0) targetName = e->preset(index).name;
            } else if (step.timeline) {
                kind = "timeline"; target = step.timeline; index = e->indexOfAnimation(target);
                if (index >= 0) targetName = e->animation(index).name;
            } else if (step.snapshot) {
                kind = "snapshot"; target = step.snapshot; index = e->indexOfSnapshot(target);
                if (index >= 0) targetName = e->snapshot(index).name;
            }
            auto cue = item(step.runtimeId, int(n), targetName);
            if (!step.actions.empty()) {
                QJsonArray actions;
                for (const auto &action : step.actions)
                    actions.append(Sequencer::stepToJson(action));
                cue["actions"] = actions;
            }
            cue["text"] = step.text; cue["description"] = step.description;
            cue["target_type"] = kind; cue["target_id"] = idText(target); cue["missing"] = target && index < 0;
            cue["prewait"] = step.preWait; cue["postwait"] = step.postWait;
            cue["action_duration"] = finiteTime(sq.stepDuration(step));
            cue["continue"] = step.next == Sequencer::StepContinue::Follow ? "follow"
                            : step.next == Sequencer::StepContinue::AutoFollow ? "auto-follow" : "wait";
            cues.append(cue);
        }
        entry["cues"] = cues;
        sequences.append(entry);
    }
    return {{"version", 1}, {"snapshots", snapshots}, {"presets", presets}, {"timelines", timelines}, {"sequences", sequences}, {"layers", layers}};
}

QJsonObject state(Engine *e)
{
    Engine::Lock lock(&e->mutex());
    QJsonObject values;
    for (const auto &message : SequenceFeedback::stateFor(e)) {
        if (message.address == QLatin1String("/sequence/modified")) continue; // GUI-only edit indicator
        values[message.address] = QJsonValue::fromVariant(message.args.value(0));
    }
    auto &sq = e->sequencer();
    values["/sequence/id"] = idText(sq.sequenceId(sq.current()));
    values["/composition/pause"] = e->paused();
    values["/composition/blackout"] = e->blackout();
    values["/composition/speed"] = e->compositionSpeed();
    QJsonArray timelines, sequences;
    std::vector<Engine::AnimationPlayback> playback;
    e->sampleAnimations(&playback); // small telemetry; never copies keyframes
    for (const auto &a : playback) {
        const double remaining = std::isfinite(a.length) ? std::max(0.0, a.length - a.clock) : -1;
        timelines.append(QJsonObject{{"id", idText(a.id)},
            {"state", a.state == AnimState::Playing ? "playing" : a.state == AnimState::Paused ? "paused" : "stopped"},
            {"position", a.position}, {"duration", a.duration}, {"elapsed", a.clock}, {"remaining", remaining},
            {"progress", a.duration > 0 ? std::clamp(a.position / a.duration, 0.0, 1.0) : 0},
            {"pass", a.pass}, {"speed", a.speed}});
    }
    for (int k = 0; k < sq.count(); ++k)
        sequences.append(QJsonObject{{"id", idText(sq.sequenceId(k))}, {"active", sq.active(k)}});
    return {{"version", 1}, {"values", values}, {"timelines", timelines}, {"sequences", sequences}};
}
}

void OscNamespace::addShowControl()
{
    Engine *e = m_e;
    add("/show/catalog", "s", 1, "Show catalog (JSON, schema version 1)").get = [e] { return QVariantList{json(catalog(e))}; };
    add("/show/state", "s", 1, "Show playback state (JSON, schema version 1)").get = [e] { return QVariantList{json(state(e))}; };
    add("/sequence/count", "i", 1, "Sequence count").get = [e] { return QVariantList{e->sequencer().count()}; };
    add("/timeline/count", "i", 1, "Timeline count").get = [e] { return QVariantList{e->animationCount()}; };

    for (int k = 0; k < e->snapshotCount(); ++k) {
        const quint64 id = e->snapshot(k).id;
        add(QStringLiteral("/snapshot/%1/id").arg(k + 1), "s", 1, "Stable snapshot ID").get = [id] { return QVariantList{idText(id)}; };
        add("/snapshot/by-id/" + idText(id) + "/recall", "N", 2, "Recall snapshot").set = [e, id](const QVariantList &args) {
            if (!trigger(args)) return false;
            if (e->sequencer().requestSnapshot(id)) return true;
            const int index = e->indexOfSnapshot(id);
            if (index < 0) return false;
            e->recallSnapshot(index);
            return true;
        };
    }
    for (int k = 0; k < e->presetCount(); ++k) {
        const quint64 id = e->preset(k).id;
        add(QStringLiteral("/preset/%1/id").arg(k + 1), "s", 1, "Stable preset ID").get = [id] { return QVariantList{idText(id)}; };
        add("/preset/by-id/" + idText(id) + "/recall", "N", 2, "Recall preset").set = [e, id](const QVariantList &args) {
            if (!trigger(args)) return false;
            const int index = e->indexOfPreset(id);
            if (index < 0) return false;
            e->recallPreset(index);
            return true;
        };
    }
    for (int k = 0; k < e->animationCount(); ++k) {
        const quint64 id = e->animation(k).id;
        add(QStringLiteral("/timeline/%1/id").arg(k + 1), "s", 1, "Stable timeline ID").get = [id] { return QVariantList{idText(id)}; };
        add(QStringLiteral("/timeline/%1/name").arg(k + 1), "s", 1, "Timeline name").get = [e, id] {
            return QVariantList{e->animation(e->indexOfAnimation(id)).name};
        };
        const QString base = "/timeline/by-id/" + idText(id);
        for (const auto &[word, action] : {std::pair{"play", AnimAction::Play}, {"pause", AnimAction::Pause},
                                         {"stop", AnimAction::Stop}, {"rewind", AnimAction::Rewind}})
            add(base + '/' + word, "N", 2, word).set = [e, id, act = action](const QVariantList &args) {
                if (!trigger(args) || e->indexOfAnimation(id) < 0) return false;
                e->controlAnimation(id, act); return true;
            };
        for (const auto &[word, action] : {std::pair{"seek", AnimAction::Seek}, {"speed", AnimAction::Speed}})
            add(base + '/' + word, "f", 2, word).set = [e, id, act = action](const QVariantList &args) {
                bool ok = false;
                const double value = args.value(0).toDouble(&ok);
                if (!ok || !std::isfinite(value) || e->indexOfAnimation(id) < 0) return false;
                e->controlAnimation(id, act, value); return true;
            };
    }
    auto &sq = e->sequencer();
    for (int k = 0; k < sq.count(); ++k) {
        const quint64 id = sq.sequenceId(k);
        add(QStringLiteral("/sequence/%1/id").arg(k + 1), "s", 1, "Stable sequence ID").get = [id] { return QVariantList{idText(id)}; };
        const QString base = "/sequence/by-id/" + idText(id);
        for (const auto &word : {"go", "back", "stop", "select"})
            add(base + '/' + word, "N", 2, word).set = [e, id, word = QString::fromLatin1(word)](const QVariantList &args) {
                if (!trigger(args)) return false;
                auto &seq = e->sequencer();
                if (word == "go") return seq.goById(id);
                if (word == "back") return seq.backById(id);
                if (word == "stop") return seq.stopById(id);
                return seq.selectById(id);
            };
        add(base + "/cue/go", "s", 2, "GO to cue by runtime ID").set = [e, id](const QVariantList &args) {
            bool ok = false;
            const quint64 cue = args.value(0).toString().toULongLong(&ok);
            return ok && e->sequencer().cueById(id, cue);
        };
    }
}
