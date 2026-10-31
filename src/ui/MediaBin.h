#pragma once
#include <QHash>
#include <QWidget>

class Engine;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QLabel;
class QLineEdit;
class QComboBox;
class QToolButton;

// Media Bin: two roots, Video and Audio, and below each what a layer can load.
//   Video: Videos (image sequences and network streams among them), Images, Generators (Text, Timecode, Countdown,
//          Stopwatch, Clock, Line, Web Page), Inputs (cameras, screens, NDI, OMT, Syphon, Spout, DeckLink),
//          ISF (the shaders of the library)
//   Audio: Sounds (files), Generators (the Faust instruments and the .dsp programs), Inputs (sound card inputs)
// Items are dragged onto layers to load them. The search, the media (All / Video / Audio), the kind (Files /
// Generators / Inputs / ISF) and the use filters narrow the tree.
class MediaBin : public QWidget
{
    Q_OBJECT
public:
    explicit MediaBin(Engine *engine, QWidget *parent = nullptr);

    void refresh();          // re-reads usage from the engine (called after every change)
    void importFiles(const QStringList &paths);
    QString selectedPath() const;

signals:
    void relinkRequested(const QString &from, const QString &to);
    void useAsSourceRequested(const QString &path);              // load into the selected layer
    void loadIntoNewLayerRequested(const QString &path);          // a new layer above the selected one
    void loadIntoLayerRequested(int layer, const QString &path);  // load into that layer (right-click submenu)
    void reloadIsfRequested(const QString &path);
    void binEdited(); // import / removal: the project is modified
    void rebuildRequested(); // Rebuild: the ISF library scanned again (the bin is refreshed after)

private:
    void probe(const QString &path);
    void refreshIsf();
    void refreshText(); // Text: its users
    void refreshInputs(); // Video > Inputs: the live sources there now, and those the layers use
    void refreshAudioInputs(bool force = false); // Audio > Inputs: the sound card inputs, and those the layers use
    void refreshAudioGenerators(); // Audio > Generators: the built-in instruments, the .dsp programs of the project, their users
    void updateButtons();
    void importDialog();
    void importSequenceDialog(); // a folder of numbered pictures
    void addStreamDialog();      // a network stream's URL
    void relinkSelected();
    void removeSelected();
    void revealSelected();
    void contextMenu(const QPoint &pos);
    void applyFilter(); // search field, type and use filters: hides what does not match
    void rebuild();     // everything read again: the files probed, the inputs looked for, the ISF library scanned

    Engine *m_engine;
    QTreeWidget *m_tree;
    QTreeWidgetItem *m_videoRoot, *m_audioRoot;
    QTreeWidgetItem *m_videos, *m_images, *m_audios, *m_generators, *m_text, *m_timecode, *m_line, *m_web, *m_inputs, *m_isf, *m_isfGenerators;
    QTreeWidgetItem *m_audioGenerators, *m_audioInputs;
    QTreeWidgetItem *m_clocks[3] = {}; // Countdown, Stopwatch, Clock (ClockKind)
    QPushButton *m_relink, *m_remove, *m_reveal;
    QLabel *m_summary;
    QLineEdit *m_search = nullptr;
    QComboBox *m_type = nullptr;  // The media: All, Video, Audio
    QComboBox *m_kind = nullptr;  // What of it: All, Files, Generators, Inputs, ISF
    QComboBox *m_usage = nullptr; // All, Used (by a layer), Unused
    QToolButton *m_rebuild = nullptr;
    bool m_isfOpened = false; // ISF > Generators expanded once filled
    QHash<QString, QString> m_info;  // path -> description (resolution, duration…)
    QHash<QString, bool> m_probing;
    QStringList m_inputsSignature, m_audioInputsSignature;
    QStringList m_audioDevices;
    qint64 m_audioScanned = 0; // when the sound inputs were listed (ms): listing opens the audio system, not every 3 s
};
