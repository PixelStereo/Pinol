#pragma once
// A show control: a value the operator makes — a slider, a switch, a selector, a button, a color — that belongs to the
// show, not to a layer. It is a parameter of the show (control/<key>), so everything that reaches a parameter reaches it:
// an interaction follows it (an Internal source) or drives it (a target), a timeline animates it, OSC reads and sets it
// (/control/<key>), a remote control moves it. One control read by several interactions is how one gesture drives
// several things at once; one driven by several is how several things meet in one.
//
// The definitions and the current values are saved with the project. Never in a snapshot: a control is the operator's.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>

struct ShowControl {
    enum class Kind { Slider = 0, Toggle, Choice, Button, Color };
    quint64 id = 0;
    QString name = QStringLiteral("Control");
    QString key; // its address, control/<key>: made from the name, unique (Engine::addControl)
    Kind kind = Kind::Slider;
    double min = 0, max = 1, step = 0.01; // Slider
    QString unit;                         // Slider: " %", " Hz", " s"
    double def = 0;                       // Slider: where a reset goes
    QStringList choices;                  // Choice: its names (also its keys: renaming one renames what was set)
    // Its value: a slider's number, a switch's 0 / 1, a choice's index, a button's 0 / 1 (1 while it is held down)
    double value = 0;
    std::array<double, 3> rgb{{1, 1, 1}}; // Color

    static QString kindKey(Kind k)
    {
        static const char *const keys[] = {"slider", "toggle", "choice", "button", "color"};
        return QString::fromLatin1(keys[std::clamp(int(k), 0, 4)]);
    }
    static Kind kindFromKey(const QString &s)
    {
        for (int k = 0; k < 5; ++k)
            if (kindKey(Kind(k)) == s) return Kind(k);
        return Kind::Slider;
    }
    static QString kindName(Kind k)
    {
        static const char *const names[] = {"Slider", "Toggle", "Selector", "Button", "Color"};
        return QString::fromLatin1(names[std::clamp(int(k), 0, 4)]);
    }
    // The value kept within what the control takes
    double clamped(double v) const
    {
        if (!std::isfinite(v)) return value;
        switch (kind) {
        case Kind::Slider: return std::clamp(v, std::min(min, max), std::max(min, max));
        case Kind::Toggle:
        case Kind::Button: return v >= 0.5 ? 1.0 : 0.0;
        case Kind::Choice: return choices.isEmpty() ? 0.0 : std::clamp(std::round(v), 0.0, double(choices.size() - 1));
        case Kind::Color: return value;
        }
        return value;
    }
    QJsonObject toJson() const
    {
        QJsonObject o{{"id", QString::number(id)}, {"name", name}, {"key", key}, {"kind", kindKey(kind)}};
        switch (kind) {
        case Kind::Slider:
            o["min"] = min;
            o["max"] = max;
            o["step"] = step;
            o["default"] = def;
            o["value"] = value;
            if (!unit.isEmpty()) o["unit"] = unit;
            break;
        case Kind::Toggle: o["value"] = value >= 0.5; break;
        case Kind::Button: break; // never saved down
        case Kind::Choice: {
            QJsonArray list;
            for (const QString &c : choices) list.append(c);
            o["choices"] = list;
            o["value"] = int(value);
            break;
        }
        case Kind::Color: o["rgb"] = QJsonArray{rgb[0], rgb[1], rgb[2]}; break;
        }
        return o;
    }
    static ShowControl fromJson(const QJsonObject &o)
    {
        ShowControl c;
        c.id = o.value("id").toString().toULongLong();
        c.name = o.value("name").toString(QStringLiteral("Control"));
        c.key = o.value("key").toString();
        c.kind = kindFromKey(o.value("kind").toString());
        auto finite = [](double v, double fallback) { return std::isfinite(v) ? v : fallback; };
        switch (c.kind) {
        case Kind::Slider:
            c.min = finite(o.value("min").toDouble(0), 0);
            c.max = finite(o.value("max").toDouble(1), 1);
            if (c.max <= c.min) c.max = c.min + 1;
            c.step = std::clamp(finite(o.value("step").toDouble(0.01), 0.01), 1e-6, std::max(1e-6, c.max - c.min));
            c.unit = o.value("unit").toString();
            c.def = std::clamp(finite(o.value("default").toDouble(c.min), c.min), c.min, c.max);
            c.value = std::clamp(finite(o.value("value").toDouble(c.def), c.def), c.min, c.max);
            break;
        case Kind::Toggle: c.value = o.value("value").toBool() ? 1.0 : 0.0; break;
        case Kind::Button: break;
        case Kind::Choice:
            for (const QJsonValue &v : o.value("choices").toArray()) c.choices << v.toString();
            if (c.choices.isEmpty()) c.choices << QStringLiteral("A") << QStringLiteral("B");
            c.value = std::clamp(double(o.value("value").toInt(0)), 0.0, double(c.choices.size() - 1));
            break;
        case Kind::Color: {
            const QJsonArray a = o.value("rgb").toArray();
            for (int i = 0; i < 3; ++i) c.rgb[size_t(i)] = std::clamp(finite(a.at(i).toDouble(1), 1), 0.0, 1.0);
            break;
        }
        }
        return c;
    }
};
