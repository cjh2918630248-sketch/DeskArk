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
    app.setQuitOnLastWindowClosed(false);

    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(3, 3);
    format.setAlphaBufferSize(8);
    format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);

    PetWindow window;
    window.show();

    return app.exec();
}
