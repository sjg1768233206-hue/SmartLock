#include "gpu_video_widget.h"
#include <QDebug>

GPUVideoWidget::GPUVideoWidget(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_textureId(0)
    , m_textureWidth(0)
    , m_textureHeight(0)
    , m_textureInitialized(false)
{
    setMinimumSize(640, 480);
}

GPUVideoWidget::~GPUVideoWidget()
{
    makeCurrent();
    if (m_textureId) {
        glDeleteTextures(1, &m_textureId);
    }
    doneCurrent();
}

void GPUVideoWidget::initializeGL()
{
    initializeOpenGLFunctions();

    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glEnable(GL_TEXTURE_2D);

    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    // 创建着色器程序
    const char *vertexShaderSource =
        "attribute vec4 vertexIn;"
        "attribute vec2 textureIn;"
        "varying vec2 textureOut;"
        "void main() {"
        "    gl_Position = vertexIn;"
        "    textureOut = textureIn;"
        "}";

    const char *fragmentShaderSource =
        "uniform sampler2D texture;"
        "varying vec2 textureOut;"
        "void main() {"
        "    gl_FragColor = texture2D(texture, textureOut);"
        "}";

    m_program = new QOpenGLShaderProgram(this);
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    m_program->link();

    m_vertexIn = m_program->attributeLocation("vertexIn");
    m_textureIn = m_program->attributeLocation("textureIn");

    // 顶点坐标和纹理坐标
    static const GLfloat vertices[] = {
        -1.0f, -1.0f,
        1.0f, -1.0f,
        1.0f,  1.0f,
        -1.0f,  1.0f
    };

    static const GLfloat texCoords[] = {
        0.0f, 1.0f,
        1.0f, 1.0f,
        1.0f, 0.0f,
        0.0f, 0.0f
    };

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices) + sizeof(texCoords), NULL, GL_STATIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBufferSubData(GL_ARRAY_BUFFER, sizeof(vertices), sizeof(texCoords), texCoords);

    qDebug() << "[GPU] OpenGL initialized";
}

void GPUVideoWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void GPUVideoWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);

    QMutexLocker locker(&m_mutex);

    if (m_currentFrame.isNull()) {
        return;
    }

    // 更新纹理
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                 m_currentFrame.width(), m_currentFrame.height(),
                 0, GL_RGB, GL_UNSIGNED_BYTE,
                 m_currentFrame.constBits());
    glBindTexture(GL_TEXTURE_2D, 0);

    m_program->bind();

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glVertexAttribPointer(m_vertexIn, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glVertexAttribPointer(m_textureIn, 2, GL_FLOAT, GL_FALSE, 0,
                          (void*)(8 * sizeof(GLfloat)));
    glEnableVertexAttribArray(m_vertexIn);
    glEnableVertexAttribArray(m_textureIn);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    m_program->setUniformValue("texture", 0);

    glDrawArrays(GL_QUADS, 0, 4);

    glDisableVertexAttribArray(m_vertexIn);
    glDisableVertexAttribArray(m_textureIn);
    m_program->release();
}

void GPUVideoWidget::updateFrame(const QImage &image)
{
    QMutexLocker locker(&m_mutex);
    if (image.format() != QImage::Format_RGB888) {
        m_currentFrame = image.convertToFormat(QImage::Format_RGB888);
    } else {
        m_currentFrame = image;
    }
    update();
}