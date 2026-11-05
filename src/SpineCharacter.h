#pragma once

#include <QString>
#include <QPoint>
#include <memory>
#include <spine/Vector.h>

class QWidget;

namespace spine {
    class Atlas;
    class SkeletonData;
    class Skeleton;
    class AnimationStateData;
    class AnimationState;
}

// 移动中的状态机
enum class MovingState {
    MOVE,
    RELAX,
    INTERACT,
    SIT_OR_SLEEP,
    BOUNDARY,
    NONE,
};

class SpineCharacter {
public:
    SpineCharacter();
    ~SpineCharacter();
    bool load(const QString &atlasPath, const QString &skelPath);
    void update(float deltaSeconds);

    spine::Skeleton *skeleton() const { return skeleton_.get(); } //获取骨骼

    void Interact(); //互动
    void Move(); //移动
    void Relax(); //放松
    void Sit(); //坐下
    void Sleep(); //睡觉
    void SetMovingState(MovingState state); //设置移动状态
    void Moving(float deltaSeconds); //移动中的事件
    bool Boundary(); //边界情况
    
    void setHost(QWidget *view) { view_ = view; }
    float moveRemainder() const { return move_remainder_; } //获取移动余数
    void OnClick(); //点击事件
    bool Contains(const QPoint &pos); //判断点击是否落在小人身上

    void UpdateWindowMovement(float deltaSeconds); 
    void UpdateBoundary();
    void UpdateBoundaryTurnDirection(float deltaSeconds);
    void InteractInMoving();
    void SitOrSleepInMoving();
    void DefaultInMoving();

private:
    std::unique_ptr<spine::Atlas> atlas_; //纹理集
    std::unique_ptr<spine::SkeletonData> skeleton_data_; //骨骼数据
    std::unique_ptr<spine::Skeleton> skeleton_; //骨骼
    std::unique_ptr<spine::AnimationStateData> state_data_; //动画状态数据
    std::unique_ptr<spine::AnimationState> state_; //动画状态

    MovingState moving_state_ = MovingState::NONE; //移动状态
    float moving_state_time_ = 0.f; //移动状态时间
    float move_gap_ = 5.f; //移动间隙时间
    float sit_or_sleep_action_duration_ = 0.f; //move状态机下sit和sleep动作持续时间

    MovingState action_after_relax_ = MovingState::MOVE; //放松后执行的动作
    bool moving_sleep_ = false; //是否在睡眠状态    
    float pet_scale_ = 0.45f; //桌宠缩放，正比

    int move_dir_ = 1; //移动方向: 1为右, -1为左
    float move_speed_ = 20.f; //移动速度
    float move_remainder_ = 0.f; //移动余数

    QWidget *view_ = nullptr;
    spine::Vector<float> bounds_vertices_; //边界顶点
    int boundary_turn_dir_ = 1; //边界转向方向: 1为右, -1为左
};
