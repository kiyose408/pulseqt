//==============================================================================
// MagDataPoint — 磁场值数据点
//
// 单次磁场采样值，含上位机时间戳。
// TCP: data[0]=Mag_X1, data[1]=Mag_Y1
// Serial: data[0]=Mag_Serial, data[1]=0 (unused)
//==============================================================================

#ifndef MAGDATAPOINT_H
#define MAGDATAPOINT_H

#include <cstdint>

struct MagDataPoint
{
    qint64  timestamp = 0;          // 上位机时间戳 (ms)
    double  data[2]   = {0, 0};    // 磁场值 [0]=主值, [1]=次值

    // 便捷构造
    static MagDataPoint single(double val, qint64 ts = 0) {
        MagDataPoint p;
        p.timestamp = ts;
        p.data[0] = val;
        p.data[1] = 0.0;
        return p;
    }
    static MagDataPoint dual(double v0, double v1, qint64 ts = 0) {
        MagDataPoint p;
        p.timestamp = ts;
        p.data[0] = v0;
        p.data[1] = v1;
        return p;
    }
};

#endif // MAGDATAPOINT_H
