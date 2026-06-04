//==============================================================================
// DatabaseManager 实现
//
// SQLite 写入性能对比（参考）：
//   逐条 INSERT + 逐条 COMMIT    →  ~10 条/秒
//   逐条 INSERT + 批量事务 COMMIT →  ~500 条/秒  （本实现采用）
//   batchSize 太小 → 事务开销大；太大 → 崩溃丢数据多
//   100 条/批是经验值，兼顾性能和安全
//==============================================================================

#include "DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QIODevice>

//==============================================================================
// channels 序列化 / 反序列化
// QDataStream 原生支持 QVector<double>，无需手动循环
// setVersion(Qt_6_0) 锁定格式，跨平台/跨版本兼容
//==============================================================================

QByteArray DatabaseManager::serializeChannels(const QVector<double> &channels)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << channels;
    return data;
}

QVector<double> DatabaseManager::deserializeChannels(const QByteArray &data)
{
    if (data.isEmpty())
        return {};

    QVector<double> channels;
    QDataStream stream(data);
    stream.setVersion(QDataStream::Qt_6_0);
    stream >> channels;
    return channels;
}

//==============================================================================
// 构造 / 析构
//==============================================================================

DatabaseManager::DatabaseManager(QObject *parent)
    : QObject(parent)
{
    // 用静态计数器生成唯一连接名，防止多次实例化时冲突
    static int counter = 0;
    m_connectionName = QString("pulseqt_db_%1").arg(++counter);
}

DatabaseManager::~DatabaseManager()
{
    flush();   // 析构前强制提交未满的缓冲

    if (m_db.isOpen())
        m_db.close();

    // 移除连接，避免 QSqlDatabase 警告
    QSqlDatabase::removeDatabase(m_connectionName);
}

//==============================================================================
// init — 打开数据库 + 建表 + WAL + 索引
//==============================================================================

bool DatabaseManager::init(const QString &dbPath)
{
    // 1. 创建 SQLite 连接（命名连接，防止多实例冲突）
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qCritical() << "DatabaseManager: failed to open" << dbPath
                     << "-" << m_db.lastError().text();
        return false;
    }

    QSqlQuery query(m_db);

    // 2. 开启 WAL 模式
    //    WAL (Write-Ahead Logging): 写操作写到独立的 WAL 文件，
    //    不阻塞同时进行的读操作。适合采集场景——一边写一边历史查询。
    if (!query.exec("PRAGMA journal_mode=WAL")) {
        qWarning() << "DatabaseManager: WAL mode failed, using default journal";
    }

    // 确认 WAL 是否生效
    query.exec("PRAGMA journal_mode");
    if (query.next())
        qInfo() << "DatabaseManager: journal_mode =" << query.value(0).toString();

    // 3. 建表
    //    timestamp 用 INTEGER 存毫秒时间戳（uint64_t 直接存）
    //    channels 用 BLOB 存序列化后的 QVector<double>
    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS data_points ("
            "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  timestamp INTEGER NOT NULL,"
            "  channels  BLOB"
            ")")) {
        qCritical() << "DatabaseManager: CREATE TABLE failed -"
                     << query.lastError().text();
        return false;
    }

    // 4. 建时间索引
    //    query() 和 cleanup() 都按 timestamp 过滤，没索引会全表扫描
    query.exec(
        "CREATE INDEX IF NOT EXISTS idx_timestamp ON data_points(timestamp)");

    qInfo() << "DatabaseManager: initialized" << dbPath;
    return true;
}

//==============================================================================
// insert — 单条写入（缓冲 100 条批量提交）
//==============================================================================

void DatabaseManager::insert(const DataPoint &point)
{
    m_pending.append(point);

    if (m_pending.size() >= m_batchSize)
        commitBatch();
}

//==============================================================================
// flush — 强制提交缓冲区剩余数据
//==============================================================================

void DatabaseManager::flush()
{
    if (!m_pending.isEmpty())
        commitBatch();
}

//==============================================================================
// commitBatch — 事务批量写入
//
// 为什么事务包裹？
//   SQLite 默认每条 INSERT 都是一个隐式事务（写磁盘 → fsync → 下一条）。
//   显式 BEGIN→INSERT×100→COMMIT 把 100 次 fsync 合并成 1 次，快 10-50 倍。
//==============================================================================

void DatabaseManager::commitBatch()
{
    if (m_pending.isEmpty())
        return;

    QSqlQuery query(m_db);

    // 开启显式事务
    if (!m_db.transaction()) {
        qWarning() << "DatabaseManager: transaction failed -"
                    << m_db.lastError().text();
        return;
    }

    // 预编译 INSERT 语句（? 是占位符）
    query.prepare("INSERT INTO data_points (timestamp, channels) VALUES (?, ?)");

    for (const DataPoint &dp : m_pending) {
        query.addBindValue(static_cast<qulonglong>(dp.timestamp));
        query.addBindValue(serializeChannels(dp.channels));

        if (!query.exec()) {
            qWarning() << "DatabaseManager: INSERT failed -"
                        << query.lastError().text();
            m_db.rollback();
            m_pending.clear();
            return;
        }
    }

    // 提交事务
    if (!m_db.commit()) {
        qWarning() << "DatabaseManager: commit failed -"
                    << m_db.lastError().text();
        m_db.rollback();
    }

    qInfo() << "DatabaseManager: committed" << m_pending.size() << "records";
    m_pending.clear();
}

//==============================================================================
// query — 时间范围查询
//==============================================================================

QVector<DataPoint> DatabaseManager::query(uint64_t tBegin, uint64_t tEnd,
                                           int limit)
{
    QVector<DataPoint> results;
    QSqlQuery query(m_db);

    query.prepare(
        "SELECT timestamp, channels FROM data_points "
        "WHERE timestamp BETWEEN ? AND ? "
        "ORDER BY timestamp ASC "
        "LIMIT ?");

    query.addBindValue(static_cast<qulonglong>(tBegin));
    query.addBindValue(static_cast<qulonglong>(tEnd));
    query.addBindValue(limit);

    if (!query.exec()) {
        qWarning() << "DatabaseManager: query failed -"
                    << query.lastError().text();
        return results;
    }

    // 遍历结果集，反序列化 channels BLOB
    while (query.next()) {
        DataPoint dp;
        dp.timestamp = query.value(0).toULongLong();
        dp.channels  = deserializeChannels(query.value(1).toByteArray());
        results.append(dp);
    }

    return results;
}

//==============================================================================
// cleanup — 自动清理旧数据
//==============================================================================

int DatabaseManager::cleanup(int retentionDays)
{
    QSqlQuery query(m_db);

    // 计算截止时间：当前时间 - retentionDays 天
    uint64_t cutoff = QDateTime::currentMSecsSinceEpoch()
                      - static_cast<uint64_t>(retentionDays) * 86400000ULL;

    query.prepare("DELETE FROM data_points WHERE timestamp < ?");
    query.addBindValue(static_cast<qulonglong>(cutoff));

    if (!query.exec()) {
        qWarning() << "DatabaseManager: cleanup failed -"
                    << query.lastError().text();
        return 0;
    }

    int deleted = query.numRowsAffected();
    if (deleted > 0)
        qInfo() << "DatabaseManager: cleaned up" << deleted << "old records";

    return deleted;
}

//==============================================================================
// rowCount — 总行数（调试用）
//==============================================================================

int DatabaseManager::rowCount() const
{
    QSqlQuery query(m_db);
    query.exec("SELECT COUNT(*) FROM data_points");
    if (query.next())
        return query.value(0).toInt();
    return 0;
}
uint64_t DatabaseManager::minTimestamp() const
{
    QSqlQuery query(m_db);
    query.exec("SELECT MIN(timestamp) FROM data_points");
    if (query.next() && !query.value(0).isNull())
        return query.value(0).toULongLong();
    return 0;
}

uint64_t DatabaseManager::maxTimestamp() const
{
    QSqlQuery query(m_db);
    query.exec("SELECT MAX(timestamp) FROM data_points");
    if (query.next() && !query.value(0).isNull())
        return query.value(0).toULongLong();
    return 0;
}
