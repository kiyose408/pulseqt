#ifndef DATATABLEMODEL_H
#define DATATABLEMODEL_H
#include <QAbstractTableModel>
#include <QDateTime>
#include "DataBuffer.h"
class DataTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit DataTableModel(QObject *parent = nullptr);

    void setDataBuffer(DataBuffer *buffer);         //绑定数据源
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

private:
    DataBuffer *m_buffer = nullptr;
    QVector<DataPoint> m_snapshot;   // 模型持有的数据副本（不含锁）
};

#endif // DATATABLEMODEL_H
