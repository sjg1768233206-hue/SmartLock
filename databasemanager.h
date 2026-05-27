// databasemanager.h
#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlQueryModel>
#include <QSqlTableModel>
#include <QDateTime>
#include <QDebug>

class DatabaseManager
{
public:
    static DatabaseManager& instance();

    bool init();
    void close();

    // ========== 开门记录 ==========
    bool addLog(QString userName, QString method, bool success);
    QSqlQueryModel* getRecentLogs(int limit = 100);
    QSqlQueryModel* getTodayLogs();

    // ========== 统计 ==========
    int getTodayCount();
    int getUserCount(QString userName, int days = 7);

    // 报警相关
    bool addAlert(QString type, QString detail, int count, QString photoPath);
    QSqlQueryModel* getAlerts(int limit = 50);
    bool resolveAlert(int alertId);

private:
    DatabaseManager() = default;
    bool createTables();

    QSqlDatabase m_db;
};

#endif