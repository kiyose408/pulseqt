//==============================================================================
// FilterConfigDialog 实现
//==============================================================================

#include "FilterConfigDialog.h"
#include "FilterPipeline.h"
#include "MovingAverageFilter.h"
#include "MedianFilter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QMessageBox>

FilterConfigDialog::FilterConfigDialog(FilterPipeline *pipeline, QWidget *parent)
    : QDialog(parent), m_pipeline(pipeline)
{
    setWindowTitle("配置过滤器");
    setMinimumWidth(380);

    auto *mainLayout = new QVBoxLayout(this);

    // ── 过滤器类型 + 窗口大小 ──
    auto *addGroup = new QGroupBox("添加过滤器", this);
    auto *addLayout = new QVBoxLayout(addGroup);

    auto *row1 = new QHBoxLayout;
    row1->addWidget(new QLabel("类型:", this));
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItems({"滑动平均(Simple)", "滑动平均(EMA)", "中值滤波"});
    row1->addWidget(m_typeCombo, 1);

    row1->addWidget(new QLabel("窗口:", this));
    m_windowSpin = new QSpinBox(this);
    m_windowSpin->setRange(2, 30);
    m_windowSpin->setValue(5);
    row1->addWidget(m_windowSpin);

    addLayout->addLayout(row1);
    auto *row2 = new QHBoxLayout;
    row2->addWidget(new QLabel("通道(空=全部):", this));
    m_channelEdit = new QLineEdit(this);
    m_channelEdit->setPlaceholderText("如: 1,2,3");
    row2->addWidget(m_channelEdit, 1);
    addLayout->addLayout(row2);

    auto *btnRow = new QHBoxLayout;
    auto *addBtn = new QPushButton("添加", this);
    auto *removeBtn = new QPushButton("移除选中", this);
    auto *clearBtn = new QPushButton("清空全部", this);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addWidget(clearBtn);
    btnRow->addStretch();
    addLayout->addLayout(btnRow);

    mainLayout->addWidget(addGroup);

    // ── 当前过滤器列表 ──
    auto *listGroup = new QGroupBox("已添加的过滤器", this);
    auto *listLayout = new QVBoxLayout(listGroup);
    m_listWidget = new QListWidget(this);
    listLayout->addWidget(m_listWidget);
    mainLayout->addWidget(listGroup);

    // ── 信号 ──
    connect(addBtn,    &QPushButton::clicked, this, &FilterConfigDialog::onAdd);
    connect(removeBtn, &QPushButton::clicked, this, &FilterConfigDialog::onRemove);
    connect(clearBtn,  &QPushButton::clicked, this, &FilterConfigDialog::onClear);

    refreshList();
}

void FilterConfigDialog::refreshList()
{
    m_listWidget->clear();
    if (!m_pipeline) return;

    for (int i = 0; i < m_pipeline->count(); ++i) {
        auto *f = m_pipeline->filterAt(i);
        if (!f) continue;
        auto chs = f->channels();
        QString chStr;
        if (chs.isEmpty()) chStr = "全部";
        else {
            QStringList sl;
            for (int c : chs) sl << QString::number(c);
            chStr = sl.join(",");
        }
        m_listWidget->addItem(QString("[%1] %2  CH(%3)").arg(i).arg(f->name(), chStr));
    }
}

void FilterConfigDialog::onAdd()
{
    if (!m_pipeline) return;

    // 同类型只保留一个：先清掉旧的
    int typeIdx = m_typeCombo->currentIndex();
    for (int i = m_pipeline->count() - 1; i >= 0; --i) {
        auto *f = m_pipeline->filterAt(i);
        if (!f) continue;
        QString n = f->name();
        if ((typeIdx <= 1 && n.contains("MovingAverage"))
            || (typeIdx == 2 && n.contains("Median")))
        {
            m_pipeline->removeFilter(i);
        }
    }

    int window = m_windowSpin->value();

    // 解析通道列表
    QVector<int> chList;
    QString chText = m_channelEdit->text().trimmed();
    if (!chText.isEmpty()) {
        for (const QString &s : chText.split(',', Qt::SkipEmptyParts)) {
            bool ok;
            int ch = s.trimmed().toInt(&ok);
            if (ok) chList.append(ch);
        }
    }

    IFilter *f = nullptr;
    if (typeIdx == 0)
        f = new MovingAverageFilter(window, MovingAverageFilter::Simple);
    else if (typeIdx == 1)
        f = new MovingAverageFilter(window, MovingAverageFilter::EMA);
    else
        f = new MedianFilter(window);

    if (!chList.isEmpty()) f->setChannels(chList);
    m_pipeline->addFilter(std::unique_ptr<IFilter>(f));

    refreshList();
}

void FilterConfigDialog::onRemove()
{
    if (!m_pipeline) return;
    int row = m_listWidget->currentRow();
    if (row >= 0 && row < m_pipeline->count()) {
        m_pipeline->removeFilter(row);
        refreshList();
    }
}

void FilterConfigDialog::onClear()
{
    if (!m_pipeline) return;
    m_pipeline->clear();
    refreshList();
}
