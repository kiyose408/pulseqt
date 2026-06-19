//==============================================================================
// TestChannelRegistry — 通道注册表单元测试
//
// 覆盖：注册/去重/查询/工厂创建/null保护/未知id
//==============================================================================

#include <QtTest>
#include "ChannelRegistry.h"
#include "IChannel.h"

// ── 最小化 Mock 通道 ─────────────────────────────────
class MockChannel : public IChannel
{
    Q_OBJECT
public:
    explicit MockChannel(QObject *parent = nullptr) : IChannel(parent) {}
    bool open() override           { return true; }
    void close() override          {}
    bool isOpen() const override   { return false; }
    qint64 write(const QByteArray &) override { return 0; }
};

class TestChannelRegistry : public QObject
{
    Q_OBJECT

private slots:
    // ────────────────────────────────────────────────────
    // ① 注册一个通道 → 出现在 availableChannels 中
    // ────────────────────────────────────────────────────
    void registerAndList()
    {
        ChannelDescriptor desc;
        desc.id   = "test_mock";
        desc.name = "Mock 通道";
        desc.configFields = { ConfigField("addr", "地址", "string", "0.0.0.0") };
        desc.factory = +[](const QVariantMap &cfg, QObject *p) -> IChannel* {
            Q_UNUSED(cfg); return new MockChannel(p);
        };
        ChannelRegistry::registerChannel(desc);

        auto list = ChannelRegistry::availableChannels();
        bool found = false;
        for (const auto &d : list)
            if (d.id == "test_mock") { found = true; break; }
        QVERIFY(found);
    }

    // ────────────────────────────────────────────────────
    // ② 重复注册同一 id → 第二次被忽略
    // ────────────────────────────────────────────────────
    void duplicateIgnored()
    {
        int before = ChannelRegistry::availableChannels().size();

        ChannelDescriptor dup;
        dup.id   = "test_mock";   // 与 ① 相同
        dup.name = "Duplicate";
        ChannelRegistry::registerChannel(dup);

        int after = ChannelRegistry::availableChannels().size();
        QCOMPARE(after, before);  // 数量不变
    }

    // ────────────────────────────────────────────────────
    // ③ create 正确类型 + 传入配置
    // ────────────────────────────────────────────────────
    void createReturnsCorrectType()
    {
        QVariantMap cfg; cfg["addr"] = "192.168.1.1";
        auto *ch = ChannelRegistry::create("test_mock", cfg);
        QVERIFY(ch != nullptr);
        QVERIFY(dynamic_cast<MockChannel*>(ch) != nullptr);
        delete ch;
    }

    // ────────────────────────────────────────────────────
    // ④ null factory → 返回 nullptr
    // ────────────────────────────────────────────────────
    void nullFactory()
    {
        ChannelDescriptor desc;
        desc.id   = "null_factory";
        desc.name = "Null";
        desc.factory = nullptr;
        ChannelRegistry::registerChannel(desc);

        auto *ch = ChannelRegistry::create("null_factory", {});
        QVERIFY(ch == nullptr);
    }

    // ────────────────────────────────────────────────────
    // ⑤ 未知 id → 返回 nullptr
    // ────────────────────────────────────────────────────
    void unknownId()
    {
        auto *ch = ChannelRegistry::create("nonexistent", {});
        QVERIFY(ch == nullptr);
    }
};

QTEST_MAIN(TestChannelRegistry)
#include "TestChannelRegistry.moc"
