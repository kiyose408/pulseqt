//==============================================================================
// FilterConfigDialog 实现 — 支持 MA/Median/ThresholdAlarm
//==============================================================================

#include "FilterConfigDialog.h"
#include "FilterPipeline.h"
#include "MovingAverageFilter.h"
#include "MedianFilter.h"
#include "ThresholdAlarm.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QMessageBox>

FilterConfigDialog::FilterConfigDialog(FilterPipeline *pipeline, QWidget *parent)
    : QDialog(parent), m_pipeline(pipeline)
{
    setWindowTitle("配置过滤器");
    setMinimumWidth(420);

    auto *mainLayout = new QVBoxLayout(this);

    // ── 过滤器类型 + 参数 ──
    auto *addGroup = new QGroupBox("添加过滤器", this);
    auto *addLayout = new QVBoxLayout(addGroup);

    auto *row1 = new QHBoxLayout;
    row1->addWidget(new QLabel("类型:", this));
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItems({"滑动平均(Simple)", "滑动平均(EMA)", "中值滤波", "阈值告警"});
    row1->addWidget(m_typeCombo, 1);

    m_windowLabel = new QLabel("窗口:", this);
    row1->addWidget(m_windowLabel);
    m_windowSpin = new QSpinBox(this);
    m_windowSpin->setRange(2, 30);
    m_windowSpin->setValue(5);
    row1->addWidget(m_windowSpin);
    addLayout->addLayout(row1);

    // 阈值告警专用行（默认隐藏）
    m_thresholdRow = new QWidget(this);
    auto *thrLayout = new QHBoxLayout(m_thresholdRow);
    thrLayout->setContentsMargins(0, 0, 0, 0);
    thrLayout->addWidget(new QLabel("上限:", this));
    m_upperSpin = new QDoubleSpinBox(this);
    m_upperSpin->setRange(1, 65535); m_upperSpin->setValue(900);
    thrLayout->addWidget(m_upperSpin);
    thrLayout->addWidget(new QLabel("下限:", this));
    m_lowerSpin = new QDoubleSpinBox(this);
    m_lowerSpin->setRange(1, 65535); m_lowerSpin->setValue(100);
    thrLayout->addWidget(m_lowerSpin);
    thrLayout->addWidget(new QLabel("滞回:", this));
    m_hysteresisSpin = new QDoubleSpinBox(this);
    m_hysteresisSpin->setRange(0.1, 999); m_hysteresisSpin->setValue(5);
    thrLayout->addWidget(m_hysteresisSpin);
    m_thresholdRow->setVisible(false);
    addLayout->addWidget(m_thresholdRow);

    // 类型切换
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx) {
        bool alarm = (idx == 3);
        m_windowLabel->setVisible(!alarm);
        m_windowSpin->setVisible(!alarm);
        m_thresholdRow->setVisible(alarm);
    });

    // 通道选择
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

    // ── 过滤器列表 ──
    auto *listGroup = new QGroupBox("已添加的过滤器", this);
    auto *listLayout = new QVBoxLayout(listGroup);
    m_listWidget = new QListWidget(this);
    listLayout->addWidget(m_listWidget);
    mainLayout->addWidget(listGroup);

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
        else { QStringList sl; for (int c : chs) sl << QString::number(c); chStr = sl.join(","); }
        m_listWidget->addItem(QString("[%1] %2  CH(%3)").arg(i).arg(f->name(), chStr));
    }
}

void FilterConfigDialog::onAdd()
{
    if (!m_pipeline) return;
    int typeIdx = m_typeCombo->currentIndex();

    // 解析通道列表
    QVector<int> chList;
    QString chText = m_channelEdit->text().trimmed();
    if (!chText.isEmpty()) {
        for (const QString &s : chText.split(',', Qt::SkipEmptyParts)) {
            bool ok; int c = s.trimmed().toInt(&ok);
            if (ok) chList.append(c);
        }
    }

    IFilter *f = nullptr;
    if (typeIdx == 0)
        f = new MovingAverageFilter(m_windowSpin->value(), MovingAverageFilter::Simple);
    else if (typeIdx == 1)
        f = new MovingAverageFilter(m_windowSpin->value(), MovingAverageFilter::EMA);
    else if (typeIdx == 2)
        f = new MedianFilter(m_windowSpin->value());
    else {
        auto *alarm = new ThresholdAlarm;
        alarm->setUpperLimit(0, m_upperSpin->value());
        alarm->setLowerLimit(0, m_lowerSpin->value());
        alarm->setHysteresis(m_hysteresisSpin->value());
        f = alarm;
    }

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
