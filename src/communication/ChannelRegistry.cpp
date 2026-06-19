//==============================================================================
// ChannelRegistry 实现
//
// 注册表用静态 QVector 存储，线程安全由 C++11 静态局部变量保证。
// 每个通道类型的注册在 .cpp 的静态初始化块中完成（main() 之前）。
//==============================================================================

#include "ChannelRegistry.h"
#include "IChannel.h"
#include <QDebug>

QVector<ChannelDescriptor> ChannelRegistry::s_registry;

void ChannelRegistry::registerChannel(const ChannelDescriptor &desc)
{
    // 防止重复注册（同一 id 只注册一次）
    for (const auto &d : s_registry) {
        if (d.id == desc.id) {
            qWarning() << "ChannelRegistry: duplicate id" << desc.id;
            return;
        }
    }
    s_registry.append(desc);
}

QVector<ChannelDescriptor> ChannelRegistry::availableChannels()
{
    return s_registry;
}

IChannel* ChannelRegistry::create(const QString &id,
                                   const QVariantMap &config,
                                   QObject *parent)
{
    for (const auto &desc : s_registry) {
        if (desc.id == id) {
            if (desc.factory)
                return desc.factory(config, parent);
            qWarning() << "ChannelRegistry:" << id << "has null factory";
            return nullptr;
        }
    }
    qWarning() << "ChannelRegistry: unknown channel id" << id;
    return nullptr;
}
