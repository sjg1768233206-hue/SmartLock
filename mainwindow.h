#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QLabel>
#include <QStackedWidget>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QTableView>
#include <opencv2/opencv.hpp>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class PasswordWidget;
class CameraThread;
class RC522Thread;
class HttpServer;
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

    // 页面切换槽函数
    void onFaceRecognitionClicked();
    void onPasswordInputClicked();
    void onChangePasswordClicked();
    void onFaceTrainClicked();
    void onManualUnlockClicked();

    // 训练相关槽函数
    void loadPersonList();
    void onStartCapture();
    void onSaveFace();
    void onTrainModel();

    // 数据库相关槽函数
    void refreshLogTable();
    void refreshTodayLogTable();
    // websocket
    void onRemoteUnlockRequested();
    void onRemoteLockRequested();
    void onRemoteStatusRequested();

    void onAlertTriggered(QString photoPath);  // 添加这行

private:
    void setupUI();
    void initCamera();
    void initAHT20();
    void initRC522();
    void initDatabase();
    void initWebSocket();

    float readTemperature();
    float readHumidity();

    void initHttpServer();  // 添加这行
private:
    Ui::MainWindow *ui;

    QStackedWidget *m_stackedWidget;
    PasswordWidget *m_passwordWidget;

    // 摄像头显示标签
    QLabel *m_cameraLabel;           // 人脸识别页面用
    QLabel *m_trainCameraLabel;      // 人脸训练页面用

    QLabel *m_lockStatusLabel;
    QLabel *m_tempLabel;
    QLabel *m_humiLabel;

    // 多线程相关
    CameraThread *m_cameraThread;
    RC522Thread *m_rc522Thread;

    // AHT20 I2C
    int m_i2c_fd;
    QTimer *m_tempTimer;

    // 人脸训练相关
    QLineEdit *m_trainNameEdit;
    QComboBox *m_trainPersonList;
    QPushButton *m_trainStartBtn;
    QPushButton *m_trainModelBtn;
    QLabel *m_trainProgressLabel;
    QLabel *m_trainStatusLabel;

    // 数据库相关
    QTableView *m_logTableView;
    QLabel *m_todayCountLabel;

    int m_captureCount;
    QString m_currentPerson;
    bool m_isCapturing;

    HttpServer *m_httpServer;
};

#endif // MAINWINDOW_H