//==============================================================================
// IFilter — 数据管道过滤器接口
//==============================================================================

#ifndef IFILTER_H
#define IFILTER_H

#include <QString>
#include <QVector>
#include "DataPoint.h"

class IFilter
{
public:
    virtual ~IFilter() = default;
    virtual DataPoint process(const DataPoint &dp) = 0;
    virtual QString name() const = 0;
    virtual bool isEnabled() const { return true; }
    virtual void setEnabled(bool) {}

    // 通道掩码：空 = 全部通道生效；非空 = 只对列出的 channel index 生效
    virtual QVector<int> channels() const { return m_channels; }
    virtual void setChannels(const QVector<int> &ch) { m_channels = ch; }

protected:
    bool channelActive(int ch) const {
        return m_channels.isEmpty() || m_channels.contains(ch);
    }
    QVector<int> m_channels;
};

#endif // IFILTER_H
