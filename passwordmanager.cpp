#include "passwordmanager.h"
#include "logger.h"
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QCryptographicHash>
#include <QDebug>

PasswordManager& PasswordManager::instance()
{
    static PasswordManager instance;
    return instance;
}

QString PasswordManager::passwordFilePath()
{
    QDir dir(QDir::homePath());
    dir.mkpath(".smartlock");
    return dir.absolutePath() + "/.smartlock/admin.hash";
}

QString PasswordManager::hashPassword(const QString &pwd)
{
    QByteArray hash = QCryptographicHash::hash(pwd.toUtf8(), QCryptographicHash::Sha256);
    return hash.toHex();
}

bool PasswordManager::verifyUserPassword(const QString &input)
{
    // 开门密码：可以是固定的临时密码，或与管理员密码相同
    // 简化版：用户密码 == 管理员密码
    return verifyAdminPassword(input);
}

bool PasswordManager::verifyAdminPassword(const QString &input)
{
    QString storedHash = loadPassword();
    if (storedHash.isEmpty()) {
        LOG_WARNING("No stored password found");
        return false;
    }
    bool result = hashPassword(input) == storedHash;
    if (!result) {
        LOG_WARNING("Admin password verification failed");
    }
    return result;
}

bool PasswordManager::changePassword(const QString &oldPwd, const QString &newPwd)
{
    // 必须验证旧密码正确（管理员密码）
    if (!verifyAdminPassword(oldPwd)) {
        LOG_WARNING("Password change failed: incorrect old password");
        return false;
    }
    bool result = savePassword(hashPassword(newPwd));
    if (result) {
        LOG_INFO("Password changed successfully");
    } else {
        LOG_ERROR("Failed to save new password");
    }
    return result;
}

bool PasswordManager::setAdminPassword(const QString &newPwd)
{
    bool result = savePassword(hashPassword(newPwd));
    if (result) {
        LOG_INFO("Admin password set successfully");
    } else {
        LOG_ERROR("Failed to set admin password");
    }
    return result;
}

bool PasswordManager::savePassword(const QString &hashedPwd)
{
    QFile file(passwordFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LOG_ERROR(QString("Failed to save password to: %1").arg(passwordFilePath()));
        return false;
    }
    QTextStream out(&file);
    out << hashedPwd;
    file.close();
    LOG_DEBUG("Password saved successfully");
    return true;
}

QString PasswordManager::loadPassword()
{
    QFile file(passwordFilePath());
    if (!file.exists()) {
        LOG_DEBUG("Password file does not exist");
        return QString();
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_ERROR(QString("Failed to open password file: %1").arg(passwordFilePath()));
        return QString();
    }
    QTextStream in(&file);
    QString hash = in.readLine();
    file.close();
    LOG_DEBUG("Password loaded successfully");
    return hash;
}

bool PasswordManager::isInitialized()
{
    QFile file(passwordFilePath());
    bool exists = file.exists();
    if (!exists) {
        LOG_DEBUG("Password not initialized yet");
    }
    return exists;
}