#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include "SpineRenderer.h"

class SpineCharacter;

class PetView : public QOpenGLWidget, public QOpenGLFunctions{
public:
    PetView(QWidget *parent = nullptr);
    ~PetView();

    void setCharacter(SpineCharacter *character); //设置骨骼动画
    void setTexture(const QString &path); //设置纹理

protected:
    void initializeGL() override; //初始化OpenGL
    void paintGL() override; //绘制OpenGL(每帧跑一次)

private:
    SpineRenderer renderer_; //骨骼渲染器
    QString texture_path_; //纹理路径
    SpineCharacter *character_ = nullptr; //骨骼动画
};