#ifndef LOGGER_H
#define LOGGER_H

#include <QString>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QDateTime>
#include <QThreadStorage>

class Logger
{
public:
    enum LogLevel {
        DEBUG = 0,
        INFO = 1,
        WARNING = 2,
        ERROR = 3,
        NONE = 4
    };

    static Logger& instance();

    void log(LogLevel level, const QString &message, const char *file = nullptr, int line = 0);

    void setLogFile(const QString &path);
    void setLevel(LogLevel level);
    void setConsoleOutput(bool enable);

private:
    Logger();
    ~Logger();

    QString levelToString(LogLevel level);
    QString getFileName(const char *file);

    QFile m_file;
    QTextStream m_stream;
    LogLevel m_level;
    bool m_consoleOutput;
    QMutex m_mutex;  // 关键：互斥锁保证线程安全
};

// 宏定义
#define LOG_DEBUG(msg) Logger::instance().log(Logger::DEBUG, msg, __FILE__, __LINE__)
#define LOG_INFO(msg)  Logger::instance().log(Logger::INFO, msg, __FILE__, __LINE__)
#define LOG_WARNING(msg) Logger::instance().log(Logger::WARNING, msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) Logger::instance().log(Logger::ERROR, msg, __FILE__, __LINE__)

#endif // LOGGER_H