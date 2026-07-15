//==============================================================================
// IFilter — 数据管道过滤器接口
//==============================================================================

#ifndef IFILTER_H
#define IFILTER_H

#include <QString>
#include "DataPoint.h"

class IFilter
{
public:
    virtual ~IFilter() = default;
    virtual DataPoint process(const DataPoint &dp) = 0;
    virtual QString name() const = 0;
    virtual bool isEnabled() const { return true; }
    virtual void setEnabled(bool) {}
};

#endif // IFILTER_H
