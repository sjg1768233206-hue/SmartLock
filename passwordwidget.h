#ifndef PASSWORDWIDGET_H
#define PASSWORDWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

class PasswordWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PasswordWidget(QWidget *parent = nullptr);
    ~PasswordWidget();  // 添加这行

    enum Mode { ModeInput, ModeChange, ModeSet };
    void setMode(Mode mode);

signals:
    void passwordAlert(QString photoPath);  // 添加报警信号

private slots:
    void onConfirmClicked();
    void onCancelClicked();

private:
    void setupUI();
    void updateUI();

    Mode m_currentMode;
    QLabel *m_titleLabel;
    QLineEdit *m_passwordEdit;
    QLineEdit *m_newPasswordEdit;
    QLineEdit *m_confirmPasswordEdit;
    QPushButton *m_confirmBtn;
    QPushButton *m_cancelBtn;
    QLabel *m_messageLabel;

    // 报警相关
    int m_passwordErrorCount;
    QTimer *m_errorResetTimer;
};

#endif