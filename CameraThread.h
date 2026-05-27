#ifndef CAMERATHREAD_H
#define CAMERATHREAD_H

#include <QThread>
#include <QMutex>
#include <QImage>
#include <QTime>
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/face.hpp>

class CameraThread : public QThread
{
    Q_OBJECT

public:
    explicit CameraThread(QObject *parent = nullptr);
    ~CameraThread();

    void stop();
    bool isRunning() const { return m_running; }
    void setFaceDetectionEnabled(bool enabled);

    cv::Mat getCurrentFace() {
        QMutexLocker locker(&m_faceMutex);
        return m_currentFace.clone();
    }

    QImage getCurrentFrame();  // 添加这行声明

signals:
    void frameReady(const QImage &image);
    void error(const QString &msg);
    void faceDetected(int x, int y, int width, int height);
    void faceRecognized(const QString &name);
    void unknownFaceAlert(QString photoPath);
protected:
    void run() override;

private:
    bool initFaceDetection();
    void detectAndDrawFaces(cv::Mat &frame);
    bool loadRecognizer();
    int recognizeFace(const cv::Mat &faceROI);

private:
    volatile bool m_running;
    cv::VideoCapture m_cap;
    QMutex m_mutex;

    bool m_faceDetectionEnabled;
    cv::CascadeClassifier m_faceCascade;

    // 人脸识别器
    cv::Ptr<cv::face::LBPHFaceRecognizer> m_recognizer;
    std::map<int, QString> m_labelToName;
    bool m_recognizerLoaded;

    // 用于训练的人脸保存
    mutable QMutex m_faceMutex;
    cv::Mat m_currentFace;

    // ========== 新增：用于视频流的当前帧 ==========
    QMutex m_frameMutex;
    QImage m_currentFrame;

    // 避免重复开门
    QTime m_lastUnlockTime;
    QString m_lastUnlockedPerson;

    int m_unknownFaceCount;  // 连续陌生人计数
    QTime m_unknownFaceFirstTime;
};

#endif // CAMERATHREAD_H