#pragma once
#include "Engine.h"

// Clipboard fragments contain stored data, never a fresh capture of the live show.
namespace StoredContent {
enum class Owner { Layer, Composition, Interaction, Fixture };
struct Selection {
    Owner owner = Owner::Layer;
    QString id;
    QStringList path; // empty: the whole owner; params addresses remain a single JSON key
};
struct PresetSelection {
    quint64 layer = 0;
    QString key;
};
Engine::Snapshot select(const Engine::Snapshot &, const std::vector<Selection> &);
void remove(Engine::Snapshot &, const std::vector<Selection> &);
void merge(Engine::Snapshot &, const Engine::Snapshot &);
Engine::Preset select(const Engine::Preset &, const std::vector<PresetSelection> &);
void remove(Engine::Preset &, const std::vector<PresetSelection> &);
void merge(Engine::Preset &, const Engine::Preset &);
Engine::Snapshot fromPreset(const Engine::Preset &);
Engine::Preset toPreset(const Engine::Snapshot &);
QByteArray encode(const Engine::Snapshot &);
bool decode(const QByteArray &, Engine::Snapshot *);
} // namespace StoredContent
