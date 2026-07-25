//==============================================================================
// DataTableModel — 实时数据表格模型
//==============================================================================
//
// QAbstractTableModel 子类，行=时间戳，列=通道值。
// 100ms 节流刷新，避免高频数据导致 UI 卡顿。
//==============================================================================

#ifndef DATATABLEMODEL_H
#define DATATABLEMODEL_H
#include <QAbstractTableModel>
#include <QDateTime>
#include <QTimer>
#include "DataBuffer.h"

/// @brief 实时数据表格模型，提供 QTableView 的数据源
///
/// 100ms 节流（m_throttleTimer）：数据到达后标记 m_dirty，
/// 定时器触发时批量刷新，避免每帧都触发 beginReset/endReset。
class DataTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit DataTableModel(QObject *parent = nullptr);

    /// @brief 绑定数据源
    void setDataBuffer(DataBuffer *buffer);
    /// @brief 握手完成后设置通道数量（影响列数）
    /// @param count 通道数
    void setChannelCount(int count);
    /// @brief 返回当前数据源
    DataBuffer *dataBuffer() const;

    // QAbstractTableModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

signals:
    /// @brief 数据刷新后发射，MainWindow 连接以触发表格自动滚动到底部
    void dataRefreshed();

private slots:
    void onBufferUpdated(int count);
    void onThrottleTimer();

private:
    DataBuffer *m_buffer = nullptr;
    int m_channelCount = 3;
    QVector<DataPoint> m_snapshot;
    QTimer *m_throttleTimer = nullptr;  // 100ms 节流
    bool m_dirty = false;
};

#endif // DATATABLEMODEL_H
