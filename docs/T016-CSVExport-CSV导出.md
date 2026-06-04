# T016 CSV 导出 — 实现逻辑与核心代码

> 独立 SQLite 连接 + QDialog + QTextStream 写文件。

---

## 一、整体流程

```
菜单"导出CSV" → MainWindow::onExportCsv()
  → 创建独立 DatabaseManager（不和 ParseWorker 共享连接）
  → 弹出 ExportDialog（模态）
    → 用户选时间范围 + 勾通道 + 点导出
    → QFileDialog 选保存路径
    → QFile → QTextStream
    → m_db.query(from, to) → 遍历 → 写 CSV 行
    → QMessageBox 提示完成
```

---

## 二、核心设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| DB 连接 | ExportDialog 自建独立连接 | SQLite 连接不能跨线程共享 |
| 对话框类型 | QDialog（模态） | 导出时主窗口不可操作，逻辑简单 |
| 文件写入 | QTextStream | Qt 内置文本流，自动处理编码 |
| 通道选择 | QCheckBox 数组 | 支持 CH0-7，默认勾 CH0-2 |
| 查询上限 | 99999999 | 不设实际限制，由时间范围控制 |

---

## 三、核心代码

### ExportDialog::onExport()

```cpp
void ExportDialog::onExport()
{
    // ① 时间范围
    uint64_t tFrom = m_fromEdit->dateTime().toMSecsSinceEpoch();
    uint64_t tTo   = m_toEdit->dateTime().toMSecsSinceEpoch();

    if (tFrom >= tTo) { QMessageBox::warning(...); return; }

    // ② 勾选的通道
    QVector<int> selected;
    for (int i = 0; i < 8; ++i)
        if (m_chk[i]->isChecked()) selected.append(i);
    if (selected.isEmpty()) { QMessageBox::warning(...); return; }

    // ③ 保存路径
    QString path = QFileDialog::getSaveFileName(this, "保存", "", "CSV(*.csv)");
    if (path.isEmpty()) return;

    // ④ 打开文件
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream stream(&file);

    // ⑤ 标题行
    QStringList headers; headers << "时间";
    for (int ch : selected) headers << QString("CH%1").arg(ch);
    stream << headers.join(",") << "\n";

    // ⑥ 数据行
    auto results = m_db->query(tFrom, tTo, 99999999);
    for (const auto &dp : results) {
        QStringList row;
        row << QDateTime::fromMSecsSinceEpoch(dp.timestamp)
                   .toString("yyyy-MM-dd hh:mm:ss.zzz");
        for (int ch : selected)
            row << QString::number(dp.channels.value(ch, 0), 'f', 3);
        stream << row.join(",") << "\n";
    }

    file.close();
    QMessageBox::information(this, "完成", QString("已导出 %1 条").arg(results.size()));
    accept();
}
```

### MainWindow 集成

```cpp
void MainWindow::onExportCsv()
{
    DatabaseManager db;                // 独立连接
    if (!db.init("data.db")) return;
    ExportDialog dialog(&db, this);
    dialog.exec();
}   // db 自动析构
```

---

## 四、关键 API 速查

| 操作 | API |
|------|------|
| 时间控件→毫秒 | `QDateTimeEdit::dateTime().toMSecsSinceEpoch()` |
| 毫秒→格式化时间 | `QDateTime::fromMSecsSinceEpoch(ms).toString("hh:mm:ss.zzz")` |
| 文件保存对话框 | `QFileDialog::getSaveFileName(parent, title, dir, filter)` |
| 写文本文件 | `QFile::open(WriteOnly\|Text)` → `QTextStream << data << "\n"` |
| double→字符串 | `QString::number(value, 'f', 3)` — 保留 3 位小数 |
| 模态对话框 | `dialog.exec()` — 阻塞，关闭后才返回 |
