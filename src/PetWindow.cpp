#include "PetWindow.h"

#include <algorithm>
#include <QMouseEvent>
#include <QPainter>
#include <QAction>
#include <QStyle>
#include <QApplication>
#include <QWidgetAction>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QDialog>
#include <QVBoxLayout>

#include "SpineCharacter.h"
#include "PetView.h"
#include "Common.h"

constexpr int kRolePageSize = 20; //角色菜单页显示的条数

namespace
{
    bool FuzzyMatch(const QString &text, const QString &pattern, int &score)
    {
        if (pattern.isEmpty()) {
            score = 0;
            return true;
        }
        const int textLen = text.size();
        int ti = 0;          // 在 text 上滑动的游标
        int consecutive = 0; // 已连续命中的字符数
        score = 0;
        for (int pi = 0; pi < pattern.size(); ++pi) {
            const QChar pc = pattern.at(pi).toCaseFolded();
            bool hit = false;
            while (ti < textLen) {
                if (text.at(ti).toCaseFolded() == pc) {
                    hit = true;
                    score += 10;
                    score += 15 * consecutive; // 连续命中加分
                    if (ti == 0)
                        score += 20;            // 命中在开头再加分
                    ++consecutive;
                    ++ti;
                    break;
                }
                consecutive = 0;
                ++ti;
            }
            if (!hit)
                return false;
        }
        score -= textLen; // 命中数相同时，名字短的优先
        return true;
    }
} // namespace

PetWindow::PetWindow(QWidget *parent)
    : QWidget(parent)
{
    init();
}

PetWindow::~PetWindow() {
    spine_character_.reset();
}

void PetWindow::init()
{
    this->resize(260, 300);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus); //无边框+无任务栏+置顶
    setAttribute(Qt::WA_TranslucentBackground); //背景透明
    setAttribute(Qt::WA_ShowWithoutActivating); //show() 时不激活
    setAutoFillBackground(false); //禁用自动填充
    init_tray_icon();
    init_spine_character();
    init_pet_view();
    init_timer();
    // 关键：让首帧先跑完（完成 GL 初始化 + 着色器编译），再加载模型
    QCoreApplication::processEvents();
    QTimer::singleShot(0, this, &PetWindow::load_startup_model);
}

void PetWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
}

void PetWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    press_pos_ = event->globalPosition().toPoint();
    drag_offset_ = press_pos_ - frameGeometry().topLeft();
    dragging_ = false;
}

void PetWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;
    if ((event->globalPosition().toPoint() - press_pos_).manhattanLength() < 4)
        return;
    dragging_ = true;
    move(event->globalPosition().toPoint() - drag_offset_);
}

void PetWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || dragging_)
        return;
    const QPoint local = pet_view_->mapFromGlobal(event->globalPosition().toPoint());
    if (spine_character_->Contains(local))
        spine_character_->OnClick();
}

void PetWindow::init_tray_icon()
{
    init_tray_icon_menu();
    tray_icon_ = new QSystemTrayIcon(this);
    tray_icon_->setIcon(QIcon(":/assets/tray_icon.png"));
    tray_icon_->setToolTip(QStringLiteral("DeskArk"));
    tray_icon_->setContextMenu(tray_icon_menu_);
    tray_icon_->show();
}

void PetWindow::init_tray_icon_menu()
{
    tray_icon_menu_ = new QMenu(this);
    tray_icon_menu_->addAction(QStringLiteral("显示/隐藏"), this, [this] {
        is_visible_ ? hide() : show();
    });
    tray_icon_menu_->addAction(QStringLiteral("退出"), qApp, &QApplication::quit);
}

void PetWindow::init_spine_character()
{
    spine_character_ = std::make_unique<SpineCharacter>(); // 只是 new，几乎零成本
    read_startup_model();
}

void PetWindow::init_pet_view()
{
    pet_view_ = new PetView(this);
    spine_character_->setHost(pet_view_);
    pet_view_->resize(260, 300);
    pet_view_->setCharacter(spine_character_.get());
    init_pet_view_menu();
    pet_view_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(pet_view_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        pet_view_menu_->exec(pet_view_->mapToGlobal(pos));
    });
    pet_view_->show();
}

void PetWindow::init_timer()
{
    constexpr int kFrameIntervalMs = 8; //120fps 就写 8
    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    frame_clock_.start();
    connect(timer_, &QTimer::timeout, this, [this] {
        const qint64 now = frame_clock_.nsecsElapsed();
        if (now < next_frame_ns_)
            return;
        next_frame_ns_ = now + qint64(kFrameIntervalMs) * 1000000;
        spine_character_->update(float(kFrameIntervalMs) / 1000.f);
        pet_view_->update();
    });
    timer_->start(kFrameIntervalMs);
}

void PetWindow::init_pet_view_menu()
{
    pet_view_menu_ = new QMenu(this);
    role_menu_ = pet_view_menu_->addMenu(QStringLiteral("切换模型"));
    read_roleselect_json();
    role_menu_->addAction(QStringLiteral("搜索模型…"), this, &PetWindow::prompt_role_search);
    role_menu_->addSeparator();
    for (int i = 0; i < kRolePageSize; ++i) {
        QAction *action = role_menu_->addAction(QString());
        role_actions_.push_back(action);
    }
    connect(role_menu_, &QMenu::triggered, this, &PetWindow::on_role_action_triggered);
    role_menu_->addSeparator();
    auto *bar = new QWidget(role_menu_);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 4, 8, 4);
    role_prev_button_ = new QPushButton(QStringLiteral("上一页"), bar);
    role_page_label_ = new QLabel(bar);
    role_page_label_->setAlignment(Qt::AlignCenter);
    role_next_button_ = new QPushButton(QStringLiteral("下一页"), bar);
    layout->addWidget(role_prev_button_);
    layout->addWidget(role_page_label_, 1);
    layout->addWidget(role_next_button_);
    auto *bar_action = new QWidgetAction(role_menu_); //创建菜单项
    bar_action->setDefaultWidget(bar); //设置默认小部件
    role_menu_->addAction(bar_action); //添加菜单项
    connect(role_prev_button_, &QPushButton::clicked, this, &PetWindow::on_role_prev_button_clicked);
    connect(role_next_button_, &QPushButton::clicked, this, &PetWindow::on_role_next_button_clicked);
    connect(role_menu_, &QMenu::aboutToShow, this, &PetWindow::reload_role_page);
    init_pet_view_menu_action();
}

void PetWindow::switch_model(const QString &model)
{
    //const QString base = QStringLiteral("D:/CJH/Code/Project/DeskArk-Lite/models/%1/build_char_%1").arg(model);
    const QString base = App_File(QStringLiteral("models/%1/build_char_%1").arg(model));
    spine_character_->SetMovingState(MovingState::NONE);
    if (!spine_character_->load(base + QStringLiteral(".atlas"), base + QStringLiteral(".skel")))
        return;
    pet_view_->setTexture(base + QStringLiteral(".png"));
}

void PetWindow::reload_role_page()
{
    const int pages = roles_.isEmpty() ? 0 : (roles_.size() + kRolePageSize - 1) / kRolePageSize;
    if (role_page_ >= pages)
        role_page_ = std::max(0, pages - 1); //确保页码不超过总页数
    const int begin = role_page_ * kRolePageSize; //计算起始索引
    for (int i = 0; i < role_actions_.size(); ++i) {
        QAction *action = role_actions_[i]; //获取菜单项
        const int index = begin + i; //计算索引
        if (index >= roles_.size()) {
            action->setVisible(false); //隐藏菜单项
            action->setData(QString()); //清空数据
            continue;
        }
        action->setText(roles_[index].first); //设置菜单项文本
        action->setData(roles_[index].second); //设置菜单项数据
        action->setVisible(true); //显示菜单项
    }
    role_prev_button_->setEnabled(role_page_ > 0); //设置上一页按钮是否启用
    role_next_button_->setEnabled(pages > 0 && role_page_ + 1 < pages); //设置下一页按钮是否启用
    role_page_label_->setText(pages == 0
        ? QStringLiteral("0/0")
        : QStringLiteral("%1/%2").arg(role_page_ + 1).arg(pages));  //设置页码标签文本
}

void PetWindow::init_pet_view_menu_action()
{
    pet_view_menu_->addSeparator();
    pet_view_menu_->addAction(QStringLiteral("Interact"), this, [this] {
        spine_character_->SetMovingState(MovingState::NONE);
        spine_character_->Interact();
    });
    pet_view_menu_->addAction(QStringLiteral("Move"), this, [this] {
        spine_character_->SetMovingState(MovingState::MOVE);
    });
    pet_view_menu_->addAction(QStringLiteral("Relax"), this, [this] {
        spine_character_->SetMovingState(MovingState::NONE);
        spine_character_->Relax();
    });
    pet_view_menu_->addAction(QStringLiteral("Sit"), this, [this] {
        spine_character_->SetMovingState(MovingState::NONE);
        spine_character_->Sit();
    });
    pet_view_menu_->addAction(QStringLiteral("Sleep"), this, [this] {
        spine_character_->SetMovingState(MovingState::NONE);
        spine_character_->Sleep();
    });
    pet_view_menu_->addSeparator();
    pet_view_menu_->addAction(QStringLiteral("退出"), qApp, &QApplication::quit);
}

void PetWindow::read_roleselect_json()
{
    nlohmann::json roles;
    //Read_Json(QStringLiteral("D:/CJH/Code/Project/DeskArk-Lite/RoleSelect.json"), roles);
    Read_Json(App_File(QStringLiteral("RoleSelect.json")), roles);
    roles_.clear();
    for (auto it = roles.begin(); it != roles.end(); ++it) {
        roles_.push_back({
            QString::fromStdString(it.key()),
            QString::fromStdString(it.value().get<std::string>())
        });
    }
}

void PetWindow::on_role_action_triggered(QAction *action)
{
    const QString model = action->data().toString();
    if (model.isEmpty())
        return;
    switch_model(model);
    qDebug() << "切换模型: " << action->text();
}

void PetWindow::on_role_prev_button_clicked()
{
    if (role_page_ > 0) {
        --role_page_;
        reload_role_page();
    }
}

void PetWindow::on_role_next_button_clicked()
{
    ++role_page_;
    reload_role_page();
}

void PetWindow::prompt_role_search()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("搜索模型"));
    dlg.resize(280, 380);

    auto *layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *edit = new QLineEdit(&dlg);
    edit->setPlaceholderText(QStringLiteral("输入角色名"));
    edit->setClearButtonEnabled(true);

    auto *list = new QListWidget(&dlg);
    list->setSelectionMode(QAbstractItemView::SingleSelection);

    auto *hint = new QLabel(&dlg);
    hint->setStyleSheet(QStringLiteral("color:#888;"));

    layout->addWidget(edit);
    layout->addWidget(list, 1);
    layout->addWidget(hint);

    // 按关键词刷新列表
    auto refresh = [&](const QString &text) {
        const QString pattern = text.trimmed();

        QVector<int> indices; //roles_ 中应显示的下标
        if (pattern.isEmpty()) {
            indices.reserve(roles_.size());
            for (int i = 0; i < roles_.size(); ++i)
                indices.push_back(i);
        } else {
            QVector<QPair<int, int>> scored; //<score, 原下标>
            for (int i = 0; i < roles_.size(); ++i) {
                int score = 0;
                if (FuzzyMatch(roles_[i].first, pattern, score))
                    scored.push_back({score, i});
            }
            std::stable_sort(scored.begin(), scored.end(),
                             [](const QPair<int, int> &a, const QPair<int, int> &b) {
                                 return a.first > b.first; //分数高的在前
                             });
            indices.reserve(scored.size());
            for (const auto &item : scored)
                indices.push_back(item.second);
        }

        list->clear();
        for (int index : indices) {
            auto *item = new QListWidgetItem(roles_[index].first, list);
            item->setData(Qt::UserRole, roles_[index].second); //存模型名
        }
        if (list->count() > 0)
            list->setCurrentRow(0);

        hint->setText(pattern.isEmpty()
            ? QStringLiteral("共 %1 个角色").arg(list->count())
            : QStringLiteral("匹配 %1 个角色").arg(list->count()));
    };

    refresh(QString());
    connect(edit, &QLineEdit::textChanged, &dlg, [&](const QString &t) { refresh(t); });
    connect(edit, &QLineEdit::returnPressed, &dlg, &QDialog::accept); //回车选中第一项
    connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept); //双击选中

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QListWidgetItem *item = list->currentItem();
    if (!item)
        return;
    const QString model = item->data(Qt::UserRole).toString();
    if (model.isEmpty())
        return;
    switch_model(model);
    qDebug() << "搜索切换模型: " << item->text();
}

void PetWindow::read_startup_model()
{
    nlohmann::json roles;
    //Read_Json(QStringLiteral("D:/CJH/Code/Project/DeskArk-Lite/RoleSelect.json"), roles);
    Read_Json(App_File(QStringLiteral("RoleSelect.json")), roles);
    if (roles.empty())
        return;
    startup_model_ = QString::fromStdString(roles.begin().value().get<std::string>());
}

void PetWindow::load_startup_model()
{
    if (!spine_character_ || startup_model_.isEmpty() || !pet_view_)
        return;
    // const QString base = QStringLiteral("D:/CJH/Code/Project/DeskArk-Lite/models/%1/build_char_%1")
    //                          .arg(startup_model_);
    const QString base = App_File(QStringLiteral("models/%1/build_char_%1").arg(startup_model_));
    if (!spine_character_->load(base + QStringLiteral(".atlas"), base + QStringLiteral(".skel")))
        return;
    pet_view_->setTexture(base + QStringLiteral(".png"));
    QString name = startup_model_; 
    for (const auto &role : roles_) {
        if (role.second == startup_model_) {
            name = role.first;
            break;
        }
    }
    qDebug() << "初始化模型: " << name;
    // 模型就绪后再预热菜单，避免两者抢 GUI 线程
    QTimer::singleShot(50, this, &PetWindow::prewarm_menus);
}

void PetWindow::prewarm_menus()
{
    // 强制完成样式 polish（分配 QStyleOption、解析样式表）
    pet_view_menu_->ensurePolished();
    role_menu_->ensurePolished();
    for (QAction *action : role_actions_) {
        if (action)
            action->isEnabled(); //触发 action 的样式初始化
    }

    // 强制首次布局计算
    pet_view_menu_->sizeHint();
    role_menu_->sizeHint();

    // 离屏弹出一次，创建 popup 原生窗口 + 建立 DWM 合成通道
    const QPoint offscreen(-30000, -30000);
    pet_view_menu_->popup(offscreen);
    pet_view_menu_->hide();

    qDebug() << "[menu] 预热完成";
}
