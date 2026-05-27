#include "trainwindow.h"
#include "CameraThread.h"
#include "logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDir>
#include <QDebug>
#include <QtConcurrent>
#include <QFile>
#include <QTextStream>

TrainWindow::TrainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_cameraThread(nullptr)
    , m_captureCount(0)
{
    LOG_INFO("TrainWindow constructor start");

    setupUI();

    // 启动摄像头线程
    m_cameraThread = new CameraThread(this);
    connect(m_cameraThread, &CameraThread::frameReady, this, &TrainWindow::onFrameReady);
    connect(m_cameraThread, &CameraThread::faceDetected, this, &TrainWindow::onFaceDetected);
    m_cameraThread->start();

    // 加载已有人员列表
    onLoadPersonList();

    LOG_INFO("TrainWindow constructor completed");
}

TrainWindow::~TrainWindow()
{
    LOG_INFO("TrainWindow destructor called");
    if (m_cameraThread) {
        m_cameraThread->stop();
        m_cameraThread->wait();
    }
}

void TrainWindow::setupUI()
{
    setWindowTitle("人脸训练工具");
    setFixedSize(1000, 650);

    QWidget *central = new QWidget(this);
    QHBoxLayout *mainLayout = new QHBoxLayout(central);

    // ========== 左侧：摄像头画面 ==========
    QWidget *cameraWidget = new QWidget(this);
    QVBoxLayout *cameraLayout = new QVBoxLayout(cameraWidget);

    m_videoLabel = new QLabel(this);
    m_videoLabel->setMinimumSize(640, 480);
    m_videoLabel->setStyleSheet("background-color: #2c3e50; border-radius: 10px;");
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setText("摄像头启动中...");
    cameraLayout->addWidget(m_videoLabel);

    m_statusLabel = new QLabel("请选择人员或输入新名字，然后点击'开始拍照'", this);
    m_statusLabel->setStyleSheet("color: #27ae60; padding: 10px; font-size: 12px;");
    m_statusLabel->setWordWrap(true);
    cameraLayout->addWidget(m_statusLabel);

    // ========== 右侧：控制面板 ==========
    QWidget *controlWidget = new QWidget(this);
    controlWidget->setFixedWidth(300);
    QVBoxLayout *controlLayout = new QVBoxLayout(controlWidget);
    controlLayout->setSpacing(15);

    // 标题
    QLabel *titleLabel = new QLabel("👤 人脸管理", this);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #2c3e50;");
    controlLayout->addWidget(titleLabel);

    // 已有人员列表
    QLabel *listLabel = new QLabel("已有人员:", this);
    listLabel->setStyleSheet("font-weight: bold; margin-top: 10px;");
    controlLayout->addWidget(listLabel);

    m_personList = new QListWidget(this);
    m_personList->setStyleSheet("border: 1px solid #bdc3c7; border-radius: 5px; padding: 5px;");
    m_personList->setMaximumHeight(150);
    controlLayout->addWidget(m_personList);

    // 新人员输入
    QLabel *newLabel = new QLabel("新人员姓名:", this);
    newLabel->setStyleSheet("font-weight: bold; margin-top: 10px;");
    controlLayout->addWidget(newLabel);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("输入姓名，如：张三");
    m_nameEdit->setStyleSheet("padding: 8px; border: 1px solid #bdc3c7; border-radius: 5px;");
    controlLayout->addWidget(m_nameEdit);

    controlLayout->addSpacing(20);

    // 按钮
    m_captureBtn = new QPushButton("📸 开始拍照 (20张)", this);
    m_captureBtn->setStyleSheet("QPushButton { background-color: #27ae60; color: white; padding: 10px; border-radius: 5px; font-size: 14px; }"
                                "QPushButton:hover { background-color: #2ecc71; }"
                                "QPushButton:disabled { background-color: #95a5a6; }");
    controlLayout->addWidget(m_captureBtn);

    m_trainBtn = new QPushButton("🚀 训练模型", this);
    m_trainBtn->setStyleSheet("QPushButton { background-color: #3498db; color: white; padding: 10px; border-radius: 5px; font-size: 14px; }"
                              "QPushButton:hover { background-color: #2980b9; }");
    controlLayout->addWidget(m_trainBtn);

    QPushButton *btnRefresh = new QPushButton("🔄 刷新列表", this);
    btnRefresh->setStyleSheet("QPushButton { background-color: #95a5a6; color: white; padding: 8px; border-radius: 5px; }"
                              "QPushButton:hover { background-color: #7f8c8d; }");
    controlLayout->addWidget(btnRefresh);

    QPushButton *btnBack = new QPushButton("🔙 返回主界面", this);
    btnBack->setStyleSheet("QPushButton { background-color: #e74c3c; color: white; padding: 10px; border-radius: 5px; }"
                           "QPushButton:hover { background-color: #c0392b; }");
    controlLayout->addWidget(btnBack);

    controlLayout->addStretch();

    // 提示信息
    QLabel *tipLabel = new QLabel("💡 提示：\n• 每人需要20张不同角度的照片\n• 请保持人脸在绿框内\n• 可以转头、变换表情", this);
    tipLabel->setStyleSheet("color: #7f8c8d; font-size: 11px; background-color: #ecf0f1; padding: 10px; border-radius: 5px;");
    tipLabel->setWordWrap(true);
    controlLayout->addWidget(tipLabel);

    mainLayout->addWidget(cameraWidget);
    mainLayout->addWidget(controlWidget);
    setCentralWidget(central);

    // 连接信号
    connect(m_captureBtn, &QPushButton::clicked, this, &TrainWindow::onStartCapture);
    connect(m_trainBtn, &QPushButton::clicked, this, &TrainWindow::onTrainModel);
    connect(btnRefresh, &QPushButton::clicked, this, &TrainWindow::onLoadPersonList);
    connect(btnBack, &QPushButton::clicked, this, &TrainWindow::close);

    LOG_DEBUG("TrainWindow UI setup completed");
}

void TrainWindow::onLoadPersonList()
{
    m_personList->clear();

    QDir trainDir("data/train");
    if (!trainDir.exists()) {
        trainDir.mkpath(".");
        return;
    }

    QStringList persons = trainDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    LOG_DEBUG(QString("Loading person list, found %1 persons").arg(persons.size()));

    for (const QString &person : persons) {
        QDir personDir(QString("data/train/%1").arg(person));
        int photoCount = personDir.entryList(QStringList() << "*.jpg", QDir::Files).size();
        m_personList->addItem(QString("%1 (%2张)").arg(person).arg(photoCount));
    }
}

void TrainWindow::onStartCapture()
{
    QString personName = m_nameEdit->text().trimmed();

    if (!personName.isEmpty()) {
        m_currentPerson = personName;
        LOG_INFO(QString("Starting capture for new person: %1").arg(m_currentPerson));
    } else {
        QListWidgetItem *item = m_personList->currentItem();
        if (!item) {
            LOG_WARNING("No person selected and no name entered");
            QMessageBox::warning(this, "提示", "请选择人员或输入新名字");
            return;
        }
        QString text = item->text();
        m_currentPerson = text.split(" (").first();
        LOG_INFO(QString("Starting capture for existing person: %1").arg(m_currentPerson));
    }

    m_captureCount = 0;

    QDir dir;
    dir.mkpath(QString("data/train/%1").arg(m_currentPerson));

    m_statusLabel->setText(QString("正在为 【%1】 拍照，请保持人脸在框内...").arg(m_currentPerson));
    m_statusLabel->setStyleSheet("color: #e74c3c; padding: 10px; font-weight: bold;");
    m_captureBtn->setEnabled(false);
    m_trainBtn->setEnabled(false);
    m_nameEdit->setEnabled(false);
    m_personList->setEnabled(false);

    QTimer::singleShot(1000, this, &TrainWindow::onSaveFace);
}

void TrainWindow::onSaveFace()
{
    if (m_captureCount >= NEED_COUNT) {
        LOG_INFO(QString("Capture completed for %1, %2 photos saved").arg(m_currentPerson).arg(m_captureCount));

        m_statusLabel->setText(QString("✅ %1 拍照完成！共 %2 张照片，请点击'训练模型'")
                                   .arg(m_currentPerson).arg(m_captureCount));
        m_statusLabel->setStyleSheet("color: #27ae60; padding: 10px; font-weight: bold;");
        m_captureBtn->setEnabled(true);
        m_trainBtn->setEnabled(true);
        m_nameEdit->setEnabled(true);
        m_personList->setEnabled(true);
        onLoadPersonList();
        return;
    }

    if (m_currentFace.empty()) {
        m_statusLabel->setText(QString("未检测到人脸，请面对摄像头... (%1/20)").arg(m_captureCount));
        QTimer::singleShot(500, this, &TrainWindow::onSaveFace);
        return;
    }

    QString filename = QString("data/train/%1/%2.jpg")
                           .arg(m_currentPerson)
                           .arg(m_captureCount + 1, 3, 10, QChar('0'));

    cv::Mat faceResized;
    cv::resize(m_currentFace, faceResized, cv::Size(100, 100));
    cv::imwrite(filename.toStdString(), faceResized);

    LOG_DEBUG(QString("Photo saved: %1").arg(filename));

    m_captureCount++;
    m_statusLabel->setText(QString("📸 已拍照 %1 / 20").arg(m_captureCount));

    QTimer::singleShot(500, this, &TrainWindow::onSaveFace);
}

void TrainWindow::onTrainModel()
{
    LOG_INFO("Starting model training");

    m_statusLabel->setText("正在训练模型，请稍候...");
    m_statusLabel->setStyleSheet("color: #f39c12; padding: 10px; font-weight: bold;");
    m_trainBtn->setEnabled(false);
    m_captureBtn->setEnabled(false);

    QtConcurrent::run([this]() {
        try {
            std::vector<cv::Mat> images;
            std::vector<int> labels;
            std::map<int, QString> labelToName;

            QDir trainDir("data/train");
            QStringList persons = trainDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

            if (persons.isEmpty()) {
                QMetaObject::invokeMethod(this, [this]() {
                    LOG_WARNING("No training images found");
                    QMessageBox::warning(this, "错误", "没有找到训练图片！请先拍照。");
                    m_trainBtn->setEnabled(true);
                    m_captureBtn->setEnabled(true);
                });
                return;
            }

            LOG_INFO(QString("Found %1 persons for training").arg(persons.size()));

            int labelId = 0;
            for (const QString &person : persons) {
                QDir personDir(QString("data/train/%1").arg(person));
                QStringList imageFiles = personDir.entryList(QStringList() << "*.jpg", QDir::Files);

                LOG_DEBUG(QString("Person %1: %2 images").arg(person).arg(imageFiles.size()));

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
                    LOG_WARNING("No valid training images found");
                    QMessageBox::warning(this, "错误", "没有找到有效的训练图片！");
                    m_trainBtn->setEnabled(true);
                    m_captureBtn->setEnabled(true);
                });
                return;
            }

            LOG_INFO(QString("Training with %1 images").arg(images.size()));

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
            LOG_INFO(QString("Training completed! %1 persons trained").arg(personCount));

            QMetaObject::invokeMethod(this, [this, personCount]() {
                m_statusLabel->setText(QString("✅ 训练完成！共训练 %1 人，模型已保存").arg(personCount));
                m_statusLabel->setStyleSheet("color: #27ae60; padding: 10px; font-weight: bold;");
                m_trainBtn->setEnabled(true);
                m_captureBtn->setEnabled(true);
                QMessageBox::information(this, "训练成功",
                                         QString("模型训练完成！\n共训练 %1 人\n模型保存在 data/models/face_model.yml")
                                             .arg(personCount));
            });

        } catch (const std::exception &e) {
            QString errorMsg = QString::fromStdString(e.what());
            LOG_ERROR(QString("Training failed: %1").arg(errorMsg));
            QMetaObject::invokeMethod(this, [this, errorMsg]() {
                QMessageBox::critical(this, "错误", QString("训练失败：%1").arg(errorMsg));
                m_trainBtn->setEnabled(true);
                m_captureBtn->setEnabled(true);
            });
        }
    });
}

void TrainWindow::onFrameReady(const QImage &image)
{
    if (m_videoLabel) {
        QPixmap pixmap = QPixmap::fromImage(image);
        m_videoLabel->setPixmap(pixmap.scaled(m_videoLabel->size(),
                                              Qt::KeepAspectRatio,
                                              Qt::FastTransformation));
    }
}

void TrainWindow::onFaceDetected(int x, int y, int w, int h)
{
    Q_UNUSED(x);
    Q_UNUSED(y);
    Q_UNUSED(w);
    Q_UNUSED(h);
}

void TrainWindow::saveFaceImage(const cv::Mat &face, const QString &personName)
{
    QString dirPath = QString("data/train/%1").arg(personName);
    QDir().mkpath(dirPath);

    int count = QDir(dirPath).entryList(QStringList() << "*.jpg", QDir::Files).size();
    QString filename = QString("%1/%2.jpg").arg(dirPath).arg(count + 1, 3, 10, QChar('0'));
    cv::imwrite(filename.toStdString(), face);

    LOG_DEBUG(QString("Face image saved: %1").arg(filename));
}