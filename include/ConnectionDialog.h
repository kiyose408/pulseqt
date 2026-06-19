//==============================================================================
// ConnectionDialog — 由 ChannelRegistry 驱动的动态连接对话框
//
// 通道列表、配置字段全部由注册表提供，
// 加新通道时此对话框不需要改动。
//==============================================================================

#ifndef CONNECTIONDIALOG_H
#define CONNECTIONDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QStackedWidget>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QVariantMap>
#include <QMap>
#include "ChannelRegistry.h"

class ConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionDialog(QWidget *parent = nullptr);

    QString   selectedChannelId() const;
    QVariantMap config() const;

private slots:
    void onChannelChanged(int index);

private:
    void buildChannelList();
    QWidget* buildConfigPage(const ChannelDescriptor &desc);

    QComboBox       *m_channelCombo  = nullptr;
    QStackedWidget  *m_configStack   = nullptr;

    // 当前通道的配置控件映射: key → QWidget*
    QMap<QString, QWidget*> m_configWidgets;
    QString                  m_currentId;
    QVariantMap              m_defaults;  // 未填字段的兜底值
};

#endif // CONNECTIONDIALOG_H
