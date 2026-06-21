#ifndef DATATABLEMODEL_H
#define DATATABLEMODEL_H
#include <QAbstractTableModel>
#include <QDateTime>
#include <QTimer>
#include "DataBuffer.h"
class DataTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit DataTableModel(QObject *parent = nullptr);

    void setDataBuffer(DataBuffer *buffer);         //绑定数据源
    void setChannelCount(int count);                //握手完成后设置通道数
    DataBuffer *dataBuffer() const;
    // QAbstractTableModel 必须覆写的 3 个纯虚函数
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

signals:
    void dataRefreshed();   // 数据刷新后发射，MainWindow 用于触发自动滚动

private slots:
    void onBufferUpdated(int count);
    void onThrottleTimer();           // 节流定时器

private:
    DataBuffer *m_buffer = nullptr;
    int m_channelCount = 3;             // 通道数（默认 3，握手后更新）
    QVector<DataPoint> m_snapshot;
    QTimer *m_throttleTimer = nullptr; // 节流：100ms 最多刷新一次
    bool m_dirty = false;              // 有新数据但未刷新
};

#endif // DATATABLEMODEL_H
