//==============================================================================
// ChannelRegistry — 通道注册表
//
// 每个通道类型通过静态初始化块自注册到全局注册表。
// MainWindow 通过注册表获取可用通道列表 + 创建实例，
// 不需要知道具体通道类名。
//
// 后期可平滑升级为插件系统（扫描 .dll 目录），接口层零改动。
//==============================================================================

#ifndef CHANNELREGISTRY_H
#define CHANNELREGISTRY_H

#include <QString>
#include <QVariant>
#include <QVector>
#include <QObject>

class IChannel;

// ── 配置字段描述 ──────────────────────────────────────
struct ConfigField {
    QString     key;
    QString     label;
    QString     type;
    QVariant    defaultValue;
    QStringList candidates;

    // 简单字段（string / int）
    ConfigField(const QString &k, const QString &lbl, const QString &t,
                const QVariant &def = {})
        : key(k), label(lbl), type(t), defaultValue(def) {}

    // combo 字段（带候选值）
    ConfigField(const QString &k, const QString &lbl, const QString &t,
                const QVariant &def, const QStringList &opts)
        : key(k), label(lbl), type(t), defaultValue(def), candidates(opts) {}
};

// ── 通道类型描述 ──────────────────────────────────────
struct ChannelDescriptor {
    QString id;                                    // "tcp", "serial"
    QString name;                                  // "TCP 客户端", "串口"
    QVector<ConfigField> configFields;             // 该通道需要的配置项
    IChannel* (*factory)(const QVariantMap &, QObject *);  // 工厂函数指针
};

// ── 全局注册表 ────────────────────────────────────────
class ChannelRegistry {
public:
    // 注册一个通道类型（由各通道 .cpp 的静态初始化块调用）
    static void registerChannel(const ChannelDescriptor &desc);

    // 返回当前平台可用的所有通道
    static QVector<ChannelDescriptor> availableChannels();

    // 根据 id 创建通道实例
    static IChannel* create(const QString &id,
                            const QVariantMap &config,
                            QObject *parent = nullptr);

private:
    static QVector<ChannelDescriptor> s_registry;
};

#endif // CHANNELREGISTRY_H
