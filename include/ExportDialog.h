#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include <QDialog>
#include <QDateTimeEdit>
#include <QCheckBox>
#include <QPushButton>
#include "DatabaseManager.h"

class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ExportDialog(DatabaseManager *db, QWidget *parent = nullptr);

private slots:
    void onExport();

private:
    DatabaseManager *m_db;

    // 时间范围
    QDateTimeEdit *m_fromEdit;
    QDateTimeEdit *m_toEdit;

    // 通道选择
    QCheckBox *m_chk[8];

    // 按钮
    QPushButton *m_exportBtn;
    QPushButton *m_cancelBtn;
};

#endif // EXPORTDIALOG_H
