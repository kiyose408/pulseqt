//==============================================================================
// DollarParser 实现
//==============================================================================

#include "DollarParser.h"
#include "Logger.h"
#include <QDebug>
#include <QRegularExpressionMatch>

DollarParser::DollarParser(QObject *parent)
    : QObject(parent)
    , m_regex(QStringLiteral(R"(^\$(\d{7})\.(\d{6})$)"))
{
}

void DollarParser::feed(const QByteArray &data)
{
    // 追加新数据
    m_pending.append(data);

    // 按 '\n' 拆行
    while (true) {
        int idx = m_pending.indexOf('\n');
        if (idx < 0) break;

        // 提取一行（不含 \n）
        QByteArray line = m_pending.left(idx);

        // 去掉可能的 '\r'
        if (line.endsWith('\r'))
            line.chop(1);

        // 移除已处理部分（含 \n）
        m_pending.remove(0, idx + 1);

        // 跳过空行
        if (line.isEmpty())
            continue;

        processLine(line);
    }
}

void DollarParser::processLine(const QByteArray &line)
{
    // 必须以 '$' 开头
    if (line.isEmpty() || line[0] != '$') {
        qInfo() << "DollarParser: skipping non-$ line:" << line;
        return;
    }

    // 正则匹配: $xxxxxxx.xxxxxx
    QRegularExpressionMatch match = m_regex.match(QString::fromLatin1(line));
    if (!match.hasMatch()) {
        qWarning() << "DollarParser: format mismatch:" << line;
        emit parseError("format mismatch", line);
        return;
    }

    // 提取整数和小数部分
    bool okInt = false, okFrac = false;
    int intPart   = match.captured(1).toInt(&okInt);
    int fracPart  = match.captured(2).toInt(&okFrac);

    if (!okInt || !okFrac) {
        qWarning() << "DollarParser: number parse error:" << line;
        emit parseError("number parse error", line);
        return;
    }

    // 合成浮点值
    double value = static_cast<double>(intPart) + static_cast<double>(fracPart) / 1000000.0;

    // 上位机时间戳
    qint64 ts = QDateTime::currentMSecsSinceEpoch();

    emit valueReady(value, ts);
}
