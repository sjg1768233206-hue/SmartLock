#include "lockcontroller.h"
#include "logger.h"
#include <QDebug>
#include <QFile>
#include <QThread>
#include <QDir>
#include <stdlib.h>
#include <unistd.h>

#define PWM_CHIP "/sys/class/pwm/pwmchip3"
#define PWM_CHANNEL 0
#define PWM_PERIOD_NS 20000000
#define PWM_MIN_PULSE_NS 500000
#define PWM_MAX_PULSE_NS 2500000

LockController::LockController()
    : m_locked(true)
    , m_moving(false)
{
    LOG_INFO("LockController initializing...");

    // 修复权限
    system("sudo chmod 755 /sys/class/pwm/pwmchip3/ 2>/dev/null");
    system("sudo chmod 755 /sys/class/pwm/pwmchip3/pwm0/ 2>/dev/null");
    system("sudo chmod 666 /sys/class/pwm/pwmchip3/pwm0/* 2>/dev/null");

    // 初始化 PWM
    if (!initGPIO()) {
        LOG_ERROR("Failed to initialize PWM");
        return;
    }

    // 默认上锁
    lock();

    LOG_INFO("LockController ready");
}

LockController::~LockController()
{
    LOG_INFO("LockController destructor called");
}

LockController& LockController::instance()
{
    static LockController instance;
    return instance;
}

bool LockController::initGPIO()
{
    LOG_DEBUG("Initializing PWM...");

    QString pwm0Path = QString(PWM_CHIP "/pwm%1").arg(PWM_CHANNEL);
    if (!QDir(pwm0Path).exists()) {
        LOG_DEBUG("Exporting PWM channel...");
        system("sudo sh -c 'echo 0 > /sys/class/pwm/pwmchip3/export' 2>/dev/null");
        QThread::msleep(100);
        system("sudo chmod 755 /sys/class/pwm/pwmchip3/pwm0/ 2>/dev/null");
        system("sudo chmod 666 /sys/class/pwm/pwmchip3/pwm0/* 2>/dev/null");
    }

    system("sudo sh -c 'echo 20000000 > /sys/class/pwm/pwmchip3/pwm0/period' 2>/dev/null");
    system("sudo sh -c 'echo 1 > /sys/class/pwm/pwmchip3/pwm0/enable' 2>/dev/null");

    LOG_INFO("PWM initialized");
    return true;
}

void LockController::setServoAngle(int angle)
{
    LOG_DEBUG(QString("setServoAngle called, angle: %1").arg(angle));

    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;

    int dutyNs = PWM_MIN_PULSE_NS + (angle * (PWM_MAX_PULSE_NS - PWM_MIN_PULSE_NS) / 180);
    LOG_DEBUG(QString("Duty_ns: %1").arg(dutyNs));

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "sudo sh -c 'echo %d > /sys/class/pwm/pwmchip3/pwm0/duty_cycle'", dutyNs);

    int ret = system(cmd);
    if (ret != 0) {
        LOG_WARNING(QString("system command returned: %1").arg(ret));
    }

    usleep(300000);
}

void LockController::lock()
{
    LOG_INFO("lock() called");

    if (m_locked) {
        LOG_DEBUG("Door already locked");
        return;
    }

    LOG_INFO("Locking door...");
    m_moving = true;
    setServoAngle(0);
    m_moving = false;
    m_locked = true;
    emit lockStateChanged(true);
    LOG_INFO("Door locked");
}

void LockController::unlock()
{
    LOG_INFO("unlock() called");

    if (!m_locked) {
        LOG_DEBUG("Door already unlocked");
        return;
    }

    LOG_INFO("Unlocking door...");
    m_moving = true;
    setServoAngle(90);
    m_moving = false;
    m_locked = false;
    emit lockStateChanged(false);
    LOG_INFO("Door unlocked");
}