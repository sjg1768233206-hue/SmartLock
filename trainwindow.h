#ifndef TRAINWINDOW_H
#define TRAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QLineEdit>
#include <QTimer>
#include <opencv2/opencv.hpp>
#include <opencv2/face.hpp>

class CameraThread;

class TrainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit TrainWindow(QWidget *parent = nullptr);
    ~TrainWindow();

private slots:
    void onStartCapture();      // 开始拍照
    void onTrainModel();        // 训练模型
    void onFrameReady(const QImage &image);  // 接收视频帧
    void onFaceDetected(int x, int y, int w, int h);  // 检测到人脸
    void onSaveFace();          // 保存人脸照片
    void onLoadPersonList();    // 加载人员列表

private:
    void setupUI();
    void saveFaceImage(const cv::Mat &face, const QString &personName);

    CameraThread *m_cameraThread;
    QLabel *m_videoLabel;
    QListWidget *m_personList;
    QLineEdit *m_nameEdit;
    QPushButton *m_captureBtn;
    QPushButton *m_trainBtn;
    QLabel *m_statusLabel;

    cv::Mat m_currentFace;      // 当前检测到的人脸
    QString m_currentPerson;    // 当前拍照的人员
    int m_captureCount;         // 已拍照数量
    static const int NEED_COUNT = 20;  // 每人需要20张照片
};

#endif // TRAINWINDOW_H