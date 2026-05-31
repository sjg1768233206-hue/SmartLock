#include "FaceManageWindow.h"
#include "CameraThread.h"
#include "FeatureDatabase.h"
#include "logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <opencv2/opencv.hpp>

FaceManageWindow::FaceManageWindow(CameraThread *cameraThread, QWidget *parent)
    : QDialog(parent)
    , m_cameraThread(cameraThread)
    , m_waitingForName(false)
{
    setWindowTitle("人脸管理");
    setFixedSize(400, 300);
    setModal(false);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(30, 30, 30, 30);

    // 标题
    QLabel *title = new QLabel("人脸管理", this);
    title->setStyleSheet("font-size: 20px; font-weight: bold; color: #2c3e50;");
    title->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(title);

    // 状态标签
    m_statusLabel = new QLabel("就绪", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: #27ae60; font-weight: bold;");
    mainLayout->addWidget(m_statusLabel);

    // 录入区域（拍照后显示）
    QHBoxLayout *enrollLayout = new QHBoxLayout();
    enrollLayout->setSpacing(10);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("输入姓名");
    m_nameEdit->setFixedWidth(150);
    m_nameEdit->setVisible(false);

    m_confirmBtn = new QPushButton("确认", this);
    m_confirmBtn->setFixedSize(70, 35);
    m_confirmBtn->setStyleSheet("background-color: #3498db; color: white; border-radius: 5px;");
    m_confirmBtn->setVisible(false);

    m_cancelBtn = new QPushButton("取消", this);
    m_cancelBtn->setFixedSize(70, 35);
    m_cancelBtn->setStyleSheet("background-color: #95a5a6; color: white; border-radius: 5px;");
    m_cancelBtn->setVisible(false);

    enrollLayout->addStretch();
    enrollLayout->addWidget(m_nameEdit);
    enrollLayout->addWidget(m_confirmBtn);
    enrollLayout->addWidget(m_cancelBtn);
    enrollLayout->addStretch();

    mainLayout->addLayout(enrollLayout);

    // ========== 两个大按钮 ==========
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(20);

    m_captureBtn = new QPushButton("📷 拍照录入", this);
    m_captureBtn->setFixedSize(150, 80);
    m_captureBtn->setStyleSheet("background-color: #27ae60; color: white; font-size: 16px; border-radius: 10px;");

    m_viewBtn = new QPushButton("📋 已录入人员", this);
    m_viewBtn->setFixedSize(150, 80);
    m_viewBtn->setStyleSheet("background-color: #3498db; color: white; font-size: 16px; border-radius: 10px;");

    btnLayout->addStretch();
    btnLayout->addWidget(m_captureBtn);
    btnLayout->addWidget(m_viewBtn);
    btnLayout->addStretch();

    mainLayout->addLayout(btnLayout);

    // 连接信号
    connect(m_captureBtn, &QPushButton::clicked, this, &FaceManageWindow::onCapturePhoto);
    connect(m_viewBtn, &QPushButton::clicked, this, &FaceManageWindow::onViewPersonList);
    connect(m_confirmBtn, &QPushButton::clicked, this, &FaceManageWindow::onConfirmEnroll);
    connect(m_cancelBtn, &QPushButton::clicked, this, &FaceManageWindow::onCancelEnroll);
}

FaceManageWindow::~FaceManageWindow()
{
}

void FaceManageWindow::onCapturePhoto()
{
    cv::Mat face = m_cameraThread->getCurrentFace();
    if (face.empty()) {
        QMessageBox::warning(this, "提示", "未检测到人脸，请面对摄像头");
        return;
    }

    // 保存临时照片
    QDir().mkpath("data/temp");
    m_tempPhotoPath = QString("data/temp/temp_%1.jpg").arg(QDateTime::currentMSecsSinceEpoch());
    cv::imwrite(m_tempPhotoPath.toStdString(), face);

    m_statusLabel->setText("拍照成功！请输入姓名");
    m_statusLabel->setStyleSheet("color: #27ae60; font-weight: bold;");
    m_captureBtn->setEnabled(false);
    m_viewBtn->setEnabled(false);
    m_nameEdit->setVisible(true);
    m_nameEdit->clear();
    m_nameEdit->setFocus();
    m_confirmBtn->setVisible(true);
    m_cancelBtn->setVisible(true);
    m_waitingForName = true;
}

void FaceManageWindow::onConfirmEnroll()
{
    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入姓名");
        return;
    }

    cv::Mat face = cv::imread(m_tempPhotoPath.toStdString());
    if (face.empty()) {
        QMessageBox::warning(this, "错误", "照片读取失败");
        onCancelEnroll();
        return;
    }

    // 保存照片
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
    QFile::remove(m_tempPhotoPath);
    onCancelEnroll();
}

void FaceManageWindow::onCancelEnroll()
{
    m_statusLabel->setText("就绪");
    m_statusLabel->setStyleSheet("color: #27ae60; font-weight: bold;");
    m_captureBtn->setEnabled(true);
    m_viewBtn->setEnabled(true);
    m_nameEdit->setVisible(false);
    m_nameEdit->clear();
    m_confirmBtn->setVisible(false);
    m_cancelBtn->setVisible(false);
    m_waitingForName = false;

    if (!m_tempPhotoPath.isEmpty() && QFile::exists(m_tempPhotoPath)) {
        QFile::remove(m_tempPhotoPath);
    }
}

void FaceManageWindow::onViewPersonList()
{
    // 加载人员列表
    QStringList persons;
    QDir trainDir("data/train");
    if (trainDir.exists()) {
        persons = trainDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    }

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
            QDir dir(QString("data/train/%1").arg(name));
            if (dir.exists()) {
                dir.removeRecursively();
            }
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
