#ifndef FACEMANAGEWINDOW_H
#define FACEMANAGEWINDOW_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>

class CameraThread;

class FaceManageWindow : public QDialog
{
    Q_OBJECT

public:
    explicit FaceManageWindow(CameraThread *cameraThread, QWidget *parent = nullptr);
    ~FaceManageWindow();

private slots:
    void onCapturePhoto();
    void onConfirmEnroll();
    void onCancelEnroll();
    void onViewPersonList();

private:
    CameraThread *m_cameraThread;

    QLabel *m_statusLabel;
    QPushButton *m_captureBtn;
    QPushButton *m_viewBtn;
    QLineEdit *m_nameEdit;
    QPushButton *m_confirmBtn;
    QPushButton *m_cancelBtn;

    QString m_tempPhotoPath;
    bool m_waitingForName;
};

#endif // FACEMANAGEWINDOW_H
