//==============================================================================
// FilterPipeline 实现
//==============================================================================

#include "FilterPipeline.h"

FilterPipeline::FilterPipeline(QObject *parent) : QObject(parent) {}

void FilterPipeline::addFilter(std::unique_ptr<IFilter> filter)
{
    if (filter)
        m_filters.push_back(std::move(filter));
}

void FilterPipeline::removeFilter(int index)
{
    if (index >= 0 && index < m_filters.size())
        m_filters.erase(m_filters.begin() + index);
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
    return result;
}
