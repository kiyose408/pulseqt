//==============================================================================
// AlarmPanel 实现 — 纯 UI（DB 写入由 ParseWorker 线程处理）
//==============================================================================

#include "AlarmPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDateTime>

AlarmPanel::AlarmPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *top = new QHBoxLayout;
    m_indicator = new QLabel("●", this);
    m_indicator->setStyleSheet("color: green; font-size: 18px; font-weight: bold;");
    top->addWidget(m_indicator);
    m_countLabel = new QLabel("告警: 0", this);
    top->addWidget(m_countLabel);
    top->addStretch();

    auto *clearBtn = new QPushButton("清空", this);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_list->clear();
        m_totalTriggered = 0;
        m_countLabel->setText("告警: 0");
    });
    top->addWidget(clearBtn);
    layout->addLayout(top);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    layout->addWidget(m_list);

    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(800);
    connect(m_blinkTimer, &QTimer::timeout, this, &AlarmPanel::blinkIndicator);
}

void AlarmPanel::onAlarmTriggered(int channel, double value, double threshold, bool isUpper)
{
    auto key = qMakePair(channel, isUpper);
    auto now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastTrigger.contains(key) && (now - m_lastTrigger[key]) < 1000) {
        m_pendingDedup[key] = value;
        return;
    }
    m_lastTrigger[key] = now;

    if (m_pendingDedup.contains(key)) {
        value = m_pendingDedup.take(key);
    }

    QString dir = isUpper ? "超上限" : "低下限";
    QString text = QString("[%1] CH%2 %3: %4 %5 %6")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
        .arg(channel).arg(dir)
        .arg(value, 0, 'f', 1)
        .arg(isUpper ? ">=" : "<=")
        .arg(threshold, 0, 'f', 1);

    addEntry(text, QColor(255, 230, 230));

    m_indicator->setStyleSheet("color: red; font-size: 18px; font-weight: bold;");
    m_blinkTimer->start();

    m_totalTriggered++;
    m_countLabel->setText(QString("告警: %1").arg(m_totalTriggered));
}

void AlarmPanel::onAlarmCleared(int channel)
{
    auto key = qMakePair(channel, true);
    auto key2 = qMakePair(channel, false);
    m_lastTrigger.remove(key);
    m_lastTrigger.remove(key2);
    m_pendingDedup.remove(key);
    m_pendingDedup.remove(key2);

    QString text = QString("[%1] CH%2 告警清除")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
        .arg(channel);

    addEntry(text, QColor(230, 255, 230));

    if (m_totalTriggered > 0) m_totalTriggered--;
    m_countLabel->setText(QString("告警: %1").arg(m_totalTriggered));

    bool anyActive = false;
    for (auto it = m_lastTrigger.begin(); it != m_lastTrigger.end(); ++it) {
        if (QDateTime::currentMSecsSinceEpoch() - it.value() < 5000) {
            anyActive = true; break;
        }
    }
    if (!anyActive) {
        m_blinkTimer->stop();
        m_indicator->setStyleSheet("color: green; font-size: 18px; font-weight: bold;");
    }
}

void AlarmPanel::blinkIndicator()
{
    m_blinkOn = !m_blinkOn;
    if (m_blinkOn)
        m_indicator->setStyleSheet("color: red; font-size: 18px; font-weight: bold;");
    else
        m_indicator->setStyleSheet("color: #cc6666; font-size: 18px; font-weight: bold;");
}

void AlarmPanel::addEntry(const QString &text, const QColor &bg)
{
    auto *item = new QListWidgetItem(text);
    item->setBackground(bg);
    m_list->insertItem(0, item);
    while (m_list->count() > 100)
        delete m_list->takeItem(m_list->count() - 1);
}
