#pragma once

#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <memory>
#include <vector>

class QOpenGLShaderProgram;
class QOpenGLTexture;
class QImage;
class QSize;

namespace spine {
class Skeleton;
class SkeletonClipping;
class Slot;
class Color;
}

class SpineRenderer : protected QOpenGLFunctions {
public:
    static constexpr int kFloatsPerVertex = 8; // x y u v r g b a
    static constexpr float kOriginY = 85.f;    // 骨骼原点在 view 内的竖向偏移

    SpineRenderer();
    ~SpineRenderer();

    void initialize();                     // 需 GL context current
    void shutdown();                       // 释放 GL 资源
    void setTexture(const QImage &image);  // 需 GL context current

    void draw(spine::Skeleton &skeleton, float remainder, QSize view_size);

private:
    void collectVertices(spine::Skeleton &skeleton);
    void appendSlot(spine::Slot *slot, const spine::Color &skeleton_color);
    void appendTriangles(const float *world, const unsigned short *indexes, size_t count,
                         const float *uvs, const float rgba[4]);
    void appendClippedTriangles(const float rgba[4]);
    void drawStream(QSize view_size, float remainder);

    QOpenGLShaderProgram *program_ = nullptr;
    QOpenGLTexture *texture_ = nullptr;
    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    std::unique_ptr<spine::SkeletonClipping> clipping_;
    std::vector<float> stream_; // 复用，避免每帧重新分配
    std::vector<float> mesh_world_; // 复用 mesh 顶点临时缓冲
    int vbo_capacity_ = 0;          // 已分配的字节数
};