//==============================================================================
// FilterPipeline 实现
//==============================================================================

#include "FilterPipeline.h"
#include <QDebug>

FilterPipeline::FilterPipeline(QObject *parent) : QObject(parent) {}

void FilterPipeline::setEnabled(bool on)
{
    if (m_enabled != on) {
        m_enabled = on;
        qInfo() << "FilterPipeline:" << (on ? "ENABLED" : "DISABLED");
    }
}

void FilterPipeline::addFilter(std::unique_ptr<IFilter> filter)
{
    if (filter) {
        qInfo() << "FilterPipeline: filter added:" << filter->name()
                << "(total:" << m_filters.size() + 1 << ")";
        m_filters.push_back(std::move(filter));
    }
}

void FilterPipeline::removeFilter(int index)
{
    if (index >= 0 && index < m_filters.size()) {
        qInfo() << "FilterPipeline: filter removed at" << index;
        m_filters.erase(m_filters.begin() + index);
    }
}

void FilterPipeline::clear()
{
    m_filters.clear();
}

IFilter *FilterPipeline::filterAt(int i) const
{
    return (i >= 0 && i < m_filters.size()) ? m_filters[i].get() : nullptr;
}

DataPoint FilterPipeline::process(const DataPoint &dp) const
{
    if (!m_enabled) return dp;

    DataPoint result = dp;
    for (const auto &f : m_filters) {
        if (f && f->isEnabled())
            result = f->process(result);
    }

    // 每 100 条打印一次，确认管道在数据路径中生效
    static int callCount = 0;
    if (++callCount % 100 == 0) {
        qDebug() << "FilterPipeline: processed" << callCount
                 << "points," << m_filters.size() << "active filters";
    }
    return result;
}
