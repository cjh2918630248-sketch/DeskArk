#include "Common.h"
#include <QCoreApplication>
#include <QDir>
#include <QSurfaceFormat>
#include <QFile>

void Init_OpenGL() {
    QSurfaceFormat format; // 配置OpenGL格式的核心类
    format.setRenderableType(QSurfaceFormat::OpenGL); // 设置渲染类型为OpenGL
    format.setProfile(QSurfaceFormat::CoreProfile); // 设置OpenGL核心配置文件
    format.setVersion(3, 3); // 设置OpenGL版本为3.3
    format.setAlphaBufferSize(8); // 设置Alpha缓冲区大小为8位
    format.setSamples(0); // 设置采样数为16
    QSurfaceFormat::setDefaultFormat(format); // 设置默认OpenGL格式
}

void Read_Json(const QString &path, nlohmann::json &json)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        json = nlohmann::json::object();
        return;
    }
    json = nlohmann::json::parse(file.readAll().toStdString(), nullptr, false);
    if (json.is_discarded())
        json = nlohmann::json::object();
}

const char *Vertex_Shader()
{
    return R"(#version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec2 aUv;
        layout(location = 2) in vec4 aColor;
        uniform vec2 uOrigin;
        uniform vec2 uHalfSize;
        out vec2 vUv;
        out vec4 vColor;
        void main() {
            vec2 ndc = (aPos - uOrigin) / uHalfSize;
            gl_Position = vec4(ndc, 0.0, 1.0);
            vUv = aUv;
            vColor = aColor;
        })";
}

const char *Fragment_Shader()
{
    return R"(#version 330 core
        in vec2 vUv;
        in vec4 vColor;
        out vec4 fragColor;
        uniform sampler2D uTex;
        void main() {
            fragColor = texture(uTex, vUv) * vColor;
        })";
}

const float *Vertices()
{
    static const float vertices[] = {
        -0.8f, -0.8f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f,
        0.8f, -0.8f, 1.f, 0.f, 1.f, 1.f, 1.f, 1.f,
       -0.8f,  0.8f, 0.f, 1.f, 1.f, 1.f, 1.f, 1.f,
        0.8f,  0.8f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
    };
    return vertices;
}


QString App_File(const QString &relativePath)
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(relativePath);
}