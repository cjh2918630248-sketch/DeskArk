#include "SpineRenderer.h"

#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QImage>
#include <QSize>
#include <QDebug>
#include <cmath>

#include <spine/Skeleton.h>
#include <spine/Slot.h>
#include <spine/Color.h>
#include <spine/Attachment.h>
#include <spine/RegionAttachment.h>
#include <spine/MeshAttachment.h>
#include <spine/ClippingAttachment.h>
#include <spine/SkeletonClipping.h>

#include "Common.h"

namespace {

constexpr size_t kInitialVertexCapacity = 4096; // 初始顶点数，约 32KB
constexpr size_t kInitialMeshVertexCapacity = 256;

// 骨骼色 × 槽位色 × 附件色
void MultiplyColor(const spine::Color &skeleton, const spine::Color &slot,
                   const spine::Color &attachment, float out[4])
{
    out[0] = skeleton.r * slot.r * attachment.r;
    out[1] = skeleton.g * slot.g * attachment.g;
    out[2] = skeleton.b * slot.b * attachment.b;
    out[3] = skeleton.a * slot.a * attachment.a;
}

// 区域附件固定的两个三角形（四边形拆成 0-1-2 / 0-2-3）
constexpr unsigned short kQuadTriangles[6] = {0, 1, 2, 0, 2, 3};

} // namespace

SpineRenderer::SpineRenderer() = default;

SpineRenderer::~SpineRenderer()
{
    shutdown();
}

void SpineRenderer::initialize()
{
    initializeOpenGLFunctions(); // QOpenGLFunctions 必须先初始化函数指针

    if (program_)
        return;

    program_ = new QOpenGLShaderProgram();
    if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, Vertex_Shader())
        || !program_->addShaderFromSourceCode(QOpenGLShader::Fragment, Fragment_Shader())
        || !program_->link()) {
        qWarning() << "SpineRenderer: 着色器编译或链接失败:" << program_->log();
        delete program_;
        program_ = nullptr;
        return;
    }

    // VAO 只记录「顶点布局」，真正的数据每帧上传
    vao_.create();
    vao_.bind();
    vbo_.create();
    vbo_.bind();

    program_->bind();
    program_->enableAttributeArray(0); // aPos
    program_->setAttributeBuffer(0, GL_FLOAT, 0, 2, kFloatsPerVertex * sizeof(float));
    program_->enableAttributeArray(1); // aUv
    program_->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, kFloatsPerVertex * sizeof(float));
    program_->enableAttributeArray(2); // aColor
    program_->setAttributeBuffer(2, GL_FLOAT, 4 * sizeof(float), 4, kFloatsPerVertex * sizeof(float));
    program_->release();

    vbo_.release();
    vao_.release();

    clipping_ = std::make_unique<spine::SkeletonClipping>();
    stream_.reserve(kInitialVertexCapacity * kFloatsPerVertex);
    mesh_world_.reserve(kInitialMeshVertexCapacity);
}

void SpineRenderer::shutdown()
{
    delete texture_;
    texture_ = nullptr;

    if (vbo_.isCreated())
        vbo_.destroy();
    if (vao_.isCreated())
        vao_.destroy();

    delete program_;
    program_ = nullptr;

    clipping_.reset();
    stream_.clear();
    stream_.shrink_to_fit();
    mesh_world_.clear();
    mesh_world_.shrink_to_fit();
    vbo_capacity_ = 0;
}

void SpineRenderer::setTexture(const QImage &image)
{
    if (image.isNull()) {
        qWarning() << "SpineRenderer: 纹理图为空";
        return;
    }
    delete texture_;
    texture_ = new QOpenGLTexture(image, QOpenGLTexture::GenerateMipMaps);
    texture_->setMinMagFilters(QOpenGLTexture::LinearMipMapLinear, QOpenGLTexture::Linear);
    texture_->setWrapMode(QOpenGLTexture::ClampToEdge);
}

void SpineRenderer::draw(spine::Skeleton &skeleton, float remainder, QSize view_size)
{
    if (!program_ || !texture_ || !clipping_)
        return;

    collectVertices(skeleton);
    if (stream_.empty())
        return;

    drawStream(view_size, remainder);
}

// ---------- 骨架 -> 顶点流 ----------

void SpineRenderer::collectVertices(spine::Skeleton &skeleton)
{
    stream_.clear();

    // 防止上一帧残留的裁剪状态
    clipping_->clipEnd();

    const spine::Color &skeleton_color = skeleton.getColor();
    auto &drawOrder = skeleton.getDrawOrder();
    for (size_t i = 0; i < drawOrder.size(); ++i)
        appendSlot(drawOrder[i], skeleton_color);

    clipping_->clipEnd();
}

void SpineRenderer::appendSlot(spine::Slot *slot, const spine::Color &skeleton_color)
{
    spine::Attachment *attachment = slot->getAttachment();
    if (!attachment) {
        clipping_->clipEnd(*slot);
        return;
    }

    if (attachment->getRTTI().isExactly(spine::ClippingAttachment::rtti)) {
        clipping_->clipStart(*slot, static_cast<spine::ClippingAttachment *>(attachment));
        return; // 起始槽位本身不产生三角形，clipEnd 由结尾统一收
    }

    float rgba[4] = {1.f, 1.f, 1.f, 1.f};

    if (attachment->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
        auto *region = static_cast<spine::RegionAttachment *>(attachment);
        MultiplyColor(skeleton_color, slot->getColor(), region->getColor(), rgba);
        if (rgba[3] > 0.f) {
            float world[8];
            region->computeWorldVertices(slot->getBone(), world, 0, 2);
            auto &uvs = region->getUVs();
            if (clipping_->isClipping()) {
                clipping_->clipTriangles(world, const_cast<unsigned short *>(kQuadTriangles), 6,
                                         uvs.buffer(), 2);
                appendClippedTriangles(rgba);
            } else {
                appendTriangles(world, kQuadTriangles, 6, uvs.buffer(), rgba);
            }
        }
    } else if (attachment->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
        auto *mesh = static_cast<spine::MeshAttachment *>(attachment);
        MultiplyColor(skeleton_color, slot->getColor(), mesh->getColor(), rgba);
        if (rgba[3] > 0.f) {
            const size_t length = mesh->getWorldVerticesLength();
            if (mesh_world_.size() < length)
                mesh_world_.resize(length);
            mesh->computeWorldVertices(*slot, mesh_world_.data());

            auto &uvs = mesh->getUVs();
            auto &triangles = mesh->getTriangles();
            if (clipping_->isClipping()) {
                clipping_->clipTriangles(mesh_world_.data(), triangles.buffer(),
                                         triangles.size(), uvs.buffer(), 2);
                appendClippedTriangles(rgba);
            } else {
                appendTriangles(mesh_world_.data(), triangles.buffer(), triangles.size(),
                                uvs.buffer(), rgba);
            }
        }
    }

    clipping_->clipEnd(*slot);
}

void SpineRenderer::appendTriangles(const float *world, const unsigned short *indexes,
                                    size_t count, const float *uvs, const float rgba[4])
{
    for (size_t i = 0; i < count; ++i) {
        const size_t index = indexes[i];
        stream_.push_back(world[index * 2]);
        stream_.push_back(world[index * 2 + 1]);
        stream_.push_back(uvs[index * 2]);
        stream_.push_back(uvs[index * 2 + 1]);
        stream_.push_back(rgba[0]);
        stream_.push_back(rgba[1]);
        stream_.push_back(rgba[2]);
        stream_.push_back(rgba[3]);
    }
}

void SpineRenderer::appendClippedTriangles(const float rgba[4])
{
    auto &cv = clipping_->getClippedVertices();   // stride 2
    auto &cu = clipping_->getClippedUVs();        // stride 2
    auto &ct = clipping_->getClippedTriangles();
    appendTriangles(cv.buffer(), ct.buffer(), ct.size(), cu.buffer(), rgba);
}

// ---------- 顶点流 -> GPU ----------

void SpineRenderer::drawStream(QSize view_size, float remainder)
{
    program_->bind();
    program_->setUniformValue("uOrigin", -remainder, kOriginY); // 亚像素补偿 + 原点下移
    program_->setUniformValue("uHalfSize", view_size.width() / 2.f, view_size.height() / 2.f);
    program_->setUniformValue("uTex", 0);

    texture_->bind(0);

    vao_.bind();
    vbo_.bind();

    const int byteSize = static_cast<int>(stream_.size() * sizeof(float));
    if (byteSize > vbo_capacity_) {
        // 只在扩容时重新分配
        vbo_.allocate(stream_.data(), byteSize);
        vbo_capacity_ = byteSize;
    } else {
        vbo_.write(0, stream_.data(), byteSize);
    }

    glDrawArrays(GL_TRIANGLES, 0, static_cast<int>(stream_.size() / kFloatsPerVertex));

    vbo_.release();
    vao_.release();
    texture_->release(0);
    program_->release();
}