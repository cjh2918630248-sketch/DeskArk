#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QRegion>
#include <QTimer>
#include <QVector>

#include <spine/AnimationState.h>
#include <spine/AnimationStateData.h>
#include <spine/Atlas.h>
#include <spine/Skeleton.h>
#include <spine/SkeletonBinary.h>
#include <spine/SkeletonClipping.h>
#include <spine/SkeletonData.h>
#include <spine/TextureLoader.h>
#include <spine/Vector.h>

class SpineWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit SpineWidget(QWidget *parent = nullptr);
    ~SpineWidget() override;

    bool loadSkeleton(const QString &atlasPath, const QString &skelPath, float scale = 1.0f);
    // 释放旧模型并加载新模型，用于运行时切换角色
    bool loadModel(const QString &atlasPath, const QString &skelPath, float scale = 1.0f);
    void setAnimation(const QString &name, bool loop);
    // 播放动画（自动分类）：触发型（Interact）播完回 Relax，其余循环
    void playAnimation(const QString &name);
    void setSkeletonPosition(float x, float y);
    void fitToView();
    QStringList animations() const;
    // 角色不透明区域（逻辑坐标），用于鼠标命中测试
    const QRegion &hitRegion() const { return region_; }
    // 水平翻转角色（移动朝左时翻转，实现反向 move）
    void setFlipped(bool flipped);
    // 返回动画自然时长（秒），找不到返回 0
    float animationDuration(const QString &name) const;
    // 触发型动画播完后是否自动切回 Relax（移动期间的休息动作需关闭）
    void setAutoReturnToRelax(bool enabled);
    // 冻结窗口形状更新：移动期间跳过 grabFramebuffer，避免周期性卡顿
    void setMaskFrozen(bool frozen);
    // 返回角色当前姿态的可见包围盒尺寸（逻辑像素），用于紧贴角色设置窗口大小
    QSize characterSize() const;

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    class QtSpineTextureLoader : public spine::TextureLoader {
    public:
        void load(spine::AtlasPage &page, const spine::String &path) override;
        void unload(void *texture) override;
    };

    // 动画完成监听：触发型动画（非循环）播完自动切回 Relax
    class AnimListener : public spine::AnimationStateListenerObject {
    public:
        explicit AnimListener(SpineWidget *owner) : owner_(owner) {}
        void callback(spine::AnimationState *state, spine::EventType type,
                      spine::TrackEntry *entry, spine::Event *event) override;
    private:
        SpineWidget *owner_;
    };

    bool buildShaders();
    GLuint createGlTexture(const QImage &image);
    void uploadTextures();
    void drawSlot(spine::Slot *slot);
    void updateWindowMask();
    // 是否为触发型动画（点一下播放一次，播完回 Relax）
    static bool isTriggerAnimation(const QString &name);
    void handleAnimationComplete(spine::TrackEntry *entry);
    // 播放默认 Relax（循环），不存在则退回第一个动画
    void playDefaultAnimation();

    QtSpineTextureLoader texture_loader_;
    AnimListener anim_listener_{this};
    spine::Atlas *atlas_ = nullptr;
    spine::SkeletonData *skeleton_data_ = nullptr;
    spine::Skeleton *skeleton_ = nullptr;
    spine::AnimationStateData *anim_state_data_ = nullptr;
    spine::AnimationState *anim_state_ = nullptr;
    spine::SkeletonClipping clipper_;

    QOpenGLShaderProgram *program_ = nullptr;
    QOpenGLVertexArrayObject vao_;
    QOpenGLBuffer vbo_pos_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer vbo_uv_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer vbo_color_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer ibo_{QOpenGLBuffer::IndexBuffer};

    spine::Vector<float> world_vertices_;
    QVector<uint32_t> color_buffer_;

    QHash<QImage *, GLuint> textures_;
    QElapsedTimer clock_;
    QTimer *frame_timer_ = nullptr;

    qint64 last_frame_time_ = 0;
    int mask_frame_counter_ = 0;
    QRegion region_;
    bool auto_return_to_relax_ = true;
    bool mask_frozen_ = false;
};
