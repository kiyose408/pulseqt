//==============================================================================
// ExportDialog — CSV 导出对话框
//==============================================================================
//
// 功能：选择时间范围 + 通道，从 SQLite 查询历史数据，输出 CSV 文件。
// 使用 QFileDialog 选择保存路径。
//==============================================================================

#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include <QDialog>
#include <QDateTimeEdit>
#include <QCheckBox>
#include <QPushButton>
#include "DatabaseManager.h"

/// @brief CSV 导出对话框
///
/// 用户选择时间范围和通道 → 从 DatabaseManager 查询 → 写入 CSV。
class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    /// @brief 构造，传入数据库管理器
    /// @param db DatabaseManager 指针（通常来自 ParseWorker）
    explicit ExportDialog(DatabaseManager *db, QWidget *parent = nullptr);

private slots:
    /// @brief 执行导出：查询 → QFileDialog → 写入 CSV
    void onExport();

private:
    DatabaseManager *m_db;
    QDateTimeEdit *m_fromEdit;     // 起始时间
    QDateTimeEdit *m_toEdit;       // 结束时间
    QCheckBox *m_chk[8];           // 通道选择复选框
    QPushButton *m_exportBtn;
    QPushButton *m_cancelBtn;
};

#endif // EXPORTDIALOG_H
