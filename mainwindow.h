#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QLabel>
#include <QStackedWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QTableView>
#include <opencv2/opencv.hpp>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class PasswordWidget;
class CameraThread;
class RC522Thread;
class GPUVideoWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void updateLockStatus(bool locked);
    void updateTemperature();
    void onFrameReady(const QImage &image);
    void onCardDetected(const QString &uid);
    void onCameraError(const QString &msg);
    void onRC522Error(const QString &msg);
    void onFaceDetected(int x, int y, int width, int height);
    void onFaceRecognized(const QString &name);

    // 页面切换
    void onFaceRecognitionClicked();
    void onPasswordInputClicked();
    void onChangePasswordClicked();
    void onFaceTrainClicked();
    void onManualUnlockClicked();
    void onAccessLogClicked();

    // 人脸管理
    void onCapturePhoto();
    void onConfirmEnroll();
    void onCancelEnroll();
    void onShowPersonList();

    // 数据库
    void refreshLogTable();
    void refreshTodayLogTable();

    void onRemoteUnlockRequested();
    void onRemoteLockRequested();
    void onRemoteStatusRequested();
    void onAlertTriggered(QString photoPath);

private:
    void setupUI();
    void initCamera();
    void initAHT20();
    void initRC522();
    void initDatabase();
    void initWebSocket();
    void initHttpServer();

    float readTemperature();
    float readHumidity();

private:
    Ui::MainWindow *ui;

    QStackedWidget *m_stackedWidget;
    PasswordWidget *m_passwordWidget;
    GPUVideoWidget *m_cameraLabel;
    GPUVideoWidget *m_trainCameraLabel;

    QLabel *m_lockStatusLabel;
    QLabel *m_tempLabel;
    QLabel *m_humiLabel;

    // 多线程
    CameraThread *m_cameraThread;
    RC522Thread *m_rc522Thread;

    // AHT20
    int m_i2c_fd;
    QTimer *m_tempTimer;

    // 人脸管理
    QLineEdit *m_trainNameEdit;
    QLabel *m_trainStatusLabel;
    QPushButton *m_btnConfirm;
    QPushButton *m_btnCancel;
    QString m_tempFacePath;

    // 开门记录
    QTableView *m_logTableView;
    QLabel *m_todayCountLabel;

    // HTTP 服务器
    class HttpServer *m_httpServer;
};

#endif // MAINWINDOW_H
