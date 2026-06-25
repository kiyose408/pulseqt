//==============================================================================
// DollarParser — 串口 $ 格式 ASCII 数据解析器
//
// 解析格式: $xxxxxxx.xxxxxx\n
//   7 位整数 + '.' + 6 位小数 + 换行符 (\n 或 \r\n)
//   例: $1234567.123456\n → value = 1234567.123456
//
// 设计:
//   1. feed() 追加数据到 m_pending 缓冲区
//   2. 按 '\n' 拆行，逐行解析
//   3. 不以 '$' 开头的行 → 跳过 + 日志
//   4. '$' 开头但格式不匹配 → 跳过 + WARN
//   5. 末尾无 '\n' 的半行 → 保留到下次 feed()
//   6. 解析成功 → emit valueReady(value, QDateTime::currentMSecsSinceEpoch())
//==============================================================================

#ifndef DOLLARPARSER_H
#define DOLLARPARSER_H

#include <QObject>
#include <QByteArray>
#include <QDateTime>
#include <QRegularExpression>

class DollarParser : public QObject
{
    Q_OBJECT

public:
    explicit DollarParser(QObject *parent = nullptr);

    // 喂入原始字节数据
    void feed(const QByteArray &data);

signals:
    // 解析成功 — 磁场值 + 上位机时间戳 (ms)
    void valueReady(double value, qint64 timestamp);

    // 解析失败（用于调试统计）
    void parseError(const QString &reason, const QByteArray &line);

private:
    void processLine(const QByteArray &line);

    QByteArray m_pending;       // 未完成行缓存（半行数据）
    QRegularExpression m_regex; // 预编译正则: ^\$(\d{7})\.(\d{6})$
};

#endif // DOLLARPARSER_H
