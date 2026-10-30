#include "SpineWidget.h"

#include <spine/Animation.h>
#include <spine/Bone.h>
#include <spine/ClippingAttachment.h>
#include <spine/MeshAttachment.h>
#include <spine/RegionAttachment.h>
#include <spine/Slot.h>
#include <spine/SlotData.h>

#include <QDebug>
#include <QVector2D>

namespace {
const char *kVertexSrc = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uViewport;
out vec2 vUV;
out vec4 vColor;
void main() {
    vec2 ndc = vec2((aPos.x / uViewport.x) * 2.0 - 1.0,
                    1.0 - (aPos.y / uViewport.y) * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor.bgra;
}
)";

const char *kFragmentSrc = R"(
#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 fragColor;
uniform sampler2D uTex;
void main() {
    fragColor = texture(uTex, vUV) * vColor;
}
)";

const unsigned short kQuadIndices[6] = {0, 1, 2, 2, 3, 0};
}  // namespace

void SpineWidget::QtSpineTextureLoader::load(spine::AtlasPage &page, const spine::String &path) {
    QImage *image = new QImage(QString::fromUtf8(path.buffer()));
    if (image->isNull()) {
        qWarning() << "texture load failed:" << path.buffer();
    }
    page.setRendererObject(image);
}

void SpineWidget::QtSpineTextureLoader::unload(void *texture) {
    delete static_cast<QImage *>(texture);
}

SpineWidget::SpineWidget(QWidget *parent) : QOpenGLWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    // 以 60 FPS 上限驱动重绘，避免 paintGL 自旋空转
    frame_timer_ = new QTimer(this);
    frame_timer_->setTimerType(Qt::PreciseTimer);
    connect(frame_timer_, &QTimer::timeout, this, QOverload<>::of(&SpineWidget::update));
    frame_timer_->start(16);
}

SpineWidget::~SpineWidget() {
    makeCurrent();
    for (GLuint tex : textures_) {
        glDeleteTextures(1, &tex);
    }
    textures_.clear();
    vbo_pos_.destroy();
    vbo_uv_.destroy();
    vbo_color_.destroy();
    ibo_.destroy();
    vao_.destroy();
    delete program_;
    doneCurrent();

    delete anim_state_;
    delete anim_state_data_;
    delete skeleton_;
    delete skeleton_data_;
    delete atlas_;
}

bool SpineWidget::loadSkeleton(const QString &atlasPath, const QString &skelPath, float scale) {
    atlas_ = new spine::Atlas(spine::String(atlasPath.toUtf8().constData()), &texture_loader_);
    if (atlas_->getPages().size() == 0) {
        qWarning() << "atlas empty:" << atlasPath;
        return false;
    }

    spine::SkeletonBinary binary(atlas_);
    binary.setScale(scale);
    skeleton_data_ = binary.readSkeletonDataFile(spine::String(skelPath.toUtf8().constData()));
    if (!skeleton_data_) {
        qWarning() << "skeleton load failed:" << binary.getError().buffer();
        return false;
    }

    skeleton_ = new spine::Skeleton(skeleton_data_);
    // Spine 世界坐标 Y 轴向上，屏幕坐标 Y 轴向下，翻转模型使其正立
    skeleton_->setScaleY(-1.0f);
    anim_state_data_ = new spine::AnimationStateData(skeleton_data_);
    anim_state_data_->setDefaultMix(0.2f);
    anim_state_ = new spine::AnimationState(anim_state_data_);

    auto &anims = skeleton_data_->getAnimations();
    for (size_t i = 0; i < anims.size(); ++i) {
        qInfo() << "animation:" << anims[i]->getName().buffer();
    }

    skeleton_->setToSetupPose();
    // 注册动画完成监听：触发型动画播完自动切回 Relax
    anim_state_->setListener(&anim_listener_);
    // 默认播放 Relax（循环）
    playDefaultAnimation();
    anim_state_->apply(*skeleton_);
    skeleton_->updateWorldTransform();

    clock_.start();
    last_frame_time_ = 0;
    return true;
}

void SpineWidget::playDefaultAnimation() {
    if (!anim_state_ || !skeleton_data_) {
        return;
    }
    spine::Animation *relax = skeleton_data_->findAnimation(spine::String("Relax"));
    if (relax) {
        anim_state_->setAnimation(0, relax, true);
        return;
    }
    auto &anims = skeleton_data_->getAnimations();
    if (anims.size() > 0) {
        anim_state_->setAnimation(0, anims[0]->getName(), true);
    }
}

void SpineWidget::playAnimation(const QString &name) {
    if (!anim_state_) {
        return;
    }
    bool loop = !isTriggerAnimation(name);
    anim_state_->setAnimation(0, spine::String(name.toUtf8().constData()), loop);
}

bool SpineWidget::isTriggerAnimation(const QString &name) {
    // 触发型动画：点一下播放一次，播完回 Relax（Move 已改为循环移动，不再是一次性触发）
    return name.compare(QStringLiteral("Interact"), Qt::CaseInsensitive) == 0;
}

void SpineWidget::handleAnimationComplete(spine::TrackEntry *entry) {
    if (!entry || !anim_state_) {
        return;
    }
    spine::Animation *anim = entry->getAnimation();
    if (!anim) {
        return;
    }
    if (auto_return_to_relax_ &&
        isTriggerAnimation(QString::fromUtf8(anim->getName().buffer()))) {
        playDefaultAnimation();
    }
}

void SpineWidget::AnimListener::callback(spine::AnimationState *state, spine::EventType type,
                                         spine::TrackEntry *entry, spine::Event *event) {
    (void)state;
    (void)event;
    if (type == spine::EventType_Complete) {
        owner_->handleAnimationComplete(entry);
    }
}

bool SpineWidget::loadModel(const QString &atlasPath, const QString &skelPath, float scale) {
    // 释放旧 GL 纹理（需在当前上下文）
    makeCurrent();
    for (GLuint tex : textures_) {
        glDeleteTextures(1, &tex);
    }
    textures_.clear();
    doneCurrent();

    // 释放旧 spine 资源；atlas 析构会通过 texture_loader_ 释放 QImage
    delete anim_state_;
    anim_state_ = nullptr;
    delete anim_state_data_;
    anim_state_data_ = nullptr;
    delete skeleton_;
    skeleton_ = nullptr;
    delete skeleton_data_;
    skeleton_data_ = nullptr;
    delete atlas_;
    atlas_ = nullptr;

    // 加载新模型
    if (!loadSkeleton(atlasPath, skelPath, scale)) {
        return false;
    }

    // 若 GL 已初始化，立即重建纹理
    if (program_) {
        makeCurrent();
        uploadTextures();
        doneCurrent();
    }

    // 重新居中并尽快刷新命中区域
    fitToView();
    mask_frame_counter_ = 5;
    update();
    return true;
}

void SpineWidget::setAnimation(const QString &name, bool loop) {
    if (!anim_state_) {
        return;
    }
    anim_state_->setAnimation(0, spine::String(name.toUtf8().constData()), loop);
}

void SpineWidget::setSkeletonPosition(float x, float y) {
    if (skeleton_) {
        skeleton_->setPosition(x, y);
    }
}

void SpineWidget::setFlipped(bool flipped) {
    if (skeleton_) {
        skeleton_->setScaleX(flipped ? -1.0f : 1.0f);
    }
}

float SpineWidget::animationDuration(const QString &name) const {
    if (!skeleton_data_) {
        return 0.0f;
    }
    spine::Animation *anim = skeleton_data_->findAnimation(spine::String(name.toUtf8().constData()));
    return anim ? anim->getDuration() : 0.0f;
}

void SpineWidget::setAutoReturnToRelax(bool enabled) {
    auto_return_to_relax_ = enabled;
}

void SpineWidget::setMaskFrozen(bool frozen) {
    mask_frozen_ = frozen;
}

// 用骨骼包围盒把模型居中到当前视口
void SpineWidget::fitToView() {
    if (!skeleton_) {
        return;
    }
    spine::Vector<float> buffer;
    float bx = 0, by = 0, bw = 0, bh = 0;
    skeleton_->getBounds(bx, by, bw, bh, buffer);
    if (bw <= 0 || bh <= 0) {
        return;
    }
    qreal dpr = devicePixelRatioF();
    float vpW = static_cast<float>(width() * dpr);
    float vpH = static_cast<float>(height() * dpr);
    skeleton_->setPosition((vpW - bw) / 2.0f - bx, (vpH - bh) / 2.0f - by);
}

QStringList SpineWidget::animations() const {
    QStringList list;
    if (!skeleton_data_) {
        return list;
    }
    auto &anims = skeleton_data_->getAnimations();
    for (size_t i = 0; i < anims.size(); ++i) {
        list << QString::fromUtf8(anims[i]->getName().buffer());
    }
    return list;
}

void SpineWidget::initializeGL() {
    initializeOpenGLFunctions();
    if (!atlas_ || !buildShaders()) {
        return;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    uploadTextures();

    vao_.create();
    vbo_pos_.create();
    vbo_uv_.create();
    vbo_color_.create();
    ibo_.create();
}

void SpineWidget::resizeGL(int w, int h) {
    qreal dpr = devicePixelRatioF();
    glViewport(0, 0, static_cast<GLsizei>(w * dpr), static_cast<GLsizei>(h * dpr));
    // 尺寸确定后重新居中：构造期调用 fitToView 时宽度可能仍是默认值，导致角色偏位
    fitToView();
}

void SpineWidget::paintGL() {
    if (!skeleton_ || !program_ || !program_->isLinked()) {
        return;
    }
    // 帧间隔
    qint64 now = clock_.elapsed();
    float dt = static_cast<float>(now - last_frame_time_) / 1000.0f;
    last_frame_time_ = now;
    if (dt > 0.25f) {
        dt = 0.25f;
    }

    // 每 6 帧（约 10Hz）更新一次窗口形状，使窗口贴合角色边缘；
    // 移动期间冻结，避免 grabFramebuffer 回读造成周期性掉帧
    if (++mask_frame_counter_ >= 6) {
        mask_frame_counter_ = 0;
        if (!mask_frozen_) {
            updateWindowMask();
        }
    }

    qreal dpr = devicePixelRatioF();
    float vpW = static_cast<float>(width() * dpr);
    float vpH = static_cast<float>(height() * dpr);
    glViewport(0, 0, static_cast<GLsizei>(vpW), static_cast<GLsizei>(vpH));
    glClear(GL_COLOR_BUFFER_BIT);
    qDebug() << "窗口:" << width() << "x" << height() << "hitRegion外接矩形:" << region_.boundingRect();
    anim_state_->update(dt);
    anim_state_->apply(*skeleton_);
    skeleton_->updateWorldTransform();

    program_->bind();
    program_->setUniformValue("uTex", 0);
    program_->setUniformValue("uViewport", QVector2D(vpW, vpH));

    vao_.bind();
    glActiveTexture(GL_TEXTURE0);

    auto &drawOrder = skeleton_->getDrawOrder();
    for (size_t i = 0; i < drawOrder.size(); ++i) {
        drawSlot(drawOrder[i]);
    }
    clipper_.clipEnd();

    vao_.release();
    program_->release();
}

void SpineWidget::drawSlot(spine::Slot *slot) {
    spine::Attachment *attachment = slot->getAttachment();
    if (!attachment) {
        clipper_.clipEnd(*slot);
        return;
    }

    if (slot->getColor().a == 0 || !slot->getBone().isActive()) {
        clipper_.clipEnd(*slot);
        return;
    }

    float *positions = nullptr;
    float *uvs = nullptr;
    const unsigned short *indices = nullptr;
    int numVertices = 0;
    int numIndices = 0;
    void *texture = nullptr;
    spine::Color *attachmentColor = nullptr;

    if (attachment->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
        auto *region = static_cast<spine::RegionAttachment *>(attachment);
        attachmentColor = &region->getColor();
        if (attachmentColor->a == 0) {
            clipper_.clipEnd(*slot);
            return;
        }
        world_vertices_.setSize(8, 0);
        region->computeWorldVertices(slot->getBone(), world_vertices_, 0, 2);
        positions = world_vertices_.buffer();
        uvs = region->getUVs().buffer();
        indices = kQuadIndices;
        numVertices = 4;
        numIndices = 6;
        auto *atlasRegion = static_cast<spine::AtlasRegion *>(region->getRendererObject());
        texture = atlasRegion->page->getRendererObject();
    } else if (attachment->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
        auto *mesh = static_cast<spine::MeshAttachment *>(attachment);
        attachmentColor = &mesh->getColor();
        if (attachmentColor->a == 0) {
            clipper_.clipEnd(*slot);
            return;
        }
        world_vertices_.setSize(mesh->getWorldVerticesLength(), 0);
        mesh->computeWorldVertices(*slot, 0, mesh->getWorldVerticesLength(), world_vertices_.buffer(), 0, 2);
        positions = world_vertices_.buffer();
        uvs = mesh->getUVs().buffer();
        indices = mesh->getTriangles().buffer();
        numVertices = static_cast<int>(mesh->getWorldVerticesLength() / 2);
        numIndices = mesh->getTriangles().size();
        auto *atlasRegion = static_cast<spine::AtlasRegion *>(mesh->getRendererObject());
        texture = atlasRegion->page->getRendererObject();
    } else if (attachment->getRTTI().isExactly(spine::ClippingAttachment::rtti)) {
        clipper_.clipStart(*slot, static_cast<spine::ClippingAttachment *>(attachment));
        return;
    } else {
        return;
    }

    if (clipper_.isClipping()) {
        clipper_.clipTriangles(positions, const_cast<unsigned short *>(indices), numIndices, uvs, 2);
        auto &clippedVerts = clipper_.getClippedVertices();
        auto &clippedIndices = clipper_.getClippedTriangles();
        positions = clippedVerts.buffer();
        uvs = clipper_.getClippedUVs().buffer();
        indices = clippedIndices.buffer();
        numVertices = static_cast<int>(clippedVerts.size() / 2);
        numIndices = clippedIndices.size();
    }

    GLuint tex = textures_.value(static_cast<QImage *>(texture), 0);
    if (tex == 0 || numVertices == 0 || numIndices == 0) {
        clipper_.clipEnd(*slot);
        return;
    }

    spine::Color &skelColor = skeleton_->getColor();
    spine::Color &slotColor = slot->getColor();
    uint8_t r = static_cast<uint8_t>(skelColor.r * slotColor.r * attachmentColor->r * 255);
    uint8_t g = static_cast<uint8_t>(skelColor.g * slotColor.g * attachmentColor->g * 255);
    uint8_t b = static_cast<uint8_t>(skelColor.b * slotColor.b * attachmentColor->b * 255);
    uint8_t a = static_cast<uint8_t>(skelColor.a * slotColor.a * attachmentColor->a * 255);
    uint32_t color = (a << 24) | (r << 16) | (g << 8) | b;

    color_buffer_.fill(color, numVertices);

    glBindTexture(GL_TEXTURE_2D, tex);

    switch (slot->getData().getBlendMode()) {
        case spine::BlendMode_Additive:
            glBlendFunc(GL_ONE, GL_ONE);  // PMA 下加法混合
            break;
        // Multiply/Screen 依赖背景色，但透明桌宠窗口的帧缓冲是全透明黑，
        // 与黑色相乘会产生黑斑，这里统一退化为普通混合
        case spine::BlendMode_Multiply:
        case spine::BlendMode_Screen:
        default:
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);  // 纹理已预乘 alpha
            break;
    }

    vbo_pos_.bind();
    glBufferData(GL_ARRAY_BUFFER, numVertices * 2 * static_cast<int>(sizeof(float)), positions, GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);

    vbo_uv_.bind();
    glBufferData(GL_ARRAY_BUFFER, numVertices * 2 * static_cast<int>(sizeof(float)), uvs, GL_STREAM_DRAW);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(1);

    vbo_color_.bind();
    glBufferData(GL_ARRAY_BUFFER, numVertices * static_cast<int>(sizeof(uint32_t)), color_buffer_.constData(), GL_STREAM_DRAW);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, nullptr);
    glEnableVertexAttribArray(2);

    ibo_.bind();
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, numIndices * static_cast<int>(sizeof(uint16_t)), indices, GL_STREAM_DRAW);

    glDrawElements(GL_TRIANGLES, numIndices, GL_UNSIGNED_SHORT, nullptr);

    clipper_.clipEnd(*slot);
}

// 从帧缓冲提取角色不透明区域，供 PetWindow 做鼠标穿透判定
// 注意：不用 setMask，否则角色身上的半透明像素会被挖洞（透出背景形成黑斑）
void SpineWidget::updateWindowMask() {
    if (width() <= 0 || height() <= 0) {
        return;
    }
    QImage frame = grabFramebuffer().convertToFormat(QImage::Format_ARGB32);
    if (frame.isNull()) {
        return;
    }
    // grabFramebuffer 返回物理像素，命中区域用逻辑像素
    qreal dpr = devicePixelRatioF();
    QRegion region;
    for (int y = 0; y < frame.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(frame.constScanLine(y));
        int x0 = -1;
        for (int x = 0; x < frame.width(); ++x) {
            bool opaque = qAlpha(line[x]) > 16;
            if (opaque && x0 < 0) {
                x0 = x;
            } else if (!opaque && x0 >= 0) {
                region += QRect(QPoint(x0 / dpr, y / dpr), QPoint(x / dpr, y / dpr));
                x0 = -1;
            }
        }
        if (x0 >= 0) {
            region += QRect(QPoint(x0 / dpr, y / dpr), QPoint(frame.width() / dpr, y / dpr));
        }
    }
    region_ = region;
}

void SpineWidget::uploadTextures() {
    if (!atlas_) {
        return;
    }
    auto &pages = atlas_->getPages();
    for (size_t i = 0; i < pages.size(); ++i) {
        QImage *image = static_cast<QImage *>(pages[i]->getRendererObject());
        if (!image) {
            continue;
        }
        textures_.insert(image, createGlTexture(*image));
    }
}

bool SpineWidget::buildShaders() {
    program_ = new QOpenGLShaderProgram(this);
    if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexSrc)) {
        qWarning() << program_->log();
        return false;
    }
    if (!program_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentSrc)) {
        qWarning() << program_->log();
        return false;
    }
    if (!program_->link()) {
        qWarning() << program_->log();
        return false;
    }
    return true;
}

GLuint SpineWidget::createGlTexture(const QImage &image) {
    // 图集本身已是预乘 alpha，直接上传，不做二次预乘
    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    // 用 NEAREST 避免双线性过滤把透明黑 texel 混入边缘，产生 rgb > alpha 的黑点
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba.width(), rgba.height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits());
    return tex;
}

QSize SpineWidget::characterSize() const {
    if (!skeleton_) {
        return QSize();
    }
    spine::Vector<float> buffer;
    float bx = 0, by = 0, bw = 0, bh = 0;
    skeleton_->getBounds(bx, by, bw, bh, buffer);
    if (bw <= 0.0f || bh <= 0.0f) {
        return QSize();
    }
    // getBounds 返回物理像素（fitToView 里用 vpW = width()*dpr 与之比较可证），
    // 而 QWidget::resize 用逻辑像素，所以除以 DPR
    qreal dpr = devicePixelRatioF();
    if (dpr <= 0.0) {
        dpr = 1.0;
    }
    return QSize(qRound(bw / dpr), qRound(bh / dpr));
}