// databasemanager.cpp
#include "databasemanager.h"
#include "logger.h"
#include <QDir>
#include <QSqlError>

DatabaseManager& DatabaseManager::instance()
{
    static DatabaseManager instance;
    return instance;
}

bool DatabaseManager::init()
{
    // 创建数据目录
    QDir().mkpath("data");

    // 连接数据库
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName("data/smartlock.db");

    if (!m_db.open()) {
        LOG_ERROR(QString("Database open failed: %1").arg(m_db.lastError().text()));
        return false;
    }

    LOG_INFO("Database initialized successfully");
    return createTables();
}

void DatabaseManager::close()
{
    if (m_db.isOpen()) {
        m_db.close();
        LOG_INFO("Database closed");
    }
}

bool DatabaseManager::createTables()
{
    QSqlQuery query;

    // 开门记录表
    QString createLogs = R"(
        CREATE TABLE IF NOT EXISTS access_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_name TEXT,
            method TEXT,
            result TEXT,
            time TEXT,
            photo_path TEXT
        )
    )";

    if (!query.exec(createLogs)) {
        LOG_ERROR(QString("Create access_logs table failed: %1").arg(query.lastError().text()));
        return false;
    }

    // 报警记录表
    QString createAlerts = R"(
        CREATE TABLE IF NOT EXISTS alerts (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            type TEXT,
            detail TEXT,
            count INTEGER,
            photo_path TEXT,
            time TEXT,
            resolved INTEGER DEFAULT 0
        )
    )";

    if (!query.exec(createAlerts)) {
        LOG_ERROR(QString("Create alerts table failed: %1").arg(query.lastError().text()));
        return false;
    }

    // 创建索引
    query.exec("CREATE INDEX IF NOT EXISTS idx_time ON access_logs(time)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_user ON access_logs(user_name)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_alert_time ON alerts(time)");

    LOG_INFO("Database tables created successfully");
    return true;
}

bool DatabaseManager::addLog(QString userName, QString method, bool success)
{
    LOG_DEBUG(QString("addLog called - user: %1, method: %2, success: %3").arg(userName).arg(method).arg(success));

    QSqlQuery query;
    query.prepare("INSERT INTO access_logs (user_name, method, result, time) "
                  "VALUES (?, ?, ?, ?)");
    query.addBindValue(userName);
    query.addBindValue(method);
    query.addBindValue(success ? "✓ 成功" : "✗ 失败");
    query.addBindValue(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));

    if (!query.exec()) {
        LOG_ERROR(QString("Insert log failed: %1").arg(query.lastError().text()));
        return false;
    }

    LOG_DEBUG("Insert log success");
    return true;
}

QSqlQueryModel* DatabaseManager::getRecentLogs(int limit)
{
    QSqlQueryModel *model = new QSqlQueryModel();
    QSqlQuery query;
    query.prepare("SELECT user_name, method, result, time FROM access_logs "
                  "ORDER BY time DESC LIMIT ?");
    query.addBindValue(limit);
    query.exec();
    model->setQuery(query);

    model->setHeaderData(0, Qt::Horizontal, "用户");
    model->setHeaderData(1, Qt::Horizontal, "方式");
    model->setHeaderData(2, Qt::Horizontal, "结果");
    model->setHeaderData(3, Qt::Horizontal, "时间");

    return model;
}

QSqlQueryModel* DatabaseManager::getTodayLogs()
{
    QSqlQueryModel *model = new QSqlQueryModel();
    QSqlQuery query;
    query.exec("SELECT user_name, method, result, time FROM access_logs "
               "WHERE date(time) = date('now') "
               "ORDER BY time DESC");
    model->setQuery(query);
    return model;
}

int DatabaseManager::getTodayCount()
{
    QSqlQuery query;
    query.exec("SELECT COUNT(*) FROM access_logs "
               "WHERE date(time) = date('now') AND result='✓ 成功'");
    query.next();
    return query.value(0).toInt();
}

int DatabaseManager::getUserCount(QString userName, int days)
{
    QSqlQuery query;
    query.prepare("SELECT COUNT(*) FROM access_logs "
                  "WHERE user_name = ? AND time > datetime('now', ?) "
                  "AND result='✓ 成功'");
    query.addBindValue(userName);
    query.addBindValue(QString("-%1 days").arg(days));
    query.exec();
    query.next();
    return query.value(0).toInt();
}

bool DatabaseManager::addAlert(QString type, QString detail, int count, QString photoPath)
{
    LOG_WARNING(QString("Alert added - type: %1, detail: %2, count: %3").arg(type).arg(detail).arg(count));

    QSqlQuery query;
    query.prepare(R"(
        INSERT INTO alerts (type, detail, count, photo_path, time, resolved)
        VALUES (?, ?, ?, ?, ?, 0)
    )");
    query.addBindValue(type);
    query.addBindValue(detail);
    query.addBindValue(count);
    query.addBindValue(photoPath);
    query.addBindValue(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));

    if (!query.exec()) {
        LOG_ERROR(QString("Add alert failed: %1").arg(query.lastError().text()));
        return false;
    }

    LOG_INFO(QString("Alert added to database: %1").arg(type));
    return true;
}

QSqlQueryModel* DatabaseManager::getAlerts(int limit)
{
    QSqlQueryModel *model = new QSqlQueryModel();
    QSqlQuery query;
    query.prepare(R"(
        SELECT id, type, detail, count, photo_path, time,
               CASE WHEN resolved THEN '已处理' ELSE '未处理' END as status
        FROM alerts
        ORDER BY time DESC LIMIT ?
    )");
    query.addBindValue(limit);
    query.exec();
    model->setQuery(query);

    model->setHeaderData(0, Qt::Horizontal, "ID");
    model->setHeaderData(1, Qt::Horizontal, "类型");
    model->setHeaderData(2, Qt::Horizontal, "详情");
    model->setHeaderData(3, Qt::Horizontal, "次数");
    model->setHeaderData(4, Qt::Horizontal, "照片");
    model->setHeaderData(5, Qt::Horizontal, "时间");
    model->setHeaderData(6, Qt::Horizontal, "状态");

    return model;
}

bool DatabaseManager::resolveAlert(int alertId)
{
    QSqlQuery query;
    query.prepare("UPDATE alerts SET resolved = 1 WHERE id = ?");
    query.addBindValue(alertId);

    if (query.exec()) {
        LOG_INFO(QString("Alert resolved: ID=%1").arg(alertId));
        return true;
    } else {
        LOG_ERROR(QString("Resolve alert failed: %1").arg(query.lastError().text()));
        return false;
    }
}
