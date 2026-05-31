#ifndef CAMERATHREAD_H
#define CAMERATHREAD_H

#include <QThread>
#include <QImage>
#include <QMutex>
#include <QTime>
#include <opencv2/opencv.hpp>
#include <opencv2/objdetect.hpp>
#include "FaceRecognizer.h"
#include "FeatureDatabase.h"

class CameraThread : public QThread
{
    Q_OBJECT

public:
    explicit CameraThread(QObject *parent = nullptr);
    ~CameraThread();

    void stop();
    QImage getCurrentFrame();
    cv::Mat getCurrentFace();
    std::vector<float> extractFeatureForEnroll(const cv::Mat& face);

signals:
    void frameReady(const QImage &frame);
    void faceDetected(int x, int y, int width, int height);
    void faceRecognized(const QString &name);
    void unknownFaceAlert(const QString &photoPath);
    void error(const QString &msg);

protected:
    void run() override;

private:
    bool initFaceDetection();
    void detectAndDrawFaces(cv::Mat &frame);

private:
    volatile bool m_running;
    bool m_faceDetectionEnabled;
    bool m_recognizerInitialized;
    bool m_dbLoaded;
    int m_unknownFaceCount;
    QTime m_lastUnlockTime;

    cv::VideoCapture m_cap;
    cv::CascadeClassifier m_faceCascade;
    FaceRecognizer m_recognizer;

    QImage m_currentFrame;
    QMutex m_frameMutex;
    cv::Mat m_currentFace;
    QMutex m_faceMutex;
};

#endif // CAMERATHREAD_H
