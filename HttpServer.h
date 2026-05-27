#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QTcpServer>
#include <QTcpSocket>
#include <QDebug>
#include <QFile>

class HttpServer : public QTcpServer
{
    Q_OBJECT

public:
    explicit HttpServer(QObject *parent = nullptr);
    ~HttpServer();

    bool start(int port = 8081);
    void setWebRoot(const QString &path) { m_webRoot = path; }

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    QString m_webRoot;
};

#endif // HTTPSERVER_H