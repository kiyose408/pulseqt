#include "DataTableModel.h"
#include <QDateTime>

DataTableModel::DataTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    m_throttleTimer = new QTimer(this);
    m_throttleTimer->setInterval(100);
    connect(m_throttleTimer, &QTimer::timeout,
            this, &DataTableModel::onThrottleTimer);
    m_throttleTimer->start();
}

void DataTableModel::setDataBuffer(DataBuffer *buffer)
{
    m_buffer = buffer;
    if(!m_buffer)
        return;
    // ═══════════════════════════════════════════════
    // 目的 ①：订阅"有新数据"的通知
    //   每次 buffer.push() 后自动触发 onBufferUpdated，
    //   模型重新取 snapshot → 刷新表格
    // ═══════════════════════════════════════════════
    connect(m_buffer, &DataBuffer::bufferUpdated,
            this, &DataTableModel::onBufferUpdated,
            Qt::QueuedConnection);      //队列连接避免死锁
    // 目的 ②：立即加载已有数据
    m_dirty = true;
    onThrottleTimer();   // 首次立即刷新，不走节流等待

}

DataBuffer *DataTableModel::dataBuffer() const
{
    return m_buffer;
}

int DataTableModel::rowCount(const QModelIndex &parent) const
{
    if(parent.isValid())
        return 0;       //Qt规范：非根节点行数 = 0（平铺表没有父子层级）
    return m_snapshot.size();       //数据多少条，表格就多少行
}

int DataTableModel::columnCount(const QModelIndex &parent) const
{
    if(parent.isValid())
        return 0;
    return 1 + m_channelCount;        //时间戳+ch0 +ch1 +ch2
}

QVariant DataTableModel::data(const QModelIndex &index, int role) const
{
    // 1. 越界检查
    if (!index.isValid() || index.row() >= m_snapshot.size())
        return {};

    const DataPoint &dp = m_snapshot[index.row()];

    // 2. 根据 role 返回不同数据
    if (role == Qt::DisplayRole) {
        if (index.column() == 0) {
            // 第 0 列：格式化时间戳 → "14:30:00.123"
            return QDateTime::fromMSecsSinceEpoch(dp.timestamp)
                .toString("hh:mm:ss.zzz");
        } else {
            // 第 1~N 列：通道值（double → 保留 1 位小数）
            int chIdx = index.column() - 1;
            if (chIdx < dp.channels.size())
                return QString::number(dp.channels[chIdx], 'f', 1);
        }
    }

    return {};   // 不处理的 role 返回空
}

QVariant DataTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    if (section == 0)
        return QString("时间");

    return QString("CH%1").arg(section - 1);  // CH0, CH1, CH2
}

void DataTableModel::onBufferUpdated(int count)
{
    Q_UNUSED(count);
    m_dirty = true;   // 只标记，不立即刷新（节流到定时器）
}

void DataTableModel::onThrottleTimer()
{
    if (!m_dirty || !m_buffer) return;
    m_dirty = false;

    QVector<DataPoint> snap = m_buffer->snapshot();
    beginResetModel();
    m_snapshot = std::move(snap);
    endResetModel();
    emit dataRefreshed();
}

void DataTableModel::setChannelCount(int count)
{
    if (count < 1 || count > 16) return;
    if (count == m_channelCount) return;
    m_channelCount = count;
    beginResetModel();
    endResetModel();
}
