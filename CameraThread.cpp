#include "CameraThread.h"
#include "lockcontroller.h"
#include "databasemanager.h"
#include "RetinaFaceEngine.h"
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
    m_retinaFace.release();
}

void CameraThread::stop()
{
    m_running = false;
}

bool CameraThread::initFaceDetection()
{
    QString retinaPath = "/home/cat/smartlock/model/retinaface.rknn";
    if (!QFile::exists(retinaPath))
    {
        LOG_ERROR(QString("RetinaFace model not found: %1").arg(retinaPath));
        return false;
    }

    if (!m_retinaFace.init(retinaPath.toStdString(), 320, 320))
    {
        LOG_ERROR("RetinaFace init failed");
        return false;
    }
    LOG_INFO("RetinaFace initialized");

    QString recogPath = "/home/cat/smartlock/model/mobilefacenet.rknn";
    if (QFile::exists(recogPath) && m_recognizer.init(recogPath.toStdString())) {
        LOG_INFO("MobileFaceNet recognizer initialized");
        m_recognizerInitialized = true;
    } else {
        LOG_WARNING("Recognizer not available");
    }

    QString dbPath = "/opt/smartlock/bin/data/features.db";
    if (FeatureDatabase::instance().load()) {
        m_dbLoaded = true;
        LOG_INFO(QString("Loaded %1 users from database").arg(FeatureDatabase::instance().getAllUserNames().size()));
    } else {
        LOG_WARNING("No feature database found");
    }
    // 统一在这里设置阈值
    FeatureDatabase::instance().setThreshold(0.25f);

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
    if (!m_faceDetectionEnabled) return;

    std::vector<FaceInfo> faces = m_retinaFace.detect(frame);

    // 过滤无效检测
    std::vector<FaceInfo> filteredFaces;
    for (const auto& face : faces) {
        if (face.score < 0.1f) continue;  // 检测置信度阈值
        if (face.bbox.width < 40 || face.bbox.height < 40) continue;

        cv::Rect validRect = face.bbox;
        validRect.x = std::max(0, validRect.x);
        validRect.y = std::max(0, validRect.y);
        validRect.width = std::min(validRect.width, frame.cols - validRect.x);
        validRect.height = std::min(validRect.height, frame.rows - validRect.y);

        if (validRect.width <= 0 || validRect.height <= 0) continue;

        FaceInfo validFace = face;
        validFace.bbox = validRect;
        filteredFaces.push_back(validFace);
    }

    if (filteredFaces.empty()) return;

    bool hasRecognizedFace = false;

    for (const auto& faceInfo : filteredFaces)
    {
        const cv::Rect& face = faceInfo.bbox;

        // 画框和关键点
        cv::rectangle(frame, face, cv::Scalar(0, 255, 0), 2);
        for (const auto& pt : faceInfo.landmarks) {
            if (pt.x >= 0 && pt.x < frame.cols && pt.y >= 0 && pt.y < frame.rows) {
                cv::circle(frame, pt, 2, cv::Scalar(255, 0, 0), -1);
            }
        }

        QString displayName = "Unknown";
        cv::Scalar textColor(0, 0, 255);

        // 人脸识别
        if (m_recognizerInitialized && face.width > 40 && face.height > 40)
        {
            cv::Rect safeROI = face;
            safeROI.x = std::max(0, face.x);
            safeROI.y = std::max(0, face.y);
            safeROI.width = std::min(face.width, frame.cols - safeROI.x);
            safeROI.height = std::min(face.height, frame.rows - safeROI.y);

            if (safeROI.width > 0 && safeROI.height > 0) {
                cv::Mat faceROI = frame(safeROI).clone();
                cv::Mat aligned;
                cv::resize(faceROI, aligned, cv::Size(112, 112));
                std::vector<float> features = m_recognizer.extractFeature(aligned);

                if (!features.empty()) {
                    float bestScore = 0.0f;
                    QString name = FeatureDatabase::instance().recognize(features, bestScore);

                    // 阈值已在 initFaceDetection 中统一设置
                    if (!name.isEmpty() && bestScore > FeatureDatabase::instance().getThreshold())
                    {
                        displayName = name;
                        textColor = cv::Scalar(0, 255, 0);
                        hasRecognizedFace = true;

                        LOG_INFO(QString("Recognized: %1 score=%2").arg(name).arg(bestScore));

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
        }

        // 显示名字
        int textY = std::max(20, face.y - 10);
        cv::putText(frame, displayName.toStdString(), cv::Point(face.x, textY),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, textColor, 2);

        // 保存当前检测到的人脸
        if (face.width > 40 && face.height > 40) {
            int expandX = face.width * 0.4;
            int expandY = face.height * 0.5;
            int x = std::max(0, face.x - expandX);
            int y = std::max(0, face.y - expandY);
            int w = std::min(frame.cols - x, face.width + expandX * 2);
            int h = std::min(frame.rows - y, face.height + expandY * 2);
            if (w > 0 && h > 0) {
                cv::Mat grayFace;
                cv::cvtColor(frame(cv::Rect(x, y, w, h)), grayFace, cv::COLOR_BGR2GRAY);
                QMutexLocker locker(&m_faceMutex);
                m_currentFace = grayFace;
            }
        }

        emit faceDetected(face.x, face.y, face.width, face.height);
    }

    // 陌生人告警逻辑
    if (hasRecognizedFace) {
        m_unknownFaceCount = 0;
    } else {
        m_unknownFaceCount++;
        if (m_unknownFaceCount >= 10) {  // 连续10次未识别才告警
            m_unknownFaceCount = 0;
            QString photoPath = QString("data/alerts/unknown_%1.jpg")
                                .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
            QDir().mkpath("data/alerts");
            QString fullPath = "/opt/smartlock/bin/" + photoPath;
            if (!frame.empty()) {
                QImage qimg(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_BGR888);
                qimg.save(fullPath);
                LOG_INFO(QString("Alert photo saved: %1").arg(fullPath));
            }
            emit unknownFaceAlert(photoPath);
        }
    }
}

void CameraThread::run()
{
    LOG_INFO("CameraThread started");
    m_running = true;

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
    system("v4l2-ctl -d /dev/video0 --set-ctrl=gamma=110 2>/dev/null");

    msleep(100);
    initFaceDetection();

    cv::Mat frame;
    int frameCount = 0;

    while (m_running) {
        m_cap >> frame;
        if (frame.empty()) {
            msleep(10);
            continue;
        }

        cv::Mat aiFrame = frame.clone();
        cv::Mat displayFrame;
        cv::cvtColor(frame, displayFrame, cv::COLOR_BGR2RGB);

        QImage qimg(displayFrame.data, displayFrame.cols, displayFrame.rows,
                    displayFrame.step, QImage::Format_RGB888);
        {
            QMutexLocker locker(&m_frameMutex);
            m_currentFrame = qimg.copy();
        }

        if (++frameCount % 3 == 0) {
            detectAndDrawFaces(aiFrame);
        }

        emit frameReady(qimg.copy());
        msleep(66);
    }

    m_cap.release();
    LOG_INFO("CameraThread stopped");
}

QImage CameraThread::getCurrentFrame()
{
    QMutexLocker locker(&m_frameMutex);
    return m_currentFrame.isNull() ? QImage() : m_currentFrame.copy();
}

cv::Mat CameraThread::getCurrentFace()
{
    QMutexLocker locker(&m_faceMutex);
    return m_currentFace.clone();
}
