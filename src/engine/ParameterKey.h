#pragma once
// The address of a Faust control inside its FX or instrument: its label as a readable key (OSC, interactions, animations,
// snapshots), "cutoff", "time_ms", "level". Lower case, letters and digits, the rest as "_"; a label with none, or one that
// another control already has, is numbered ("p3", "gain_2").
#include <QString>
#include <QStringList>

inline QString faustControlKey(const QString &label, const QStringList &taken, int index)
{
    QString key;
    for (const QChar c : label.toLower()) {
        if (c.isLetterOrNumber() && c.unicode() < 128) key += c;
        else if (!key.isEmpty() && !key.endsWith(QLatin1Char('_'))) key += QLatin1Char('_');
    }
    while (key.endsWith(QLatin1Char('_'))) key.chop(1);
    if (key.isEmpty() || key == QLatin1String("enabled") || key == QLatin1String("wet") || key == QLatin1String("enable"))
        key = QStringLiteral("p%1").arg(index); // "wet" and "enabled" are the chain's own
    QString unique = key;
    for (int n = 2; taken.contains(unique); ++n) unique = QStringLiteral("%1_%2").arg(key).arg(n);
    return unique;
}
