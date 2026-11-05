#include "PetView.h"

#include <QImage>
#include "SpineCharacter.h"
#include <spine/Skeleton.h>

PetView::PetView(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus); //子控件不参与焦点链
}

PetView::~PetView()
{
    makeCurrent();
    renderer_.shutdown();
    doneCurrent();
}

void PetView::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.f, 0.f, 0.f, 0.f);
    //glClearColor(1.f, 0.f, 0.f, 0.5f);
    glEnable(GL_BLEND); //启用混合
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); //设置混合函数
    renderer_.initialize();
    if (!texture_path_.isEmpty())
        renderer_.setTexture(QImage(texture_path_));
}

void PetView::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT);
    if (!character_ || !character_->skeleton())
        return;
    renderer_.draw(*character_->skeleton(), character_->moveRemainder(), size());
}

void PetView::setCharacter(SpineCharacter *character)
{
    character_ = character;
}

void PetView::setTexture(const QString &path)
{
    texture_path_ = path;
    if (!isValid())
        return;
    makeCurrent();
    renderer_.setTexture(QImage(path));
    doneCurrent();
    update();
}