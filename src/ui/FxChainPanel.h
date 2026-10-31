#pragma once
#include <QString>
#include <QWidget>
#include <vector>

class QHBoxLayout;
class QListWidget;
class QPushButton;
class QToolButton;
class QVBoxLayout;
class FlagBox;

// The list-of-FX mechanism shared by the video FX tab and the audio FX tab of a layer: a master switch, an "Add…" button
// with the − ▲ ▼ buttons, the chain as a list whose rows have a check box (the FX's own switch), and below the
// parameters of the selected FX (a container the owner fills). The panel knows nothing of what an FX is: the owner
// gives it rows and answers its requests (add, remove, move, switch, select) — every edit goes through the owner's
// undo stack.
class FxChainPanel : public QWidget
{
    Q_OBJECT
public:
    struct Row {
        QString label;
        bool enabled = true;
        bool warning = false;  // the FX could not be loaded: a ⚠ after its name
        QString suffix;        // e.g. "  ◐" (through a mask)
        QString tooltip;
    };
    // `chain`: "FX", "Audio FX"… (the master switch's and the empty list's words)
    explicit FxChainPanel(const QString &chain, QWidget *parent = nullptr);

    FlagBox *masterSwitch() const { return m_master; }
    QPushButton *addButton() const { return m_add; }
    // Extra buttons: after "Add…" (rescan), after ▼ (reload)
    void addLeading(QWidget *button);
    void addTrailing(QWidget *button);
    // The container of the selected FX's parameters (its layout is the owner's)
    QWidget *detail() const { return m_detail; }
    QVBoxLayout *detailLayout() const;
    // An optional strip between the buttons and the list (e.g. the level of what enters the chain)
    QVBoxLayout *headerLayout() const { return m_header; }
    QVBoxLayout *footerLayout() const { return m_footer; }

    // The chain as it is now; `selected` stays selected (clamped). The list is rebuilt without emitting
    void setRows(const std::vector<Row> &rows, int selected, bool chainEnabled = true);
    int selected() const;
    int count() const { return m_count; }
    void setMaximum(int maximum); // "Add…" is disabled when the chain is full (0: no limit)
    void setEmptyText(const QString &text);
    QListWidget *list() const { return m_list; }
    QToolButton *removeButton() const { return m_remove; }
    QToolButton *upButton() const { return m_up; }
    QToolButton *downButton() const { return m_down; }

signals:
    void addRequested(QWidget *anchor); // pop the picker below this widget
    void removeRequested(int row);
    void moveRequested(int from, int to);
    void toggled(int row, bool on);
    void currentChanged(int row); // another FX picked: show its parameters (the list is not rebuilt)

private:
    void updateButtons();
    QString m_chain;
    FlagBox *m_master = nullptr;
    QPushButton *m_add = nullptr;
    QToolButton *m_remove = nullptr, *m_up = nullptr, *m_down = nullptr;
    QHBoxLayout *m_bar = nullptr;
    int m_trailingAt = 0; // index in m_bar where the buttons after ▼ go
    QVBoxLayout *m_header = nullptr, *m_footer = nullptr;
    QWidget *m_empty = nullptr;
    QListWidget *m_list = nullptr;
    QWidget *m_detail = nullptr;
    int m_count = 0, m_maximum = 0;
    bool m_filling = false;
};
