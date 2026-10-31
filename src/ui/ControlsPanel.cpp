#include "ControlsPanel.h"
#include "ColorPicker.h"
#include "Engine.h"
#include "Widgets.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
// The whole list, before and after: the definitions of the controls are small and an edit changes one entry
class SetControlsCommand final : public QUndoCommand
{
public:
    SetControlsCommand(Engine *e, QJsonArray before, QJsonArray after, const QString &text)
        : QUndoCommand(text), m_e(e), m_before(std::move(before)), m_after(std::move(after))
    {
    }
    void undo() override { m_e->setControlsJson(m_before); }
    void redo() override
    {
        if (m_first) {
            m_first = false; // the edit is already made
            return;
        }
        m_e->setControlsJson(m_after);
    }

private:
    Engine *m_e;
    QJsonArray m_before, m_after;
    bool m_first = true;
};

int decimalsFor(double step)
{
    if (step >= 1) return 0;
    return std::clamp(int(std::ceil(-std::log10(std::max(step, 1e-6)))), 0, 6);
}
} // namespace

ControlsPanel::ControlsPanel(Engine *engine, QUndoStack *undo, QWidget *parent) : QWidget(parent), m_engine(engine), m_undo(undo)
{
    setObjectName(QStringLiteral("controlsPanel"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(8, 8, 4, 4);
    v->setSpacing(6);
    auto *head = new QHBoxLayout;
    head->addWidget(new QLabel(QStringLiteral("<b>Controls</b>")));
    head->addStretch();
    auto *add = new QToolButton;
    add->setObjectName(QStringLiteral("controlsAdd"));
    add->setText(QStringLiteral("+ New"));
    add->setToolTip(QStringLiteral("Make a slider, a switch, a selector, a button or a color of your own"));
    add->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(add);
    for (int k = 0; k < 5; ++k)
        menu->addAction(ShowControl::kindName(ShowControl::Kind(k)), this, [this, k] { addControl(k); })->setObjectName(QStringLiteral("controlsAdd%1").arg(k));
    add->setMenu(menu);
    head->addWidget(add);
    v->addLayout(head);
    auto *note = new QLabel(QStringLiteral("Each control is a value of the show: an interaction can follow it or drive it, a timeline can animate it, "
                                           "OSC and the phone remote can move it."));
    note->setWordWrap(true);
    note->setStyleSheet("color:#888; font-size:11px;");
    v->addWidget(note);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget;
    m_list = new QVBoxLayout(inner);
    m_list->setContentsMargins(0, 0, 0, 0);
    m_list->setSpacing(4);
    scroll->setWidget(inner);
    v->addWidget(scroll, 1);
    m_empty = new QLabel(QStringLiteral("No control yet. Use + New."));
    m_empty->setStyleSheet("color:#888;");
    connect(m_engine, &Engine::controlsChanged, this, &ControlsPanel::rebuild);
    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (isVisible()) refreshValues();
    });
    m_timer->start();
    rebuild();
}

void ControlsPanel::editList(const QString &text, const std::function<void(QJsonArray &)> &change)
{
    const QJsonArray before = m_engine->controlsJson();
    QJsonArray after = before;
    change(after);
    QString error;
    if (!m_engine->setControlsJson(after, &error)) return;
    m_undo->push(new SetControlsCommand(m_engine, before, m_engine->controlsJson(), text));
    emit edited();
}

void ControlsPanel::addControl(int kind)
{
    ShowControl c;
    c.kind = ShowControl::Kind(kind);
    c.name = ShowControl::kindName(c.kind);
    if (c.kind == ShowControl::Kind::Choice) c.choices = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
    const QJsonArray before = m_engine->controlsJson();
    if (!m_engine->addControl(c)) return;
    m_undo->push(new SetControlsCommand(m_engine, before, m_engine->controlsJson(), QStringLiteral("Add Control")));
    emit edited();
}

void ControlsPanel::editControl(quint64 id)
{
    const auto current = m_engine->control(id);
    if (!current) return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("%1 — %2").arg(ShowControl::kindName(current->kind), current->name));
    auto *form = new QFormLayout(&dialog);
    auto *name = new QLineEdit(current->name);
    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Address"), new QLabel(QStringLiteral("/control/%1").arg(current->key)));
    QDoubleSpinBox *min = nullptr, *max = nullptr, *step = nullptr, *def = nullptr;
    QLineEdit *unit = nullptr;
    QPlainTextEdit *choices = nullptr;
    if (current->kind == ShowControl::Kind::Slider) {
        min = new QDoubleSpinBox;
        max = new QDoubleSpinBox;
        step = new QDoubleSpinBox;
        def = new QDoubleSpinBox;
        for (auto *b : {min, max, def}) {
            b->setRange(-1e9, 1e9);
            b->setDecimals(4);
        }
        step->setRange(1e-6, 1e9);
        step->setDecimals(6);
        min->setValue(current->min);
        max->setValue(current->max);
        step->setValue(current->step);
        def->setValue(current->def);
        unit = new QLineEdit(current->unit);
        unit->setPlaceholderText(QStringLiteral("e.g. %, Hz, s"));
        form->addRow(QStringLiteral("Minimum"), min);
        form->addRow(QStringLiteral("Maximum"), max);
        form->addRow(QStringLiteral("Step"), step);
        form->addRow(QStringLiteral("Default"), def);
        form->addRow(QStringLiteral("Unit"), unit);
    } else if (current->kind == ShowControl::Kind::Choice) {
        choices = new QPlainTextEdit(current->choices.join(QLatin1Char('\n')));
        choices->setMinimumHeight(90);
        form->addRow(QStringLiteral("Choices\n(one per line)"), choices);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    ShowControl change = *current;
    change.name = name->text().trimmed().isEmpty() ? current->name : name->text().trimmed();
    if (min) {
        change.min = min->value();
        change.max = max->value();
        change.step = step->value();
        change.def = def->value();
        change.unit = unit->text();
    }
    if (choices) {
        QStringList list;
        for (const QString &line : choices->toPlainText().split(QLatin1Char('\n')))
            if (!line.trimmed().isEmpty() && !list.contains(line.trimmed())) list << line.trimmed();
        if (!list.isEmpty()) change.choices = list;
    }
    const QJsonArray before = m_engine->controlsJson();
    if (!m_engine->updateControl(id, change)) return;
    m_undo->push(new SetControlsCommand(m_engine, before, m_engine->controlsJson(), QStringLiteral("Edit Control")));
    emit edited();
}

void ControlsPanel::rebuild()
{
    m_filling = true;
    while (QLayoutItem *item = m_list->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            if (w == m_empty) w->hide();
            else w->deleteLater();
        }
        delete item;
    }
    m_rows.clear();
    const std::vector<ShowControl> controls = m_engine->controls();
    if (controls.empty()) {
        m_list->addWidget(m_empty);
        m_empty->show();
    }
    for (size_t index = 0; index < controls.size(); ++index) {
        const ShowControl &c = controls[index];
        const quint64 id = c.id;
        auto *row = new QWidget;
        row->setObjectName(QStringLiteral("controlRow"));
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(6);
        auto *label = new QLabel(c.name);
        label->setMinimumWidth(84);
        label->setMaximumWidth(140);
        label->setToolTip(QStringLiteral("/control/%1").arg(c.key));
        h->addWidget(label);
        QWidget *field = nullptr;
        switch (c.kind) {
        case ShowControl::Kind::Slider: {
            auto *s = new SliderField;
            s->setObjectName(QStringLiteral("control_%1").arg(c.key));
            s->setRange(c.min, c.max);
            s->setDecimals(decimalsFor(c.step));
            s->setSingleStep(c.step);
            if (!c.unit.isEmpty()) s->setSuffix(c.unit);
            s->setValue(c.value);
            connect(s, &SliderField::valueEdited, this, [this, id](double v) { m_engine->setControlValue(id, v); });
            field = s;
            h->addWidget(s, 1);
            break;
        }
        case ShowControl::Kind::Toggle: {
            auto *f = new FlagBox(QString());
            f->setObjectName(QStringLiteral("control_%1").arg(c.key));
            f->setChecked(c.value >= 0.5);
            connect(f, &QCheckBox::toggled, this, [this, id](bool on) {
                if (!m_filling) m_engine->setControlValue(id, on ? 1.0 : 0.0);
            });
            field = f;
            h->addWidget(f);
            h->addStretch(1);
            break;
        }
        case ShowControl::Kind::Choice: {
            auto *b = new QComboBox;
            b->setObjectName(QStringLiteral("control_%1").arg(c.key));
            b->addItems(c.choices);
            b->setCurrentIndex(int(c.value));
            connect(b, qOverload<int>(&QComboBox::activated), this, [this, id](int k) { m_engine->setControlValue(id, k); });
            field = b;
            h->addWidget(b, 1);
            break;
        }
        case ShowControl::Kind::Button: {
            auto *b = new QPushButton(QStringLiteral("Hold"));
            b->setObjectName(QStringLiteral("control_%1").arg(c.key));
            b->setToolTip(QStringLiteral("1 while it is held down, 0 when let go: a gate for an Envelope, a flash, a strobe"));
            connect(b, &QPushButton::pressed, this, [this, id] { m_engine->setControlValue(id, 1.0); });
            connect(b, &QPushButton::released, this, [this, id] { m_engine->setControlValue(id, 0.0); });
            field = b;
            h->addWidget(b, 1);
            break;
        }
        case ShowControl::Kind::Color: {
            auto *b = new QPushButton;
            b->setObjectName(QStringLiteral("control_%1").arg(c.key));
            b->setMinimumHeight(24);
            const QColor color = QColor::fromRgbF(float(c.rgb[0]), float(c.rgb[1]), float(c.rgb[2]));
            b->setStyleSheet(QStringLiteral("background:%1; border:1px solid #555;").arg(color.name()));
            connect(b, &QPushButton::clicked, this, [this, id, b] {
                const auto now = m_engine->control(id);
                if (!now) return;
                const QColor start = QColor::fromRgbF(float(now->rgb[0]), float(now->rgb[1]), float(now->rgb[2]));
                const QColor picked = ColorPicker::getColor(start, this, QStringLiteral("Control color"), QStringLiteral("controls/color"), false);
                if (!picked.isValid()) return;
                m_engine->setControlColor(id, picked.redF(), picked.greenF(), picked.blueF());
                b->setStyleSheet(QStringLiteral("background:%1; border:1px solid #555;").arg(picked.name()));
            });
            field = b;
            h->addWidget(b, 1);
            break;
        }
        }
        auto *gear = new QToolButton;
        gear->setText(QStringLiteral("⋯"));
        gear->setToolTip(QStringLiteral("Edit, move, duplicate, delete"));
        gear->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(gear);
        menu->addAction(QStringLiteral("Edit…"), this, [this, id] { editControl(id); });
        menu->addAction(QStringLiteral("Duplicate"), this, [this, id] {
            const auto c = m_engine->control(id);
            if (!c) return;
            ShowControl copy = *c;
            copy.name += QStringLiteral(" copy");
            const QJsonArray before = m_engine->controlsJson();
            if (!m_engine->addControl(copy)) return;
            m_undo->push(new SetControlsCommand(m_engine, before, m_engine->controlsJson(), QStringLiteral("Duplicate Control")));
            emit edited();
        });
        menu->addSeparator();
        auto mover = [this, id](int delta) {
            const auto list = m_engine->controls();
            int at = -1;
            for (int k = 0; k < int(list.size()); ++k) if (list[size_t(k)].id == id) at = k;
            if (at < 0 || at + delta < 0 || at + delta >= int(list.size())) return;
            const QJsonArray before = m_engine->controlsJson();
            m_engine->moveControl(id, at + delta);
            m_undo->push(new SetControlsCommand(m_engine, before, m_engine->controlsJson(), QStringLiteral("Move Control")));
            emit edited();
        };
        menu->addAction(QStringLiteral("Move up"), this, [mover] { mover(-1); })->setEnabled(index > 0);
        menu->addAction(QStringLiteral("Move down"), this, [mover] { mover(1); })->setEnabled(index + 1 < controls.size());
        menu->addSeparator();
        menu->addAction(QStringLiteral("Delete"), this, [this, id] {
            const QJsonArray before = m_engine->controlsJson();
            if (!m_engine->removeControl(id)) return;
            m_undo->push(new SetControlsCommand(m_engine, before, m_engine->controlsJson(), QStringLiteral("Delete Control")));
            emit edited();
        });
        gear->setMenu(menu);
        h->addWidget(gear);
        m_list->addWidget(row);
        m_rows.push_back({id, field});
    }
    m_list->addStretch(1);
    m_filling = false;
}

void ControlsPanel::refreshValues()
{
    const std::vector<ShowControl> controls = m_engine->controls();
    m_filling = true;
    for (const Row &row : m_rows) {
        if (!row.field || row.field->hasFocus()) continue;
        const auto it = std::find_if(controls.begin(), controls.end(), [&](const ShowControl &c) { return c.id == row.id; });
        if (it == controls.end()) continue;
        const ShowControl &c = *it;
        if (auto *s = qobject_cast<SliderField *>(row.field.data())) {
            if (!s->isDragging()) {
                const QSignalBlocker b(s);
                s->setValue(c.value);
            }
        } else if (auto *f = qobject_cast<QCheckBox *>(row.field.data())) {
            const QSignalBlocker b(f);
            f->setChecked(c.value >= 0.5);
        } else if (auto *box = qobject_cast<QComboBox *>(row.field.data())) {
            if (!box->view()->isVisible() && box->currentIndex() != int(c.value)) {
                const QSignalBlocker b(box);
                box->setCurrentIndex(int(c.value));
            }
        } else if (auto *button = qobject_cast<QPushButton *>(row.field.data())) {
            if (c.kind == ShowControl::Kind::Color)
                button->setStyleSheet(QStringLiteral("background:%1; border:1px solid #555;")
                                          .arg(QColor::fromRgbF(float(c.rgb[0]), float(c.rgb[1]), float(c.rgb[2])).name()));
            else if (c.kind == ShowControl::Kind::Button && !button->isDown())
                button->setText(c.value >= 0.5 ? QStringLiteral("● on") : QStringLiteral("Hold"));
        }
    }
    m_filling = false;
}
