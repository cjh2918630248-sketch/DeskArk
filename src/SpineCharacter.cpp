#include "SpineCharacter.h"

#include <cstring>
#include <cmath>
#include <QWidget>
#include <QScreen>
#include <QDebug>
#include <QRandomGenerator>
#include <spine/Atlas.h>
#include <spine/Skeleton.h>
#include <spine/SkeletonBinary.h>
#include <spine/SkeletonData.h>
#include <spine/Animation.h>
#include <spine/AnimationState.h>
#include <spine/AnimationStateData.h>
#include <spine/Vector.h>

SpineCharacter::SpineCharacter()
{
}

SpineCharacter::~SpineCharacter() = default;

bool SpineCharacter::load(const QString &atlasPath, const QString &skelPath)
{
    const QByteArray atlasBytes = atlasPath.toUtf8();
    const QByteArray skelBytes = skelPath.toUtf8();

    atlas_ = std::make_unique<spine::Atlas>(atlasBytes.constData(), nullptr, false);
    spine::SkeletonBinary binary(atlas_.get());
    if (atlas_->getPages().size() == 0) {
        qDebug() << "atlas读取失败";
        return false;
    }

    spine::SkeletonData *data = binary.readSkeletonDataFile(skelBytes.constData());
    if (!data) {
        qDebug() << binary.getError().buffer();
        return false;
    }

    qDebug() << "动画数量: " << data->getAnimations().size();
    for (size_t i = 0; i < data->getAnimations().size(); ++i) {
        qDebug() << data->getAnimations()[i]->getName().buffer();
    }
    qDebug() << "动画读取成功";
    skeleton_data_.reset(data);
    state_data_ = std::make_unique<spine::AnimationStateData>(skeleton_data_.get());
    state_data_->setDefaultMix(0.3f);
    skeleton_ = std::make_unique<spine::Skeleton>(skeleton_data_.get());
    if (spine::Skin *skin = skeleton_data_->getDefaultSkin()) {
        skeleton_->setSkin(skin);
    }
    skeleton_->setSlotsToSetupPose();
    state_ = std::make_unique<spine::AnimationState>(state_data_.get());
    skeleton_->setScaleX(pet_scale_);
    skeleton_->setScaleY(pet_scale_);
    state_->setAnimation(0, "Relax", true);
    update(0.016f);
    return true;
}

void SpineCharacter::update(float deltaSeconds)
{
    if (!state_) {return;}
    UpdateWindowMovement(deltaSeconds); //更新窗口移动
    state_->update(deltaSeconds); //更新动画
    state_->apply(*skeleton_); //应用动画
    skeleton_->updateWorldTransform(); //更新变换
    if (moving_state_ == MovingState::MOVE && Boundary() && move_dir_ != boundary_turn_dir_) { //边界情况
        UpdateBoundary(); //更新边界
    }
    if (moving_state_ == MovingState::BOUNDARY) {
        UpdateBoundaryTurnDirection(deltaSeconds); //更新边界转向方向
        return;
    }
    if (moving_state_ != MovingState::NONE) {
        Moving(deltaSeconds);
    }
}

void SpineCharacter::Interact()
{
    if (!state_)
        return;
    state_->setAnimation(0, "Interact", false);
    qDebug() << "Aanimation: Interact";
    state_->addAnimation(0, "Relax", true, 0);
}

void SpineCharacter::Move()
{
    if (!state_ || moving_state_ != MovingState::MOVE)
        return;
    move_gap_ = 5.f + static_cast<float>(QRandomGenerator::global()->bounded(5.0));
    state_->setAnimation(0, "Move", true);
    qDebug() << "Animation: MOVING::MOVE";
}

void SpineCharacter::Relax()
{
    if (!state_)
        return;
    state_->setAnimation(0, "Relax", true);
    qDebug() << "Aanimation: Relax";
}

void SpineCharacter::Sit()
{
    if (!state_)
        return;
    state_->setAnimation(0, "Sit", true);
    qDebug() << "Aanimation: Sit";
}

void SpineCharacter::Sleep()
{
    if (!state_)
        return;
    state_->setAnimation(0, "Sleep", true);
    qDebug() << "Aanimation: Sleep";
}


void SpineCharacter::SetMovingState(MovingState state)
{
    moving_state_ = state;
    moving_state_time_ = 0.f;
    Move();
}

void SpineCharacter::Moving(float deltaSeconds) {
    if (moving_state_ == MovingState::MOVE) {
        moving_state_time_ += deltaSeconds;
        if (moving_state_time_ >= move_gap_) {
            moving_state_time_ = 0.f;
            switch (QRandomGenerator::global()->bounded(3)) {
            case 0:
                InteractInMoving(); //互动
                break;
            case 1:
                SitOrSleepInMoving(); //坐下或睡觉
                break;
            default:
                DefaultInMoving(); //默认
                break;
            }
        }
    } else if (moving_state_ == MovingState::INTERACT || moving_state_ == MovingState::SIT_OR_SLEEP) {
        spine::TrackEntry *entry = state_->getCurrent(0);
        if (entry && std::strcmp(entry->getAnimation()->getName().buffer(), "Move") == 0)
            SetMovingState(MovingState::MOVE);
    }
}

bool SpineCharacter::Boundary()
{
    if (!skeleton_ || !view_ || !view_->screen())
        return false;
    float minX = 0.f, minY = 0.f, width = 0.f, height = 0.f; //边界顶点
    skeleton_->getBounds(minX, minY, width, height, bounds_vertices_); //获取边界
    if (width <= 0.f)
        return false;

    const float viewW = view_->width(); //获取视图宽度
    const int left = view_->mapToGlobal(QPoint(qRound(minX + viewW / 2.f), 0)).x(); //获取左边界
    const int right = view_->mapToGlobal(QPoint(qRound(minX + width + viewW / 2.f), 0)).x(); //获取右边界
    const QRect screen = view_->screen()->geometry(); //获取屏幕几何

    const bool hitLeft = left <= screen.left(); //判断是否撞到左边界
    const bool hitRight = right >= screen.right(); //判断是否撞到右边界
    if (hitLeft && !hitRight) {
        boundary_turn_dir_ = 1; //转向方向: 1为右
        return true;
    }
    if (hitRight && !hitLeft) {
        boundary_turn_dir_ = -1; //转向方向: -1为左
        return true;
    }
    return false; //未撞到边界
}

void SpineCharacter::OnClick()
{
    if (!state_ || moving_state_ != MovingState::NONE)
        return;
    spine::TrackEntry *entry = state_->getCurrent(0);
    if (!entry || !entry->getAnimation())
        return;
    const char *name = entry->getAnimation()->getName().buffer();
    if (std::strcmp(name, "Relax") == 0)
        Interact();
    else if (std::strcmp(name, "Sit") == 0 || std::strcmp(name, "Sleep") == 0)
        Relax();
}

bool SpineCharacter::Contains(const QPoint &pos)
{
    if (!skeleton_ || !view_)
        return false;
    float minX = 0.f, minY = 0.f, boundsW = 0.f, boundsH = 0.f;
    skeleton_->getBounds(minX, minY, boundsW, boundsH, bounds_vertices_);
    if (boundsW <= 0.f || boundsH <= 0.f)
        return false;
    const float viewW = view_->width();
    const float viewH = view_->height();
    constexpr float originY = 85.f;
    const float left = minX + viewW / 2.f;
    const float right = minX + boundsW + viewW / 2.f;
    const float top = viewH / 2.f - (minY + boundsH - originY);
    const float bottom = viewH / 2.f - (minY - originY);
    return pos.x() >= left && pos.x() <= right && pos.y() >= top && pos.y() <= bottom;
}

void SpineCharacter::UpdateWindowMovement(float deltaSeconds)
{
    if (moving_state_ != MovingState::MOVE || !view_ || !view_->window())
        return;

    move_remainder_ += move_dir_ * move_speed_ * deltaSeconds;
    const int pixels = static_cast<int>(std::floor(move_remainder_)); //向下取整
    if (pixels != 0) {
        QWidget *window = view_->window();
        window->move(window->x() + pixels, window->y());
        move_remainder_ -= static_cast<float>(pixels);
    }
}

void SpineCharacter::UpdateBoundary()
{
    moving_state_ = MovingState::BOUNDARY;
    moving_state_time_ = 0.f;
    Relax();
}

void SpineCharacter::UpdateBoundaryTurnDirection(float deltaSeconds)
{
    moving_state_time_ += deltaSeconds;
    if (moving_state_time_ >= 2.f) {
        move_dir_ = boundary_turn_dir_; //转向方向
        skeleton_->setScaleX(static_cast<float>(move_dir_) * pet_scale_);
        skeleton_->setScaleY(static_cast<float>(pet_scale_));
        SetMovingState(MovingState::MOVE);
    }
}

void SpineCharacter::InteractInMoving()
{
    moving_state_ = MovingState::INTERACT;
    state_->setAnimation(0, "Relax", true);
    state_->addAnimation(0, "Interact", false, 1.5f); //1.5s后执行Interact动作
    qDebug() << "Animation: MOVING::INTERACT";
    state_->addAnimation(0, "Relax", true, 0); //0s后执行Relax动作
    state_->addAnimation(0, "Move", true, 1.5f); //1.5s后执行Move动作
    qDebug() << "Animation: MOVING::MOVE";
}

void SpineCharacter::SitOrSleepInMoving()
{
    moving_state_ = MovingState::SIT_OR_SLEEP;
    sit_or_sleep_action_duration_ = 6.f + static_cast<float>(QRandomGenerator::global()->bounded(2.0));
    state_->setAnimation(0, "Relax", true);
    state_->addAnimation(0, "Sit", true, 1.5f);
    qDebug() << "Animation: MOVING::SIT";
    state_->addAnimation(0, "Relax", true, sit_or_sleep_action_duration_);
    state_->addAnimation(0, "Move", true, 1.5f);
    qDebug() << "Animation: MOVING::MOVE";
}

void SpineCharacter::DefaultInMoving()
{
    moving_state_ = MovingState::SIT_OR_SLEEP;
    sit_or_sleep_action_duration_ = 8.f + static_cast<float>(QRandomGenerator::global()->bounded(2.0));
    state_->setAnimation(0, "Relax", true);
    state_->addAnimation(0, "Sleep", true, 1.5f);
    qDebug() << "Animation: MOVING::SLEEP";
    state_->addAnimation(0, "Relax", true, sit_or_sleep_action_duration_);
    state_->addAnimation(0, "Move", true, 1.5f);
    qDebug() << "Animation: MOVING::MOVE";
}