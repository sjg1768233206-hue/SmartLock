#include "CameraThread.h"
#include "lockcontroller.h"
#include "databasemanager.h"
#include "logger.h"
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QMetaObject>
#include <QtCore>

CameraThread::CameraThread(QObject *parent)
    : QThread(parent)
    , m_running(false)
    , m_faceDetectionEnabled(true)
    , m_recognizerLoaded(false)
    , m_unknownFaceCount(0)
{
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

void CameraThread::setFaceDetectionEnabled(bool enabled)
{
    m_faceDetectionEnabled = enabled;
}

bool CameraThread::initFaceDetection()
{
    LOG_INFO("Initializing face detection...");

    QString cascadePath = "/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml";

    if (!QFile::exists(cascadePath)) {
        LOG_ERROR(QString("Cascade file not found: %1").arg(cascadePath));
        m_faceDetectionEnabled = false;
        return false;
    }

    LOG_DEBUG(QString("Loading cascade from: %1").arg(cascadePath));

    if (!m_faceCascade.load(cascadePath.toStdString())) {
        LOG_ERROR("Failed to load face cascade!");
        m_faceDetectionEnabled = false;
        return false;
    }

    LOG_INFO("Face detection initialized successfully");
    return true;
}

bool CameraThread::loadRecognizer()
{
    LOG_INFO("Loading face recognizer...");

    QFile modelFile("data/models/face_model.yml");
    if (!modelFile.exists()) {
        LOG_WARNING("No face model found, please train first");
        return false;
    }

    m_recognizer = cv::face::LBPHFaceRecognizer::create();

    try {
        m_recognizer->read("data/models/face_model.yml");
        LOG_INFO("Face model loaded successfully");
    } catch (const cv::Exception &e) {
        LOG_ERROR(QString("Failed to load face model: %1").arg(e.what()));
        return false;
    }

    m_labelToName.clear();
    QFile mappingFile("data/models/label_mapping.txt");
    if (mappingFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&mappingFile);
        while (!in.atEnd()) {
            QString line = in.readLine();
            QStringList parts = line.split("=");
            if (parts.size() == 2) {
                m_labelToName[parts[0].toInt()] = parts[1];
                LOG_DEBUG(QString("Loaded label: %1 = %2").arg(parts[0]).arg(parts[1]));
            }
        }
        mappingFile.close();
    }

    LOG_INFO(QString("Recognizer ready, known faces: %1").arg(m_labelToName.size()));
    return !m_labelToName.empty();
}

int CameraThread::recognizeFace(const cv::Mat &faceROI)
{
    if (!m_recognizer || faceROI.empty()) {
        return -1;
    }

    cv::Mat resized;
    cv::resize(faceROI, resized, cv::Size(100, 100));

    int label = -1;
    double confidence = 0.0;

    try {
        m_recognizer->predict(resized, label, confidence);

        if (confidence < 80.0 && label >= 0) {
            LOG_DEBUG(QString("Recognized: label=%1, confidence=%2").arg(label).arg(confidence));
            return label;
        } else {
            LOG_DEBUG(QString("Unknown face, confidence=%1").arg(confidence));
        }
    } catch (const cv::Exception &e) {
        LOG_ERROR(QString("Recognition error: %1").arg(e.what()));
    }

    return -1;
}

void CameraThread::detectAndDrawFaces(cv::Mat &frame)
{
    if (!m_faceDetectionEnabled || m_faceCascade.empty()) {
        return;
    }

    if (!m_recognizerLoaded) {
        m_recognizerLoaded = loadRecognizer();
    }

    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_RGB2GRAY);
    cv::equalizeHist(gray, gray);

    std::vector<cv::Rect> faces;
    m_faceCascade.detectMultiScale(gray, faces, 1.1, 3, 0, cv::Size(60, 60));

    bool hasRecognizedFace = false;

    for (const auto& face : faces) {
        cv::rectangle(frame, face, cv::Scalar(0, 255, 0), 2);

        QString displayName = "Unknown";
        cv::Scalar textColor = cv::Scalar(0, 0, 255);

        if (m_recognizerLoaded) {
            cv::Mat faceROI = gray(face);
            int label = recognizeFace(faceROI);

            if (label >= 0 && m_labelToName.count(label)) {
                displayName = m_labelToName[label];
                textColor = cv::Scalar(0, 255, 0);
                hasRecognizedFace = true;

                LOG_INFO(QString("Recognized as: %1").arg(displayName));

                int elapsed = m_lastUnlockTime.msecsTo(QTime::currentTime());
                if (elapsed > 3000) {
                    m_lastUnlockedPerson = displayName;
                    m_lastUnlockTime = QTime::currentTime();

                    LOG_INFO(QString("Unlocking and logging for: %1").arg(displayName));

                    QString nameCopy = displayName;
                    QMetaObject::invokeMethod(qApp, [nameCopy]() {
                        DatabaseManager::instance().addLog(nameCopy, "人脸", true);
                        LockController::instance().unlock();
                    }, Qt::QueuedConnection);

                    emit faceRecognized(displayName);
                } else {
                    LOG_DEBUG(QString("Skip unlock, only %1 ms since last unlock").arg(elapsed));
                }
            } else {
                LOG_DEBUG(QString("Unknown face, label=%1").arg(label));
            }
        }

        cv::putText(frame, displayName.toStdString(),
                    cv::Point(face.x, face.y - 10),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, textColor, 2);

        if (face.width > 60 && face.height > 60) {
            cv::Mat faceROI = frame(face);
            cv::Mat grayFace;
            cv::cvtColor(faceROI, grayFace, cv::COLOR_RGB2GRAY);
            {
                QMutexLocker locker(&m_faceMutex);
                m_currentFace = grayFace;
            }
        }

        emit faceDetected(face.x, face.y, face.width, face.height);
    }

    if (hasRecognizedFace) {
        m_unknownFaceCount = 0;
        LOG_DEBUG("Recognized face, reset unknown counter");
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
                cv::Mat rgb;
                cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
                QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
                if (qimg.save(fullPath)) {
                    LOG_INFO(QString("Alert photo saved: %1").arg(fullPath));
                } else {
                    LOG_ERROR("Failed to save alert photo");
                }
            }

            emit unknownFaceAlert(photoPath);
        }
    }

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

    if (!m_cap.open(0, cv::CAP_V4L2)) {
        LOG_ERROR("Failed to open camera");
        emit error("摄像头打开失败！");
        return;
    }

    LOG_INFO("Camera opened successfully");

    m_cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    m_cap.set(cv::CAP_PROP_FPS, 15);

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

    initFaceDetection();

    cv::Mat frame;
    cv::Mat displayFrame;
    int frameCount = 0;

    while (m_running) {
        m_cap >> frame;

        if (frame.empty()) {
            msleep(10);
            continue;
        }

        if (frame.channels() == 3) {
            cv::cvtColor(frame, displayFrame, cv::COLOR_BGR2RGB);
        } else {
            displayFrame = frame;
        }

        if (!displayFrame.empty()) {
            QImage qimg(displayFrame.data, displayFrame.cols, displayFrame.rows,
                        displayFrame.step, QImage::Format_RGB888);
            {
                QMutexLocker locker(&m_frameMutex);
                m_currentFrame = qimg.copy();
                static int frameSaveCount = 0;
                if (++frameSaveCount % 30 == 0) {
                    LOG_DEBUG(QString("Frame saved, size: %1x%2").arg(m_currentFrame.width()).arg(m_currentFrame.height()));
                }
            }
        }

        frameCount++;
        if (frameCount % 3 == 0) {
            detectAndDrawFaces(displayFrame);
        }

        if (!displayFrame.empty()) {
            QImage qimg(displayFrame.data, displayFrame.cols, displayFrame.rows,
                        displayFrame.step, QImage::Format_RGB888);
            emit frameReady(qimg.copy());
        }

        msleep(66);
    }

    m_cap.release();
    LOG_INFO("CameraThread stopped");
}

QImage CameraThread::getCurrentFrame()
{
    QMutexLocker locker(&m_frameMutex);
    static int callCount = 0;
    if (++callCount % 30 == 0) {
        LOG_DEBUG(QString("getCurrentFrame called, frame null: %1").arg(m_currentFrame.isNull()));
    }
    if (m_currentFrame.isNull()) {
        return QImage();
    }
    return m_currentFrame.copy();
}