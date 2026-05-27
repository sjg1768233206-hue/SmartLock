#ifndef PASSWORDMANAGER_H
#define PASSWORDMANAGER_H

#include <QString>

class PasswordManager
{
public:
    static PasswordManager& instance();

    // 验证用户密码（用于开门）
    bool verifyUserPassword(const QString &input);

    // 验证管理员密码（用于修改密码、录人脸等敏感操作）
    bool verifyAdminPassword(const QString &input);

    // 修改密码（需要提供旧密码，旧密码可以是用户密码或管理员密码）
    bool changePassword(const QString &oldPwd, const QString &newPwd);

    // 设置初始管理员密码（首次使用）
    bool setAdminPassword(const QString &newPwd);

    // 是否已初始化
    bool isInitialized();

private:
    PasswordManager() = default;
    QString passwordFilePath();
    QString hashPassword(const QString &pwd);
    bool savePassword(const QString &hashedPwd);
    QString loadPassword();
};

#endif