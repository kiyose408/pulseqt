//==============================================================================
// DataPoint - 单次采集的数据点
// 包含时间戳（毫秒）和通道值数组（double 精度）
//==============================================================================
#ifndef DATAPOINT_H
#define DATAPOINT_H

#include <cstdint>
#include <QVector>

struct DataPoint
{
    uint64_t timestamp = 0;             // 毫秒级时间戳（QDateTime::currentMSecsSinceEpoch()）
    QVector<double> channels;           // 通道值数组（ch0, ch1, ch2, ...）
};

#endif // DATAPOINT_H
