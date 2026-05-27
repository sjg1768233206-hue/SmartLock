#include "passwordwidget.h"
#include "passwordmanager.h"
#include "lockcontroller.h"
#include "databasemanager.h"
#include "WebSocketServer.h"
#include "logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QDir>
#include <QDateTime>

PasswordWidget::PasswordWidget(QWidget *parent)
    : QWidget(parent)
    , m_currentMode(ModeInput)
    , m_passwordErrorCount(0)
{
    setupUI();

    m_errorResetTimer = new QTimer(this);
    m_errorResetTimer->setSingleShot(true);
    connect(m_errorResetTimer, &QTimer::timeout, [this]() {
        m_passwordErrorCount = 0;
        LOG_DEBUG("Password error count reset");
    });
}

PasswordWidget::~PasswordWidget()
{
    if (m_errorResetTimer) {
        m_errorResetTimer->stop();
        delete m_errorResetTimer;
    }
}

void PasswordWidget::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(40, 60, 40, 60);

    // 标题
    m_titleLabel = new QLabel(this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet("font-size: 26px; font-weight: bold; color: #2c3e50;");

    // 密码输入框
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText("请输入密码");
    m_passwordEdit->setStyleSheet("font-size: 16px; padding: 12px; border-radius: 6px; border: 1px solid #bdc3c7;");

    // 新密码输入框
    m_newPasswordEdit = new QLineEdit(this);
    m_newPasswordEdit->setEchoMode(QLineEdit::Password);
    m_newPasswordEdit->setPlaceholderText("请输入新密码");
    m_newPasswordEdit->setStyleSheet("font-size: 16px; padding: 12px; border-radius: 6px; border: 1px solid #bdc3c7;");
    m_newPasswordEdit->hide();

    // 确认密码输入框
    m_confirmPasswordEdit = new QLineEdit(this);
    m_confirmPasswordEdit->setEchoMode(QLineEdit::Password);
    m_confirmPasswordEdit->setPlaceholderText("请再次输入新密码");
    m_confirmPasswordEdit->setStyleSheet("font-size: 16px; padding: 12px; border-radius: 6px; border: 1px solid #bdc3c7;");
    m_confirmPasswordEdit->hide();

    // 按钮
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(20);

    m_confirmBtn = new QPushButton("确认", this);
    m_confirmBtn->setStyleSheet("QPushButton { background-color: #27ae60; color: white; border: none; padding: 12px 30px; border-radius: 6px; font-size: 16px; }"
                                "QPushButton:hover { background-color: #2ecc71; }");

    m_cancelBtn = new QPushButton("取消", this);
    m_cancelBtn->setStyleSheet("QPushButton { background-color: #95a5a6; color: white; border: none; padding: 12px 30px; border-radius: 6px; font-size: 16px; }"
                               "QPushButton:hover { background-color: #7f8c8d; }");

    btnLayout->addStretch();
    btnLayout->addWidget(m_confirmBtn);
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addStretch();

    // 提示信息
    m_messageLabel = new QLabel(this);
    m_messageLabel->setAlignment(Qt::AlignCenter);
    m_messageLabel->setStyleSheet("font-size: 14px;");

    mainLayout->addWidget(m_titleLabel);
    mainLayout->addWidget(m_passwordEdit);
    mainLayout->addWidget(m_newPasswordEdit);
    mainLayout->addWidget(m_confirmPasswordEdit);
    mainLayout->addLayout(btnLayout);
    mainLayout->addWidget(m_messageLabel);
    mainLayout->addStretch();

    connect(m_confirmBtn, &QPushButton::clicked, this, &PasswordWidget::onConfirmClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &PasswordWidget::onCancelClicked);

    setMode(ModeInput);
}

void PasswordWidget::setMode(Mode mode)
{
    m_currentMode = mode;
    updateUI();
}

void PasswordWidget::updateUI()
{
    m_passwordEdit->clear();
    m_newPasswordEdit->clear();
    m_confirmPasswordEdit->clear();
    m_messageLabel->clear();

    switch (m_currentMode) {
    case ModeInput:
        m_titleLabel->setText("🔐 请输入密码");
        m_passwordEdit->show();
        m_newPasswordEdit->hide();
        m_confirmPasswordEdit->hide();
        m_passwordEdit->setPlaceholderText("请输入密码");
        break;

    case ModeChange:
        m_titleLabel->setText("🔑 修改管理员密码");
        m_passwordEdit->show();
        m_newPasswordEdit->show();
        m_confirmPasswordEdit->show();
        m_passwordEdit->setPlaceholderText("请输入原管理员密码");
        m_newPasswordEdit->setPlaceholderText("请输入新密码");
        m_confirmPasswordEdit->setPlaceholderText("请再次输入新密码");
        break;

    case ModeSet:
        m_titleLabel->setText("⚙️ 首次使用，请设置管理员密码");
        m_passwordEdit->hide();
        m_newPasswordEdit->show();
        m_confirmPasswordEdit->show();
        m_newPasswordEdit->setPlaceholderText("请输入新密码");
        m_confirmPasswordEdit->setPlaceholderText("请再次输入新密码");
        break;
    }
}

void PasswordWidget::onConfirmClicked()
{
    QString oldPwd = m_passwordEdit->text();
    QString newPwd = m_newPasswordEdit->text();
    QString confirmPwd = m_confirmPasswordEdit->text();

    switch (m_currentMode) {
    case ModeInput:
        if (PasswordManager::instance().verifyUserPassword(oldPwd)) {
            // 开门成功，重置错误计数
            m_passwordErrorCount = 0;
            m_errorResetTimer->stop();

            LockController::instance().unlock();
            DatabaseManager::instance().addLog("密码开门", "密码", true);
            WebSocketServer::instance().sendDoorStatus("密码开门", "密码", true);

            LOG_INFO("Password door unlock success");

            m_messageLabel->setStyleSheet("color: #27ae60; font-size: 14px;");
            m_messageLabel->setText("✓ 验证成功，门已开");
            m_passwordEdit->clear();
        } else {
            // 密码错误，增加计数
            m_passwordErrorCount++;
            m_errorResetTimer->start(30000);

            LOG_WARNING(QString("Password error count: %1").arg(m_passwordErrorCount));

            DatabaseManager::instance().addLog("", "密码", false);
            WebSocketServer::instance().sendDoorStatus("未知", "密码", false);

            // 连续3次密码错误 → 报警
            if (m_passwordErrorCount >= 3) {
                m_passwordErrorCount = 0;
                m_errorResetTimer->stop();

                LOG_WARNING("Alert triggered: multiple password errors");

                QString photoPath = QString("data/alerts/password_%1.jpg")
                                        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
                QDir().mkpath("data/alerts");

                emit passwordAlert(photoPath);
                LOG_INFO(QString("Password alert emitted, photo: %1").arg(photoPath));
            }

            m_messageLabel->setStyleSheet("color: #e74c3c; font-size: 14px;");
            m_messageLabel->setText("✗ 密码错误，请重试");
            m_passwordEdit->clear();
        }
        break;

    case ModeChange:
        if (newPwd.isEmpty() || newPwd != confirmPwd) {
            LOG_WARNING("Password change: new password mismatch");
            m_messageLabel->setStyleSheet("color: #e74c3c; font-size: 14px;");
            m_messageLabel->setText("✗ 新密码输入不一致");
            return;
        }

        if (newPwd.length() < 6) {
            LOG_WARNING("Password change: password too short");
            m_messageLabel->setStyleSheet("color: #e74c3c; font-size: 14px;");
            m_messageLabel->setText("✗ 密码长度不能少于6位");
            return;
        }

        if (PasswordManager::instance().changePassword(oldPwd, newPwd)) {
            DatabaseManager::instance().addLog("管理员", "修改密码", true);
            LOG_INFO("Password changed successfully");
            m_messageLabel->setStyleSheet("color: #27ae60; font-size: 14px;");
            m_messageLabel->setText("✓ 密码修改成功");
            m_passwordEdit->clear();
            m_newPasswordEdit->clear();
            m_confirmPasswordEdit->clear();
        } else {
            DatabaseManager::instance().addLog("", "修改密码", false);
            LOG_WARNING("Password change failed: incorrect old password");
            m_messageLabel->setStyleSheet("color: #e74c3c; font-size: 14px;");
            m_messageLabel->setText("✗ 原密码错误");
            m_passwordEdit->clear();
            m_newPasswordEdit->clear();
            m_confirmPasswordEdit->clear();
        }
        break;

    case ModeSet:
        if (newPwd.isEmpty() || newPwd != confirmPwd) {
            LOG_WARNING("Password set: new password mismatch");
            m_messageLabel->setStyleSheet("color: #e74c3c; font-size: 14px;");
            m_messageLabel->setText("✗ 密码输入不一致");
            return;
        }

        if (newPwd.length() < 6) {
            LOG_WARNING("Password set: password too short");
            m_messageLabel->setStyleSheet("color: #e74c3c; font-size: 14px;");
            m_messageLabel->setText("✗ 密码长度不能少于6位");
            return;
        }

        if (PasswordManager::instance().setAdminPassword(newPwd)) {
            DatabaseManager::instance().addLog("管理员", "设置密码", true);
            LOG_INFO("Admin password set successfully");
            m_messageLabel->setStyleSheet("color: #27ae60; font-size: 14px;");
            m_messageLabel->setText("✓ 管理员密码设置成功");
            setMode(ModeInput);
        } else {
            LOG_ERROR("Failed to set admin password");
            m_messageLabel->setStyleSheet("color: #e74c3c; font-size: 14px;");
            m_messageLabel->setText("✗ 密码设置失败");
        }
        break;
    }
}

void PasswordWidget::onCancelClicked()
{
    m_passwordEdit->clear();
    m_newPasswordEdit->clear();
    m_confirmPasswordEdit->clear();
    m_messageLabel->clear();
}