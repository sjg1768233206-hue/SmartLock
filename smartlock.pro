QT += core gui widgets multimedia concurrent websockets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000

# 添加当前目录到头文件搜索路径
INCLUDEPATH += $$PWD

SOURCES += \
    CameraThread.cpp \
    HttpServer.cpp \
    RC522Thread.cpp \
    WebSocketServer.cpp \
    databasemanager.cpp \
    lockcontroller.cpp \
    logger.cpp \
    main.cpp \
    mainwindow.cpp \
    passwordmanager.cpp \
    passwordwidget.cpp \
    trainwindow.cpp

HEADERS += \
    CameraThread.h \
    HttpServer.h \
    RC522Thread.h \
    WebSocketServer.h \
    databasemanager.h \
    lockcontroller.h \
    logger.h \
    mainwindow.h \
    passwordmanager.h \
    passwordwidget.h \
    trainwindow.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# ========== 鲁班猫4 交叉编译配置 ==========
# 指定 sysroot 路径
QMAKE_CFLAGS += --sysroot=/home/sjg/sysroot_debian11
QMAKE_CXXFLAGS += --sysroot=/home/sjg/sysroot_debian11
QMAKE_LFLAGS += --sysroot=/home/sjg/sysroot_debian11

# 指定额外的头文件路径
INCLUDEPATH += /home/sjg/sysroot_debian11/usr/include
INCLUDEPATH += /home/sjg/sysroot_debian11/usr/include/aarch64-linux-gnu
INCLUDEPATH += /home/sjg/sysroot_debian11/usr/include/opencv4

# 添加当前项目的头文件路径（确保能找到自定义头文件）
INCLUDEPATH += $$PWD

# 指定额外的库搜索路径
QMAKE_LFLAGS += -Wl,-rpath-link,/home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu
QMAKE_LFLAGS += -Wl,-rpath-link,/home/sjg/sysroot_debian11/lib/aarch64-linux-gnu
QMAKE_LFLAGS += -Wl,-rpath-link,/opt/qt-5.15.8-debian11/ext/lib

LIBS += -L/home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu
LIBS += -L/opt/qt-5.15.8-debian11/ext/lib

# 链接必要的系统库（使用完整路径确保找到）
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libpcre2-16.so
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libglib-2.0.so
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libicui18n.so
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libicuuc.so
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libicudata.so
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libpulse.so
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libpulse-simple.so
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libz.so
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libpng16.so
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libsystemd.so.0
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libstdc++.so.6
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libkrb5.so
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libgssapi_krb5.so
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libEGL.so
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libGLESv2.so
LIBS += /home/sjg/sysroot_debian11/usr/lib/aarch64-linux-gnu/libOpenGL.so

# Qt 库（使用完整路径）
LIBS += /opt/qt-5.15.8-debian11/ext/lib/libQt5MultimediaWidgets.so
LIBS += /opt/qt-5.15.8-debian11/ext/lib/libQt5Multimedia.so
LIBS += /opt/qt-5.15.8-debian11/ext/lib/libQt5Widgets.so
LIBS += /opt/qt-5.15.8-debian11/ext/lib/libQt5Gui.so
LIBS += /opt/qt-5.15.8-debian11/ext/lib/libQt5Network.so
LIBS += /opt/qt-5.15.8-debian11/ext/lib/libQt5Core.so
LIBS += /opt/qt-5.15.8-debian11/ext/lib/libQt5OpenGL.so

# 线程库和SPI支持
LIBS += -lpthread
LIBS += -lrt

# ========== OpenCV 配置 ==========
LIBS += -lopencv_core \
        -lopencv_imgproc \
        -lopencv_objdetect \
        -lopencv_videoio \
        -lopencv_highgui \
        -lopencv_imgcodecs\
        -lopencv_face          # 添加这行！

 QT += sql