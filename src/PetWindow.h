#pragma once

#include <QElapsedTimer>
#include <QMap>
#include <QPoint>
#include <QSystemTrayIcon>
#include <QWidget>

class SpineWidget;
class QTimer;

class PetWindow : public QWidget {
    Q_OBJECT
public:
    explicit PetWindow(QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    // 透明区域鼠标穿透：命中测试落到角色不透明区域外时返回 HTTRANSPARENT
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void cycleAnimation();
    void setupTray();
    // 读取 RoleSelect.json 的角色映射（显示名 → 模型目录）
    void loadRoles();
    // 切换到指定模型目录的角色
    void switchRole(const QString &modelDir);
    // Move：循环播放 + 定时器移动窗口，实现真正左右移动
    void playPetAnimation(const QString &name);
    void startMove();
    void stopMove();
    void tickMove();
    // 移动中随机休息：播放除 Default/Move 外的动作，随后恢复移动
    void beginMoving();
    void enterBoundaryPause();
    void advancePause();
    void doBreak();
    void startPauseTimer(int ms);

    // 移动/暂停阶段
    enum class MovePhase { Moving, BoundaryRelax, BreakRelax, BreakSitSleep, BreakRelaxOut, BreakOneShot };

    SpineWidget *spine_ = nullptr;
    QSystemTrayIcon *tray_ = nullptr;
    QMap<QString, QString> roles_;  // 显示名 → 模型目录
    QPoint press_pos_;
    QPoint drag_offset_;
    bool dragging_ = false;
    int anim_index_ = -1;

    QTimer *move_timer_ = nullptr;
    QTimer *break_timer_ = nullptr;   // 移动中安排下一次随机休息(单次触发)
    QTimer *pause_timer_ = nullptr;   // 暂停阶段计时(边界Relax/休息过渡/SitSleep)
    int move_dir_ = 1;   // 人物朝向: 1 向右, -1 向左
    float move_step_ = 4.0f / 6.0f;  // 每16ms移动像素(约原速1/3)，浮点累积保证平滑
    float move_x_ = 0.0f;  // 累积的精确X坐标(逻辑像素)
    QElapsedTimer move_clock_;  // 基于真实时间计算位移，消除定时器抖动
    bool move_active_ = false;  // 是否处于移动模式（含休息）
    MovePhase phase_ = MovePhase::Moving;
    QString break_name_;  // 当前随机休息动作名
};
