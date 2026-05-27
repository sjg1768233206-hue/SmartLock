#include "RC522Thread.h"
#include "logger.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <QDebug>

#define SPI_DEVICE "/dev/spidev0.0"
#define SPI_SPEED   1000000
#define SPI_MODE    0

// RC522寄存器地址
#define CommandReg      0x01
#define ComIrqReg       0x04
#define ErrorReg        0x06
#define Status2Reg      0x08
#define FIFODataReg     0x09
#define FIFOLevelReg    0x0A
#define BitFramingReg   0x0D
#define CollReg         0x0E
#define ModeReg         0x11
#define TxControlReg    0x14
#define TxAutoReg       0x15
#define RxSelReg        0x17
#define RFCfgReg        0x26
#define TModeReg        0x2A
#define TPrescalerReg   0x2B
#define TReloadRegL     0x2D
#define VersionReg      0x37

#define PCD_IDLE        0x00
#define PCD_RESETPHASE  0x0F
#define PCD_TRANSCEIVE  0x0C
#define PICC_REQIDL     0x26
#define PICC_ANTICOLL1  0x93
#define MI_OK           0x26
#define MI_ERR          0xBB

RC522Thread::RC522Thread(QObject *parent)
    : QThread(parent)
    , m_running(false)
    , spi_fd(-1)
{
}

RC522Thread::~RC522Thread()
{
    stop();
    wait();
}

void RC522Thread::stop()
{
    m_running = false;
}

bool RC522Thread::initSPI()
{
    spi_fd = open(SPI_DEVICE, O_RDWR);
    if (spi_fd < 0) {
        LOG_ERROR(QString("Failed to open SPI device: %1").arg(SPI_DEVICE));
        return false;
    }

    uint8_t mode = SPI_MODE;
    uint8_t bits = 8;
    uint32_t speed = SPI_SPEED;
    ioctl(spi_fd, SPI_IOC_WR_MODE, &mode);
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    LOG_INFO("SPI device opened successfully");
    return true;
}

void RC522Thread::closeSPI()
{
    if (spi_fd >= 0) {
        ::close(spi_fd);
        spi_fd = -1;
        LOG_DEBUG("SPI device closed");
    }
}

void RC522Thread::writeReg(uint8_t reg, uint8_t value)
{
    if (spi_fd < 0) return;
    uint8_t tx[2];
    tx[0] = (reg << 1) & 0x7E;
    tx[1] = value;

    struct spi_ioc_transfer tr = {};
    tr.tx_buf = (unsigned long)tx;
    tr.len = 2;
    tr.speed_hz = SPI_SPEED;
    tr.bits_per_word = 8;
    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
}

uint8_t RC522Thread::readReg(uint8_t reg)
{
    if (spi_fd < 0) return 0xFF;
    uint8_t tx[2], rx[2];
    tx[0] = ((reg << 1) & 0x7E) | 0x80;
    tx[1] = 0x00;

    struct spi_ioc_transfer tr = {};
    tr.tx_buf = (unsigned long)tx;
    tr.rx_buf = (unsigned long)rx;
    tr.len = 2;
    tr.speed_hz = SPI_SPEED;
    tr.bits_per_word = 8;
    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
    return rx[1];
}

void RC522Thread::setBitMask(uint8_t reg, uint8_t mask)
{
    uint8_t temp = readReg(reg);
    writeReg(reg, temp | mask);
}

void RC522Thread::clearBitMask(uint8_t reg, uint8_t mask)
{
    uint8_t temp = readReg(reg);
    writeReg(reg, temp & ~mask);
}

void RC522Thread::pcdReset()
{
    writeReg(CommandReg, PCD_RESETPHASE);
    usleep(10000);

    while (readReg(CommandReg) & 0x10) {
        usleep(1000);
    }

    writeReg(ModeReg, 0x3D);
    writeReg(TReloadRegL, 30);
    writeReg(TModeReg, 0x8D);
    writeReg(TPrescalerReg, 0x3E);
    writeReg(TxAutoReg, 0x40);

    LOG_DEBUG("RC522 reset completed");
}

void RC522Thread::configISOType(char type)
{
    if (type == 'A') {
        clearBitMask(Status2Reg, 0x08);
        writeReg(ModeReg, 0x3D);
        writeReg(RxSelReg, 0x86);
        writeReg(RFCfgReg, 0x7F);
        writeReg(TReloadRegL, 30);
        writeReg(TModeReg, 0x8D);
        writeReg(TPrescalerReg, 0x3E);
        usleep(2000);

        uint8_t uc = readReg(TxControlReg);
        if (!(uc & 0x03)) {
            setBitMask(TxControlReg, 0x03);
        }
        LOG_INFO("ISO14443_A configured, antenna enabled");
    }
}

int RC522Thread::request(uint8_t req_code, uint8_t *tag_type)
{
    clearBitMask(Status2Reg, 0x08);
    writeReg(BitFramingReg, 0x07);
    setBitMask(TxControlReg, 0x03);

    writeReg(CommandReg, PCD_IDLE);
    setBitMask(FIFOLevelReg, 0x80);

    writeReg(FIFODataReg, req_code);
    writeReg(CommandReg, PCD_TRANSCEIVE);
    setBitMask(BitFramingReg, 0x80);

    int timeout = 1000;
    uint8_t irq;
    do {
        irq = readReg(ComIrqReg);
        usleep(1000);
        timeout--;
    } while (timeout > 0 && !(irq & 0x01) && !(irq & 0x30));

    clearBitMask(BitFramingReg, 0x80);

    if (timeout == 0) {
        return MI_ERR;
    }

    if (!(readReg(ErrorReg) & 0x1B)) {
        uint8_t fifo_level = readReg(FIFOLevelReg);
        if (fifo_level == 2) {
            tag_type[0] = readReg(FIFODataReg);
            tag_type[1] = readReg(FIFODataReg);
            return MI_OK;
        }
    }

    return MI_ERR;
}

int RC522Thread::anticoll(uint8_t *snr)
{
    clearBitMask(Status2Reg, 0x08);
    writeReg(BitFramingReg, 0x00);
    clearBitMask(CollReg, 0x80);

    writeReg(CommandReg, PCD_IDLE);
    setBitMask(FIFOLevelReg, 0x80);

    writeReg(FIFODataReg, PICC_ANTICOLL1);
    writeReg(FIFODataReg, 0x20);
    writeReg(CommandReg, PCD_TRANSCEIVE);
    setBitMask(BitFramingReg, 0x80);

    int timeout = 1000;
    uint8_t irq;
    do {
        irq = readReg(ComIrqReg);
        usleep(1000);
        timeout--;
    } while (timeout > 0 && !(irq & 0x01) && !(irq & 0x30));

    clearBitMask(BitFramingReg, 0x80);

    if (timeout == 0) {
        return MI_ERR;
    }

    if (!(readReg(ErrorReg) & 0x1B)) {
        uint8_t fifo_level = readReg(FIFOLevelReg);
        if (fifo_level == 5) {
            for (int i = 0; i < 4; i++) {
                snr[i] = readReg(FIFODataReg);
            }

            uint8_t check = 0;
            for (int i = 0; i < 4; i++) {
                check ^= snr[i];
            }
            uint8_t received_check = readReg(FIFODataReg);
            if (check == received_check) {
                setBitMask(CollReg, 0x80);
                return MI_OK;
            }
        }
    }

    return MI_ERR;
}

QString RC522Thread::readCardUID()
{
    if (spi_fd < 0) return QString();

    uint8_t tag_type[2];
    uint8_t uid[4];

    if (request(PICC_REQIDL, tag_type) != MI_OK) {
        return QString();
    }

    if (anticoll(uid) != MI_OK) {
        return QString();
    }

    return QString("%1:%2:%3:%4")
        .arg(uid[0], 2, 16, QLatin1Char('0'))
        .arg(uid[1], 2, 16, QLatin1Char('0'))
        .arg(uid[2], 2, 16, QLatin1Char('0'))
        .arg(uid[3], 2, 16, QLatin1Char('0'))
        .toUpper();
}

void RC522Thread::run()
{
    LOG_INFO("RC522Thread started");

    // 先执行系统命令修复权限
    system("sudo chmod 666 /dev/spidev0.0 2>/dev/null");
    system("sudo chmod 666 /dev/spidev0.1 2>/dev/null");

    if (!initSPI()) {
        LOG_ERROR("RC522 SPI initialization failed");
        emit error("RC522 SPI初始化失败");
        return;
    }

    pcdReset();
    configISOType('A');

    uint8_t version = readReg(VersionReg);
    LOG_INFO(QString("RC522 version: 0x%1").arg(QString::number(version, 16)));

    if (version == 0x00 || version == 0xFF) {
        LOG_ERROR("RC522 communication failed");
        emit error("RC522通信失败");
        closeSPI();
        return;
    }

    LOG_INFO("RC522 initialized successfully, waiting for card...");
    m_running = true;

    while (m_running) {
        QString uid = readCardUID();

        if (!uid.isEmpty()) {
            if (uid != m_lastUID) {
                m_lastUID = uid;
                LOG_INFO(QString("Card detected! UID: %1").arg(uid));
                emit cardDetected(uid);
            }
        } else {
            if (!m_lastUID.isEmpty()) {
                m_lastUID.clear();
            }
        }

        // 扫描间隔 300ms
        msleep(300);
    }

    closeSPI();
    LOG_INFO("RC522Thread stopped");
}