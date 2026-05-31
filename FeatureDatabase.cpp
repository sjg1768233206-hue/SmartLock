#include "FeatureDatabase.h"
#include <QDir>
#include <QDebug>
#include <QCoreApplication>

// 获取数据库文件路径
static QString getDatabasePath()
{
    // 使用绝对路径
    return "/opt/smartlock/bin/data/features.db";
}

FeatureDatabase& FeatureDatabase::instance()
{
    static FeatureDatabase db;
    return db;
}

bool FeatureDatabase::addUser(const QString& name, const std::vector<float>& features)
{
    // 检查是否已存在，存在则更新
    for (int i = 0; i < m_users.size(); i++) {
        if (m_users[i].name == name) {
            m_users[i].features = features;
            qDebug() << "Updated user:" << name;
            return true;
        }
    }

    UserFeature user;
    user.name = name;
    user.features = features;
    m_users.push_back(user);
    qDebug() << "Added user:" << name << "features size:" << features.size();
    return true;
}

bool FeatureDatabase::removeUser(const QString& name)
{
    for (int i = 0; i < m_users.size(); i++) {
        if (m_users[i].name == name) {
            m_users.remove(i);
            qDebug() << "Removed user:" << name;
            return true;
        }
    }
    return false;
}

QString FeatureDatabase::recognize(const std::vector<float>& features, float& bestScore)
{
    bestScore = -1.0f;
    QString bestName;

    if (m_users.isEmpty()) {
        qDebug() << "No users in database";
        return QString();
    }

    if (features.empty()) {
        qDebug() << "Features is empty";
        return QString();
    }

    for (const auto& user : m_users) {
        // 检查特征大小是否匹配
        if (user.features.size() != features.size()) {
            qDebug() << "Feature size mismatch:" << user.features.size() << "vs" << features.size();
            continue;
        }

        float dot = 0.0f;
        for (size_t i = 0; i < features.size(); i++) {
            dot += features[i] * user.features[i];
        }

        qDebug() << "Compare with" << user.name << "score:" << dot;

        if (dot > bestScore) {
            bestScore = dot;
            bestName = user.name;
        }
    }

    qDebug() << "Best match:" << bestName << "score:" << bestScore << "threshold:" << m_threshold;

    // 降低阈值，使用余弦相似度，范围是 -1 到 1，通常 0.5 以上算匹配
    if (bestScore < m_threshold) {
        return QString();
    }

    return bestName;
}

bool FeatureDatabase::save(const QString& path)
{
    QString fullPath = getDatabasePath();

    // 确保目录存在
    QDir dir("/opt/smartlock/bin/data");
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open" << fullPath << "for writing";
        return false;
    }

    QDataStream out(&file);
    out << (qint32)m_users.size();

    for (const auto& user : m_users) {
        out << user.name;
        out << (qint32)user.features.size();
        for (float v : user.features) {
            out << v;
        }
    }

    file.close();
    qDebug() << "Saved" << m_users.size() << "users to" << fullPath;
    return true;
}

bool FeatureDatabase::load()
{
    QString fullPath = getDatabasePath();
    QFile file(fullPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "No database file at" << fullPath;
        return false;
    }

    QDataStream in(&file);
    qint32 count;
    in >> count;

    m_users.clear();
    for (int i = 0; i < count; i++) {
        QString name;
        qint32 featSize;
        in >> name >> featSize;

        if (featSize <= 0 || featSize > 1000) {
            qWarning() << "Invalid feature size:" << featSize;
            continue;
        }

        UserFeature user;
        user.name = name;
        user.features.resize(featSize);
        for (int j = 0; j < featSize; j++) {
            in >> user.features[j];
        }
        m_users.push_back(user);
    }

    file.close();
    qDebug() << "Loaded" << m_users.size() << "users from" << fullPath;
    return true;
}
QStringList FeatureDatabase::getAllUserNames() const
{
    QStringList list;
    for (const auto& user : m_users) {
        list << user.name;
    }
    return list;
}
