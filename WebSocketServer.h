#ifndef WEBSOCKETSERVER_H
#define WEBSOCKETSERVER_H

#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QList>
#include <QJsonObject>
#include <QJsonDocument>

class WebSocketServer : public QObject
{
    Q_OBJECT

public:
    static WebSocketServer& instance();

    bool start(int port = 8080);
    void stop();

    void setWebRoot(const QString &path);

    void sendDoorStatus(QString userName, QString method, bool success);
    void sendLockStatus(bool locked);
    void sendAlert(QString type, QString detail, QString photoPath);
signals:
    void remoteUnlockRequested();
    void remoteLockRequested();
    void remoteStatusRequested();

private slots:
    void onNewWebSocketConnection();
    void onTextMessageReceived(QString message);
    void onSocketDisconnected();

private:
    WebSocketServer() = default;
    ~WebSocketServer();

    void sendToAllClients(const QJsonObject &message);
    void handleCommand(const QJsonObject &cmd, QWebSocket *sender);

    QWebSocketServer *m_webSocketServer = nullptr;
    QList<QWebSocket*> m_clients;
    QString m_webRoot;
};

#endif // WEBSOCKETSERVER_H