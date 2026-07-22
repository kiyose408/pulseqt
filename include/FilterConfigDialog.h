//==============================================================================
// FilterConfigDialog — 过滤器配置对话框
//
// 允许用户选择过滤器类型、调整窗口大小、查看当前管道状态。
// 工业场景：操作员调强度，不是无限叠加。
//==============================================================================

#ifndef FILTERCONFIGDIALOG_H
#define FILTERCONFIGDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QListWidget>
#include <QLineEdit>

class FilterPipeline;

class FilterConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FilterConfigDialog(FilterPipeline *pipeline, QWidget *parent = nullptr);

private slots:
    void onAdd();
    void onRemove();
    void onClear();

private:
    void refreshList();

    FilterPipeline *m_pipeline;
    QComboBox      *m_typeCombo;
    QSpinBox       *m_windowSpin;
    QLineEdit      *m_channelEdit;
    QListWidget    *m_listWidget;
};

#endif // FILTERCONFIGDIALOG_H
