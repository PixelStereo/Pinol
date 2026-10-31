#include "FxChainPanel.h"
#include "Widgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QToolButton *button(const QString &text, const QString &tip)
{
    auto *b = new QToolButton;
    b->setText(text);
    b->setToolTip(tip);
    b->setMinimumWidth(28);
    return b;
}
} // namespace

FxChainPanel::FxChainPanel(const QString &chain, QWidget *parent) : QWidget(parent), m_chain(chain)
{
    setObjectName(QStringLiteral("fxChainPanel"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);

    m_master = new FlagBox(chain + QStringLiteral(" Enable"));
    m_master->setObjectName(QStringLiteral("fxChainMaster"));
    m_master->setToolTip(QStringLiteral("Turns the whole %1 chain on or off (each FX keeps its own switch)").arg(chain));
    m_master->setStyleSheet("QCheckBox { font-weight:bold; }");
    v->addWidget(m_master);

    m_header = new QVBoxLayout;
    m_header->setContentsMargins(0, 0, 0, 0);
    v->addLayout(m_header);

    m_bar = new QHBoxLayout;
    m_add = new QPushButton(QStringLiteral("Add FX…"));
    m_add->setObjectName(QStringLiteral("fxChainAdd"));
    m_remove = button(QStringLiteral("−"), QStringLiteral("Remove FX"));
    m_remove->setObjectName(QStringLiteral("fxChainRemove"));
    m_up = button(QStringLiteral("▲"), QStringLiteral("Move Up (applied earlier)"));
    m_up->setObjectName(QStringLiteral("fxChainUp"));
    m_down = button(QStringLiteral("▼"), QStringLiteral("Move Down (applied later)"));
    m_down->setObjectName(QStringLiteral("fxChainDown"));
    m_bar->addWidget(m_add, 1);
    m_bar->addWidget(m_remove);
    m_bar->addWidget(m_up);
    m_bar->addWidget(m_down);
    m_trailingAt = m_bar->count();
    v->addLayout(m_bar);

    m_empty = new QLabel(QStringLiteral("No FX. FX are applied in list order."));
    static_cast<QLabel *>(m_empty)->setWordWrap(true);
    m_empty->setStyleSheet("color:#888;");
    v->addWidget(m_empty);

    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("fxChainList"));
    m_list->setProperty("allowLocked", true); // selecting an FX to see its parameters is not an edit
    v->addWidget(m_list);

    m_detail = new QWidget;
    auto *detail = new QVBoxLayout(m_detail);
    detail->setContentsMargins(0, 0, 0, 0);
    m_detail->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    v->addWidget(m_detail);
    m_footer = new QVBoxLayout;
    m_footer->setContentsMargins(0, 0, 0, 0);
    v->addLayout(m_footer);
    v->addStretch(1); // the room left goes below, not between the rows

    connect(m_add, &QPushButton::clicked, this, [this] { emit addRequested(m_add); });
    connect(m_remove, &QToolButton::clicked, this, [this] {
        if (selected() >= 0) emit removeRequested(selected());
    });
    connect(m_up, &QToolButton::clicked, this, [this] {
        if (selected() > 0) emit moveRequested(selected(), selected() - 1);
    });
    connect(m_down, &QToolButton::clicked, this, [this] {
        if (selected() >= 0 && selected() < m_count - 1) emit moveRequested(selected(), selected() + 1);
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (!m_filling) emit toggled(m_list->row(item), item->checkState() == Qt::Checked);
    });
    // Only the owner's parameters below are swapped on a pick. Rebuilding the whole list here would destroy it between
    // the press and the release, and the click that picked the row would never reach its check box (enabling an FX would
    // take two clicks).
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_filling) return;
        updateButtons();
        if (row >= 0) emit currentChanged(row);
    });
    setRows({}, 0);
}

QVBoxLayout *FxChainPanel::detailLayout() const { return static_cast<QVBoxLayout *>(m_detail->layout()); }

void FxChainPanel::addLeading(QWidget *w)
{
    m_bar->insertWidget(1, w);
    ++m_trailingAt;
}

void FxChainPanel::addTrailing(QWidget *w)
{
    m_bar->insertWidget(m_trailingAt++, w);
}

int FxChainPanel::selected() const { return m_count ? m_list->currentRow() : -1; }

void FxChainPanel::setMaximum(int maximum)
{
    m_maximum = maximum;
    updateButtons();
}

void FxChainPanel::setEmptyText(const QString &text) { static_cast<QLabel *>(m_empty)->setText(text); }

void FxChainPanel::updateButtons()
{
    const int row = selected();
    m_remove->setEnabled(row >= 0);
    m_up->setEnabled(row > 0);
    m_down->setEnabled(row >= 0 && row < m_count - 1);
    m_add->setEnabled(m_maximum <= 0 || m_count < m_maximum);
}

void FxChainPanel::setRows(const std::vector<Row> &rows, int selected, bool chainEnabled)
{
    m_filling = true;
    m_list->clear();
    for (const Row &r : rows) {
        auto *item = new QListWidgetItem(r.label + r.suffix + (r.warning ? QStringLiteral("  ⚠") : QString()));
        if (!r.tooltip.isEmpty()) item->setToolTip(r.tooltip);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
        m_list->addItem(item);
    }
    m_count = int(rows.size());
    m_empty->setVisible(m_count == 0);
    m_list->setVisible(m_count > 0);
    if (m_count) {
        m_list->setCurrentRow(std::clamp(selected, 0, m_count - 1));
        m_list->setMaximumHeight(qMin(160, 26 * m_count + 8));
    }
    m_list->setStyleSheet(chainEnabled ? QString() : QStringLiteral("QListWidget { color:#777; }"));
    m_filling = false;
    updateButtons();
}
