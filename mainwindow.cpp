#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "passwordwidget.h"
#include "lockcontroller.h"
#include "passwordmanager.h"
#include "CameraThread.h"
#include "RC522Thread.h"
#include "databasemanager.h"
#include "logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <QThread>
#include <QDebug>
#include <QDateTime>
#include <QComboBox>
#include <QDir>
#include <QtConcurrent>
#include <QFile>
#include <QTextStream>
#include <QTableView>
#include <QHeaderView>
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

// 定义页面索引常量
#define PAGE_FACE_RECOGNITION  0  // 人脸识别
#define PAGE_PASSWORD_INPUT    1  // 输入密码
#define PAGE_PASSWORD_CHANGE   2  // 修改密码
#define PAGE_FACE_TRAIN        3  // 人脸训练
#define PAGE_MANUAL_CONTROL    4  // 手动调试
#define PAGE_ACCESS_LOG        5  // 开门记录

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
    , m_trainPersonList(nullptr)
    , m_trainStartBtn(nullptr)
    , m_trainModelBtn(nullptr)
    , m_trainProgressLabel(nullptr)
    , m_trainStatusLabel(nullptr)
    , m_logTableView(nullptr)
    , m_todayCountLabel(nullptr)
    , m_captureCount(0)
    , m_isCapturing(false)
{
    // 初始化日志系统
    Logger::instance().setLevel(Logger::DEBUG);
    Logger::instance().setConsoleOutput(true);
    LOG_INFO("========================================");
    LOG_INFO("SmartLock Application Starting");
    LOG_INFO("========================================");

    LOG_DEBUG("MainWindow constructor start");

    setupUI();
    LOG_DEBUG("setupUI completed");

    initDatabase();      // 初始化数据库
    initCamera();        // 初始化摄像头
    initWebSocket();     // 初始化 WebSocket
    initHttpServer();    // 添加这行

    initRC522();         // 初始化 RFID
    initAHT20();         // 初始化温湿度传感器

    // 默认显示人脸识别页面
    m_stackedWidget->setCurrentIndex(PAGE_FACE_RECOGNITION);
    m_passwordWidget->setMode(PasswordWidget::ModeInput);

    connect(&LockController::instance(), &LockController::lockStateChanged,
            this, &MainWindow::updateLockStatus);

    updateLockStatus(LockController::instance().isLocked());

    LOG_INFO(QString("MainWindow constructor completed, current page: %1").arg(m_stackedWidget->currentIndex()));
}

MainWindow::~MainWindow()
{
    LOG_INFO("MainWindow destructor called");

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
    if (m_tempTimer) {
        m_tempTimer->stop();
        delete m_tempTimer;
    }
    if (m_i2c_fd >= 0) {
        ::close(m_i2c_fd);
    }

    if (m_httpServer) {
        delete m_httpServer;
        m_httpServer = nullptr;
    }
    DatabaseManager::instance().close();
    delete ui;
}

void MainWindow::setupUI()
{
    setWindowTitle("智能门锁");
    setFixedSize(900, 550);

    QWidget *centralWidget = new QWidget(this);
    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ========== 左侧菜单栏 (200px) ==========
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

    // 菜单按钮
    QPushButton *btnFaceRecognition = new QPushButton("👤 人脸识别", this);
    QPushButton *btnPasswordInput = new QPushButton("🔐 输入密码", this);
    QPushButton *btnChangePassword = new QPushButton("🔑 修改密码", this);
    QPushButton *btnFaceTrain = new QPushButton("📸 人脸训练", this);
    QPushButton *btnManualUnlock = new QPushButton("🔧 手动调试", this);
    QPushButton *btnAccessLog = new QPushButton("📋 开门记录", this);

    QString btnStyle = "QPushButton {"
                       "background-color: #34495e;"
                       "color: white;"
                       "border: none;"
                       "padding: 12px;"
                       "border-radius: 8px;"
                       "font-size: 14px;"
                       "text-align: left;"
                       "padding-left: 20px;"
                       "}"
                       "QPushButton:hover { background-color: #3d566e; }"
                       "QPushButton:pressed { background-color: #1e2f3a; }";

    btnFaceRecognition->setStyleSheet(btnStyle);
    btnPasswordInput->setStyleSheet(btnStyle);
    btnChangePassword->setStyleSheet(btnStyle);
    btnFaceTrain->setStyleSheet(btnStyle);
    btnManualUnlock->setStyleSheet(btnStyle);
    btnAccessLog->setStyleSheet(btnStyle);

    menuLayout->addWidget(btnFaceRecognition);
    menuLayout->addWidget(btnPasswordInput);
    menuLayout->addWidget(btnChangePassword);
    menuLayout->addWidget(btnFaceTrain);
    menuLayout->addWidget(btnManualUnlock);
    menuLayout->addWidget(btnAccessLog);
    menuLayout->addStretch();

    // ========== 右侧内容区域 ==========
    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->setStyleSheet("background-color: #ecf0f1;");

    // ---------- 页面0: 人脸识别页面 ----------
    QWidget *faceRecognitionWidget = new QWidget(this);
    QVBoxLayout *faceRecognitionLayout = new QVBoxLayout(faceRecognitionWidget);
    faceRecognitionLayout->setContentsMargins(0, 0, 0, 0);

    m_cameraLabel = new QLabel(this);
    m_cameraLabel->setMinimumSize(640, 480);
    m_cameraLabel->setAlignment(Qt::AlignCenter);
    m_cameraLabel->setStyleSheet("background-color: #2c3e50; border-radius: 10px; color: white;");
    m_cameraLabel->setText("摄像头启动中...");
    faceRecognitionLayout->addWidget(m_cameraLabel);

    m_stackedWidget->addWidget(faceRecognitionWidget);

    // ---------- 页面1: 输入密码页面 ----------
    PasswordWidget *inputPwdWidget = new PasswordWidget(this);
    inputPwdWidget->setMode(PasswordWidget::ModeInput);
    m_stackedWidget->addWidget(inputPwdWidget);

    // ---------- 页面2: 修改密码页面 ----------
    PasswordWidget *changePwdWidget = new PasswordWidget(this);
    changePwdWidget->setMode(PasswordWidget::ModeChange);
    m_stackedWidget->addWidget(changePwdWidget);

    // 在创建 PasswordWidget 后添加
    connect(inputPwdWidget, &PasswordWidget::passwordAlert, this, &MainWindow::onAlertTriggered);

    // 保存指针供外部调用
    m_passwordWidget = inputPwdWidget;

    // ---------- 页面3: 人脸训练页面 ----------
    QWidget *trainWidget = new QWidget(this);
    QVBoxLayout *trainLayout = new QVBoxLayout(trainWidget);
    trainLayout->setContentsMargins(0, 0, 0, 0);
    trainLayout->setSpacing(10);

    m_trainCameraLabel = new QLabel(this);
    m_trainCameraLabel->setMinimumSize(640, 350);
    m_trainCameraLabel->setAlignment(Qt::AlignCenter);
    m_trainCameraLabel->setStyleSheet("background-color: #2c3e50; border-radius: 10px; color: white;");
    m_trainCameraLabel->setText("摄像头准备就绪");
    trainLayout->addWidget(m_trainCameraLabel);

    // 训练控制面板
    QHBoxLayout *trainControlLayout = new QHBoxLayout();
    QLabel *nameLabel = new QLabel("姓名:", this);
    m_trainNameEdit = new QLineEdit(this);
    m_trainNameEdit->setPlaceholderText("输入姓名，如：张三");
    m_trainNameEdit->setFixedWidth(150);

    m_trainPersonList = new QComboBox(this);
    m_trainPersonList->setFixedWidth(150);
    m_trainPersonList->addItem("选择已有人员");

    m_trainStartBtn = new QPushButton("📸 开始拍照", this);
    m_trainStartBtn->setStyleSheet("background-color: #27ae60; color: white; padding: 8px; border-radius: 5px;");

    m_trainProgressLabel = new QLabel("待拍照: 0/20", this);
    m_trainProgressLabel->setStyleSheet("font-weight: bold;");

    trainControlLayout->addWidget(nameLabel);
    trainControlLayout->addWidget(m_trainNameEdit);
    trainControlLayout->addWidget(m_trainPersonList);
    trainControlLayout->addWidget(m_trainStartBtn);
    trainControlLayout->addWidget(m_trainProgressLabel);
    trainControlLayout->addStretch();

    trainLayout->addLayout(trainControlLayout);

    // 训练状态和按钮
    QHBoxLayout *trainActionLayout = new QHBoxLayout();
    m_trainStatusLabel = new QLabel("就绪", this);
    m_trainStatusLabel->setStyleSheet("color: #27ae60;");

    m_trainModelBtn = new QPushButton("🚀 训练模型", this);
    m_trainModelBtn->setStyleSheet("background-color: #3498db; color: white; padding: 10px; border-radius: 5px;");

    trainActionLayout->addWidget(m_trainStatusLabel);
    trainActionLayout->addStretch();
    trainActionLayout->addWidget(m_trainModelBtn);

    trainLayout->addLayout(trainActionLayout);

    QLabel *trainTipLabel = new QLabel("💡 提示：\n• 每人需要20张不同角度的照片\n• 请保持人脸在绿框内\n• 可以转头、变换表情", this);
    trainTipLabel->setStyleSheet("color: #7f8c8d; font-size: 11px; background-color: #ecf0f1; padding: 10px; border-radius: 5px;");
    trainTipLabel->setWordWrap(true);
    trainLayout->addWidget(trainTipLabel);

    m_stackedWidget->addWidget(trainWidget);

    // ---------- 页面4: 手动调试页面 ----------
    QWidget *manualControlWidget = new QWidget(this);
    QVBoxLayout *manualLayout = new QVBoxLayout(manualControlWidget);
    manualLayout->setAlignment(Qt::AlignCenter);

    QLabel *manualTitle = new QLabel("🔧 手动控制", this);
    manualTitle->setAlignment(Qt::AlignCenter);
    manualTitle->setStyleSheet("font-size: 24px; font-weight: bold; color: #2c3e50; margin-bottom: 30px;");
    manualLayout->addWidget(manualTitle);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(20);

    QPushButton *btnUnlock = new QPushButton("🔓 解锁", this);
    QPushButton *btnLock = new QPushButton("🔒 锁定", this);
    QPushButton *btnTest = new QPushButton("🔄 测试摆动", this);

    QString bigBtnStyle = "QPushButton {"
                          "background-color: #3498db;"
                          "color: white;"
                          "border: none;"
                          "padding: 20px;"
                          "border-radius: 15px;"
                          "font-size: 18px;"
                          "font-weight: bold;"
                          "min-width: 150px;"
                          "}"
                          "QPushButton:hover { background-color: #2980b9; }"
                          "QPushButton:pressed { background-color: #1c6ea4; }";

    btnUnlock->setStyleSheet(bigBtnStyle);
    btnLock->setStyleSheet(bigBtnStyle);
    btnTest->setStyleSheet(bigBtnStyle);

    buttonLayout->addWidget(btnUnlock);
    buttonLayout->addWidget(btnLock);
    buttonLayout->addWidget(btnTest);
    manualLayout->addLayout(buttonLayout);

    QLabel *infoLabel = new QLabel("提示：解锁（90°）| 锁定（0°）| 测试（0°→90°→180°循环）", this);
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setStyleSheet("color: #7f8c8d; margin-top: 30px;");
    manualLayout->addWidget(infoLabel);

    m_stackedWidget->addWidget(manualControlWidget);

    // ---------- 页面5: 开门记录页面 ----------
    QWidget *logWidget = new QWidget(this);
    QVBoxLayout *logLayout = new QVBoxLayout(logWidget);
    logLayout->setContentsMargins(10, 10, 10, 10);

    // 标题栏
    QHBoxLayout *logTitleLayout = new QHBoxLayout();
    QLabel *logTitleLabel = new QLabel("📋 开门记录", this);
    logTitleLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: #2c3e50;");

    QPushButton *btnRefresh = new QPushButton("🔄 刷新", this);
    btnRefresh->setFixedWidth(100);
    btnRefresh->setStyleSheet("background-color: #3498db; color: white; padding: 5px; border-radius: 5px;");

    QPushButton *btnToday = new QPushButton("📅 今日", this);
    btnToday->setFixedWidth(100);
    btnToday->setStyleSheet("background-color: #27ae60; color: white; padding: 5px; border-radius: 5px;");

    logTitleLayout->addWidget(logTitleLabel);
    logTitleLayout->addStretch();
    logTitleLayout->addWidget(btnRefresh);
    logTitleLayout->addWidget(btnToday);
    logLayout->addLayout(logTitleLayout);

    // 统计信息栏
    QHBoxLayout *statsLayout = new QHBoxLayout();
    m_todayCountLabel = new QLabel("今日开门: 0次", this);
    m_todayCountLabel->setStyleSheet("background-color: #27ae60; color: white; padding: 5px 15px; border-radius: 10px; font-weight: bold;");
    statsLayout->addWidget(m_todayCountLabel);
    statsLayout->addStretch();
    logLayout->addLayout(statsLayout);

    // 表格显示日志
    m_logTableView = new QTableView(this);
    m_logTableView->setAlternatingRowColors(true);
    m_logTableView->setStyleSheet("QTableView::item { padding: 8px; }"
                                  "QHeaderView::section { background-color: #34495e; color: white; padding: 5px; }");
    m_logTableView->horizontalHeader()->setStretchLastSection(true);
    logLayout->addWidget(m_logTableView);

    m_stackedWidget->addWidget(logWidget);

    // ========== 右上角状态栏 ==========
    QWidget *titleBar = new QWidget(this);
    QHBoxLayout *titleBarLayout = new QHBoxLayout(titleBar);
    titleBarLayout->setSpacing(20);

    m_tempLabel = new QLabel("🌡️ --.- °C", this);
    m_tempLabel->setAlignment(Qt::AlignCenter);
    m_tempLabel->setStyleSheet("color: #2c3e50; font-size: 14px; font-weight: bold; padding: 5px 10px; background-color: #ecf0f1; border-radius: 8px;");

    m_humiLabel = new QLabel("💧 --.- %", this);
    m_humiLabel->setAlignment(Qt::AlignCenter);
    m_humiLabel->setStyleSheet("color: #2c3e50; font-size: 14px; font-weight: bold; padding: 5px 10px; background-color: #ecf0f1; border-radius: 8px;");

    m_lockStatusLabel = new QLabel("🔒 已锁定", this);
    m_lockStatusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_lockStatusLabel->setStyleSheet("color: #e74c3c; font-size: 14px; font-weight: bold; padding: 10px;");

    titleBarLayout->addWidget(m_tempLabel);
    titleBarLayout->addWidget(m_humiLabel);
    titleBarLayout->addStretch();
    titleBarLayout->addWidget(m_lockStatusLabel);
    titleBarLayout->setContentsMargins(10, 5, 10, 0);

    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->addWidget(titleBar);
    rightLayout->addWidget(m_stackedWidget, 1);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    QWidget *rightWidget = new QWidget(this);
    rightWidget->setLayout(rightLayout);

    mainLayout->addWidget(leftMenu);
    mainLayout->addWidget(rightWidget, 1);
    setCentralWidget(centralWidget);

    // ========== 连接信号 ==========
    connect(btnFaceRecognition, &QPushButton::clicked, [this]() {
        m_stackedWidget->setCurrentIndex(PAGE_FACE_RECOGNITION);
        LOG_DEBUG("Switch to page 0: Face Recognition");
    });

    connect(btnPasswordInput, &QPushButton::clicked, [this]() {
        m_stackedWidget->setCurrentIndex(PAGE_PASSWORD_INPUT);
        LOG_DEBUG("Switch to page 1: Password Input");
    });

    connect(btnChangePassword, &QPushButton::clicked, [this]() {
        m_stackedWidget->setCurrentIndex(PAGE_PASSWORD_CHANGE);
        LOG_DEBUG("Switch to page 2: Password Change");
    });

    connect(btnFaceTrain, &QPushButton::clicked, [this]() {
        m_stackedWidget->setCurrentIndex(PAGE_FACE_TRAIN);
        loadPersonList();
        LOG_DEBUG("Switch to page 3: Face Train");
    });

    connect(btnManualUnlock, &QPushButton::clicked, [this]() {
        m_stackedWidget->setCurrentIndex(PAGE_MANUAL_CONTROL);
        LOG_DEBUG("Switch to page 4: Manual Control");
    });

    connect(btnAccessLog, &QPushButton::clicked, [this]() {
        m_stackedWidget->setCurrentIndex(PAGE_ACCESS_LOG);
        refreshLogTable();
        LOG_DEBUG("Switch to page 5: Access Log");
    });

    connect(btnRefresh, &QPushButton::clicked, [this]() {
        refreshLogTable();
    });

    connect(btnToday, &QPushButton::clicked, [this]() {
        refreshTodayLogTable();
    });

    connect(btnUnlock, &QPushButton::clicked, [this]() {
        DatabaseManager::instance().addLog("手动", "手动", true);
        LockController::instance().unlock();
        QMessageBox::information(this, "提示", "正在解锁...");
    });

    connect(btnLock, &QPushButton::clicked, [this]() {
        LockController::instance().lock();
        QMessageBox::information(this, "提示", "正在锁定...");
    });

    connect(btnTest, &QPushButton::clicked, [this]() {
        QMessageBox::information(this, "提示", "开始测试，舵机将循环摆动");
        QTimer::singleShot(100, []() {
            LockController::instance().setServoAngle(0);
            QThread::msleep(500);
            LockController::instance().setServoAngle(90);
            QThread::msleep(500);
            LockController::instance().setServoAngle(180);
            QThread::msleep(500);
            LockController::instance().setServoAngle(90);
        });
    });

    // 训练按钮连接
    connect(m_trainStartBtn, &QPushButton::clicked, this, &MainWindow::onStartCapture);
    connect(m_trainModelBtn, &QPushButton::clicked, this, &MainWindow::onTrainModel);
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

void MainWindow::onFrameReady(const QImage &image)
{
    QPixmap pixmap = QPixmap::fromImage(image);
    QPixmap scaled = pixmap.scaled(640, 480, Qt::KeepAspectRatio, Qt::FastTransformation);

    int currentPage = m_stackedWidget->currentIndex();

    if (currentPage == PAGE_FACE_RECOGNITION) {
        if (m_cameraLabel) {
            m_cameraLabel->setPixmap(scaled);
        }
    } else if (currentPage == PAGE_FACE_TRAIN) {
        if (m_trainCameraLabel) {
            m_trainCameraLabel->setPixmap(scaled);
        }
    }
}

void MainWindow::loadPersonList()
{
    m_trainPersonList->clear();
    m_trainPersonList->addItem("选择已有人员");

    QDir trainDir("data/train");
    if (!trainDir.exists()) {
        trainDir.mkpath(".");
        return;
    }

    QStringList persons = trainDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &person : persons) {
        QDir personDir(QString("data/train/%1").arg(person));
        int photoCount = personDir.entryList(QStringList() << "*.jpg", QDir::Files).size();
        m_trainPersonList->addItem(QString("%1 (%2张)").arg(person).arg(photoCount));
    }
}

void MainWindow::onStartCapture()
{
    QString personName = m_trainNameEdit->text().trimmed();

    if (!personName.isEmpty()) {
        m_currentPerson = personName;
    } else if (m_trainPersonList->currentIndex() > 0) {
        QString text = m_trainPersonList->currentText();
        m_currentPerson = text.split(" (").first();
    } else {
        QMessageBox::warning(this, "提示", "请选择人员或输入新名字");
        return;
    }

    m_captureCount = 0;
    m_isCapturing = true;

    QDir dir;
    dir.mkpath(QString("data/train/%1").arg(m_currentPerson));

    m_trainStatusLabel->setText(QString("正在为 【%1】 拍照...").arg(m_currentPerson));
    m_trainStatusLabel->setStyleSheet("color: #e74c3c;");
    m_trainStartBtn->setEnabled(false);
    m_trainModelBtn->setEnabled(false);
    m_trainNameEdit->setEnabled(false);
    m_trainPersonList->setEnabled(false);
    m_trainProgressLabel->setText("准备拍照...");

    QTimer::singleShot(1000, this, &MainWindow::onSaveFace);
}

void MainWindow::onSaveFace()
{
    if (!m_isCapturing) return;

    if (m_captureCount >= 20) {
        m_isCapturing = false;
        m_trainStatusLabel->setText(QString("✅ %1 拍照完成！共 %2 张照片").arg(m_currentPerson).arg(m_captureCount));
        m_trainStatusLabel->setStyleSheet("color: #27ae60;");
        m_trainStartBtn->setEnabled(true);
        m_trainModelBtn->setEnabled(true);
        m_trainNameEdit->setEnabled(true);
        m_trainPersonList->setEnabled(true);
        loadPersonList();
        m_trainProgressLabel->setText("拍照完成！");
        return;
    }

    cv::Mat face = m_cameraThread->getCurrentFace();
    if (face.empty()) {
        m_trainProgressLabel->setText(QString("未检测到人脸... (%1/20)").arg(m_captureCount));
        QTimer::singleShot(500, this, &MainWindow::onSaveFace);
        return;
    }

    QString filename = QString("data/train/%1/%2.jpg")
                           .arg(m_currentPerson)
                           .arg(m_captureCount + 1, 3, 10, QChar('0'));

    cv::Mat faceResized;
    cv::resize(face, faceResized, cv::Size(100, 100));
    cv::imwrite(filename.toStdString(), faceResized);

    m_captureCount++;
    m_trainProgressLabel->setText(QString("已拍照 %1/20").arg(m_captureCount));

    QTimer::singleShot(500, this, &MainWindow::onSaveFace);
}

void MainWindow::onTrainModel()
{
    m_trainStatusLabel->setText("正在训练模型，请稍候...");
    m_trainStatusLabel->setStyleSheet("color: #f39c12;");
    m_trainModelBtn->setEnabled(false);
    m_trainStartBtn->setEnabled(false);

    QtConcurrent::run([this]() {
        try {
            std::vector<cv::Mat> images;
            std::vector<int> labels;
            std::map<int, QString> labelToName;

            QDir trainDir("data/train");
            QStringList persons = trainDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

            if (persons.isEmpty()) {
                QMetaObject::invokeMethod(this, [this]() {
                    QMessageBox::warning(this, "错误", "没有找到训练图片！请先拍照。");
                    m_trainModelBtn->setEnabled(true);
                    m_trainStartBtn->setEnabled(true);
                    m_trainStatusLabel->setText("训练失败");
                });
                return;
            }

            int labelId = 0;
            for (const QString &person : persons) {
                QDir personDir(QString("data/train/%1").arg(person));
                QStringList imageFiles = personDir.entryList(QStringList() << "*.jpg", QDir::Files);

                for (const QString &imageFile : imageFiles) {
                    QString fullPath = QString("data/train/%1/%2").arg(person).arg(imageFile);
                    cv::Mat img = cv::imread(fullPath.toStdString(), cv::IMREAD_GRAYSCALE);
                    if (!img.empty()) {
                        if (img.cols != 100 || img.rows != 100) {
                            cv::resize(img, img, cv::Size(100, 100));
                        }
                        images.push_back(img);
                        labels.push_back(labelId);
                    }
                }
                labelToName[labelId] = person;
                labelId++;
            }

            if (images.empty()) {
                QMetaObject::invokeMethod(this, [this]() {
                    QMessageBox::warning(this, "错误", "没有找到有效的训练图片！");
                    m_trainModelBtn->setEnabled(true);
                    m_trainStartBtn->setEnabled(true);
                });
                return;
            }

            auto recognizer = cv::face::LBPHFaceRecognizer::create();
            recognizer->train(images, labels);

            QDir().mkpath("data/models");
            recognizer->write("data/models/face_model.yml");

            QFile mappingFile("data/models/label_mapping.txt");
            if (mappingFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&mappingFile);
                for (const auto &pair : labelToName) {
                    out << pair.first << "=" << pair.second << "\n";
                }
                mappingFile.close();
            }

            int personCount = labelToName.size();
            QMetaObject::invokeMethod(this, [this, personCount]() {
                m_trainStatusLabel->setText(QString("✅ 训练完成！共训练 %1 人").arg(personCount));
                m_trainStatusLabel->setStyleSheet("color: #27ae60;");
                m_trainModelBtn->setEnabled(true);
                m_trainStartBtn->setEnabled(true);
                QMessageBox::information(this, "成功", QString("模型训练完成！\n共训练 %1 人").arg(personCount));
            });

        } catch (const std::exception &e) {
            QString errorMsg = QString::fromStdString(e.what());
            QMetaObject::invokeMethod(this, [this, errorMsg]() {
                QMessageBox::critical(this, "错误", QString("训练失败：%1").arg(errorMsg));
                m_trainModelBtn->setEnabled(true);
                m_trainStartBtn->setEnabled(true);
                m_trainStatusLabel->setText("训练失败");
            });
        }
    });
}

void MainWindow::initCamera()
{
    LOG_INFO("Starting camera thread...");
    m_cameraThread = new CameraThread(this);
    connect(m_cameraThread, &CameraThread::frameReady, this, &MainWindow::onFrameReady);
    connect(m_cameraThread, &CameraThread::error, this, &MainWindow::onCameraError);
    connect(m_cameraThread, &CameraThread::faceDetected, this, &MainWindow::onFaceDetected);
    connect(m_cameraThread, &CameraThread::faceRecognized, this, &MainWindow::onFaceRecognized);
    connect(m_cameraThread, &CameraThread::unknownFaceAlert, this, &MainWindow::onAlertTriggered);
    m_cameraThread->start();
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
    if (m_cameraLabel) {
        m_cameraLabel->setText("摄像头打开失败！\n" + msg);
    }
    if (m_trainCameraLabel) {
        m_trainCameraLabel->setText("摄像头打开失败！\n" + msg);
    }
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
    m_stackedWidget->setCurrentIndex(0);
    LOG_DEBUG("Switch to face recognition page");
}

void MainWindow::onPasswordInputClicked()
{
    m_stackedWidget->setCurrentIndex(1);
    LOG_DEBUG("Switch to password input page");
}

void MainWindow::onChangePasswordClicked()
{
    m_stackedWidget->setCurrentIndex(2);
    LOG_DEBUG("Switch to change password page");
}

void MainWindow::onFaceTrainClicked()
{
    m_stackedWidget->setCurrentIndex(3);
    loadPersonList();
    LOG_DEBUG("Switch to face train page");
}

void MainWindow::onManualUnlockClicked()
{
    m_stackedWidget->setCurrentIndex(4);
    LOG_DEBUG("Switch to manual control page");
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