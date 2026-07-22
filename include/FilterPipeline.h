//==============================================================================
// FilterPipeline — 数据管道容器，链式调用 IFilter
//==============================================================================

#ifndef FILTERPIPELINE_H
#define FILTERPIPELINE_H

#include <QObject>
#include <vector>
#include <memory>
#include "IFilter.h"

class FilterPipeline : public QObject
{
    Q_OBJECT

public:
    explicit FilterPipeline(QObject *parent = nullptr);

    void addFilter(std::unique_ptr<IFilter> filter);
    void removeFilter(int index);
    void clear();
    int  count() const { return static_cast<int>(m_filters.size()); }
    IFilter *filterAt(int i) const;

    DataPoint process(const DataPoint &dp) const;
    bool enabled() const { return m_enabled; }

public slots:
    void setEnabled(bool on);

private:
    std::vector<std::unique_ptr<IFilter>> m_filters;
    bool m_enabled = true;
};

#endif // FILTERPIPELINE_H
