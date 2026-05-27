#include "WebSocketServer.h"
#include "logger.h"
#include <QDebug>
#include <QDateTime>

WebSocketServer& WebSocketServer::instance()
{
    static WebSocketServer instance;
    return instance;
}

WebSocketServer::~WebSocketServer()
{
    stop();
}

void WebSocketServer::setWebRoot(const QString &path)
{
    m_webRoot = path;
    LOG_DEBUG(QString("Web root set to: %1").arg(m_webRoot));
}

bool WebSocketServer::start(int port)
{
    if (m_webSocketServer) {
        LOG_DEBUG("WebSocket server already running");
    } else {
        m_webSocketServer = new QWebSocketServer("SmartLock Server",
                                                 QWebSocketServer::NonSecureMode,
                                                 this);

        if (!m_webSocketServer->listen(QHostAddress::Any, port)) {
            LOG_ERROR(QString("Failed to start WebSocket server on port %1").arg(port));
            return false;
        }

        connect(m_webSocketServer, &QWebSocketServer::newConnection,
                this, &WebSocketServer::onNewWebSocketConnection);

        LOG_INFO(QString("WebSocket server started on port %1").arg(port));
    }

    return true;
}

void WebSocketServer::stop()
{
    LOG_INFO("WebSocket server stopping");

    if (m_webSocketServer) {
        m_webSocketServer->close();
        delete m_webSocketServer;
        m_webSocketServer = nullptr;
    }

    for (QWebSocket *client : m_clients) {
        client->close();
        client->deleteLater();
    }
    m_clients.clear();

    LOG_DEBUG("WebSocket server stopped");
}

void WebSocketServer::onNewWebSocketConnection()
{
    QWebSocket *socket = m_webSocketServer->nextPendingConnection();

    connect(socket, &QWebSocket::textMessageReceived,
            this, &WebSocketServer::onTextMessageReceived);
    connect(socket, &QWebSocket::disconnected,
            this, &WebSocketServer::onSocketDisconnected);

    m_clients.append(socket);
    LOG_INFO(QString("New client connected, total: %1").arg(m_clients.size()));

    // 发送当前门锁状态
    QJsonObject status;
    status["type"] = "status";
    status["locked"] = true;
    sendToAllClients(status);
}

void WebSocketServer::onSocketDisconnected()
{
    QWebSocket *socket = qobject_cast<QWebSocket*>(sender());
    if (socket) {
        m_clients.removeAll(socket);
        socket->deleteLater();
        LOG_INFO(QString("Client disconnected, remaining: %1").arg(m_clients.size()));
    }
}

void WebSocketServer::onTextMessageReceived(QString message)
{
    QWebSocket *socket = qobject_cast<QWebSocket*>(sender());
    if (!socket) return;

    LOG_DEBUG(QString("Received message: %1").arg(message));

    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull()) {
        LOG_WARNING("Invalid JSON received");
        return;
    }

    QJsonObject cmd = doc.object();
    handleCommand(cmd, socket);
}

void WebSocketServer::handleCommand(const QJsonObject &cmd, QWebSocket *sender)
{
    QString type = cmd["type"].toString();

    if (type == "unlock") {
        LOG_INFO("Remote unlock command received");
        emit remoteUnlockRequested();

        QJsonObject response;
        response["type"] = "response";
        response["command"] = "unlock";
        response["result"] = "ok";
        sender->sendTextMessage(QJsonDocument(response).toJson());

    } else if (type == "lock") {
        LOG_INFO("Remote lock command received");
        emit remoteLockRequested();

        QJsonObject response;
        response["type"] = "response";
        response["command"] = "lock";
        response["result"] = "ok";
        sender->sendTextMessage(QJsonDocument(response).toJson());

    } else if (type == "get_status") {
        LOG_DEBUG("Remote status request received");
        emit remoteStatusRequested();

    } else {
        LOG_WARNING(QString("Unknown command: %1").arg(type));
    }
}

void WebSocketServer::sendToAllClients(const QJsonObject &message)
{
    QString json = QJsonDocument(message).toJson();

    for (QWebSocket *client : m_clients) {
        client->sendTextMessage(json);
    }
}

void WebSocketServer::sendDoorStatus(QString userName, QString method, bool success)
{
    QJsonObject msg;
    msg["type"] = "door_event";
    msg["user"] = userName;
    msg["method"] = method;
    msg["result"] = success ? "success" : "failed";
    msg["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    sendToAllClients(msg);
    LOG_INFO(QString("Door event sent - user: %1, method: %2, result: %3").arg(userName).arg(method).arg(success ? "success" : "failed"));
}

void WebSocketServer::sendLockStatus(bool locked)
{
    QJsonObject msg;
    msg["type"] = "lock_status";
    msg["locked"] = locked;
    msg["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    sendToAllClients(msg);
    LOG_DEBUG(QString("Lock status sent: %1").arg(locked ? "locked" : "unlocked"));
}

void WebSocketServer::sendAlert(QString type, QString detail, QString photoPath)
{
    QJsonObject msg;
    msg["type"] = "alert";
    msg["alert_type"] = type;
    msg["detail"] = detail;
    msg["photo"] = photoPath;
    msg["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    sendToAllClients(msg);
    LOG_WARNING(QString("Alert sent - type: %1, detail: %2").arg(type).arg(detail));
}