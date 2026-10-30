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
    void mousePressEvent(QMouseEvent *event) override; // 鼠标按下事件
    void mouseMoveEvent(QMouseEvent *event) override; // 鼠标移动事件
    void mouseReleaseEvent(QMouseEvent *event) override; // 鼠标释放事件
    void contextMenuEvent(QContextMenuEvent *event) override; // 右键菜单事件
    // 点击事件只响应在角色模型上，其余点击穿透到下层桌面
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override; // Windows事件处理

private:
    void cycleAnimation(); // 单击角色时循环播放动画
    void setupTray(); // 初始化系统托盘图标
    void loadRoles(); // 加载角色映射
    void switchRole(const QString &modelDir); // 切换到指定模型目录的角色
    void playPetAnimation(const QString &name); // 播放指定动画
    void startMove(); // 进入移动模式
    void stopMove(); // 停止移动模式
    void tickMove(); // 移动窗口
    // 移动中随机休息：播放除 Default/Move 外的动作，随后恢复移动
    void beginMoving(); // 开始移动行为
    void enterBoundaryPause(); // 进入边界后的暂停(relax)阶段
    void advancePause(); // 暂停结束后状态
    void doBreak(); // 移动中随机休息事件
    void startPauseTimer(int ms); // 启动单次暂停阶段计时器

    // 移动/暂停阶段
    enum class MovePhase { 
        Moving,  // 移动中 
        BoundaryRelax,  // 边界后的暂停(relax)阶段
        BreakRelax,  // 随机休息后的暂停(relax)阶段
        BreakSitSleep,  // 随机休息后的sit、sleep二选一阶段
        BreakRelaxOut,  // 随机休息后的sit、sleep二选一执行后的relax阶段
        BreakOneShot  // 随机休息后的单次互动触发阶段   
    };

    SpineWidget *spine_ = nullptr;  // 角色模型
    QSystemTrayIcon *tray_ = nullptr;  // 系统托盘图标
    QMap<QString, QString> roles_;  // 显示名 → 模型目录
    QPoint press_pos_;  // 鼠标按下时的坐标
    QPoint drag_offset_;  // 按下点相对窗口左上角的偏移量
    bool dragging_ = false;  // 是否正在拖动窗口
    int anim_index_ = -1;  // 当前播放的动画索引, -1为初始值

    QTimer *move_timer_ = nullptr;  // 移动窗口定时器
    QTimer *break_timer_ = nullptr;   // 移动中安排下一次随机休息(单次触发)
    QTimer *pause_timer_ = nullptr;   // 暂停阶段计时(边界Relax/休息过渡/SitSleep)
    int move_dir_ = 1;   // 人物朝向: 1 向右, -1 向左
    float move_step_ = 4.0f / 6.0f;  // 每16ms的像素移动基准
    float move_x_ = 0.0f;  // 累积的精确X坐标
    QElapsedTimer move_clock_;  // 基于真实时间计算位移，消除定时器抖动
    bool move_active_ = false;  // 是否处于移动模式, true为移动中, false为暂停中
    MovePhase phase_ = MovePhase::Moving;  // 当前状态机阶段
    QString break_name_;  // 当前随机休息动作名
};
