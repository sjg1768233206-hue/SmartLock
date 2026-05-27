#ifndef LOCKCONTROLLER_H
#define LOCKCONTROLLER_H

#include <QObject>
#include <QMutex>

class LockController : public QObject
{
    Q_OBJECT

public:
    static LockController& instance();
    ~LockController();

    void lock();
    void unlock();
    void setServoAngle(int angle);
    bool isLocked() const { return m_locked; }
    bool isMoving() const { return m_moving; }

signals:
    void lockStateChanged(bool locked);

private:
    LockController();
    LockController(const LockController&) = delete;
    LockController& operator=(const LockController&) = delete;

    bool initGPIO();

private:
    bool m_locked;
    bool m_moving;
};

#endif // LOCKCONTROLLER_H