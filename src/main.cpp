#include <QApplication>
#include <QSurfaceFormat>

#include <spine/Extension.h>

#include "PetWindow.h"

// spine-cpp 要求集成方提供默认扩展（内存分配/文件读取的实现）
namespace spine {
SpineExtension *getDefaultExtension() {
    return new DefaultSpineExtension();
}
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("DeskArk");
    app.setQuitOnLastWindowClosed(false); // 禁用最后一个窗口关闭后的退出行为

    QSurfaceFormat format; // 配置OpenGL格式的核心类
    format.setRenderableType(QSurfaceFormat::OpenGL); // 设置渲染类型为OpenGL
    format.setProfile(QSurfaceFormat::CoreProfile); // 设置OpenGL核心配置文件
    format.setVersion(3, 3); // 设置OpenGL版本为3.3
    format.setAlphaBufferSize(8); // 设置Alpha缓冲区大小为8位
    format.setSamples(16); // 设置采样数为16
    QSurfaceFormat::setDefaultFormat(format); // 设置默认OpenGL格式

    PetWindow window;
    window.show();

    return app.exec();
}
