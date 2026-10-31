#include "StoredContent.h"
#include "EngineInternal.h"
#include <QJsonDocument>
#include <algorithm>

namespace {
using Owner = StoredContent::Owner;
QJsonValue at(const QJsonObject &object, const QStringList &path, int i = 0)
{
    if (i == path.size())
        return object;
    const auto value = object.value(path[i]);
    return i + 1 == path.size() ? value : at(value.toObject(), path, i + 1);
}
QJsonObject with(QJsonObject object, const QStringList &path, const QJsonValue &value, int i = 0)
{
    if (i >= path.size())
        return value.toObject();
    if (i + 1 == path.size()) {
        if (value.isUndefined())
            object.remove(path[i]);
        else
            object[path[i]] = value;
    } else
        object[path[i]] = with(object.value(path[i]).toObject(), path, value, i + 1);
    return object;
}
QJsonObject overlay(QJsonObject base, const QJsonObject &added)
{
    for (auto i = added.begin(); i != added.end(); ++i)
        base[i.key()] = i.value().isObject() && base.value(i.key()).isObject()
                            ? QJsonValue(overlay(base.value(i.key()).toObject(), i.value().toObject()))
                            : QJsonValue(i.value());
    return base;
}
const QJsonArray &rows(const Engine::Snapshot &snapshot, Owner owner)
{
    return owner == Owner::Interaction ? snapshot.interactions
           : owner == Owner::Fixture   ? snapshot.fixtures
                                       : snapshot.layers;
}
QJsonArray &rows(Engine::Snapshot &snapshot, Owner owner)
{
    return owner == Owner::Interaction ? snapshot.interactions
           : owner == Owner::Fixture   ? snapshot.fixtures
                                       : snapshot.layers;
}
int index(const QJsonArray &rows, const QString &id)
{
    for (int i = 0; i < rows.size(); ++i)
        if (rows[i].toObject().value("id").toString() == id)
            return i;
    return -1;
}
QJsonObject object(const Engine::Snapshot &snapshot, const StoredContent::Selection &selection)
{
    if (selection.owner == Owner::Composition)
        return snapshot.composition;
    const auto &list = selection.owner == Owner::Interaction ? snapshot.interactions
                       : selection.owner == Owner::Fixture   ? snapshot.fixtures
                                                             : snapshot.layers;
    const int i = index(list, selection.id);
    return i < 0 ? QJsonObject{} : list[i].toObject();
}
void put(Engine::Snapshot &snapshot, const StoredContent::Selection &selection, const QJsonObject &object)
{
    if (selection.owner == Owner::Composition) {
        snapshot.composition = object;
        return;
    }
    auto &list = rows(snapshot, selection.owner);
    const int i = index(list, selection.id);
    if (object.isEmpty()) {
        if (i >= 0)
            list.removeAt(i);
    } else if (i < 0)
        list.append(object);
    else
        list[i] = object;
}
QString address(const QStringList &path)
{
    if (path.isEmpty())
        return {};
    if (path[0] == "params")
        return path.value(1);
    return path[0] == "source" ? QStringLiteral("file") : path[0];
}
void timing(QJsonObject &destination, const QJsonObject &source, const QString &address)
{
    for (const auto *name : {"timing", "easing", "delay"}) {
        auto values = destination.value(name).toObject();
        const auto original = source.value(name).toObject();
        const auto key = Engine::timingKeyOf(original, address);
        if (!key.isEmpty())
            values[address] = original.value(key);
        if (!values.isEmpty())
            destination[name] = values;
    }
}
bool chosen(const Engine::PresetValue &value, const StoredContent::PresetSelection &item)
{
    return value.layer == item.layer && value.path == item.key;
}
} // namespace
Engine::Snapshot StoredContent::select(const Engine::Snapshot &snapshot, const std::vector<Selection> &selections)
{
    Engine::Snapshot fragment;
    fragment.partial = true;
    fragment.fade = snapshot.fade;
    for (const auto &selection : selections) {
        const auto original = object(snapshot, selection);
        if (original.isEmpty())
            continue;
        auto selected = object(fragment, selection);
        if (selection.path.isEmpty())
            selected = original;
        else {
            const auto value = at(original, selection.path);
            if (value.isUndefined())
                continue;
            for (const auto *key : {"id", "included", "group", "viewport"})
                if (original.contains(key))
                    selected[key] = original.value(key);
            if (original.contains("name"))
                selected["_label"] = original.value("name");
            selected["_partial"] = true;
            selected = with(selected, selection.path, value);
            timing(selected, original, address(selection.path));
        }
        put(fragment, selection, selected);
    }
    return fragment;
}
void StoredContent::remove(Engine::Snapshot &snapshot, const std::vector<Selection> &selections)
{
    snapshot.partial = true;
    for (const auto &selection : selections) {
        auto original = object(snapshot, selection);
        if (original.isEmpty())
            continue;
        if (selection.path.isEmpty()) {
            put(snapshot, selection, {});
            continue;
        }
        original["_partial"] = true;
        original = with(original, selection.path, QJsonValue(QJsonValue::Undefined));
        for (const auto *key : {"timing", "easing", "delay"}) {
            auto values = original.value(key).toObject();
            const auto path = address(selection.path);
            for (auto it = values.begin(); it != values.end();)
                if (it.key() == path || it.key().startsWith(path + '/'))
                    it = values.erase(it);
                else
                    ++it;
            if (values.isEmpty())
                original.remove(key);
            else
                original[key] = values;
        }
        put(snapshot, selection, original);
    }
}
void StoredContent::merge(Engine::Snapshot &snapshot, const Engine::Snapshot &fragment)
{
    if (snapshot.layers.isEmpty() && snapshot.composition.isEmpty() && snapshot.interactions.isEmpty() &&
        snapshot.fixtures.isEmpty())
        snapshot.partial = true;
    for (auto owner : {Owner::Layer, Owner::Interaction, Owner::Fixture})
        for (const auto &value : rows(fragment, owner)) {
            auto added = value.toObject();
            Selection s{owner, added.value("id").toString(), {}};
            put(snapshot, s, overlay(object(snapshot, s), added));
        }
    snapshot.composition = overlay(snapshot.composition, fragment.composition);
}
Engine::Preset StoredContent::select(const Engine::Preset &preset, const std::vector<PresetSelection> &items)
{
    Engine::Preset result;
    result.fade = preset.fade;
    for (const auto &item : items) {
        for (const auto &value : preset.values)
            if (chosen(value, item) && result.indexOf(value.layer, value.path) < 0)
                result.values.push_back(value);
        if (item.key.startsWith("@category/")) {
            const auto key = item.key.mid(10);
            if (preset.categories.contains(key))
                result.categories[key] = preset.categories.value(key);
        }
        if (item.key.startsWith("@geometry/")) {
            const auto key = item.key.mid(10);
            if (preset.geometry.contains(key))
                result.geometry[key] = preset.geometry.value(key);
        }
        if (item.key.startsWith("@animation/"))
            for (const auto &a : preset.animations)
                if (!a.tracks.empty() && a.tracks.front().layer == item.layer &&
                    a.tracks.front().param == item.key.mid(11))
                    result.animations.push_back(a);
        if (item.key.startsWith("@interaction/"))
            for (const auto &i : preset.interactions)
                if (i.id == item.key.mid(13).toULongLong())
                    result.interactions.push_back(i);
    }
    return result;
}
void StoredContent::remove(Engine::Preset &preset, const std::vector<PresetSelection> &items)
{
    auto removed = select(preset, items);
    preset.values.erase(std::remove_if(preset.values.begin(), preset.values.end(),
                                       [&](const auto &v) { return removed.indexOf(v.layer, v.path) >= 0; }),
                        preset.values.end());
    for (auto it = removed.categories.begin(); it != removed.categories.end(); ++it)
        preset.categories.remove(it.key());
    for (auto it = removed.geometry.begin(); it != removed.geometry.end(); ++it)
        preset.geometry.remove(it.key());
    preset.animations.erase(
        std::remove_if(preset.animations.begin(), preset.animations.end(),
                       [&](const auto &a) {
                           return std::any_of(removed.animations.begin(), removed.animations.end(), [&](const auto &r) {
                               return !a.tracks.empty() && !r.tracks.empty() &&
                                      a.tracks.front().layer == r.tracks.front().layer &&
                                      a.tracks.front().param == r.tracks.front().param;
                           });
                       }),
        preset.animations.end());
    preset.interactions.erase(std::remove_if(preset.interactions.begin(), preset.interactions.end(),
                                             [&](const auto &i) {
                                                 return std::any_of(removed.interactions.begin(),
                                                                    removed.interactions.end(),
                                                                    [&](const auto &r) { return r.id == i.id; });
                                             }),
                              preset.interactions.end());
}
void StoredContent::merge(Engine::Preset &preset, const Engine::Preset &added)
{
    for (const auto &value : added.values) {
        const int i = preset.indexOf(value.layer, value.path);
        if (i < 0)
            preset.values.push_back(value);
        else
            preset.values[size_t(i)] = value;
    }
    // These entries represent complete definitions, and replace the matching definition atomically.
    for (auto it = added.categories.begin(); it != added.categories.end(); ++it)
        preset.categories[it.key()] = it.value();
    for (auto it = added.geometry.begin(); it != added.geometry.end(); ++it)
        preset.geometry[it.key()] = it.value();
    for (const auto &a : added.animations) {
        if (a.tracks.empty())
            continue;
        remove(preset, {{a.tracks.front().layer, "@animation/" + a.tracks.front().param}});
        preset.animations.push_back(a);
    }
    for (const auto &i : added.interactions) {
        remove(preset, {{0, QStringLiteral("@interaction/%1").arg(i.id)}});
        preset.interactions.push_back(i);
    }
}
Engine::Snapshot StoredContent::fromPreset(const Engine::Preset &preset)
{
    Engine::Snapshot out;
    out.partial = true;
    out.fade = preset.fade;
    auto layerRow = [&](quint64 layer) {
        Selection selection{Owner::Layer, QString::number(layer), {}};
        auto row = object(out, selection);
        if (row.isEmpty())
            row = {{"id", QString::number(layer)}, {"_partial", true}};
        return row;
    };
    for (const auto &value : preset.values) {
        Owner owner = value.layer ? Owner::Layer : Owner::Composition;
        QString id = QString::number(value.layer), path = value.path;
        if (!value.layer && (path.startsWith("interaction/") || path.startsWith("fixture/"))) {
            owner = path.startsWith("interaction/") ? Owner::Interaction : Owner::Fixture;
            id = path.section('/', 1, 1);
            path = path.section('/', 2);
        }
        Selection selection{owner, id, {}};
        auto row = object(out, selection);
        if (owner != Owner::Composition) {
            row["id"] = id;
            row["_partial"] = true;
        }
        if (owner == Owner::Composition || (owner == Owner::Layer && path == "enable"))
            row[path] = value.value;
        else {
            auto params = row.value("params").toObject();
            params[path] = value.value;
            row["params"] = params;
        }
        if (value.time >= 0) {
            auto times = row.value("timing").toObject();
            times[path] = value.time;
            row["timing"] = times;
        }
        if (value.delay > 0) {
            auto times = row.value("delay").toObject();
            times[path] = value.delay;
            row["delay"] = times;
        }
        if (!value.easing.isEmpty()) {
            auto times = row.value("easing").toObject();
            times[path] = value.easing;
            row["easing"] = times;
        }
        put(out, selection, row);
    }
    for (auto it = preset.categories.begin(); it != preset.categories.end(); ++it) {
        auto id = it.key().section(':', 0, 0);
        Selection selection{Owner::Layer, id, {}};
        auto row = overlay(object(out, selection), it.value().toObject());
        row["id"] = id;
        row["_partial"] = true;
        put(out, selection, row);
    }
    for (auto it = preset.geometry.begin(); it != preset.geometry.end(); ++it) {
        auto row = layerRow(it.key().toULongLong());
        row["mesh"] = it.value();
        put(out, {Owner::Layer, it.key(), {}}, row);
    }
    for (const auto &a : preset.animations)
        if (!a.tracks.empty()) {
            auto row = layerRow(a.tracks.front().layer);
            auto anims = row.value("anims").toArray();
            anims.append(layerAnimToJson(a));
            row["anims"] = anims;
            put(out, {Owner::Layer, QString::number(a.tracks.front().layer), {}}, row);
        }
    for (const auto &i : preset.interactions) {
        Selection selection{Owner::Interaction, QString::number(i.id), {}};
        auto row = object(out, selection);
        row["id"] = QString::number(i.id);
        row["name"] = i.name;
        row["definition"] = i.toJson();
        put(out, selection, row);
    }
    return out;
}
Engine::Preset StoredContent::toPreset(const Engine::Snapshot &snapshot)
{
    Engine::Preset out;
    out.fade = snapshot.fade;
    auto values = [&](quint64 layer, const QString &prefix, const QJsonObject &params, const QJsonObject &row) {
        for (auto it = params.begin(); it != params.end(); ++it) {
            Engine::PresetValue value;
            value.layer = layer;
            value.path = prefix + it.key();
            value.value = it.value();
            const auto t = row.value("timing").toObject(), e = row.value("easing").toObject(),
                       d = row.value("delay").toObject();
            value.time = t.value(Engine::timingKeyOf(t, it.key())).toDouble(-1);
            value.easing = e.value(Engine::timingKeyOf(e, it.key())).toString();
            value.delay = d.value(Engine::timingKeyOf(d, it.key())).toDouble();
            out.values.push_back(value);
        }
    };
    for (const auto &v : snapshot.layers) {
        auto row = v.toObject();
        if (!row.value("included").toBool(true))
            continue;
        const auto id = row.value("id").toString().toULongLong();
        values(id, {}, row.value("params").toObject(), row);
        if (row.contains("enable")) {
            Engine::PresetValue visible;
            visible.layer = id;
            visible.path = "enable";
            visible.value = row.value("enable");
            visible.time = 0;
            out.values.push_back(visible);
        }
        for (auto entry : {std::pair<const char *, int>{"source", Engine::PartSource},
                           {"fx", Engine::PartEffects},
                           {"masks", Engine::PartMasks},
                           {"viewports", Engine::PartViewports}})
            if (row.contains(entry.first)) {
                auto key = QStringLiteral("%1:%2").arg(id).arg(entry.second);
                auto category = out.categories.value(key).toObject();
                category[entry.first] = row.value(entry.first);
                if (entry.second == Engine::PartMasks && row.contains("matte"))
                    category["matte"] = row.value("matte");
                out.categories[key] = category;
            }
        if (row.contains("mesh"))
            out.geometry[QString::number(id)] = row.value("mesh");
        for (const auto &a : row.value("anims").toArray())
            out.animations.push_back(layerAnimFromJson(a.toObject(), id));
    }
    auto composition = snapshot.composition;
    for (const auto *key :
         {"included", "timing", "easing", "delay", "_partial", "_label", "id", "name", "group", "viewport"})
        composition.remove(key);
    if (snapshot.composition.value("included").toBool(true))
        values(0, {}, composition, snapshot.composition);
    for (auto owner : {Owner::Interaction, Owner::Fixture})
        for (const auto &v : rows(snapshot, owner)) {
            auto row = v.toObject();
            if (!row.value("included").toBool(true))
                continue;
            const auto id = row.value("id").toString();
            values(0, (owner == Owner::Interaction ? "interaction/" : "fixture/") + id + '/',
                   row.value("params").toObject(), row);
            if (owner == Owner::Interaction && row.contains("definition"))
                out.interactions.push_back(Interaction::fromJson(row.value("definition").toObject()));
        }
    return out;
}
QByteArray StoredContent::encode(const Engine::Snapshot &fragment)
{
    return QJsonDocument(QJsonObject{{"format", "nyxmap-stored-content"},
                                     {"version", 1},
                                     {"fade", fragment.fade},
                                     {"layers", fragment.layers},
                                     {"composition", fragment.composition},
                                     {"interactions", fragment.interactions},
                                     {"fixtures", fragment.fixtures}})
        .toJson(QJsonDocument::Compact);
}
bool StoredContent::decode(const QByteArray &bytes, Engine::Snapshot *out)
{
    if (!out || bytes.isEmpty() || bytes.size() > 16 * 1024 * 1024)
        return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    const auto object = document.object();
    if (error.error != QJsonParseError::NoError || object.value("format") != "nyxmap-stored-content" ||
        object.value("version").toInt() != 1)
        return false;
    for (const auto *key : {"layers", "interactions", "fixtures"})
        if (!object.value(key).isArray())
            return false;
    if (!object.value("composition").isObject())
        return false;
    Engine::Snapshot fragment;
    fragment.partial = true;
    fragment.fade = std::clamp(object.value("fade").toDouble(1), 0., 600.);
    fragment.layers = object.value("layers").toArray();
    fragment.interactions = object.value("interactions").toArray();
    fragment.fixtures = object.value("fixtures").toArray();
    fragment.composition = object.value("composition").toObject();
    for (auto owner : {Owner::Layer, Owner::Interaction, Owner::Fixture})
        for (const auto &row : rows(fragment, owner))
            if (!row.isObject() || !row.toObject().value("id").toString().toULongLong())
                return false;
    *out = std::move(fragment);
    return true;
}
