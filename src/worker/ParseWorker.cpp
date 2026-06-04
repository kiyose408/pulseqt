#include "ParseWorker.h"
#include <QDateTime>

ParseWorker::ParseWorker(QObject *parent): QObject(parent)
{
    m_dbManager.init("data.db");

    // 解码器出帧 → 转 DataPoint → 双写
    connect(&m_decoder, &ProtocolDecoder::frameDecoded, this,
            [this](const Frame &frame) {
                if (!m_collecting) return;   // 暂停中
                if (frame.type != Frame::TYPE_DATA || frame.payload.size() < 6) return;

                DataPoint dp;
                dp.timestamp = QDateTime::currentMSecsSinceEpoch();
                auto d = reinterpret_cast<const uint8_t*>(frame.payload.constData());
                dp.channels = {
                    double(d[0] | (d[1] << 8)),
                    double(d[2] | (d[3] << 8)),
                    double(d[4] | (d[5] << 8))
                };

                m_buffer.push(dp);
                m_dbManager.insert(dp);
                emit dataPointReady();   // 通知 UI
            });
}
void ParseWorker::onRawDataReceived(const QByteArray &data)
{
    m_decoder.feed(data);
}

void ParseWorker::setCollecting(bool on)
{
    m_collecting = on;
}
DataBuffer *ParseWorker::buffer()
{
    return &m_buffer;
}

DatabaseManager *ParseWorker::dbManager()
{
    return &m_dbManager;
}
ParseWorker::~ParseWorker()
{
    m_dbManager.flush();   // 提交缓冲的最后一波数据（不到 100 条也落盘）
}
