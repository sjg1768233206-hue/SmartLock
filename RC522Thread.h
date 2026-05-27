#ifndef RC522THREAD_H
#define RC522THREAD_H

#include <QThread>
#include <QMutex>
#include <QString>

class RC522Thread : public QThread
{
    Q_OBJECT

public:
    explicit RC522Thread(QObject *parent = nullptr);
    ~RC522Thread();

    void stop();
    bool isRunning() const { return m_running; }

signals:
    void cardDetected(const QString &uid);
    void error(const QString &msg);

protected:
    void run() override;

private:
    volatile bool m_running;
    QMutex m_mutex;

    // RC522 SPI 操作函数
    int spi_fd;
    bool initSPI();
    void closeSPI();
    uint8_t readReg(uint8_t reg);
    void writeReg(uint8_t reg, uint8_t value);
    void setBitMask(uint8_t reg, uint8_t mask);
    void clearBitMask(uint8_t reg, uint8_t mask);
    void pcdReset();
    void configISOType(char type);
    int request(uint8_t req_code, uint8_t *tag_type);
    int anticoll(uint8_t *snr);
    QString readCardUID();

    QString m_lastUID;
};

#endif // RC522THREAD_H