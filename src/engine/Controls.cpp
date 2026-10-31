// The show's controls (ShowControl.h): their list, and each as a parameter of the show (control/<key>, ShowParams.cpp).
#include "EngineInternal.h"
#include "ParameterKey.h"
#include "ShowControl.h"

namespace {
constexpr int kMaxControls = 256;
} // namespace

std::vector<ShowControl> Engine::controls() const
{
    Lock lk(&m_mutex);
    return m_controls;
}

QJsonArray Engine::controlsJson() const
{
    Lock lk(&m_mutex);
    QJsonArray a;
    for (const ShowControl &c : m_controls) a.append(c.toJson());
    return a;
}

// Names to keys, unique: "Master Level" is control/master_level, a second one control/master_level_2. Never changed once made
// (an interaction or a remote knows it by that address); a control that is renamed keeps its key
static void keyAll(std::vector<ShowControl> &list)
{
    QStringList taken;
    for (const ShowControl &c : list)
        if (!c.key.isEmpty()) taken << c.key;
    int index = 0;
    for (ShowControl &c : list) {
        ++index;
        if (!c.key.isEmpty()) continue;
        QStringList others = taken;
        c.key = faustControlKey(c.name, others, index);
        taken << c.key;
    }
}

bool Engine::setControlsJson(const QJsonArray &a, QString *error)
{
    if (a.size() > kMaxControls) {
        if (error) *error = QStringLiteral("At most %1 controls").arg(kMaxControls);
        return false;
    }
    std::vector<ShowControl> next;
    quint64 highest = 0;
    QStringList seen;
    for (const QJsonValue &v : a) {
        if (!v.isObject()) {
            if (error) *error = QStringLiteral("Invalid control");
            return false;
        }
        ShowControl c = ShowControl::fromJson(v.toObject());
        if (c.key.contains(QLatin1Char('/')) || seen.contains(c.key)) c.key.clear(); // a hand-edited duplicate: a new key
        if (!c.key.isEmpty()) seen << c.key;
        highest = std::max(highest, c.id);
        next.push_back(std::move(c));
    }
    // Ids: those saved, a new one for what has none or repeats one
    QSet<quint64> ids;
    for (ShowControl &c : next) {
        if (!c.id || ids.contains(c.id)) c.id = 0;
        else ids.insert(c.id);
    }
    quint64 id = std::max(highest, m_nextControlId - 1);
    for (ShowControl &c : next)
        if (!c.id) c.id = ++id;
    keyAll(next);
    {
        Lock lk(&m_mutex);
        m_controls = std::move(next);
        m_nextControlId = std::max(m_nextControlId, id + 1);
        ++m_controlsRevision;
    }
    emit controlsChanged();
    return true;
}

quint64 Engine::addControl(const ShowControl &control)
{
    quint64 id = 0;
    {
        Lock lk(&m_mutex);
        if (int(m_controls.size()) >= kMaxControls) return 0;
        ShowControl c = control;
        c.id = m_nextControlId++;
        c.key.clear();
        if (c.name.trimmed().isEmpty()) c.name = ShowControl::kindName(c.kind);
        c.value = c.kind == ShowControl::Kind::Slider ? std::clamp(c.value, std::min(c.min, c.max), std::max(c.min, c.max)) : c.clamped(c.value);
        if (c.kind == ShowControl::Kind::Choice && c.choices.isEmpty()) c.choices = {QStringLiteral("A"), QStringLiteral("B")};
        id = c.id;
        m_controls.push_back(std::move(c));
        keyAll(m_controls);
        ++m_controlsRevision;
    }
    emit controlsChanged();
    return id;
}

bool Engine::updateControl(quint64 id, const ShowControl &change)
{
    {
        Lock lk(&m_mutex);
        auto it = std::find_if(m_controls.begin(), m_controls.end(), [id](const ShowControl &c) { return c.id == id; });
        if (it == m_controls.end()) return false;
        ShowControl c = change;
        c.id = id;
        c.key = it->key; // its address does not follow its name
        if (c.name.trimmed().isEmpty()) c.name = it->name;
        if (c.kind == ShowControl::Kind::Slider) {
            if (!(c.max > c.min)) c.max = c.min + 1;
            c.def = std::clamp(c.def, c.min, c.max);
        }
        if (c.kind == ShowControl::Kind::Choice && c.choices.isEmpty()) c.choices = it->choices;
        c.value = c.kind == it->kind ? c.clamped(c.value) : (c.kind == ShowControl::Kind::Slider ? c.def : 0.0);
        *it = c;
        ++m_controlsRevision;
    }
    emit controlsChanged();
    return true;
}

bool Engine::removeControl(quint64 id)
{
    {
        Lock lk(&m_mutex);
        auto it = std::find_if(m_controls.begin(), m_controls.end(), [id](const ShowControl &c) { return c.id == id; });
        if (it == m_controls.end()) return false;
        m_controls.erase(it);
        ++m_controlsRevision;
    }
    emit controlsChanged();
    return true;
}

bool Engine::moveControl(quint64 id, int to)
{
    {
        Lock lk(&m_mutex);
        auto it = std::find_if(m_controls.begin(), m_controls.end(), [id](const ShowControl &c) { return c.id == id; });
        if (it == m_controls.end()) return false;
        const int from = int(it - m_controls.begin());
        to = std::clamp(to, 0, int(m_controls.size()) - 1);
        if (from == to) return true;
        ShowControl c = *it;
        m_controls.erase(it);
        m_controls.insert(m_controls.begin() + to, std::move(c));
        ++m_controlsRevision;
    }
    emit controlsChanged();
    return true;
}

bool Engine::setControlValue(quint64 id, double value)
{
    Lock lk(&m_mutex);
    auto it = std::find_if(m_controls.begin(), m_controls.end(), [id](const ShowControl &c) { return c.id == id; });
    if (it == m_controls.end() || it->kind == ShowControl::Kind::Color || !std::isfinite(value)) return false;
    it->value = it->clamped(value);
    return true;
}

bool Engine::setControlColor(quint64 id, double r, double g, double b)
{
    Lock lk(&m_mutex);
    auto it = std::find_if(m_controls.begin(), m_controls.end(), [id](const ShowControl &c) { return c.id == id; });
    if (it == m_controls.end() || it->kind != ShowControl::Kind::Color) return false;
    const double c[3] = {r, g, b};
    for (int i = 0; i < 3; ++i)
        if (std::isfinite(c[i])) it->rgb[size_t(i)] = std::clamp(c[i], 0.0, 1.0);
    return true;
}

std::optional<ShowControl> Engine::control(quint64 id) const
{
    Lock lk(&m_mutex);
    for (const ShowControl &c : m_controls)
        if (c.id == id) return c;
    return std::nullopt;
}

// A control's parameters, by its id (the lookups of the bindings: its place in the list changes)
ShowControl *Engine::controlLocked(quint64 id)
{
    for (ShowControl &c : m_controls)
        if (c.id == id) return &c;
    return nullptr;
}
