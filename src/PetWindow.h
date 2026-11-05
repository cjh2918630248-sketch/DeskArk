#ifndef PETWINDOW_H
#define PETWINDOW_H

#include <QWidget>
#include <QPoint>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>

class QLabel;
class QPushButton;
class SpineCharacter;
class PetView;

class PetWindow : public QWidget
{
    Q_OBJECT

public:
    PetWindow(QWidget *parent = nullptr);
    ~PetWindow();


public slots:
    void on_role_action_triggered(QAction *action); //切换角色菜单项触发
    void on_role_prev_button_clicked(); //切换角色菜单上一页按钮触发
    void on_role_next_button_clicked(); //切换角色菜单下一页按钮触发

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QPoint drag_offset_;
    QSystemTrayIcon *tray_icon_ = nullptr; //托盘图标
    QMenu *tray_icon_menu_ = nullptr; //托盘右键菜单
    bool is_visible_ = true; //窗口是否可见
    std::unique_ptr<SpineCharacter> spine_character_; //桌宠骨骼
    PetView *pet_view_ = nullptr; //桌宠视图
    QTimer *timer_ = nullptr;
    QMenu *pet_view_menu_ = nullptr; //桌宠视图右键菜单
    QElapsedTimer frame_clock_; //帧时间
    qint64 next_frame_ns_ = 0; //下一帧时间
    QPoint press_pos_; //按下位置
    bool dragging_ = false; //是否拖拽
    QString startup_model_; //启动时纹理路径

    QVector<QPair<QString, QString>> roles_; //切换角色菜单角色列表
    int role_page_ = 0; //切换角色菜单页码
    QLabel *role_page_label_ = nullptr; //切换角色菜单页码标签
    QPushButton *role_prev_button_ = nullptr; //切换角色菜单上一页按钮
    QPushButton *role_next_button_ = nullptr; //切换角色菜单下一页按钮
    QVector<QAction *> role_actions_; //切换角色菜单项
    QMenu *role_menu_ = nullptr; //切换模型子菜单(需要指针做 resize/聚焦)

    void init(); //初始化
    void init_tray_icon(); //初始化托盘图标
    void init_tray_icon_menu(); //初始化托盘右键菜单
    void init_spine_character(); //初始化桌宠骨骼
    void init_pet_view(); //初始化桌宠视图
    void init_pet_view_menu(); //初始化桌宠视图右键菜单
    void init_pet_view_menu_action(); //初始化桌宠视图右键菜单项
    void init_timer(); //初始化定时器
    void switch_model(const QString &model); //切换模型
    void reload_role_page(); //加载切换角色菜单
    void read_roleselect_json(); //读取角色选择JSON
    void prompt_role_search(); //弹出角色搜索对话框

    void read_startup_model();  // 只读 JSON，取启动模型名
    void load_startup_model();  // 真正的 atlas/skel 加载 + 纹理上传
    void prewarm_menus(); //预热右键菜单，消除首次弹出的卡顿
};
#endif // PETWINDOW_H
