#include "HttpServer.h"
#include "logger.h"
#include <QFile>
#include <QDebug>

HttpServer::HttpServer(QObject *parent) : QTcpServer(parent)
{
}

HttpServer::~HttpServer()
{
    close();
}

bool HttpServer::start(int port)
{
    if (!listen(QHostAddress::Any, port)) {
        LOG_ERROR(QString("Failed to start HTTP server on port %1").arg(port));
        return false;
    }

    LOG_INFO(QString("HTTP server started on port %1").arg(port));
    return true;
}

void HttpServer::incomingConnection(qintptr socketDescriptor)
{
    QTcpSocket *socket = new QTcpSocket(this);
    socket->setSocketDescriptor(socketDescriptor);

    if (socket->waitForReadyRead(3000)) {
        QByteArray data = socket->readAll();
        QString request = QString::fromUtf8(data);

        // 解析路径
        QString path = "/";
        if (request.startsWith("GET ")) {
            int start = 4;
            int end = request.indexOf(" ", start);
            if (end > start) {
                path = request.mid(start, end - start);
            }
        }

        // 去掉 URL 参数
        int queryIndex = path.indexOf("?");
        if (queryIndex >= 0) {
            path = path.left(queryIndex);
        }

        LOG_DEBUG(QString("HTTP request: %1").arg(path));

        // 默认首页
        if (path == "/" || path.isEmpty()) {
            path = "/control.html";
        }

        // 构建文件路径
        QString filePath;
        if (path.startsWith("/data/")) {
            filePath = "/opt/smartlock/bin" + path;
        } else {
            filePath = m_webRoot + path;
        }

        QFile file(filePath);

        if (file.exists() && file.open(QIODevice::ReadOnly)) {
            QByteArray content = file.readAll();

            // 获取 MIME 类型
            QString mimeType = "text/html";
            if (path.endsWith(".jpg") || path.endsWith(".jpeg")) {
                mimeType = "image/jpeg";
            } else if (path.endsWith(".png")) {
                mimeType = "image/png";
            } else if (path.endsWith(".css")) {
                mimeType = "text/css";
            } else if (path.endsWith(".js")) {
                mimeType = "application/javascript";
            }

            QString response = "HTTP/1.1 200 OK\r\n"
                               "Content-Type: " + mimeType + "\r\n"
                                            "Content-Length: " + QString::number(content.size()) + "\r\n"
                                                                   "Connection: close\r\n"
                                                                   "\r\n";
            socket->write(response.toUtf8());
            socket->write(content);
            socket->flush();

            LOG_DEBUG(QString("File served: %1 (size: %2)").arg(filePath).arg(content.size()));
        } else {
            QString response = "HTTP/1.1 404 Not Found\r\n\r\n";
            socket->write(response.toUtf8());
            socket->flush();

            LOG_WARNING(QString("File not found: %1").arg(filePath));
        }
    } else {
        LOG_WARNING("HTTP request timeout");
    }

    socket->disconnectFromHost();
    delete socket;
}