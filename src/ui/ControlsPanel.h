#pragma once
#include <QWidget>
#include <QHash>
#include <QPointer>

class Engine;
class QUndoStack;
class QVBoxLayout;
class QLabel;
class QTimer;

// The Controls tab: the operator's own sliders, switches, selectors, buttons and colors (ShowControl.h), each a parameter
// of the show that an interaction can follow (Internal source) or drive (target), a timeline animate and OSC reach. The
// panel is where they are made, shaped and played; the phone remote (RemoteServer.h) shows the same list. Definitions are
// edited through the undo stack; moving a control is a performance, not an edit.
class ControlsPanel : public QWidget
{
    Q_OBJECT
public:
    ControlsPanel(Engine *engine, QUndoStack *undo, QWidget *parent = nullptr);
    void rebuild();      // the list again (a control added, removed, shaped)
    void refreshValues(); // the values as they are now (OSC, an interaction, a timeline, the remote), except one being dragged

signals:
    void edited(); // the project is modified

private:
    struct Row {
        quint64 id = 0;
        QPointer<QWidget> field; // SliderField, FlagBox, QComboBox, QPushButton or a swatch
    };
    void addControl(int kind);
    void editControl(quint64 id);
    void editList(const QString &text, const std::function<void(QJsonArray &)> &change);
    Engine *m_engine;
    QUndoStack *m_undo;
    QVBoxLayout *m_list = nullptr;
    QLabel *m_empty = nullptr;
    QTimer *m_timer = nullptr;
    std::vector<Row> m_rows;
    quint64 m_revisionSeen = ~0ull;
    bool m_filling = false;
};
