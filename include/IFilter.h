//==============================================================================
// IFilter — 数据管道过滤器接口
//==============================================================================
//
// 所有过滤器的基类。纯虚接口，无 QObject 依赖。
// 通道掩码：空 = 全部通道生效；非空 = 只对列出的 channel index 生效。
//==============================================================================

#ifndef IFILTER_H
#define IFILTER_H

#include <QString>
#include <QVector>
#include "DataPoint.h"

/**
 * @brief 过滤器抽象基类
 *
 * 子类只需实现 process() 和 name()。
 * channels()/setChannels() 提供通道选择性过滤。
 */
class IFilter
{
public:
    virtual ~IFilter() = default;

    /// @brief 处理一个数据点
    /// @param dp 输入数据点
    /// @return 处理后的数据点（未选中通道原值保留）
    virtual DataPoint process(const DataPoint &dp) = 0;

    /// @brief 返回过滤器名称（用于 UI 显示和序列化）
    virtual QString name() const = 0;

    /// @brief 是否启用（默认 true）
    virtual bool isEnabled() const { return true; }
    /// @brief 设置启用状态
    virtual void setEnabled(bool) {}

    /// @brief 返回生效的通道掩码（空 = 全部通道）
    virtual QVector<int> channels() const { return m_channels; }
    /// @brief 设置生效通道
    /// @param ch 通道索引列表，如 {0, 2, 3}
    virtual void setChannels(const QVector<int> &ch) { m_channels = ch; }

protected:
    /// @brief 检查通道是否在掩码内
    /// @param ch 通道索引
    /// @return true=该通道生效，false=跳过
    bool channelActive(int ch) const {
        return m_channels.isEmpty() || m_channels.contains(ch);
    }
    QVector<int> m_channels;
};

#endif // IFILTER_H
