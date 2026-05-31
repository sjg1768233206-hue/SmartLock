#include "CameraThread.h"
#include "lockcontroller.h"
#include "databasemanager.h"
#include "logger.h"
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QMetaObject>
#include <QCoreApplication>

CameraThread::CameraThread(QObject *parent)
    : QThread(parent)
    , m_running(false)
    , m_faceDetectionEnabled(true)
    , m_recognizerInitialized(false)
    , m_dbLoaded(false)
    , m_unknownFaceCount(0)
{
    m_lastUnlockTime = QTime::currentTime().addSecs(-3);
}

CameraThread::~CameraThread()
{
    stop();
    wait();
}

void CameraThread::stop()
{
    m_running = false;
}


bool CameraThread::initFaceDetection()
{
    LOG_INFO("Initializing face detection...");

    // 初始化 OpenCV 人脸检测器
    QString cascadePath = "/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml";

    if (!QFile::exists(cascadePath)) {
        LOG_ERROR(QString("Cascade file not found: %1").arg(cascadePath));
        m_faceDetectionEnabled = false;
        return false;
    }

    if (!m_faceCascade.load(cascadePath.toStdString())) {
        LOG_ERROR("Failed to load face cascade!");
        m_faceDetectionEnabled = false;
        return false;
    }

    LOG_INFO("OpenCV face detection initialized");

    // 初始化 NPU 识别器 (MobileFaceNet)
    QString recogPath = "/home/cat/smartlock/model/mobilefacenet.rknn";
    if (QFile::exists(recogPath) && m_recognizer.init(recogPath.toStdString())) {
        LOG_INFO("MobileFaceNet recognizer initialized");
        m_recognizerInitialized = true;
    } else {
        LOG_WARNING("Recognizer not available");
    }

    // 加载特征数据库
    QString dbPath = "/opt/smartlock/bin/data/features.db";
    if (FeatureDatabase::instance().load()) {
        m_dbLoaded = true;
        LOG_INFO(QString("Loaded %1 users from database").arg(FeatureDatabase::instance().getAllUserNames().size()));
    } else {
        LOG_WARNING("No feature database found");
        FeatureDatabase::instance().setThreshold(0.4f);
    }

    m_faceDetectionEnabled = true;
    return true;
}

std::vector<float> CameraThread::extractFeatureForEnroll(const cv::Mat& face)
{
    if (!m_recognizerInitialized || face.empty()) {
        return std::vector<float>();
    }
    return m_recognizer.extractFeature(face);
}

void CameraThread::detectAndDrawFaces(cv::Mat &frame)
{
    if (!m_faceDetectionEnabled) {
        return;
    }

    if (m_faceCascade.empty()) {
        return;
    }

    // 1. OpenCV 人脸检测
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_RGB2GRAY);
    cv::equalizeHist(gray, gray);

    std::vector<cv::Rect> faces;
    m_faceCascade.detectMultiScale(gray, faces, 1.1, 3, 0, cv::Size(60, 60));

    LOG_DEBUG(QString("OpenCV detected %1 faces").arg(faces.size()));

    bool hasRecognizedFace = false;

    for (const auto& face : faces) {
        // 绘制绿色边框
        cv::rectangle(frame, face, cv::Scalar(0, 255, 0), 2);

        QString displayName = "Unknown";
        cv::Scalar textColor = cv::Scalar(0, 0, 255);

        // 2. NPU 识别
        if (m_recognizerInitialized && face.width > 60 && face.height > 60) {
            cv::Mat faceROI = frame(face);
            std::vector<float> features = m_recognizer.extractFeature(faceROI);

            if (!features.empty()) {
                float bestScore = 0.0f;
                QString name = FeatureDatabase::instance().recognize(features, bestScore);

                if (!name.isEmpty() && bestScore > FeatureDatabase::instance().getThreshold()) {
                    displayName = name;
                    textColor = cv::Scalar(0, 255, 0);
                    hasRecognizedFace = true;

                    LOG_INFO(QString("Recognized: %1, score=%2").arg(name).arg(bestScore));

                    // 开锁防抖（3秒内只开锁一次）
                    int elapsed = m_lastUnlockTime.msecsTo(QTime::currentTime());
                    if (elapsed > 3000) {
                        m_lastUnlockTime = QTime::currentTime();

                        QString nameCopy = name;
                        QMetaObject::invokeMethod(qApp, [nameCopy]() {
                            DatabaseManager::instance().addLog(nameCopy, "人脸", true);
                            LockController::instance().unlock();
                        }, Qt::QueuedConnection);

                        emit faceRecognized(name);
                    }
                }
            }
        }

        // 绘制姓名标签
        cv::putText(frame, displayName.toStdString(),
                    cv::Point(face.x, face.y - 10),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, textColor, 2);

        // ========== 保存人脸图像（用于注册）- 扩展区域 ==========
        if (face.width > 50 && face.height > 50) {  // 降低阈值到 50x50
            // 扩展人脸区域（扩大 40%，包含更多头部区域）
            int expandX = face.width * 0.4;   // 水平扩展 40%
            int expandY = face.height * 0.5;  // 垂直扩展 50%（多包含额头和下巴）

            int x = std::max(0, face.x - expandX);
            int y = std::max(0, face.y - expandY);
            int w = std::min(frame.cols - x, face.width + expandX * 2);
            int h = std::min(frame.rows - y, face.height + expandY * 2);

            cv::Rect expandedFace(x, y, w, h);

            // 确保区域有效
            if (expandedFace.width > 0 && expandedFace.height > 0) {
                cv::Mat grayFace;
                cv::cvtColor(frame(expandedFace), grayFace, cv::COLOR_RGB2GRAY);
                {
                    QMutexLocker locker(&m_faceMutex);
                    m_currentFace = grayFace;
                }
                LOG_DEBUG(QString("Saved face region: %1x%2 (expanded from %3x%4)")
                          .arg(expandedFace.width).arg(expandedFace.height)
                          .arg(face.width).arg(face.height));
            } else {
                // 如果扩展后无效，使用原始区域
                cv::Mat grayFace;
                cv::cvtColor(frame(face), grayFace, cv::COLOR_RGB2GRAY);
                {
                    QMutexLocker locker(&m_faceMutex);
                    m_currentFace = grayFace;
                }
                LOG_DEBUG(QString("Saved original face region: %1x%2")
                          .arg(face.width).arg(face.height));
            }
        }

        emit faceDetected(face.x, face.y, face.width, face.height);
    }

    // 陌生人报警逻辑
    if (hasRecognizedFace) {
        m_unknownFaceCount = 0;
    } else if (!faces.empty()) {
        m_unknownFaceCount++;
        LOG_DEBUG(QString("Unknown face count: %1").arg(m_unknownFaceCount));

        if (m_unknownFaceCount >= 5) {
            m_unknownFaceCount = 0;

            QString photoPath = QString("data/alerts/unknown_%1.jpg")
                                    .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
            QDir().mkpath("data/alerts");

            QString fullPath = "/opt/smartlock/bin/" + photoPath;
            if (!frame.empty()) {
                // frame 已经是 RGB 格式，不需要再转换
                QImage qimg(frame.data, frame.cols, frame.rows,
                            frame.step, QImage::Format_RGB888);
                if (qimg.save(fullPath)) {
                    LOG_INFO(QString("Alert photo saved: %1").arg(fullPath));
                } else {
                    LOG_ERROR("Failed to save alert photo");
                }
            }

            emit unknownFaceAlert(photoPath);
        }
    }

    // 显示人脸数量
    if (!faces.empty()) {
        std::string text = "Faces: " + std::to_string(faces.size());
        cv::putText(frame, text, cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
    }
}
void CameraThread::run()
{
    LOG_INFO("CameraThread started");
    m_running = true;

    // 打开摄像头
    if (!m_cap.open(0, cv::CAP_V4L2)) {
        LOG_ERROR("Failed to open camera");
        emit error("摄像头打开失败！");
        return;
    }

    LOG_INFO("Camera opened successfully");
    m_cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    m_cap.set(cv::CAP_PROP_FPS, 15);

    // 摄像头参数优化
    system("v4l2-ctl -d /dev/video0 --set-ctrl=exposure_auto=1 2>/dev/null");
    system("v4l2-ctl -d /dev/video0 --set-ctrl=exposure_absolute=650 2>/dev/null");
    system("v4l2-ctl -d /dev/video0 --set-ctrl=gain=65 2>/dev/null");
    system("v4l2-ctl -d /dev/video0 --set-ctrl=brightness=18 2>/dev/null");
    system("v4l2-ctl -d /dev/video0 --set-ctrl=contrast=48 2>/dev/null");
    system("v4l2-ctl -d /dev/video0 --set-ctrl=backlight_compensation=160 2>/dev/null");
    system("v4l2-ctl -d /dev/video0 --set-ctrl=gamma=110 2>/dev/null");
    system("v4l2-ctl -d /dev/video0 --set-ctrl=saturation=70 2>/dev/null");
    system("v4l2-ctl -d /dev/video0 --set-ctrl=white_balance_temperature_auto=1 2>/dev/null");

    msleep(100);

    // 初始化人脸检测
    initFaceDetection();

    cv::Mat frame, displayFrame;
    int frameCount = 0;

    while (m_running) {
        m_cap >> frame;
        if (frame.empty()) {
            msleep(10);
            continue;
        }

        // BGR → RGB
        cv::cvtColor(frame, displayFrame, cv::COLOR_BGR2RGB);

        // 保存当前帧
        if (!displayFrame.empty()) {
            QImage qimg(displayFrame.data, displayFrame.cols, displayFrame.rows,
                        displayFrame.step, QImage::Format_RGB888);
            {
                QMutexLocker locker(&m_frameMutex);
                m_currentFrame = qimg.copy();
            }
        }

        // 人脸检测（每3帧检测一次，降低负载）
        frameCount++;
        if (frameCount % 3 == 0) {
            detectAndDrawFaces(displayFrame);
        }

        // 发送到 UI
        if (!displayFrame.empty()) {
            QImage qimg(displayFrame.data, displayFrame.cols, displayFrame.rows,
                        displayFrame.step, QImage::Format_RGB888);
            emit frameReady(qimg.copy());
        }

        msleep(66);  // 约15fps
    }

    m_cap.release();
    LOG_INFO("CameraThread stopped");
}

QImage CameraThread::getCurrentFrame()
{
    QMutexLocker locker(&m_frameMutex);
    if (m_currentFrame.isNull()) {
        return QImage();
    }
    return m_currentFrame.copy();
}

cv::Mat CameraThread::getCurrentFace()
{
    QMutexLocker locker(&m_faceMutex);
    return m_currentFace.clone();
}
