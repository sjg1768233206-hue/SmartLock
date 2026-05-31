#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "passwordwidget.h"
#include "lockcontroller.h"
#include "passwordmanager.h"
#include "CameraThread.h"
#include "RC522Thread.h"
#include "databasemanager.h"
#include "logger.h"
#include "gpu_video_widget.h"
#include "FeatureDatabase.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <QThread>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QtConcurrent>
#include <QFile>
#include <QTextStream>
#include <QTableView>
#include <QHeaderView>
#include <QListWidget>
#include <QDialog>
#include <opencv2/face.hpp>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include "WebSocketServer.h"
#include "HttpServer.h"

#define I2C_DEVICE "/dev/i2c-5"
#define AHT20_ADDR 0x38

// 页面索引
#define PAGE_FACE_RECOGNITION  0
#define PAGE_PASSWORD_INPUT    1
#define PAGE_PASSWORD_CHANGE   2
#define PAGE_FACE_TRAIN        3
#define PAGE_MANUAL_CONTROL    4
#define PAGE_ACCESS_LOG        5

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_stackedWidget(nullptr)
    , m_passwordWidget(nullptr)
    , m_cameraLabel(nullptr)
    , m_trainCameraLabel(nullptr)
    , m_lockStatusLabel(nullptr)
    , m_tempLabel(nullptr)
    , m_humiLabel(nullptr)
    , m_cameraThread(nullptr)
    , m_rc522Thread(nullptr)
    , m_i2c_fd(-1)
    , m_tempTimer(nullptr)
    , m_trainNameEdit(nullptr)
    , m_trainStatusLabel(nullptr)
    , m_btnConfirm(nullptr)
    , m_btnCancel(nullptr)
    , m_logTableView(nullptr)
    , m_todayCountLabel(nullptr)
    , m_httpServer(nullptr)
{
    Logger::instance().setLevel(Logger::DEBUG);
    Logger::instance().setConsoleOutput(true);
    LOG_INFO("SmartLock Starting");

    setupUI();
    initDatabase();
    initCamera();
    initWebSocket();
    initHttpServer();
    initRC522();
    initAHT20();

    m_stackedWidget->setCurrentIndex(PAGE_FACE_RECOGNITION);
    m_passwordWidget->setMode(PasswordWidget::ModeInput);

    connect(&LockController::instance(), &LockController::lockStateChanged,
            this, &MainWindow::updateLockStatus);
    updateLockStatus(LockController::instance().isLocked());
}

MainWindow::~MainWindow()
{
    if (m_cameraThread) {
        m_cameraThread->stop();
        m_cameraThread->wait();
        delete m_cameraThread;
    }
    if (m_rc522Thread) {
        m_rc522Thread->stop();
        m_rc522Thread->wait();
        delete m_rc522Thread;
    }
    if (m_tempTimer) delete m_tempTimer;
    if (m_i2c_fd >= 0) ::close(m_i2c_fd);
    if (m_httpServer) delete m_httpServer;
    DatabaseManager::instance().close();
    delete ui;
}

void MainWindow::setupUI()
{
    setWindowTitle("智能门锁");
    setFixedSize(950, 650);

    QWidget *centralWidget = new QWidget(this);
    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ========== 左侧菜单 ==========
    QWidget *leftMenu = new QWidget(this);
    leftMenu->setFixedWidth(200);
    leftMenu->setStyleSheet("background-color: #2c3e50;");
    QVBoxLayout *menuLayout = new QVBoxLayout(leftMenu);
    menuLayout->setSpacing(15);
    menuLayout->setContentsMargins(0, 50, 0, 30);

    QLabel *titleLabel = new QLabel("智能门锁", this);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("color: white; font-size: 20px; font-weight: bold; padding-bottom: 20px;");
    menuLayout->addWidget(titleLabel);

    QString btnStyle = "QPushButton { background-color: #34495e; color: white; border: none; padding: 12px; border-radius: 8px; font-size: 14px; text-align: left; padding-left: 20px; }"
                       "QPushButton:hover { background-color: #3d566e; }";

    QPushButton *btnFace = new QPushButton("👤 人脸识别", this);
    QPushButton *btnPwd = new QPushButton("🔐 输入密码", this);
    QPushButton *btnChangePwd = new QPushButton("🔑 修改密码", this);
    QPushButton *btnTrain = new QPushButton("📸 人脸管理", this);
    QPushButton *btnManual = new QPushButton("🔧 手动调试", this);
    QPushButton *btnLog = new QPushButton("📋 开门记录", this);

    for (auto btn : {btnFace, btnPwd, btnChangePwd, btnTrain, btnManual, btnLog}) {
        btn->setStyleSheet(btnStyle);
        menuLayout->addWidget(btn);
    }
    menuLayout->addStretch();

    // ========== 右侧内容 ==========
    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->setStyleSheet("background-color: #ecf0f1;");

    // --- 页面0: 人脸识别 ---
    QWidget *faceWidget = new QWidget(this);
    QVBoxLayout *faceLayout = new QVBoxLayout(faceWidget);
    faceLayout->setContentsMargins(0, 0, 0, 0);

    m_cameraLabel = new GPUVideoWidget(this);
    m_cameraLabel->setMinimumSize(640, 480);
    m_cameraLabel->setStyleSheet("background-color: #2c3e50; border-radius: 10px;");
    faceLayout->addWidget(m_cameraLabel);

    QLabel *tipLabel = new QLabel("人脸识别中，请面对摄像头", this);
    tipLabel->setAlignment(Qt::AlignCenter);
    tipLabel->setStyleSheet("color: #7f8c8d; padding: 10px;");
    faceLayout->addWidget(tipLabel);

    m_stackedWidget->addWidget(faceWidget);

    // --- 页面1: 输入密码 ---
    PasswordWidget *inputPwd = new PasswordWidget(this);
    inputPwd->setMode(PasswordWidget::ModeInput);
    m_stackedWidget->addWidget(inputPwd);
    m_passwordWidget = inputPwd;

    // --- 页面2: 修改密码 ---
    PasswordWidget *changePwd = new PasswordWidget(this);
    changePwd->setMode(PasswordWidget::ModeChange);
    m_stackedWidget->addWidget(changePwd);

    // --- 页面3: 人脸管理 ---
    QWidget *trainWidget = new QWidget(this);
    QVBoxLayout *trainLayout = new QVBoxLayout(trainWidget);
    trainLayout->setContentsMargins(10, 10, 10, 10);
    trainLayout->setSpacing(15);

    // 摄像头预览
    m_trainCameraLabel = new GPUVideoWidget(this);
    m_trainCameraLabel->setMinimumSize(600, 420);
    m_trainCameraLabel->setStyleSheet("background-color: #2c3e50; border-radius: 10px;");
    trainLayout->addWidget(m_trainCameraLabel);

    // 录入区域（拍照后显示）
    QHBoxLayout *enrollLayout = new QHBoxLayout();
    enrollLayout->setSpacing(10);

    m_trainNameEdit = new QLineEdit(this);
    m_trainNameEdit->setPlaceholderText("输入姓名");
    m_trainNameEdit->setFixedWidth(200);
    m_trainNameEdit->setVisible(false);

    m_btnConfirm = new QPushButton("确认", this);
    m_btnConfirm->setFixedSize(80, 35);
    m_btnConfirm->setStyleSheet("background-color: #3498db; color: white; border-radius: 5px;");
    m_btnConfirm->setVisible(false);

    m_btnCancel = new QPushButton("取消", this);
    m_btnCancel->setFixedSize(80, 35);
    m_btnCancel->setStyleSheet("background-color: #95a5a6; color: white; border-radius: 5px;");
    m_btnCancel->setVisible(false);

    enrollLayout->addStretch();
    enrollLayout->addWidget(m_trainNameEdit);
    enrollLayout->addWidget(m_btnConfirm);
    enrollLayout->addWidget(m_btnCancel);
    enrollLayout->addStretch();

    trainLayout->addLayout(enrollLayout);

    // 两个大按钮
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(30);

    QPushButton *captureBtn = new QPushButton("📷 拍照录入", this);
    captureBtn->setFixedSize(180, 60);
    captureBtn->setStyleSheet("background-color: #27ae60; color: white; font-size: 16px; border-radius: 10px;");

    QPushButton *listBtn = new QPushButton("📋 已录入人员", this);
    listBtn->setFixedSize(180, 60);
    listBtn->setStyleSheet("background-color: #3498db; color: white; font-size: 16px; border-radius: 10px;");

    btnLayout->addStretch();
    btnLayout->addWidget(captureBtn);
    btnLayout->addWidget(listBtn);
    btnLayout->addStretch();

    trainLayout->addLayout(btnLayout);
    trainLayout->addStretch();

    m_stackedWidget->addWidget(trainWidget);

    // --- 页面4: 手动控制 ---
    QWidget *manualWidget = new QWidget(this);
    QVBoxLayout *manualLayout = new QVBoxLayout(manualWidget);
    manualLayout->setAlignment(Qt::AlignCenter);
    QLabel *manualTitle = new QLabel("🔧 手动控制", this);
    manualTitle->setAlignment(Qt::AlignCenter);
    manualTitle->setStyleSheet("font-size: 24px; font-weight: bold;");
    manualLayout->addWidget(manualTitle);

    QHBoxLayout *btnLayout2 = new QHBoxLayout();
    QPushButton *btnUnlock = new QPushButton("🔓 解锁", this);
    QPushButton *btnLock = new QPushButton("🔒 锁定", this);
    QString bigStyle = "QPushButton { background-color: #3498db; color: white; padding: 20px; border-radius: 15px; font-size: 18px; min-width: 150px; }"
                       "QPushButton:hover { background-color: #2980b9; }";
    btnUnlock->setStyleSheet(bigStyle);
    btnLock->setStyleSheet(bigStyle);
    btnLayout2->addWidget(btnUnlock);
    btnLayout2->addWidget(btnLock);
    manualLayout->addLayout(btnLayout2);
    m_stackedWidget->addWidget(manualWidget);

    // --- 页面5: 开门记录 ---
    QWidget *logWidget = new QWidget(this);
    QVBoxLayout *logLayout = new QVBoxLayout(logWidget);
    QHBoxLayout *logTitleLayout = new QHBoxLayout();
    QLabel *logTitle = new QLabel("📋 开门记录", this);
    logTitle->setStyleSheet("font-size: 20px; font-weight: bold;");
    QPushButton *btnRefresh = new QPushButton("🔄 刷新", this);
    QPushButton *btnToday = new QPushButton("📅 今日", this);
    logTitleLayout->addWidget(logTitle);
    logTitleLayout->addStretch();
    logTitleLayout->addWidget(btnRefresh);
    logTitleLayout->addWidget(btnToday);
    logLayout->addLayout(logTitleLayout);

    m_todayCountLabel = new QLabel("今日开门: 0次", this);
    m_todayCountLabel->setStyleSheet("background-color: #27ae60; color: white; padding: 5px 15px; border-radius: 10px;");
    logLayout->addWidget(m_todayCountLabel);

    m_logTableView = new QTableView(this);
    m_logTableView->setAlternatingRowColors(true);
    logLayout->addWidget(m_logTableView);
    m_stackedWidget->addWidget(logWidget);

    // ========== 顶部状态栏 ==========
    QWidget *titleBar = new QWidget(this);
    QHBoxLayout *titleBarLayout = new QHBoxLayout(titleBar);
    m_tempLabel = new QLabel("🌡️ --.- °C", this);
    m_humiLabel = new QLabel("💧 --.- %", this);
    m_lockStatusLabel = new QLabel("🔒 已锁定", this);
    m_lockStatusLabel->setAlignment(Qt::AlignRight);
    titleBarLayout->addWidget(m_tempLabel);
    titleBarLayout->addWidget(m_humiLabel);
    titleBarLayout->addStretch();
    titleBarLayout->addWidget(m_lockStatusLabel);

    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->addWidget(titleBar);
    rightLayout->addWidget(m_stackedWidget, 1);
    QWidget *rightWidget = new QWidget(this);
    rightWidget->setLayout(rightLayout);

    mainLayout->addWidget(leftMenu);
    mainLayout->addWidget(rightWidget, 1);
    setCentralWidget(centralWidget);

    // 连接信号
    connect(btnFace, &QPushButton::clicked, this, &MainWindow::onFaceRecognitionClicked);
    connect(btnPwd, &QPushButton::clicked, this, &MainWindow::onPasswordInputClicked);
    connect(btnChangePwd, &QPushButton::clicked, this, &MainWindow::onChangePasswordClicked);
    connect(btnTrain, &QPushButton::clicked, this, &MainWindow::onFaceTrainClicked);
    connect(btnManual, &QPushButton::clicked, this, &MainWindow::onManualUnlockClicked);
    connect(btnLog, &QPushButton::clicked, this, &MainWindow::onAccessLogClicked);
    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::refreshLogTable);
    connect(btnToday, &QPushButton::clicked, this, &MainWindow::refreshTodayLogTable);
    connect(btnUnlock, &QPushButton::clicked, [this]() {
        DatabaseManager::instance().addLog("手动", "手动", true);
        LockController::instance().unlock();
    });
    connect(btnLock, &QPushButton::clicked, [this]() {
        LockController::instance().lock();
    });
    connect(captureBtn, &QPushButton::clicked, this, &MainWindow::onCapturePhoto);
    connect(listBtn, &QPushButton::clicked, this, &MainWindow::onShowPersonList);
    connect(m_btnConfirm, &QPushButton::clicked, this, &MainWindow::onConfirmEnroll);
    connect(m_btnCancel, &QPushButton::clicked, this, &MainWindow::onCancelEnroll);
    connect(inputPwd, &PasswordWidget::passwordAlert, this, &MainWindow::onAlertTriggered);
}

void MainWindow::onCapturePhoto()
{
    cv::Mat face = m_cameraThread->getCurrentFace();
    if (face.empty()) {
        QMessageBox::warning(this, "提示", "未检测到人脸，请面对摄像头");
        return;
    }

    // 保存临时照片
    QDir().mkpath("data/temp");
    m_tempFacePath = QString("data/temp/temp_%1.jpg").arg(QDateTime::currentMSecsSinceEpoch());
    cv::imwrite(m_tempFacePath.toStdString(), face);

    m_trainStatusLabel->setText("拍照成功！请输入姓名");
    m_trainStatusLabel->setStyleSheet("color: #27ae60; font-weight: bold;");
    m_trainNameEdit->setVisible(true);
    m_trainNameEdit->clear();
    m_trainNameEdit->setFocus();
    m_btnConfirm->setVisible(true);
    m_btnCancel->setVisible(true);
}

void MainWindow::onConfirmEnroll()
{
    QString name = m_trainNameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入姓名");
        return;
    }

    cv::Mat face = cv::imread(m_tempFacePath.toStdString());
    if (face.empty()) {
        QMessageBox::warning(this, "错误", "照片读取失败");
        onCancelEnroll();
        return;
    }

    // 保存到正式目录
    QDir().mkpath(QString("data/train/%1").arg(name));
    QString filename = QString("data/train/%1/001.jpg").arg(name);
    cv::imwrite(filename.toStdString(), face);

    // 提取 NPU 特征
    cv::Mat grayFace;
    if (face.channels() == 3) {
        cv::cvtColor(face, grayFace, cv::COLOR_BGR2GRAY);
    } else {
        grayFace = face;
    }

    std::vector<float> features = m_cameraThread->extractFeatureForEnroll(grayFace);

    if (!features.empty()) {
        FeatureDatabase::instance().addUser(name, features);
        FeatureDatabase::instance().save("/opt/smartlock/bin/data/features.db");
        LOG_INFO(QString("Enrolled user: %1").arg(name));
        QMessageBox::information(this, "成功", QString("✅ %1 录入成功！").arg(name));
    } else {
        QMessageBox::warning(this, "失败", "特征提取失败，请重试");
    }

    // 清理
    QFile::remove(m_tempFacePath);
    onCancelEnroll();
}

void MainWindow::onCancelEnroll()
{
    m_trainStatusLabel->setText("就绪");
    m_trainStatusLabel->setStyleSheet("color: #27ae60; font-weight: bold;");
    m_trainNameEdit->setVisible(false);
    m_trainNameEdit->clear();
    m_btnConfirm->setVisible(false);
    m_btnCancel->setVisible(false);

    if (!m_tempFacePath.isEmpty() && QFile::exists(m_tempFacePath)) {
        QFile::remove(m_tempFacePath);
    }
}

void MainWindow::onShowPersonList()
{
    // 从 FeatureDatabase 获取人员列表（而不是文件系统）
    QStringList persons = FeatureDatabase::instance().getAllUserNames();

    if (persons.isEmpty()) {
        QMessageBox::information(this, "提示", "暂无已录入人员");
        return;
    }

    // 创建人员列表对话框
    QDialog *listDialog = new QDialog(this);
    listDialog->setWindowTitle("已录入人员");
    listDialog->setModal(true);
    listDialog->setFixedSize(350, 450);

    QVBoxLayout *layout = new QVBoxLayout(listDialog);

    QListWidget *listWidget = new QListWidget(listDialog);
    for (const QString &person : persons) {
        listWidget->addItem(person);
    }

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *viewBtn = new QPushButton("查看照片", listDialog);
    QPushButton *deleteBtn = new QPushButton("删除", listDialog);
    deleteBtn->setStyleSheet("background-color: #e74c3c; color: white;");
    QPushButton *closeBtn = new QPushButton("关闭", listDialog);

    btnLayout->addWidget(viewBtn);
    btnLayout->addWidget(deleteBtn);
    btnLayout->addWidget(closeBtn);
    btnLayout->addStretch();

    layout->addWidget(listWidget);
    layout->addLayout(btnLayout);

    // 查看照片
    connect(viewBtn, &QPushButton::clicked, [this, listWidget, listDialog]() {
        QListWidgetItem *item = listWidget->currentItem();
        if (!item) {
            QMessageBox::warning(listDialog, "提示", "请先选择要查看的人员");
            return;
        }
        QString name = item->text();
        // 从 FeatureDatabase 获取照片路径（需要扩展数据库存储照片路径）
        QString photoPath = QString("data/train/%1/001.jpg").arg(name);
        if (!QFile::exists(photoPath)) {
            QMessageBox::information(listDialog, "提示", "没有找到照片");
            return;
        }
        QImage img(photoPath);
        if (!img.isNull()) {
            QDialog *photoDialog = new QDialog(listDialog);
            photoDialog->setWindowTitle(QString("%1 的照片").arg(name));
            photoDialog->setModal(true);
            QVBoxLayout *photoLayout = new QVBoxLayout(photoDialog);
            QLabel *label = new QLabel();
            label->setPixmap(QPixmap::fromImage(img).scaled(300, 300, Qt::KeepAspectRatio));
            photoLayout->addWidget(label);
            QPushButton *closePhotoBtn = new QPushButton("关闭");
            photoLayout->addWidget(closePhotoBtn);
            connect(closePhotoBtn, &QPushButton::clicked, photoDialog, &QDialog::accept);
            photoDialog->exec();
            delete photoDialog;
        }
    });

    // 删除人员
    connect(deleteBtn, &QPushButton::clicked, [this, listWidget, listDialog]() {
        QListWidgetItem *item = listWidget->currentItem();
        if (!item) {
            QMessageBox::warning(listDialog, "提示", "请先选择要删除的人员");
            return;
        }
        QString name = item->text();
        QMessageBox::StandardButton reply = QMessageBox::question(
            listDialog, "确认删除",
            QString("确定要删除「%1」的人脸数据吗？").arg(name),
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply == QMessageBox::Yes) {
            // 删除照片文件
            QDir dir(QString("data/train/%1").arg(name));
            if (dir.exists()) {
                dir.removeRecursively();
            }
            // 从数据库删除特征
            FeatureDatabase::instance().removeUser(name);
            FeatureDatabase::instance().save("/opt/smartlock/bin/data/features.db");

            listWidget->takeItem(listWidget->row(item));
            QMessageBox::information(listDialog, "成功", QString("已删除 %1").arg(name));
        }
    });

    connect(closeBtn, &QPushButton::clicked, listDialog, &QDialog::accept);
    listDialog->exec();
    delete listDialog;
}

void MainWindow::initCamera()
{
    m_cameraThread = new CameraThread(this);
    connect(m_cameraThread, &CameraThread::frameReady, this, &MainWindow::onFrameReady);
    connect(m_cameraThread, &CameraThread::error, this, &MainWindow::onCameraError);
    connect(m_cameraThread, &CameraThread::faceDetected, this, &MainWindow::onFaceDetected);
    connect(m_cameraThread, &CameraThread::faceRecognized, this, &MainWindow::onFaceRecognized);
    connect(m_cameraThread, &CameraThread::unknownFaceAlert, this, &MainWindow::onAlertTriggered);
    m_cameraThread->start();
}

void MainWindow::onFrameReady(const QImage &image)
{
    int currentPage = m_stackedWidget->currentIndex();

    if (currentPage == PAGE_FACE_RECOGNITION && m_cameraLabel) {
        m_cameraLabel->updateFrame(image);
    } else if (currentPage == PAGE_FACE_TRAIN && m_trainCameraLabel) {
        m_trainCameraLabel->updateFrame(image);
    }
}

void MainWindow::initHttpServer()
{
    m_httpServer = new HttpServer(this);
    m_httpServer->setWebRoot("/opt/smartlock/bin/web");

    if (m_httpServer->start(8081)) {
        LOG_INFO("HTTP server started on port 8081");
    } else {
        LOG_ERROR("HTTP server failed to start");
    }
}

void MainWindow::initDatabase()
{
    DatabaseManager::instance().init();
    LOG_INFO(QString("Today's unlocks: %1").arg(DatabaseManager::instance().getTodayCount()));
}

void MainWindow::initWebSocket()
{
    WebSocketServer::instance().setWebRoot("/opt/smartlock/bin/web");

    if (WebSocketServer::instance().start(8080)) {
        LOG_INFO("WebSocket server started on port 8080");
    } else {
        LOG_ERROR("WebSocket server failed to start");
    }

    connect(&WebSocketServer::instance(), &WebSocketServer::remoteUnlockRequested,
            this, &MainWindow::onRemoteUnlockRequested);
    connect(&WebSocketServer::instance(), &WebSocketServer::remoteLockRequested,
            this, &MainWindow::onRemoteLockRequested);
    connect(&WebSocketServer::instance(), &WebSocketServer::remoteStatusRequested,
            this, &MainWindow::onRemoteStatusRequested);
}

void MainWindow::refreshLogTable()
{
    QSqlQueryModel *model = DatabaseManager::instance().getRecentLogs(100);
    if (m_logTableView) {
        m_logTableView->setModel(model);
        m_logTableView->setColumnWidth(0, 120);
        m_logTableView->setColumnWidth(1, 100);
        m_logTableView->setColumnWidth(2, 80);
        m_logTableView->setColumnWidth(3, 150);
    }

    if (m_todayCountLabel) {
        m_todayCountLabel->setText(QString("今日开门: %1次").arg(DatabaseManager::instance().getTodayCount()));
    }
}

void MainWindow::refreshTodayLogTable()
{
    QSqlQueryModel *model = DatabaseManager::instance().getTodayLogs();
    if (m_logTableView) {
        m_logTableView->setModel(model);
        m_logTableView->setColumnWidth(0, 120);
        m_logTableView->setColumnWidth(1, 100);
        m_logTableView->setColumnWidth(2, 80);
        m_logTableView->setColumnWidth(3, 150);
    }
}

void MainWindow::onFaceRecognized(const QString &name)
{
    LOG_INFO(QString("Face recognized: %1, unlocking door!").arg(name));

    DatabaseManager::instance().addLog(name, "人脸", true);
    WebSocketServer::instance().sendDoorStatus(name, "人脸", true);
    LockController::instance().unlock();

    statusBar()->showMessage(QString("欢迎回家，%1").arg(name), 3000);

    if (m_stackedWidget->currentIndex() == PAGE_ACCESS_LOG) {
        refreshLogTable();
    }
}

void MainWindow::onFaceDetected(int x, int y, int width, int height)
{
    Q_UNUSED(x);
    Q_UNUSED(y);
    Q_UNUSED(width);
    Q_UNUSED(height);
}

void MainWindow::onCameraError(const QString &msg)
{
    LOG_ERROR(QString("Camera error: %1").arg(msg));
    statusBar()->showMessage("摄像头错误: " + msg, 3000);
}

void MainWindow::initRC522()
{
    LOG_INFO("Starting RC522 thread...");
    m_rc522Thread = new RC522Thread(this);
    connect(m_rc522Thread, &RC522Thread::cardDetected, this, &MainWindow::onCardDetected);
    connect(m_rc522Thread, &RC522Thread::error, this, &MainWindow::onRC522Error);
    m_rc522Thread->start();
}

void MainWindow::onCardDetected(const QString &uid)
{
    static QStringList authorizedCards = {"9D:58:25:07"};

    if (authorizedCards.contains(uid.toUpper())) {
        LOG_INFO(QString("Authorized card detected: %1, unlocking").arg(uid));
        DatabaseManager::instance().addLog("RFID卡", "RFID", true);
        LockController::instance().unlock();
        WebSocketServer::instance().sendDoorStatus("RFID卡", "RFID", true);
    } else {
        LOG_WARNING(QString("Unauthorized card: %1, access denied").arg(uid));
        DatabaseManager::instance().addLog(uid, "RFID", false);
        WebSocketServer::instance().sendDoorStatus("RFID卡", "RFID", false);
    }

    if (m_stackedWidget->currentIndex() == PAGE_ACCESS_LOG) {
        refreshLogTable();
    }
}

void MainWindow::onRC522Error(const QString &msg)
{
    LOG_ERROR(QString("RC522 error: %1").arg(msg));
}

void MainWindow::initAHT20()
{
    LOG_INFO("Initializing AHT20 sensor...");

    m_i2c_fd = open(I2C_DEVICE, O_RDWR);
    if (m_i2c_fd < 0) {
        LOG_ERROR(QString("Failed to open I2C device: %1").arg(I2C_DEVICE));
        m_tempLabel->setText("🌡️ 传感器错误");
        m_humiLabel->setText("💧 传感器错误");
        return;
    }

    if (ioctl(m_i2c_fd, I2C_SLAVE, AHT20_ADDR) < 0) {
        LOG_ERROR("Failed to set I2C address");
        ::close(m_i2c_fd);
        m_i2c_fd = -1;
        m_tempLabel->setText("🌡️ 传感器错误");
        m_humiLabel->setText("💧 传感器错误");
        return;
    }

    unsigned char reset_cmd = 0xba;
    write(m_i2c_fd, &reset_cmd, 1);
    usleep(40000);

    LOG_INFO("AHT20 initialized successfully");

    m_tempTimer = new QTimer(this);
    connect(m_tempTimer, &QTimer::timeout, this, &MainWindow::updateTemperature);
    m_tempTimer->start(2000);
}

float MainWindow::readTemperature()
{
    if (m_i2c_fd < 0) return -999.0f;

    unsigned char cmd[3] = {0xac, 0x33, 0x00};
    unsigned char data[7];

    if (write(m_i2c_fd, cmd, 3) != 3) return -999.0f;
    usleep(80000);
    if (read(m_i2c_fd, data, 7) != 7) return -999.0f;
    if (data[0] & 0x01) return -999.0f;

    unsigned int temp_raw = ((data[3] & 0x0F) << 16) | (data[4] << 8) | data[5];
    return temp_raw * 200.0f / 1048576.0f - 50.0f;
}

float MainWindow::readHumidity()
{
    if (m_i2c_fd < 0) return -999.0f;

    unsigned char cmd[3] = {0xac, 0x33, 0x00};
    unsigned char data[7];

    if (write(m_i2c_fd, cmd, 3) != 3) return -999.0f;
    usleep(80000);
    if (read(m_i2c_fd, data, 7) != 7) return -999.0f;
    if (data[0] & 0x01) return -999.0f;

    unsigned int hum_raw = (data[1] << 12) | (data[2] << 4) | (data[3] >> 4);
    return hum_raw * 100.0f / 1048576.0f;
}

void MainWindow::updateTemperature()
{
    float temp = readTemperature();
    float humi = readHumidity();

    if (temp > -100 && temp < 100) {
        m_tempLabel->setText(QString("🌡️ %1 °C").arg(temp, 5, 'f', 1));
        m_tempLabel->setStyleSheet("color: #27ae60; font-size: 14px; font-weight: bold; padding: 5px 10px; background-color: #ecf0f1; border-radius: 8px;");
    } else {
        m_tempLabel->setText("🌡️ --.- °C");
        m_tempLabel->setStyleSheet("color: #e74c3c; font-size: 14px; font-weight: bold; padding: 5px 10px; background-color: #ecf0f1; border-radius: 8px;");
    }

    if (humi > 0 && humi < 100) {
        m_humiLabel->setText(QString("💧 %1 %").arg(humi, 5, 'f', 1));
        m_humiLabel->setStyleSheet("color: #2980b9; font-size: 14px; font-weight: bold; padding: 5px 10px; background-color: #ecf0f1; border-radius: 8px;");
    } else {
        m_humiLabel->setText("💧 --.- %");
        m_humiLabel->setStyleSheet("color: #e74c3c; font-size: 14px; font-weight: bold; padding: 5px 10px; background-color: #ecf0f1; border-radius: 8px;");
    }
}

void MainWindow::updateLockStatus(bool locked)
{
    if (locked) {
        m_lockStatusLabel->setText("🔒 已锁定");
        m_lockStatusLabel->setStyleSheet("color: #e74c3c; font-size: 14px; font-weight: bold; padding: 10px;");
    } else {
        m_lockStatusLabel->setText("🔓 已开启");
        m_lockStatusLabel->setStyleSheet("color: #2ecc71; font-size: 14px; font-weight: bold; padding: 10px;");
    }
}

void MainWindow::onFaceRecognitionClicked()
{
    m_stackedWidget->setCurrentIndex(PAGE_FACE_RECOGNITION);
    LOG_DEBUG("Switch to face recognition page");
}

void MainWindow::onPasswordInputClicked()
{
    m_stackedWidget->setCurrentIndex(PAGE_PASSWORD_INPUT);
    LOG_DEBUG("Switch to password input page");
}

void MainWindow::onChangePasswordClicked()
{
    m_stackedWidget->setCurrentIndex(PAGE_PASSWORD_CHANGE);
    LOG_DEBUG("Switch to change password page");
}

void MainWindow::onFaceTrainClicked()
{
    m_stackedWidget->setCurrentIndex(PAGE_FACE_TRAIN);
    LOG_DEBUG("Switch to face train page");
}

void MainWindow::onManualUnlockClicked()
{
    m_stackedWidget->setCurrentIndex(PAGE_MANUAL_CONTROL);
    LOG_DEBUG("Switch to manual control page");
}

void MainWindow::onAccessLogClicked()
{
    m_stackedWidget->setCurrentIndex(PAGE_ACCESS_LOG);
    refreshLogTable();
    LOG_DEBUG("Switch to access log page");
}

void MainWindow::onRemoteUnlockRequested()
{
    LOG_INFO("Remote unlock requested");
    DatabaseManager::instance().addLog("远程", "远程控制", true);
    LockController::instance().unlock();
}

void MainWindow::onRemoteLockRequested()
{
    LOG_INFO("Remote lock requested");
    DatabaseManager::instance().addLog("远程", "远程控制", true);
    LockController::instance().lock();
}

void MainWindow::onRemoteStatusRequested()
{
    LOG_DEBUG("Remote status requested");
    WebSocketServer::instance().sendLockStatus(LockController::instance().isLocked());
}

void MainWindow::onAlertTriggered(QString photoPath)
{
    LOG_WARNING(QString("Alert triggered! Photo path: %1").arg(photoPath));

    QImage frame = m_cameraThread->getCurrentFrame();
    if (!frame.isNull()) {
        QString fullPath = "/opt/smartlock/bin/" + photoPath;
        if (frame.save(fullPath)) {
            LOG_INFO(QString("Alert photo saved: %1").arg(fullPath));
        } else {
            LOG_ERROR(QString("Failed to save alert photo: %1").arg(fullPath));
        }
    } else {
        LOG_ERROR("No frame available for alert photo");
    }

    QString type, detail;
    if (photoPath.contains("unknown")) {
        type = "陌生人";
        detail = "连续多次检测到陌生人";
    } else if (photoPath.contains("password")) {
        type = "密码错误";
        detail = "连续多次密码输入错误";
    } else {
        type = "未知";
        detail = "异常行为";
    }

    DatabaseManager::instance().addAlert(type, detail, 1, photoPath);
    WebSocketServer::instance().sendAlert(type, detail, photoPath);
}
