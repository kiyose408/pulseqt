//==============================================================================
// ConnectionDialog 实现
//
// UI 由 ChannelRegistry::availableChannels() 驱动：
//   通道下拉框 ← descriptor.name
//   配置面板 ← descriptor.configFields（每项动态创建对应控件）
//==============================================================================

#include "ConnectionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QDebug>

ConnectionDialog::ConnectionDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("连接设备");
    setMinimumWidth(360);

    auto *mainLayout = new QVBoxLayout(this);

    // ── 通道类型选择 ─────────────────────────────────
    mainLayout->addWidget(new QLabel("通道类型:"));
    m_channelCombo = new QComboBox(this);
    mainLayout->addWidget(m_channelCombo);

    // ── 配置面板（堆叠，切换通道类型时换页）────────────
    m_configStack = new QStackedWidget(this);
    mainLayout->addWidget(m_configStack);

    // ── 确认 / 取消 ──────────────────────────────────
    auto *btnBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btnBox->button(QDialogButtonBox::Ok)->setText("连接");
    connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(btnBox);

    // 填充通道列表
    buildChannelList();
    connect(m_channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ConnectionDialog::onChannelChanged);
}

// ── 从注册表填充通道下拉框 + 配置页 ──────────────────
void ConnectionDialog::buildChannelList()
{
    auto channels = ChannelRegistry::availableChannels();
    for (int i = 0; i < channels.size(); ++i) {
        const auto &desc = channels[i];
        m_channelCombo->addItem(desc.name, desc.id);
        m_configStack->addWidget(buildConfigPage(desc));
    }
    if (!channels.isEmpty())
        onChannelChanged(0);
}

// ── 根据 descriptor 动态生成配置页 ────────────────────
QWidget* ConnectionDialog::buildConfigPage(const ChannelDescriptor &desc)
{
    auto *page  = new QWidget(this);
    auto *form  = new QFormLayout(page);
    form->setContentsMargins(0, 10, 0, 0);

    for (const auto &field : desc.configFields) {
        if (field.type == "combo") {
            auto *cb = new QComboBox(page);
            cb->addItems(field.candidates);
            int idx = field.candidates.indexOf(field.defaultValue.toString());
            if (idx >= 0) cb->setCurrentIndex(idx);
            form->addRow(field.label + ":", cb);
            m_configWidgets[field.key] = cb;
            m_defaults[field.key] = field.defaultValue;

        } else if (field.type == "int") {
            auto *sb = new QSpinBox(page);
            sb->setRange(1, 65535);
            sb->setValue(field.defaultValue.toInt());
            form->addRow(field.label + ":", sb);
            m_configWidgets[field.key] = sb;
            m_defaults[field.key] = field.defaultValue;

        } else {
            // "string" 类型
            auto *le = new QLineEdit(page);
            le->setText(field.defaultValue.toString());
            form->addRow(field.label + ":", le);
            m_configWidgets[field.key] = le;
            m_defaults[field.key] = field.defaultValue;
        }
    }
    return page;
}

// ── 通道类型切换 → 换配置页 ──────────────────────────
void ConnectionDialog::onChannelChanged(int index)
{
    if (index >= 0 && index < m_configStack->count()) {
        m_configStack->setCurrentIndex(index);
        m_currentId = m_channelCombo->itemData(index).toString();
    }
}

// ── 获取用户选择的通道 id ────────────────────────────
QString ConnectionDialog::selectedChannelId() const
{
    return m_currentId;
}

// ── 获取用户填写/修改的配置 map ──────────────────────
QVariantMap ConnectionDialog::config() const
{
    QVariantMap result = m_defaults;
    for (auto it = m_configWidgets.begin(); it != m_configWidgets.end(); ++it) {
        const QString &key = it.key();
        auto *w = it.value();

        if (auto *cb = qobject_cast<QComboBox*>(w))
            result[key] = cb->currentText();
        else if (auto *sb = qobject_cast<QSpinBox*>(w))
            result[key] = sb->value();
        else if (auto *le = qobject_cast<QLineEdit*>(w))
            result[key] = le->text();
    }
    return result;
}
