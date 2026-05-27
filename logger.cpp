#include "logger.h"
#include <QDebug>
#include <QDir>

Logger& Logger::instance()
{
    static Logger instance;  // C++11 保证线程安全的单例
    return instance;
}

Logger::Logger()
    : m_level(INFO)
    , m_consoleOutput(true)
{
    // 创建日志目录
    QDir().mkpath("/opt/smartlock/bin/logs");
    setLogFile("/opt/smartlock/bin/logs/smartlock.log");
}

Logger::~Logger()
{
    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen()) {
        m_stream.flush();
        m_file.close();
    }
}

void Logger::setLogFile(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen()) {
        m_file.close();
    }
    m_file.setFileName(path);
    if (m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        m_stream.setDevice(&m_file);
    } else {
        qDebug() << "Failed to open log file:" << path;
    }
}

void Logger::setLevel(LogLevel level)
{
    QMutexLocker locker(&m_mutex);
    m_level = level;
}

void Logger::setConsoleOutput(bool enable)
{
    QMutexLocker locker(&m_mutex);
    m_consoleOutput = enable;
}

QString Logger::levelToString(LogLevel level)
{
    switch (level) {
    case DEBUG:   return "DEBUG";
    case INFO:    return "INFO ";
    case WARNING: return "WARN ";
    case ERROR:   return "ERROR";
    default:      return "UNKNOWN";
    }
}

QString Logger::getFileName(const char *file)
{
    QString path = QString::fromUtf8(file);
    return path.mid(path.lastIndexOf('/') + 1);
}

void Logger::log(LogLevel level, const QString &message, const char *file, int line)
{
    // 先检查级别（不需要锁）
    if (level < m_level) return;

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString location;
    if (file && line > 0) {
        location = QString("[%1:%2] ").arg(getFileName(file)).arg(line);
    }

    QString logLine = QString("%1 [%2] %3%4")
                          .arg(timestamp)
                          .arg(levelToString(level))
                          .arg(location)
                          .arg(message);

    // 写入文件（加锁保护）
    {
        QMutexLocker locker(&m_mutex);
        if (m_file.isOpen()) {
            m_stream << logLine << "\n";
            m_stream.flush();
        }

        // 输出到控制台（也在锁内，保证顺序）
        if (m_consoleOutput) {
            qDebug() << logLine;
        }
    }
}