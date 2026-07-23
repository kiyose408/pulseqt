//==============================================================================
// FilterConfigDialog — 过滤器配置对话框
//==============================================================================

#ifndef FILTERCONFIGDIALOG_H
#define FILTERCONFIGDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>

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

    FilterPipeline  *m_pipeline;
    QComboBox       *m_typeCombo;
    QLabel          *m_windowLabel;
    QSpinBox        *m_windowSpin;
    QLineEdit       *m_channelEdit;
    QListWidget     *m_listWidget;

    // 阈值告警专用
    QWidget         *m_thresholdRow;
    QDoubleSpinBox  *m_upperSpin;
    QDoubleSpinBox  *m_lowerSpin;
    QDoubleSpinBox  *m_hysteresisSpin;
};

#endif // FILTERCONFIGDIALOG_H
