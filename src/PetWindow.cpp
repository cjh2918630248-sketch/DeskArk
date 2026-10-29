#include "PetWindow.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QDebug>

#include "SpineWidget.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

PetWindow::PetWindow(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);

    spine_ = new SpineWidget(this);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(spine_);

    QString baseDir = QCoreApplication::applicationDirPath();
    QString atlasPath = baseDir + "/models/4231_clemnt/build_char_4231_clemnt.atlas";
    QString skelPath = baseDir + "/models/4231_clemnt/build_char_4231_clemnt.skel";

    if (!QFileInfo::exists(atlasPath) || !QFileInfo::exists(skelPath)) {
        atlasPath = QDir::currentPath() + "/models/4231_clemnt/build_char_4231_clemnt.atlas";
        skelPath = QDir::currentPath() + "/models/4231_clemnt/build_char_4231_clemnt.skel";
    }

    if (!spine_->loadSkeleton(atlasPath, skelPath, 0.5f)) {
        qWarning() << "skeleton load failed";
    }

    resize(320, 320);
    spine_->fitToView();

    loadRoles();
    setupTray();
}

void PetWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        stopMove();  // 拖拽时暂停自动移动，避免与定时器抢位置
        press_pos_ = event->globalPosition().toPoint();
        drag_offset_ = press_pos_ - frameGeometry().topLeft();
        event->accept();
    }
}

void PetWindow::mouseMoveEvent(QMouseEvent *event) {
    if (dragging_ && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - drag_offset_);
        event->accept();
    }
}

void PetWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        if ((event->globalPosition().toPoint() - press_pos_).manhattanLength() < 8) {
            cycleAnimation();
        }
        event->accept();
    }
}

void PetWindow::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);

    // 切换角色
    if (!roles_.isEmpty()) {
        QMenu *roleMenu = menu.addMenu(QStringLiteral("切换角色"));
        for (auto it = roles_.begin(); it != roles_.end(); ++it) {
            const QString displayName = it.key();
            const QString modelDir = it.value();
            roleMenu->addAction(displayName, this, [this, modelDir]() { switchRole(modelDir); });
        }
        menu.addSeparator();
    }

    const QStringList names = spine_->animations();
    for (const QString &name : names) {
        if (name.compare(QStringLiteral("Default"), Qt::CaseInsensitive) == 0) {
            continue;  // 隐藏 Default
        }
        menu.addAction(name, this, [this, name]() { playPetAnimation(name); });
    }
    menu.addSeparator();
    menu.addAction(QStringLiteral("退出"), []() { QApplication::quit(); });
    menu.exec(event->globalPos());
}

void PetWindow::cycleAnimation() {
    QStringList names;
    const QStringList all = spine_->animations();
    for (const QString &n : all) {
        if (n.compare(QStringLiteral("Default"), Qt::CaseInsensitive) != 0) {
            names << n;
        }
    }
    if (names.isEmpty()) {
        return;
    }
    anim_index_ = (anim_index_ + 1) % names.size();
    playPetAnimation(names[anim_index_]);
}

void PetWindow::loadRoles() {
    roles_.clear();

    // 优先 exe 目录，其次当前工作目录
    QString path = QCoreApplication::applicationDirPath() + "/RoleSelect.json";
    if (!QFileInfo::exists(path)) {
        path = QDir::currentPath() + "/RoleSelect.json";
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "RoleSelect.json not found:" << path;
        return;
    }
    QByteArray data = file.readAll();
    file.close();

    // 去掉可能的 UTF-8 BOM
    if (data.startsWith("\xEF\xBB\xBF")) {
        data.remove(0, 3);
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "RoleSelect.json parse error:" << err.errorString();
        return;
    }

    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        roles_.insert(it.key(), it.value().toString());
    }
}

void PetWindow::switchRole(const QString &modelDir) {
    stopMove();  // 取消移动/休息等所有动作状态，新角色由 loadSkeleton 自动播放 Relax
    QString baseDir = QCoreApplication::applicationDirPath();
    QString atlasPath = baseDir + "/models/" + modelDir + "/build_char_" + modelDir + ".atlas";
    QString skelPath = baseDir + "/models/" + modelDir + "/build_char_" + modelDir + ".skel";

    if (!QFileInfo::exists(atlasPath) || !QFileInfo::exists(skelPath)) {
        atlasPath = QDir::currentPath() + "/models/" + modelDir + "/build_char_" + modelDir + ".atlas";
        skelPath = QDir::currentPath() + "/models/" + modelDir + "/build_char_" + modelDir + ".skel";
    }

    if (!QFileInfo::exists(atlasPath) || !QFileInfo::exists(skelPath)) {
        qWarning() << "model files not found for role:" << modelDir;
        return;
    }

    if (!spine_->loadModel(atlasPath, skelPath, 0.5f)) {
        qWarning() << "switch role failed:" << modelDir;
        return;
    }
    anim_index_ = -1;
}

bool PetWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_NCHITTEST && spine_ && !spine_->hitRegion().isEmpty()) {
            // 屏幕坐标 → 窗口逻辑坐标
            QPoint globalPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
            // Windows 原生消息的坐标是物理像素，而 Qt 的 mapFromGlobal 使用逻辑坐标，
            // 高 DPI（缩放 >100%）下必须除以 devicePixelRatio 换算，否则命中测试错位。
            qreal dpr = devicePixelRatioF();
            QPoint logicalGlobal(qRound(globalPos.x() / dpr), qRound(globalPos.y() / dpr));
            QPoint local = mapFromGlobal(logicalGlobal);
            if (!spine_->hitRegion().contains(local)) {
                *result = HTTRANSPARENT;  // 点击穿透到下层窗口
                return true;
            }
        }
    }
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void PetWindow::setupTray() {
    tray_ = new QSystemTrayIcon(this);

    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(90, 130, 210));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(6, 6, 52, 52);
    tray_->setIcon(QIcon(pixmap));

    QMenu *menu = new QMenu(this);
    menu->addAction(QStringLiteral("隐藏/显示"), this, [this]() { setVisible(!isVisible()); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("退出"), []() { QApplication::quit(); });
    tray_->setContextMenu(menu);
    tray_->show();
}

void PetWindow::playPetAnimation(const QString &name) {
    if (name.compare(QStringLiteral("Move"), Qt::CaseInsensitive) == 0) {
        startMove();
        return;
    }
    stopMove();
    spine_->playAnimation(name);
}

void PetWindow::startMove() {
    stopMove();
    move_active_ = true;
    move_x_ = static_cast<float>(x());
    spine_->setAutoReturnToRelax(false);
    spine_->setMaskFrozen(true);
    beginMoving();
}

void PetWindow::stopMove() {
    move_active_ = false;
    phase_ = MovePhase::Moving;
    break_name_.clear();
    if (move_timer_) {
        move_timer_->stop();
    }
    if (break_timer_) {
        break_timer_->stop();
    }
    if (pause_timer_) {
        pause_timer_->stop();
    }
    //spine_->setFlipped(false);
    spine_->setAutoReturnToRelax(true);
    spine_->setMaskFrozen(false);
}

void PetWindow::beginMoving() {
    if (!move_active_) {
        return;
    }
    phase_ = MovePhase::Moving;
    spine_->setFlipped(move_dir_ < 0);
    spine_->setAnimation(QStringLiteral("Move"), true);
    move_clock_.restart();

    if (!move_timer_) {
        move_timer_ = new QTimer(this);
        move_timer_->setTimerType(Qt::PreciseTimer);
        connect(move_timer_, &QTimer::timeout, this, &PetWindow::tickMove);
    }
    move_timer_->start(16);

    // 移动中安排下一次随机休息：5~10 秒
    if (!break_timer_) {
        break_timer_ = new QTimer(this);
        break_timer_->setSingleShot(true);
        connect(break_timer_, &QTimer::timeout, this, &PetWindow::doBreak);
    }
    break_timer_->start(QRandomGenerator::global()->bounded(5000, 10001));
}

void PetWindow::tickMove() {
    if (!move_active_ || phase_ != MovePhase::Moving) {
        return;
    }
    QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const QRect avail = screen->availableGeometry();
    const QRect geo = frameGeometry();

    // 用角色可见区域判断边界：向左看左边界，向右看右边界，避免透明留白导致过早停或跑出屏幕
    int charLeft = 0;
    int charRight = geo.width() - 1;
    const QRegion hr = spine_->hitRegion();
    if (!hr.isEmpty()) {
        const QRect hb = hr.boundingRect();
        charLeft = hb.left();
        charRight = hb.right();
    }
    const int minX = avail.left() - charLeft;
    const int maxX = avail.right() - charRight;

    // 用真实经过时间计算位移，避免 QTimer 间隔抖动导致步进忽快忽慢
    const float dt = qMin(static_cast<float>(move_clock_.elapsed()) / 1000.0f, 0.05f);
    move_clock_.restart();
    move_x_ += move_dir_ * (move_step_ / 0.016f) * dt;
    bool hitBoundary = false;
    if (move_x_ <= minX) {
        move_x_ = static_cast<float>(minX);
        hitBoundary = true;
    } else if (move_x_ >= maxX) {
        move_x_ = static_cast<float>(maxX);
        hitBoundary = true;
    }

    move(qRound(move_x_), geo.y());

    if (hitBoundary) {
        enterBoundaryPause();
    }
}

void PetWindow::enterBoundaryPause() {
    if (!move_active_) {
        return;
    }
    phase_ = MovePhase::BoundaryRelax;
    if (move_timer_) {
        move_timer_->stop();
    }
    if (break_timer_) {
        break_timer_->stop();
    }
    // 边界停留：Relax 5 秒后反向继续移动
    spine_->setAnimation(QStringLiteral("Relax"), true);
    startPauseTimer(5000);
}

void PetWindow::advancePause() {
    if (!move_active_) {
        return;
    }
    switch (phase_) {
        case MovePhase::BoundaryRelax:
            move_dir_ = -move_dir_;  // 反向
            beginMoving();
            break;
        case MovePhase::BreakRelax:
            // Relax 过渡结束，进入 Sit/Sleep 10 秒
            phase_ = MovePhase::BreakSitSleep;
            spine_->setAnimation(break_name_, true);
            startPauseTimer(10000);
            break;
        case MovePhase::BreakSitSleep:
            // Sit/Sleep 结束，做 Relax 3 秒再继续 move
            phase_ = MovePhase::BreakRelaxOut;
            spine_->setAnimation(QStringLiteral("Relax"), true);
            startPauseTimer(1500);
            break;
        case MovePhase::BreakRelaxOut:
        case MovePhase::BreakOneShot:
            beginMoving();
            break;
        case MovePhase::Moving:
        default:
            break;
    }
}

void PetWindow::doBreak() {
    if (!move_active_ || phase_ != MovePhase::Moving) {
        return;
    }
    if (move_timer_) {
        move_timer_->stop();
    }

    // 候选：除 Default、Move、Relax 外（Relax 仅作边界停留和 Sit/Sleep 的过渡）
    QStringList candidates;
    const QStringList all = spine_->animations();
    for (const QString &n : all) {
        if (n.compare(QStringLiteral("Default"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (n.compare(QStringLiteral("Move"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (n.compare(QStringLiteral("Relax"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        candidates << n;
    }
    if (candidates.isEmpty()) {
        beginMoving();
        return;
    }

    const int idx = QRandomGenerator::global()->bounded(static_cast<int>(candidates.size()));
    break_name_ = candidates[idx];

    const bool isSitOrSleep = break_name_.compare(QStringLiteral("Sit"), Qt::CaseInsensitive) == 0 ||
                              break_name_.compare(QStringLiteral("Sleep"), Qt::CaseInsensitive) == 0;
    if (isSitOrSleep) {
        // 先 Relax 2 秒，再 Sit/Sleep 10 秒
        phase_ = MovePhase::BreakRelax;
        spine_->setAnimation(QStringLiteral("Relax"), true);
        startPauseTimer(2000);
    } else {
        // 其它（如 Interact）一次性播放，播完继续 move
        phase_ = MovePhase::BreakOneShot;
        spine_->setAnimation(break_name_, false);
        float dur = spine_->animationDuration(break_name_);
        int ms = dur > 0.0f ? qRound(dur * 1000.0f) : 1000;
        startPauseTimer(ms);
    }
}

void PetWindow::startPauseTimer(int ms) {
    if (!pause_timer_) {
        pause_timer_ = new QTimer(this);
        pause_timer_->setSingleShot(true);
        connect(pause_timer_, &QTimer::timeout, this, &PetWindow::advancePause);
    }
    pause_timer_->start(ms);
}
