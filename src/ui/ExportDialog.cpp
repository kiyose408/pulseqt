#include "ExportDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDateTime>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>

ExportDialog::ExportDialog(DatabaseManager *db, QWidget *parent)
    : QDialog(parent), m_db(db)
{
    setWindowTitle("导出 CSV");
    resize(400, 300);

    auto *mainLayout = new QVBoxLayout(this);

    // ── 时间范围 ──
    auto *timeGroup = new QGroupBox("时间范围", this);
    auto *timeLayout = new QFormLayout(timeGroup);
    m_fromEdit = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(-3600), this);
    m_toEdit   = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    m_fromEdit->setDisplayFormat("yyyy-MM-dd hh:mm:ss");
    m_toEdit->setDisplayFormat("yyyy-MM-dd hh:mm:ss");
    timeLayout->addRow("开始:", m_fromEdit);
    timeLayout->addRow("结束:", m_toEdit);
    mainLayout->addWidget(timeGroup);

    // ── 通道选择 ──
    auto *chGroup = new QGroupBox("通道选择", this);
    auto *chLayout = new QVBoxLayout(chGroup);
    for (int i = 0; i < 8; ++i) {
        m_chk[i] = new QCheckBox(QString("CH%1").arg(i), this);
        if (i < 3) m_chk[i]->setChecked(true);   // 默认勾 CH0/CH1/CH2
        chLayout->addWidget(m_chk[i]);
    }
    mainLayout->addWidget(chGroup);

    // ── 按钮 ──
    auto *btnLayout = new QHBoxLayout;
    m_exportBtn = new QPushButton("导出", this);
    m_cancelBtn = new QPushButton("取消", this);
    btnLayout->addStretch();
    btnLayout->addWidget(m_exportBtn);
    btnLayout->addWidget(m_cancelBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_exportBtn, &QPushButton::clicked, this, &ExportDialog::onExport);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}
void ExportDialog::onExport()
{
    // ① 获取时间范围（毫秒时间戳）
    uint64_t tFrom = m_fromEdit->dateTime().toMSecsSinceEpoch();
    uint64_t tTo   = m_toEdit->dateTime().toMSecsSinceEpoch();

    if (tFrom >= tTo) {
        QMessageBox::warning(this, "错误", "开始时间必须早于结束时间");
        return;
    }

    // ② 收集勾选的通道
    QVector<int> selectedChannels;
    for (int i = 0; i < 8; ++i) {
        if (m_chk[i]->isChecked())
            selectedChannels.append(i);
    }

    if (selectedChannels.isEmpty()) {
        QMessageBox::warning(this, "错误", "至少选择一个通道");
        return;
    }

    // ③ 弹出保存路径
    QString path = QFileDialog::getSaveFileName(
        this, "保存 CSV", QString(), "CSV Files (*.csv)");

    if (path.isEmpty())
        return;   // 用户取消

    // ④ 打开文件
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "错误", "无法创建文件: " + path);
        return;
    }

    QTextStream stream(&file);

    // ⑤ 写标题行
    QStringList headers;
    headers << "时间";
    for (int ch : selectedChannels)
        headers << QString("CH%1").arg(ch);
    stream << headers.join(",") << "\n";

    // ⑥ 查 DB → 逐行写入
    auto results = m_db->query(tFrom, tTo, 99999999);   // 不限条数
    int count = 0;

    for (const auto &dp : results) {
        QStringList row;
        // 第一列：格式化时间戳
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(dp.timestamp);
        row << dt.toString("yyyy-MM-dd hh:mm:ss.zzz");

        // 后续列：勾选的通道值
        for (int ch : selectedChannels) {
            if (ch < dp.channels.size())
                row << QString::number(dp.channels[ch], 'f', 3);   // 保留 3 位小数
            else
                row << "0";
        }

        stream << row.join(",") << "\n";
        count++;
    }

    file.close();

    // ⑦ 完成提示
    QMessageBox::information(this, "导出完成",
                             QString("已导出 %1 条记录到\n%2").arg(count).arg(path));
    accept();   // 关闭对话框
}
