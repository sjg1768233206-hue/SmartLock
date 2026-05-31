#ifndef GPU_VIDEO_WIDGET_H
#define GPU_VIDEO_WIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QImage>
#include <QMutex>

class GPUVideoWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit GPUVideoWidget(QWidget *parent = nullptr);
    ~GPUVideoWidget();

public slots:
    void updateFrame(const QImage &image);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    QImage m_currentFrame;
    QMutex m_mutex;
    GLuint m_textureId;
    GLuint m_vbo;
    QOpenGLShaderProgram *m_program;
    int m_vertexIn;
    int m_textureIn;
    int m_textureWidth;
    int m_textureHeight;
    bool m_textureInitialized;
};

#endif // GPU_VIDEO_WIDGET_H