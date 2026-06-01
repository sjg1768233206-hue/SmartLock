#ifndef FEATUREDATABASE_H
#define FEATUREDATABASE_H

#include <QString>
#include <QVector>
#include <QMap>
#include <QFile>
#include <QDataStream>
#include <vector>

struct UserFeature {
    QString name;
    std::vector<float> features;
};

class FeatureDatabase
{
public:
    static FeatureDatabase& instance();

    bool addUser(const QString& name, const std::vector<float>& features);
    bool removeUser(const QString& name);
    QString recognize(const std::vector<float>& features, float& bestScore);
    bool save(const QString& path);
    bool load();
    void setThreshold(float threshold) { m_threshold = threshold; }
    float getThreshold() const { return m_threshold; }
    QStringList getAllUserNames() const;
private:
    FeatureDatabase() = default;

    QVector<UserFeature> m_users;
    float m_threshold = 0.20f;  // 相似度阈值
};

#endif // FEATUREDATABASE_H
