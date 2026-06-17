//==============================================================================
// DatabaseManager - SQLite 数据库管理器
//
// 职责：所有采集数据实时落盘，支持历史查询和自动清理。
//
// 核心机制：
//   1. WAL 模式：写操作不阻塞读取（PRAGMA journal_mode=WAL）
//   2. 批量事务：m_pending 缓冲满 100 条后一次 BEGIN→INSERT×100→COMMIT
//   3. channels 序列化：QVector<double> → QDataStream → QByteArray → BLOB
//   4. 时间索引：timestamp 列建索引，query() 和 cleanup() 走索引
//
// 表结构：
//   data_points(id INTEGER PK, timestamp INTEGER, channels BLOB)
//   idx_timestamp ON data_points(timestamp)
//==============================================================================

#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QVector>
#include "DataPoint.h"

class DatabaseManager : public QObject
{
    Q_OBJECT

    friend class TestDatabaseManager;  // 单元测试可访问 private 成员

public:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager() override;

    // 初始化数据库（建表 + WAL 模式 + 索引）
    // dbPath: 数据库文件路径，默认 "data.db"
    bool init(const QString &dbPath = "data.db");

    // 写入一条数据（内部缓冲，满 m_batchSize 条自动提交事务）
    // 析构时或手动 flush() 可强制提交未满的缓冲
    void insert(const DataPoint &point);

    // 强制提交缓冲区中待写入的数据
    void flush();

    // 按时间范围查询历史数据
    // tBegin/tEnd: 毫秒时间戳（包含边界）
    // limit:      最大返回条数（默认 10000）
    QVector<DataPoint> query(uint64_t tBegin, uint64_t tEnd, int limit = 10000);

    // 清理超过 retentionDays 天的旧数据，返回删除条数
    int cleanup(int retentionDays = 7);

    // 数据库中的总行数（调试用）
    int rowCount() const;
    uint64_t minTimestamp() const;
    uint64_t maxTimestamp() const;
private:
    // 事务批量提交缓冲区
    void commitBatch();

    // channels 序列化/反序列化
    static QByteArray serializeChannels(const QVector<double> &channels);
    static QVector<double> deserializeChannels(const QByteArray &data);

    QSqlDatabase m_db;
    QVector<DataPoint> m_pending;   // 待写入缓冲
    int m_batchSize = 100;          // 批量事务阈值
    QString m_connectionName;       // 数据库连接名称（用于 QSqlDatabase 管理）
};

#endif // DATABASEMANAGER_H
