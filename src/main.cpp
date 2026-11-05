#include "PetWindow.h"

#include <QApplication>
#include <QIcon>
#include <spine/Extension.h>

#include "Common.h"

namespace spine {
    SpineExtension *getDefaultExtension() {
        return new DefaultSpineExtension();
    }
}

int main(int argc, char *argv[])
{
    Init_OpenGL();
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false); //禁止最后一个窗口关掉就退出
    app.setWindowIcon(QIcon(":/assets/tray_icon.png"));
    PetWindow w;
    w.show();
    return app.exec();
}
