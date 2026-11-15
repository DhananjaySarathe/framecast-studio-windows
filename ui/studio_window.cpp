#include "studio_window.hpp"
#include "studio_widgets.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QUrl>
#include <QUuid>
#include <future>

namespace framecast::ui {
namespace {
QLabel *label(const QString &title, const QString &role = {}, QWidget *parent = nullptr) {
    auto *l = new QLabel(title, parent);
    if (!role.isEmpty())
        l->setProperty("role", role);
    return l;
}
QPushButton *button(const QString &title, const QString &glyph = {}, const QString &name = {}) {
    auto *b = new QPushButton(title);
    if (!glyph.isEmpty())
        b->setIcon(icon(glyph));
    b->setCursor(Qt::PointingHandCursor);
    if (!name.isEmpty())
        b->setObjectName(name);
    b->setMinimumHeight(30);
    return b;
}
QWidget *line() {
    auto *w = new QWidget;
    w->setFixedHeight(1);
    w->setStyleSheet("background:#232733;");
    return w;
}
QString clockText(qint64 milliseconds) {
    auto s = milliseconds / 1000;
    return QString("%1:%2:%3")
        .arg(s / 3600, 2, 10, QChar('0'))
        .arg(s / 60 % 60, 2, 10, QChar('0'))
        .arg(s % 60, 2, 10, QChar('0'));
}
std::string bytes(const QString &value) {
    return value.toUtf8().toStdString();
}
QJsonArray layerJson(const std::vector<Layer> &layers) {
    QJsonArray out;
    for (const auto &l : layers)
        out.append(QJsonObject{{"source", QString::fromStdString(l.source)},
                               {"x", l.rect.x},
                               {"y", l.rect.y},
                               {"width", l.rect.width},
                               {"height", l.rect.height},
                               {"fit", int(l.fit)},
                               {"mask", int(l.mask)},
                               {"radius", l.radius},
                               {"opacity", l.opacity},
                               {"mirrored", l.mirrored}});
    return out;
}
std::vector<Layer> readLayers(const QJsonArray &array) {
    std::vector<Layer> out;
    for (auto value : array) {
        if (out.size() >= 32)
            break;
        auto o = value.toObject();
        auto source = o["source"].toString();
        if (source != "screen" && source != "camera")
            continue;
        Layer l;
        l.source = bytes(source);
        l.rect = {std::clamp(o["x"].toDouble(), 0., 1.), std::clamp(o["y"].toDouble(), 0., 1.),
                  std::clamp(o["width"].toDouble(1), .01, 1.), std::clamp(o["height"].toDouble(1), .01, 1.)};
        l.fit = static_cast<Fit>(std::clamp(o["fit"].toInt(), 0, 2));
        l.mask = static_cast<Mask>(std::clamp(o["mask"].toInt(), 0, 2));
        l.radius = std::clamp(o["radius"].toDouble(), 0., .5);
        l.opacity = std::clamp(o["opacity"].toDouble(1), 0., 1.);
        l.mirrored = o["mirrored"].toBool();
        out.push_back(l);
    }
    return out;
}
const char *studioStyle = R"(
QMainWindow,QDialog { background:#0B0D11; color:#EEF0F6; }
QWidget { color:#EEF0F6; font-size:12px; }
QLabel { background:transparent; }
QLabel[role="title"] {font-size:14px;font-weight:700;}
QLabel[role="muted"] {color:#A8AFBF;font-size:11px;}
QLabel[role="notice"] {background:#1A1E27;color:#EEF0F6;border:1px solid #2C3240;border-radius:6px;padding:10px;}
QPushButton,QToolButton {background:#1A1E27;color:#EEF0F6;border:1px solid #232733;border-radius:14px;padding:4px 12px;font-weight:600;}
QPushButton:hover,QToolButton:hover {background:#262C3A;border-color:#3B4253;}
QPushButton:pressed,QToolButton:pressed {background:#30364A;}
QPushButton:focus,QToolButton:focus,QComboBox:focus,QLineEdit:focus,QDoubleSpinBox:focus {border:1px solid #B8ADFF;}
QPushButton:disabled,QToolButton:disabled {color:#7F8798;background:#161A22;}
QPushButton:checked,QToolButton:checked {background:#262C3A;border-color:#7D6BFF;}
QPushButton[role="primary"] {background:#7D6BFF;color:#FFFFFF;border:1px solid #7D6BFF;}
QPushButton[role="primary"]:hover {background:#8F80FF;}
QPushButton[role="stop"] {background:#2B151B;border-color:#5C2230;color:#FFB3BC;}
QPushButton[role="stop"]:hover {background:#401C25;}
QPushButton[role="primary"]:disabled {background:#403766;color:#A8AFBF;border-color:#403766;}
QToolButton[role="tab"] {padding:0;border:0;border-radius:0;background:transparent;}
QToolButton[role="tab"]:checked {background:#1A1E27;border-bottom:2px solid #7D6BFF;}
QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox,QListWidget {background:#0C0E13;border:1px solid #2C3240;border-radius:6px;padding:6px;color:#EEF0F6;selection-background-color:#7D6BFF;}
QLineEdit:disabled {color:#7F8798;}
QComboBox QAbstractItemView {background:#1A1E27;color:#EEF0F6;selection-background-color:#262C3A;}
QListWidget::item {padding:8px;}
QListWidget::item:selected {background:#262C3A;color:#EEF0F6;}
QScrollArea {border:0;background:transparent;}
QScrollBar:vertical {background:#101318;width:7px;margin:0;}
QScrollBar::handle:vertical {background:#3B4253;min-height:24px;border-radius:3px;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0;}
QSlider::groove:horizontal {height:4px;background:#262C3A;border-radius:2px;}
QSlider::sub-page:horizontal {background:#7D6BFF;border-radius:2px;}
QSlider::handle:horizontal {background:#EEF0F6;width:12px;margin:-4px 0;border-radius:6px;}
QCheckBox {spacing:8px;}
QCheckBox::indicator {width:16px;height:16px;border:1px solid #7F8798;border-radius:4px;background:#0C0E13;}
QCheckBox::indicator:checked {background:#7D6BFF;border-color:#B8ADFF;}
QMenu {background:#1A1E27;border:1px solid #2C3240;padding:5px;}
QMenu::item {padding:7px 18px;}
QMenu::item:selected {background:#262C3A;}
QToolTip {background:#262C3A;color:#EEF0F6;border:1px solid #3B4253;padding:6px;}
)";
} // namespace

StudioWindow::StudioWindow(UiOptions options)
    : options_(options), scenes_(defaultScenes()), settings_("Framecast", "StudioWindows") {
    setWindowTitle("Framecast Studio");
    setMinimumSize(1180, 620);
    auto available = QGuiApplication::primaryScreen()->availableGeometry().size();
    resize(std::min(1440, std::max(1180, available.width() - 32)),
           std::min(900, std::max(620, available.height() - 48)));
    setStyleSheet(studioStyle);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#0B0D11"));
    palette.setColor(QPalette::WindowText, QColor("#EEF0F6"));
    palette.setColor(QPalette::Base, QColor("#0C0E13"));
    palette.setColor(QPalette::Text, QColor("#EEF0F6"));
    palette.setColor(QPalette::Button, QColor("#1A1E27"));
    palette.setColor(QPalette::ButtonText, QColor("#EEF0F6"));
    palette.setColor(QPalette::Highlight, QColor("#7D6BFF"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    setPalette(palette);
    config_.synthetic = options.synthetic;
    config_.width = options.width;
    config_.height = options.height;
    config_.seconds = 86400;
    recordDirectory_ = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Framecast";
    if (options_.persistent)
        restore();
    program_ = scenes_[programH_].scene;
    program_.vertical = scenes_[programV_].scene.vertical;
    candidate_ = program_;
    buildUi();
    publishScenes();
    publishGains();
    updateSceneButtons();
    updateLayers();
    connect(&timer_, &QTimer::timeout, this, [this] { refresh(); });
    timer_.start(50);
    if (options_.synthetic && options_.autoStart)
        startPreview();
}
StudioWindow::~StudioWindow() {
    controller_.stop();
}
QString StudioWindow::programName() const {
    return scenes_[canvas_ == 0 ? programH_ : programV_].name;
}

void StudioWindow::buildUi() {
    auto *root = new QWidget;
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    setCentralWidget(root);
    auto *toolbar = new QWidget;
    toolbar->setObjectName("studio-toolbar");
    toolbar->setStyleSheet("#studio-toolbar{background:#0E1015;}");
    toolbar->setFixedHeight(52);
    auto *top = new QHBoxLayout(toolbar);
    top->setContentsMargins(18, 0, 18, 0);
    top->setSpacing(10);
    top->addWidget(label("Framecast Studio", "title"));
    top->addStretch();
    auto *tabs = new QWidget;
    auto *tabLayout = new QHBoxLayout(tabs);
    tabLayout->setContentsMargins(3, 3, 3, 3);
    tabLayout->setSpacing(2);
    const QStringList stageNames = {"16:9", "9:16", "Both"}, stageIcons = {"horizontal", "vertical", "both"};
    for (int n = 0; n < 3; ++n) {
        auto *b = new QToolButton;
        b->setText(stageNames[n]);
        b->setIcon(icon(stageIcons[n]));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setCheckable(true);
        b->setObjectName("stage-" + QString::number(n));
        b->setAccessibleName("Show " + stageNames[n]);
        b->setMinimumWidth(74);
        b->setFixedHeight(28);
        tabLayout->addWidget(b);
        stageButtons_.push_back(b);
        connect(b, &QToolButton::clicked, this, [this, n] { setStage(n); });
    }
    top->addWidget(tabs);
    top->addStretch();
    destinationsButton_ = button("Destinations", "broadcast", "destinations");
    top->addWidget(destinationsButton_);
    connect(destinationsButton_, &QPushButton::clicked, this, [this] { showDestinations(); });
    sources_ = button("Add", "plus", "add-sources");
    top->addWidget(sources_);
    connect(sources_, &QPushButton::clicked, this, [this] { showSources(); });
    auto *settings = button("", "gear", "settings");
    settings->setFixedWidth(32);
    settings->setAccessibleName("Settings");
    settings->setToolTip("Settings");
    top->addWidget(settings);
    connect(settings, &QPushButton::clicked, this, [this] { showSettings(); });
    layout->addWidget(toolbar);
    layout->addWidget(line());
    auto *middle = new QHBoxLayout;
    middle->setSpacing(0);
    middle->setContentsMargins(0, 0, 0, 0);
    middle->addWidget(makeSidebar());
    programArea_ = new QWidget;
    programArea_->setObjectName("program-area");
    programArea_->setStyleSheet("#program-area{background:#07080B;}");
    auto *stageLayout = new QVBoxLayout(programArea_);
    stageLayout->setContentsMargins(18, 10, 18, 18);
    stageLayout->setSpacing(10);
    auto *programHeader = new QHBoxLayout;
    sceneLabel_ = label(programName());
    sceneLabel_->setStyleSheet("background:#1A1E27;border-radius:10px;padding:4px 10px;font-weight:600;");
    programHeader->addWidget(sceneLabel_);
    programHeader->addStretch();
    take_ = button("Put on air", "arrow", "take-preview");
    take_->setProperty("role", "primary");
    take_->hide();
    connect(take_, &QPushButton::clicked, this, [this] { takePreview(); });
    programHeader->addWidget(take_);
    stageLayout->addLayout(programHeader);
    notice_ = label("Choose Add to select your screen, camera and audio sources.", "notice");
    notice_->setWordWrap(true);
    notice_->setObjectName("notice");
    stageLayout->addWidget(notice_);
    auto *canvases = new QHBoxLayout;
    canvases->setSpacing(14);
    horizontal_ = new CanvasView(false);
    vertical_ = new CanvasView(true);
    horizontal_->setObjectName("canvas-horizontal");
    vertical_->setObjectName("canvas-vertical");
    horizontal_->setAccessibleName("Landscape canvas");
    vertical_->setAccessibleName("Portrait canvas");
    horizontal_->clicked = [this] { setCanvas(0); };
    vertical_->clicked = [this] { setCanvas(1); };
    canvases->addWidget(horizontal_, 1);
    canvases->addWidget(vertical_, 1);
    airMonitor_ = new QWidget;
    airMonitor_->setFixedWidth(210);
    airMonitor_->setObjectName("on-air-monitor");
    auto *airCol = new QVBoxLayout(airMonitor_);
    airCol->setContentsMargins(0, 0, 0, 0);
    airCol->addStretch();
    auto *airLabel = label("On air", "title");
    airLabel->setStyleSheet("color:#FFB3BC;");
    airCol->addWidget(airLabel);
    auto *airNote = label("What your viewers see", "muted");
    airCol->addWidget(airNote);
    auto *airRow = new QHBoxLayout;
    airRow->setSpacing(8);
    airHorizontal_ = new CanvasView(false);
    airVertical_ = new CanvasView(true);
    airHorizontal_->onAir = airVertical_->onAir = true;
    airHorizontal_->setMinimumSize(40, 40);
    airVertical_->setMinimumSize(40, 40);
    airHorizontal_->setFixedHeight(170);
    airVertical_->setFixedHeight(170);
    airRow->addWidget(airHorizontal_);
    airRow->addWidget(airVertical_);
    airCol->addLayout(airRow);
    auto *takeNote = label("Ctrl+Enter to put the preview on air", "muted");
    takeNote->setWordWrap(true);
    airCol->addWidget(takeNote);
    airCol->addStretch();
    airMonitor_->hide();
    canvases->addWidget(airMonitor_);
    stageLayout->addLayout(canvases, 1);
    elapsed_ = label("", "muted");
    elapsed_->setAlignment(Qt::AlignCenter);
    stageLayout->addWidget(elapsed_);
    middle->addWidget(programArea_, 1);
    middle->addWidget(makeInspector());
    layout->addLayout(middle, 1);
    layout->addWidget(line());
    auto *transport = new QWidget;
    transport->setFixedHeight(64);
    transport->setObjectName("transport");
    transport->setStyleSheet("#transport{background:#0E1015;}");
    auto *bar = new QHBoxLayout(transport);
    bar->setContentsMargins(18, 0, 18, 0);
    bar->setSpacing(12);
    preview_ = button("Preview", "eye", "preview-toggle");
    preview_->setCheckable(true);
    preview_->setToolTip("Set up a scene privately, then Put on air (Ctrl+Alt+P)");
    bar->addWidget(preview_);
    connect(preview_, &QPushButton::clicked, this, [this](bool on) {
        if (on) {
            selected_ = canvas_ == 0 ? programH_ : programV_;
            candidate_ = scenes_[selected_].scene;
            previewH_ = previewV_ = selected_;
        } else if (previewMode_) {
            program_.horizontal = scenes_[programH_].scene.horizontal;
            program_.vertical = scenes_[programV_].scene.vertical;
            selected_ = canvas_ == 0 ? programH_ : programV_;
        }
        previewMode_ = on;
        airMonitor_->setVisible(on);
        take_->setVisible(on);
        publishScenes();
        updateSceneButtons();
        updateLayers();
    });
    auto *cut = label("Cut", "muted");
    cut->setToolTip(
        "Scene changes currently use Cut. Animated transitions are not yet available on Windows.");
    bar->addWidget(cut);
    bar->addStretch();
    record_ = button("Record  16:9 + 9:16", "record", "record");
    record_->setFixedSize(230, 40);
    bar->addWidget(record_);
    connect(record_, &QPushButton::clicked, this, [this] { toggleRecord(); });
    broadcast_ = button("Start broadcast", "broadcast", "broadcast");
    broadcast_->setProperty("role", "primary");
    broadcast_->setFixedSize(230, 40);
    bar->addWidget(broadcast_);
    connect(broadcast_, &QPushButton::clicked, this, [this] { toggleBroadcast(); });
    bar->addStretch();
    dropLabel_ = label("", "muted");
    bar->addWidget(dropLabel_);
    health_ = label("Add sources", "muted");
    health_->setObjectName("health-status");
    health_->setMinimumWidth(98);
    health_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    bar->addWidget(health_);
    layout->addWidget(transport);
    auto bind = [this](const QKeySequence &key, auto fn) {
        auto *shortcut = new QShortcut(key, this);
        connect(shortcut, &QShortcut::activated, this, fn);
    };
    bind(QKeySequence("Ctrl+R"), [this] { toggleRecord(); });
    bind(QKeySequence("Ctrl+Return"), [this] {
        if (previewMode_)
            takePreview();
    });
    bind(QKeySequence("Ctrl+Alt+P"), [this] { preview_->click(); });
    bind(QKeySequence("Ctrl+,"), [this] { showSettings(); });
    for (int n = 0; n < 8; ++n)
        bind(QKeySequence("Ctrl+" + QString::number(n + 1)), [this, n] { selectScene(n); });
    setStage(stage_);
    setCanvas(canvas_);
}

QWidget *StudioWindow::makeSidebar() {
    auto *sidebar = new QWidget;
    sidebar->setFixedWidth(236);
    sidebar->setObjectName("scenes-sidebar");
    sidebar->setStyleSheet("#scenes-sidebar{background:#101318;border-right:1px solid #232733;}");
    auto *col = new QVBoxLayout(sidebar);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(14, 0, 14, 0);
    header->addWidget(label("Scenes", "title"));
    header->addWidget(label(QString::number(scenes_.size()), "muted"));
    header->addStretch();
    auto *add = button("", "plus", "add-scene");
    add->setAccessibleName("Add scene");
    add->setFixedSize(26, 26);
    header->addWidget(add);
    auto *heading = new QWidget;
    heading->setFixedHeight(52);
    heading->setLayout(header);
    col->addWidget(heading);
    col->addWidget(line());
    sceneHint_ = label("Changes apply to the 16:9 canvas", "muted");
    sceneHint_->setContentsMargins(14, 10, 8, 2);
    col->addWidget(sceneHint_);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget;
    content->setObjectName("scene-list");
    content->setStyleSheet("#scene-list{background:#101318;}");
    scroll->viewport()->setStyleSheet("background:#101318;");
    auto *list = new QVBoxLayout(content);
    list->setContentsMargins(12, 12, 12, 12);
    list->setSpacing(10);
    QString group;
    for (size_t n = 0; n < scenes_.size(); ++n) {
        if (group != scenes_[n].group) {
            group = scenes_[n].group;
            auto *title = label(group, "muted");
            title->setContentsMargins(2, n ? 10 : 0, 0, 0);
            list->addWidget(title);
        }
        auto *b = new SceneButton(scenes_[n].name);
        b->setObjectName("scene-" + QString::number(n));
        b->shortcut = n < 9 ? int(n) + 1 : 0;
        b->doubleClicked = [this] {
            if (previewMode_)
                takePreview();
        };
        list->addWidget(b);
        sceneButtons_.push_back(b);
        connect(b, &QPushButton::clicked, this, [this, n] { selectScene(int(n)); });
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(b, &QWidget::customContextMenuRequested, this, [this, b, n](QPoint position) {
            QMenu menu;
            menu.addAction("Rename…", [this, b, n] {
                bool ok;
                auto name = QInputDialog::getText(this, "Rename scene", "Scene name", QLineEdit::Normal,
                                                  scenes_[n].name, &ok);
                if (ok && !name.trimmed().isEmpty()) {
                    scenes_[n].name = name.trimmed().left(80);
                    b->name = scenes_[n].name;
                    b->setAccessibleName(b->name);
                    updateSceneButtons();
                }
            });
            menu.exec(b->mapToGlobal(position));
        });
    }
    list->addStretch();
    scroll->setWidget(content);
    col->addWidget(scroll, 1);
    connect(add, &QPushButton::clicked, this, [this, list] {
        bool ok;
        auto name = QInputDialog::getText(this, "New scene", "Name for a copy of the selected scene",
                                          QLineEdit::Normal, "New scene", &ok);
        if (!ok || name.trimmed().isEmpty() || scenes_.size() >= 64)
            return;
        auto entry = scenes_[selected_];
        entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        entry.name = name.trimmed().left(80);
        entry.group = "Custom";
        const int n = int(scenes_.size());
        scenes_.push_back(entry);
        auto *b = new SceneButton(entry.name);
        b->setObjectName("scene-" + QString::number(n));
        b->doubleClicked = [this] {
            if (previewMode_)
                takePreview();
        };
        list->insertWidget(list->count() - 1, b);
        sceneButtons_.push_back(b);
        connect(b, &QPushButton::clicked, this, [this, n] { selectScene(n); });
        selectScene(n);
    });
    return sidebar;
}

QWidget *StudioWindow::makeInspector() {
    auto *inspector = new QWidget;
    inspector->setFixedWidth(340);
    inspector->setObjectName("inspector");
    inspector->setStyleSheet("#inspector{background:#12151B;border-left:1px solid #232733;}");
    auto *col = new QVBoxLayout(inspector);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto *tabRow = new QHBoxLayout;
    tabRow->setContentsMargins(4, 0, 4, 0);
    tabRow->setSpacing(0);
    QStringList names = {"Comments", "Green room", "Guests", "Angles", "Mixer",
                         "Overlays", "Captions",   "Layer",  "Rundown"};
    QStringList glyphs = {"chat",     "room",     "guests", "angles", "mixer",
                          "overlays", "captions", "layers", "rundown"};
    for (int n = 0; n < names.size(); ++n) {
        auto *b = new QToolButton;
        b->setProperty("role", "tab");
        b->setIcon(icon(glyphs[n]));
        b->setIconSize(QSize(20, 20));
        b->setCheckable(true);
        b->setFixedHeight(52);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setToolTip(names[n]);
        b->setAccessibleName(names[n]);
        b->setObjectName("panel-" + QString::number(n));
        tabRow->addWidget(b);
        panelButtons_.push_back(b);
        connect(b, &QToolButton::clicked, this, [this, n] { setPanel(n); });
    }
    col->addLayout(tabRow);
    col->addWidget(line());
    panels_ = new QStackedWidget;
    auto addPanel = [this](QWidget *panel) {
        panel->setObjectName("inspector-page");
        panel->setStyleSheet("#inspector-page{background:#12151B;}");
        auto *scroll = new QScrollArea;
        scroll->setObjectName("inspector-scroll-" + QString::number(panels_->count()));
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->viewport()->setAutoFillBackground(false);
        scroll->setWidget(panel);
        panels_->addWidget(scroll);
    };
    const QStringList descriptions = {
        "YouTube and Twitch comments will appear here when their Windows connectors are available. No chat "
        "account is connected.",
        "Private backstage messages will appear here when browser guests are available on Windows.",
        "Browser guests and the lobby are not connected in this Windows build.",
        "Phone cameras, face tracking and automatic angle switching are not connected in this Windows build.",
        "",
        "Text, lower thirds and other overlays are not connected in this Windows build.",
        "On-device captions need a Windows speech provider. Live transcription is not connected yet.",
        "",
        "The rundown, presenter view and AI director are not connected in this Windows build."};
    for (int n = 0; n < names.size(); ++n) {
        if (n == 4) {
            addPanel(makeMixer());
            continue;
        }
        if (n == 7) {
            addPanel(makeLayers());
            continue;
        }
        auto *panel = new QWidget;
        auto *l = new QVBoxLayout(panel);
        l->setContentsMargins(18, 20, 18, 18);
        l->setSpacing(16);
        l->addWidget(label(names[n], "title"));
        l->addWidget(line());
        auto *description = label(descriptions[n], "muted");
        description->setWordWrap(true);
        l->addWidget(description);
        if (n == 0) {
            auto *d = button("Manage destinations", "broadcast");
            l->addWidget(d);
            connect(d, &QPushButton::clicked, this, [this] { showDestinations(); });
        }
        l->addStretch();
        auto *note = label("Not yet available on Windows", "muted");
        l->addWidget(note);
        addPanel(panel);
    }
    col->addWidget(panels_, 1);
    setPanel(4);
    return inspector;
}

QWidget *StudioWindow::makeMixer() {
    auto *panel = new QWidget;
    auto *col = new QVBoxLayout(panel);
    col->setContentsMargins(18, 20, 18, 18);
    col->setSpacing(14);
    col->addWidget(label("Mixer", "title"));
    col->addWidget(label("Sound in your recording and broadcast", "muted"));
    col->addWidget(line());
    QStringList names = {"Microphone", "Desktop sound", "Master"}, glyphs = {"mic", "screen", "mixer"};
    for (int n = 0; n < 3; ++n) {
        auto *row = new QHBoxLayout;
        row->addWidget(label(names[n]));
        row->addStretch();
        auto *mute = button("Mute", glyphs[n], "mute-" + QString::number(n));
        mute->setCheckable(true);
        mute->setAccessibleName("Mute " + names[n]);
        row->addWidget(mute);
        col->addLayout(row);
        auto *fader = new QSlider(Qt::Horizontal);
        fader->setRange(0, 100);
        fader->setValue(80);
        fader->setObjectName("fader-" + QString::number(n));
        fader->setAccessibleName(names[n] + " level");
        col->addWidget(fader);
        auto *meter = new Meter;
        col->addWidget(meter);
        col->addSpacing(16);
        mutes_.push_back(mute);
        faders_.push_back(fader);
        meters_.push_back(meter);
        connect(fader, &QSlider::valueChanged, this, [this] { publishGains(); });
        connect(mute, &QPushButton::clicked, this, [this] { publishGains(); });
    }
    col->addWidget(line());
    recordStatus_ = label("Not recording", "muted");
    recordStatus_->setObjectName("record-status");
    recordStatus_->setWordWrap(true);
    broadcastStatus_ = label("Not broadcasting", "muted");
    broadcastStatus_->setObjectName("broadcast-status");
    broadcastStatus_->setWordWrap(true);
    col->addWidget(recordStatus_);
    col->addWidget(broadcastStatus_);
    col->addStretch();
    auto *folder = button("Open recordings folder", "folder");
    connect(folder, &QPushButton::clicked, this, [this] {
        QDir().mkpath(recordDirectory_);
        QDesktopServices::openUrl(QUrl::fromLocalFile(recordDirectory_));
    });
    col->addWidget(folder);
    return panel;
}

QWidget *StudioWindow::makeLayers() {
    auto *panel = new QWidget;
    auto *col = new QVBoxLayout(panel);
    col->setContentsMargins(18, 20, 18, 18);
    col->setSpacing(14);
    col->addWidget(label("Layer", "title"));
    auto *hint = label("Select a layer to change its frame on this canvas.", "muted");
    hint->setWordWrap(true);
    col->addWidget(hint);
    layers_ = new QListWidget;
    layers_->setObjectName("layers");
    layers_->setMaximumHeight(132);
    col->addWidget(layers_);
    connect(layers_, &QListWidget::currentRowChanged, this, [this](int n) {
        layerIndex_ = n;
        updateLayers();
    });
    auto *form = new QFormLayout;
    const QStringList names = {"X", "Y", "Width", "Height", "Opacity"};
    for (int n = 0; n < 5; ++n) {
        auto *value = new QDoubleSpinBox;
        value->setRange(n == 2 || n == 3 ? 1 : 0, 100);
        value->setDecimals(1);
        value->setSuffix(" %");
        value->setObjectName("layer-" + QString::number(n));
        value->setAccessibleName("Layer " + names[n]);
        form->addRow(names[n], value);
        transforms_.push_back(value);
        connect(value, &QDoubleSpinBox::valueChanged, this, [this] { editLayer(); });
    }
    fit_ = new QComboBox;
    fit_->addItems({"Fill", "Fit", "Stretch"});
    fit_->setObjectName("layer-fit");
    form->addRow("Fit", fit_);
    connect(fit_, &QComboBox::currentIndexChanged, this, [this] { editLayer(); });
    mask_ = new QComboBox;
    mask_->addItems({"None", "Circle", "Rounded rectangle"});
    mask_->setObjectName("layer-mask");
    form->addRow("Shape", mask_);
    connect(mask_, &QComboBox::currentIndexChanged, this, [this] { editLayer(); });
    col->addLayout(form);
    mirror_ = button("Mirror camera", "camera", "layer-mirror");
    mirror_->setCheckable(true);
    connect(mirror_, &QPushButton::clicked, this, [this] { editLayer(); });
    col->addWidget(mirror_);
    col->addStretch();
    return panel;
}

void StudioWindow::setPanel(int n) {
    panels_->setCurrentIndex(n);
    for (int i = 0; i < int(panelButtons_.size()); ++i)
        panelButtons_[i]->setChecked(i == n);
}
void StudioWindow::setStage(int n) {
    stage_ = n;
    for (int i = 0; i < 3; ++i)
        stageButtons_[i]->setChecked(i == n);
    horizontal_->setVisible(n != 1);
    vertical_->setVisible(n != 0);
    airHorizontal_->setVisible(n != 1);
    airVertical_->setVisible(n != 0);
    if (n < 2)
        setCanvas(n);
    else
        updateSceneButtons();
}
void StudioWindow::setCanvas(int n) {
    canvas_ = n;
    horizontal_->selected = n == 0;
    vertical_->selected = n == 1;
    horizontal_->update();
    vertical_->update();
    selected_ = previewMode_ ? (n == 0 ? previewH_ : previewV_) : (n == 0 ? programH_ : programV_);
    updateSceneButtons();
    updateLayers();
}
void StudioWindow::selectScene(int index) {
    if (index < 0 || index >= int(scenes_.size()))
        return;
    selected_ = index;
    if (previewMode_) {
        candidate_ = scenes_[index].scene;
        previewH_ = previewV_ = index;
    } else {
        if (stage_ != 2 || canvas_ == 0) {
            program_.horizontal = scenes_[index].scene.horizontal;
            programH_ = index;
        }
        if (stage_ != 2 || canvas_ == 1) {
            program_.vertical = scenes_[index].scene.vertical;
            programV_ = index;
        }
    }
    publishScenes();
    updateSceneButtons();
    updateLayers();
}
void StudioWindow::takePreview() {
    if (!previewMode_)
        return;
    if (stage_ != 2 || canvas_ == 0) {
        program_.horizontal = candidate_.horizontal;
        programH_ = previewH_;
    }
    if (stage_ != 2 || canvas_ == 1) {
        program_.vertical = candidate_.vertical;
        programV_ = previewV_;
    }
    publishScenes();
    updateSceneButtons();
}
void StudioWindow::publishScenes() {
    controller_.live.setScenes(program_, previewMode_ ? std::optional<Scene>(candidate_) : std::nullopt);
}
void StudioWindow::updateSceneButtons() {
    if (!sceneLabel_)
        return;
    sceneLabel_->setText((previewMode_ ? "Preview · " : "") + scenes_[selected_].name);
    sceneHint_->setText("Changes apply to the " + QString(canvas_ == 0 ? "16:9" : "9:16") + " canvas");
    sceneHint_->setVisible(stage_ == 2);
    for (int n = 0; n < int(sceneButtons_.size()); ++n) {
        auto *b = sceneButtons_[n];
        b->name = scenes_[n].name;
        b->onAir = n == (canvas_ == 0 ? programH_ : programV_);
        b->preview = previewMode_ && n == selected_;
        b->update();
    }
}
void StudioWindow::publishGains() {
    if (faders_.size() != 3)
        return;
    auto gain = [this](int n) {
        return mutes_[n]->isChecked() ? 0.f : float(std::pow(faders_[n]->value() / 80., 3));
    };
    controller_.live.setGains(gain(0), gain(1), gain(2));
}
void StudioWindow::updateLayers() {
    if (!layers_ || updatingLayer_)
        return;
    updatingLayer_ = true;
    auto &list = canvas_ == 0 ? scenes_[selected_].scene.horizontal : scenes_[selected_].scene.vertical;
    {
        QSignalBlocker block(layers_);
        layers_->clear();
        for (auto &l : list)
            new QListWidgetItem(icon(l.source == "screen" ? "screen" : "camera"),
                                l.source == "screen" ? "Screen" : "Camera", layers_);
        if (!list.empty())
            layerIndex_ = std::clamp(layerIndex_, 0, int(list.size()) - 1);
        else
            layerIndex_ = -1;
        layers_->setCurrentRow(layerIndex_);
    }
    bool valid = layerIndex_ >= 0;
    for (auto *w : transforms_)
        w->setEnabled(valid);
    fit_->setEnabled(valid);
    mask_->setEnabled(valid);
    mirror_->setEnabled(valid);
    if (valid) {
        auto &l = list[layerIndex_];
        double values[] = {l.rect.x, l.rect.y, l.rect.width, l.rect.height, l.opacity};
        for (int n = 0; n < 5; ++n) {
            QSignalBlocker b(transforms_[n]);
            transforms_[n]->setValue(values[n] * 100);
        }
        QSignalBlocker a(fit_), b(mask_), c(mirror_);
        fit_->setCurrentIndex(int(l.fit));
        mask_->setCurrentIndex(int(l.mask));
        mirror_->setChecked(l.mirrored);
    }
    updatingLayer_ = false;
}
void StudioWindow::editLayer() {
    if (updatingLayer_ || layerIndex_ < 0)
        return;
    auto &list = canvas_ == 0 ? scenes_[selected_].scene.horizontal : scenes_[selected_].scene.vertical;
    if (layerIndex_ >= int(list.size()))
        return;
    auto &l = list[layerIndex_];
    l.rect = {transforms_[0]->value() / 100, transforms_[1]->value() / 100, transforms_[2]->value() / 100,
              transforms_[3]->value() / 100};
    l.opacity = transforms_[4]->value() / 100;
    l.fit = Fit(fit_->currentIndex());
    l.mask = Mask(mask_->currentIndex());
    if (l.mask == Mask::rounded && l.radius == 0)
        l.radius = .035;
    l.mirrored = mirror_->isChecked();
    auto &target = previewMode_ ? candidate_ : program_;
    if (canvas_ == 0)
        target.horizontal = list;
    else
        target.vertical = list;
    publishScenes();
}

bool StudioWindow::groupPending(const std::vector<std::string> &ids) const {
    for (auto &id : ids) {
        if (outputs_.contains(id))
            return true;
        auto it = std::find_if(last_.outputs.begin(), last_.outputs.end(),
                               [&](const auto &s) { return s.id == id; });
        if (it == last_.outputs.end() || !(it->state == "stopped" || it->state == "failed"))
            return true;
    }
    return false;
}
bool StudioWindow::hasPendingOutputs() const {
    return groupPending(recording_) || groupPending(broadcasting_);
}
OutputSpec StudioWindow::outputSpec(Canvas c, const QString &destination, bool streaming) const {
    return {c,
            bytes(destination),
            "auto",
            c == Canvas::horizontal ? config_.width : config_.height,
            c == Canvas::horizontal ? config_.height : config_.width,
            config_.fps,
            6000,
            streaming};
}
void StudioWindow::toggleRecord() {
    watchdog().action(recording_.empty() ? "start recording" : "stop recording");
    if (!recording_.empty()) {
        for (auto &id : recording_)
            outputs_.erase(id);
        controller_.live.setOutputs(outputs_);
        record_->setEnabled(false);
        return;
    }
    if (!last_.running) {
        showSources();
        return;
    }
    outputError_.clear();
    if (!QDir().mkpath(recordDirectory_)) {
        notice_->setText("Recording folder could not be created. Choose another folder in Settings.");
        notice_->show();
        return;
    }
    const auto stem = "Framecast " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH.mm.ss.zzz") + "-" +
                      QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    for (int n = 0; n < 2; ++n) {
        if ((stage_ == 0 && n == 1) || (stage_ == 1 && n == 0))
            continue;
        auto canvas = n == 0 ? Canvas::horizontal : Canvas::vertical;
        auto id = bytes("rec-" + QUuid::createUuid().toString());
        auto path = QDir(recordDirectory_).filePath(stem + (n == 0 ? " 16x9." : " 9x16.") + container_);
        outputs_[id] = outputSpec(canvas, path, false);
        recording_.push_back(id);
    }
    recordClock_.start();
    controller_.live.setOutputs(outputs_);
    refresh();
}
void StudioWindow::toggleBroadcast() {
    watchdog().action(broadcasting_.empty() ? "start broadcast" : "end broadcast");
    if (!broadcasting_.empty()) {
        for (auto &id : broadcasting_)
            outputs_.erase(id);
        controller_.live.setOutputs(outputs_);
        broadcast_->setEnabled(false);
        return;
    }
    if (!last_.running) {
        showSources();
        return;
    }
    if (destinations_.empty() ||
        std::none_of(destinations_.begin(), destinations_.end(), [](auto &d) { return d.enabled; })) {
        showDestinations();
        return;
    }
    outputError_.clear();
    for (const auto &d : destinations_)
        if (d.enabled) {
            auto id = bytes("stream-" + QUuid::createUuid().toString());
            QString target = d.server;
            while (target.endsWith('/'))
                target.chop(1);
            if (!d.key.isEmpty())
                target += "/" + d.key;
            outputs_[id] = outputSpec(d.canvas, target, true);
            broadcasting_.push_back(id);
        }
    broadcastClock_.start();
    controller_.live.setOutputs(outputs_);
    refresh();
}

void StudioWindow::startPreview() {
    watchdog().action("start preview");
    outputError_.clear();
    outputs_.clear();
    controller_.live.setOutputs({});
    publishScenes();
    publishGains();
    controller_.start(config_);
    notice_->setText("Starting your sources…");
    notice_->show();
}
void StudioWindow::showSources() {
    if (hasPendingOutputs()) {
        QMessageBox::information(this, "Sources in use",
                                 "Stop recording and broadcasting before changing your sources.");
        return;
    }
    watchdog().action("open Add sources");
    QDialog dialog(this);
    dialog.setWindowTitle("Add sources");
    dialog.setMinimumWidth(450);
    auto *col = new QVBoxLayout(&dialog);
    col->setSpacing(16);
    col->addWidget(label("Your screen, camera and sound", "title"));
    auto *desc =
        label("Select the sources for your show. Capture starts when you choose Start preview.", "muted");
    desc->setWordWrap(true);
    col->addWidget(desc);
    auto *form = new QFormLayout;
    auto *screen = new QComboBox;
    screen->setMinimumWidth(260);
    auto *cameras = new QComboBox;
    cameras->addItem("No camera", -1);
    auto *mic = new QCheckBox("Use the default microphone");
    mic->setChecked(config_.microphone);
    auto *desktop = new QCheckBox("Capture desktop sound");
    desktop->setChecked(config_.desktopAudio);
    if (options_.synthetic) {
        screen->addItem("Test screen", 0);
        cameras->addItem("Test camera", 0);
        cameras->setCurrentIndex(1);
        mic->setText("Use the test tone");
        mic->setChecked(true);
        desktop->setEnabled(false);
    } else {
#ifdef _WIN32
        struct Choices {
            std::vector<CaptureChoice> monitors, windows, cameras;
        };
        try {
            auto listing = std::async(std::launch::async, [] {
                initializeWindowsMedia();
                try {
                    Choices c{listMonitors(), listWindows(), listCameras()};
                    shutdownWindowsMedia();
                    return c;
                } catch (...) {
                    shutdownWindowsMedia();
                    throw;
                }
            });
            // Keep answering Windows messages while devices are listed (camera enumeration can take a second),
            // so nothing that needs this thread can wait on it and the window never shows "Not responding".
            watchdog().action("list screens, windows and cameras");
            QApplication::setOverrideCursor(Qt::WaitCursor);
            while (listing.wait_for(std::chrono::milliseconds(15)) != std::future_status::ready)
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            QApplication::restoreOverrideCursor();
            auto choices = listing.get();
            watchdog().action(QString("listed %1 displays, %2 windows, %3 cameras")
                                  .arg(choices.monitors.size())
                                  .arg(choices.windows.size())
                                  .arg(choices.cameras.size()));
            for (auto &d : choices.monitors) {
                screen->addItem(QString::fromStdString(d.name), QVariant::fromValue(qulonglong(d.id)));
                screen->setItemData(screen->count() - 1, "monitor", Qt::UserRole + 1);
            }
            for (auto &d : choices.windows) {
                screen->addItem(QString::fromStdString(d.name).left(90),
                                QVariant::fromValue(qulonglong(d.id)));
                screen->setItemData(screen->count() - 1, "window", Qt::UserRole + 1);
            }
            for (auto &d : choices.cameras)
                cameras->addItem(QString::fromStdString(d.name), int(d.id));
        } catch (const std::exception &e) {
            logLine(std::string("warning: listing sources failed: ") + e.what());
            col->addWidget(label(QString::fromUtf8(e.what()), "muted"));
        }
#else
        screen->addItem("Native capture requires Windows");
        screen->setEnabled(false);
        cameras->setEnabled(false);
        mic->setEnabled(false);
        desktop->setEnabled(false);
#endif
    }
    form->addRow("Screen or window", screen);
    form->addRow("Camera", cameras);
    form->addRow("", mic);
    form->addRow("", desktop);
    col->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto *start = buttons->addButton("Start preview", QDialogButtonBox::AcceptRole);
    start->setProperty("role", "primary");
    if (!options_.synthetic) {
#ifndef _WIN32
        start->setEnabled(false);
#else
        start->setEnabled(screen->count() > 0);
#endif
    }
    col->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    if (controller_.busy()) {
        auto *stop = button("Stop preview");
        col->addWidget(stop);
        connect(stop, &QPushButton::clicked, &dialog, [this, &dialog] {
            controller_.stop();
            dialog.reject();
        });
        start->setEnabled(false);
        desc->setText("Stop preview before changing capture devices. Your scene layouts will be kept.");
    }
    if (dialog.exec() != QDialog::Accepted)
        return;
    config_.windowHandle = 0;
    config_.monitor = 0;
    if (screen->currentData(Qt::UserRole + 1).toString() == "window")
        config_.windowHandle = uintptr_t(screen->currentData().toULongLong());
    else
        config_.monitor = screen->currentData().toInt();
    config_.camera = cameras->currentData().toInt() >= 0;
    config_.cameraIndex = std::max(0, cameras->currentData().toInt());
    config_.microphone = mic->isChecked();
    config_.desktopAudio = desktop->isChecked();
    startPreview();
}

void StudioWindow::showSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle("Settings");
    dialog.setMinimumWidth(470);
    auto *col = new QVBoxLayout(&dialog);
    col->setSpacing(16);
    col->addWidget(label("Recording and canvas settings", "title"));
    auto *form = new QFormLayout;
    auto *folder = new QLineEdit(recordDirectory_);
    auto *choose = button("Choose…", "folder");
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(folder);
    folderRow->addWidget(choose);
    form->addRow("Save recordings to", folderRow);
    connect(choose, &QPushButton::clicked, &dialog, [this, folder] {
        auto path = QFileDialog::getExistingDirectory(this, "Recording folder", folder->text());
        if (!path.isEmpty())
            folder->setText(path);
    });
    auto *container = new QComboBox;
    container->addItems({"mkv", "mp4", "mov"});
    container->setCurrentText(container_);
    form->addRow("File format", container);
    auto *size = new QComboBox;
    size->addItem("1280 × 720 / 720 × 1280", QSize(1280, 720));
    size->addItem("1920 × 1080 / 1080 × 1920", QSize(1920, 1080));
    if (config_.width == 640)
        size->addItem("640 × 360 / 360 × 640", QSize(640, 360));
    for (int n = 0; n < size->count(); ++n)
        if (size->itemData(n).toSize().width() == config_.width)
            size->setCurrentIndex(n);
    size->setEnabled(!controller_.busy());
    form->addRow("Canvas size", size);
    auto *fps = new QComboBox;
    for (int n : {24, 25, 29, 30, 60})
        fps->addItem(QString::number(n) + " fps", n);
    fps->setCurrentIndex(fps->findData(config_.fps));
    fps->setEnabled(!controller_.busy());
    form->addRow("Frame rate", fps);
    container->setEnabled(!hasPendingOutputs());
    col->addLayout(form);
    auto *note = label(
        "Stop preview to change canvas size or frame rate. Each recording uses a unique file name.", "muted");
    note->setWordWrap(true);
    col->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    col->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *logs = button("Open logs folder", "folder");
    logs->setToolTip("Framecast writes a log of each session. Send the newest file when reporting a problem.");
    col->addWidget(logs);
    connect(logs, &QPushButton::clicked, &dialog, [] {
        QDir().mkpath(logDirectory());
        QDesktopServices::openUrl(QUrl::fromLocalFile(logDirectory()));
    });
    if (dialog.exec() == QDialog::Accepted && !folder->text().trimmed().isEmpty()) {
        recordDirectory_ = folder->text().trimmed();
        container_ = container->currentText();
        if (!controller_.busy()) {
            auto dimensions = size->currentData().toSize();
            config_.width = dimensions.width();
            config_.height = dimensions.height();
            config_.fps = fps->currentData().toInt();
        }
        persist();
    }
}

void StudioWindow::showDestinations() {
    QDialog dialog(this);
    dialog.setWindowTitle("Destinations");
    dialog.setMinimumSize(580, 460);
    auto *col = new QVBoxLayout(&dialog);
    col->setSpacing(14);
    col->addWidget(label("Broadcast destinations", "title"));
    auto *note =
        label("Add an RTMP or RTMPS destination. Stream keys stay in memory for this session.", "muted");
    note->setWordWrap(true);
    col->addWidget(note);
    auto *list = new QListWidget;
    for (auto &d : destinations_) {
        auto *item =
            new QListWidgetItem(d.name + " · " + (d.canvas == Canvas::horizontal ? "16:9" : "9:16"), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(d.enabled ? Qt::Checked : Qt::Unchecked);
    }
    col->addWidget(list);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit;
    name->setObjectName("destination-name");
    name->setPlaceholderText("YouTube, Twitch, or a custom server");
    auto *server = new QLineEdit;
    server->setObjectName("destination-server");
    server->setPlaceholderText("rtmps://server.example/live");
    auto *key = new QLineEdit;
    key->setObjectName("destination-key");
    key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText("Stream key");
    auto *canvas = new QComboBox;
    canvas->addItems({"16:9", "9:16"});
    form->addRow("Name", name);
    form->addRow("Server URL", server);
    form->addRow("Stream key", key);
    form->addRow("Canvas", canvas);
    col->addLayout(form);
    auto *error = label("", "muted");
    error->setWordWrap(true);
    col->addWidget(error);
    auto *row = new QHBoxLayout;
    auto *remove = button("Remove selected");
    auto *add = button("Add destination", "plus", "destination-add");
    row->addWidget(remove);
    row->addStretch();
    row->addWidget(add);
    col->addLayout(row);
    auto *done = new QDialogButtonBox(QDialogButtonBox::Close);
    col->addWidget(done);
    connect(done, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    bool locked = !broadcasting_.empty();
    for (auto *w : std::vector<QWidget *>{list, name, server, key, canvas, remove, add})
        w->setEnabled(!locked);
    if (locked)
        error->setText("End the broadcast before changing destinations.");
    connect(add, &QPushButton::clicked, &dialog, [&] {
        QUrl url(server->text().trimmed());
        if ((url.scheme() != "rtmp" && url.scheme() != "rtmps") || url.host().isEmpty() ||
            name->text().trimmed().isEmpty()) {
            error->setText("Enter a destination name and a valid RTMP or RTMPS server URL.");
            return;
        }
        if (destinations_.size() >= 8) {
            error->setText("This build supports up to eight destinations.");
            return;
        }
        Destination d{name->text().trimmed().left(80), server->text().trimmed(), key->text(),
                      canvas->currentIndex() == 0 ? Canvas::horizontal : Canvas::vertical, true};
        destinations_.push_back(d);
        auto *item =
            new QListWidgetItem(d.name + " · " + (d.canvas == Canvas::horizontal ? "16:9" : "9:16"), list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        name->clear();
        server->clear();
        key->clear();
        error->clear();
    });
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        int n = list->currentRow();
        if (n < 0)
            return;
        destinations_.erase(destinations_.begin() + n);
        delete list->takeItem(n);
    });
    connect(list, &QListWidget::itemChanged, &dialog, [&](QListWidgetItem *item) {
        int n = list->row(item);
        if (n >= 0 && n < int(destinations_.size()))
            destinations_[n].enabled = item->checkState() == Qt::Checked;
    });
    dialog.exec();
    destinationsButton_->setText(
        destinations_.empty() ? "Destinations"
                              : "Destinations · " +
                                    QString::number(std::count_if(destinations_.begin(), destinations_.end(),
                                                                  [](auto &d) { return d.enabled; })) +
                                    " on");
}

void StudioWindow::refresh() {
    watchdog().beat();
    controller_.collect();
    last_ = controller_.live.state();
    horizontal_->preview = vertical_->preview = previewMode_;
    auto &images = previewMode_ && !last_.preview.empty() ? last_.preview : last_.program;
    for (auto [canvas, view] :
         {std::pair{Canvas::horizontal, horizontal_}, std::pair{Canvas::vertical, vertical_}}) {
        auto it = images.find(canvas);
        if (it != images.end())
            view->image = imageFor(it->second);
        else
            view->image = {};
        view->update();
    }
    for (auto [canvas, view] :
         {std::pair{Canvas::horizontal, airHorizontal_}, std::pair{Canvas::vertical, airVertical_}}) {
        auto it = last_.program.find(canvas);
        view->image = it == last_.program.end() ? QImage() : imageFor(it->second);
        view->update();
    }
    meters_[0]->setLevel(last_.microphone);
    meters_[1]->setLevel(last_.desktop);
    meters_[2]->setLevel(last_.master);
    if (++refreshCount_ % 10 == 1) {
        // Live sources once capture runs; drawn stand-ins until then, and for a source that isn't selected,
        // so every scene shows its layout instead of an empty box.
        auto sources = last_.sources;
        for (const auto &[name, image] : placeholderSources())
            if (!sources[name])
                sources[name] = image;
        for (size_t n = 0; n < sceneButtons_.size(); ++n) {
            auto image = compose(scenes_[n].scene, canvas_ == 0 ? Canvas::horizontal : Canvas::vertical,
                                 canvas_ == 0 ? 320 : 180, canvas_ == 0 ? 180 : 320, sources);
            sceneButtons_[n]->thumbnail = imageFor(std::make_shared<Image>(std::move(image)));
            sceneButtons_[n]->update();
        }
    }
    QStringList errors;
    uint64_t dropped = 0;
    bool reconnecting = false, outputsChanged = false;
    for (auto &output : last_.outputs) {
        if (!output.error.empty() && output.state == "failed" && seenFailures_.insert(output.id).second)
            outputError_ = QString::fromStdString(output.error);
        dropped += output.dropped;
        if (output.state == "reconnecting")
            reconnecting = true;
        if (output.state == "failed" && outputs_.erase(output.id))
            outputsChanged = true;
    }
    if (outputsChanged)
        controller_.live.setOutputs(outputs_);
    if (!outputError_.isEmpty())
        errors << outputError_;
    if (!last_.error.empty()) {
        errors << QString::fromStdString(last_.error);
        outputs_.clear();
        recording_.clear();
        broadcasting_.clear();
        controller_.live.setOutputs({});
    }
    bool startingOutput = false, finishingOutput = false;
    auto group = [this, &startingOutput, &finishingOutput](auto &ids, auto *action, auto *status,
                                                           const QString &idle) {
        const bool isRecord = idle == "Record";
        if (!ids.empty() && !groupPending(ids)) {
            ids.clear();
            action->setEnabled(true);
        }
        bool requested =
            std::any_of(ids.begin(), ids.end(), [this](auto &id) { return outputs_.contains(id); });
        const auto oldRole = action->property("role");
        if (!ids.empty()) {
            int liveCount = 0;
            bool retrying = false;
            for (const auto &id : ids) {
                auto it = std::find_if(last_.outputs.begin(), last_.outputs.end(),
                                       [&](auto &s) { return s.id == id; });
                if (it != last_.outputs.end()) {
                    if (it->state == "live")
                        ++liveCount;
                    if (it->state == "reconnecting")
                        retrying = true;
                }
            }
            action->setText(requested ? (isRecord ? "Stop recording" : "End broadcast") : "Finishing…");
            action->setEnabled(requested);
            action->setProperty("role", "stop");
            if (!requested) {
                status->setText("Finalizing output…");
                finishingOutput = true;
            } else if (liveCount == 0) {
                status->setText(retrying   ? "Reconnecting…"
                                : isRecord ? "Starting recording…"
                                           : "Connecting…");
                if (!retrying)
                    startingOutput = true;
            } else {
                status->setText((isRecord ? "Recording · " : "Broadcast · ") +
                                clockText(isRecord ? recordClock_.elapsed() : broadcastClock_.elapsed()));
                if (liveCount < int(ids.size()) && !retrying) {
                    status->setText(status->text() + " · starting outputs");
                    startingOutput = true;
                }
            }
        } else {
            action->setText(isRecord ? "Record  " + QString(stage_ == 2   ? "16:9 + 9:16"
                                                            : stage_ == 0 ? "16:9"
                                                                          : "9:16")
                                     : "Start broadcast");
            action->setEnabled(true);
            action->setProperty("role", isRecord ? "" : "primary");
            status->setText(isRecord ? "Not recording" : "Not broadcasting");
        }
        if (oldRole != action->property("role")) {
            action->style()->unpolish(action);
            action->style()->polish(action);
        }
    };
    group(recording_, record_, recordStatus_, "Record");
    group(broadcasting_, broadcast_, broadcastStatus_, "Broadcast");
    record_->setIcon(icon(recording_.empty() ? "record" : "stop"));
    if (!errors.isEmpty()) {
        notice_->setText(errors.join("\n") + " Stop preview and check your sources or output settings.");
        notice_->show();
        health_->setText("Check status");
        health_->setStyleSheet("color:#F5B544;");
    } else if (reconnecting) {
        notice_->setText("A destination is reconnecting. Local recordings continue independently.");
        notice_->show();
        health_->setText("Reconnecting");
        health_->setStyleSheet("color:#F5B544;");
    } else if (startingOutput) {
        notice_->setText("Connecting your broadcast or starting your recording outputs…");
        notice_->show();
        health_->setText("Starting outputs");
        health_->setStyleSheet("color:#A8AFBF;");
    } else if (finishingOutput) {
        notice_->hide();
        health_->setText("Saving files…");
        health_->setStyleSheet("color:#A8AFBF;");
    } else if (last_.running && !last_.warnings.empty()) {
        // A source is missing but the show goes on; the engine keeps retrying it.
        QStringList warnings;
        for (const auto &warning : last_.warnings)
            warnings << QString::fromStdString(warning);
        notice_->setText(warnings.join("\n") + "\nEverything else keeps running. Framecast retries every few seconds.");
        notice_->show();
        health_->setText("Check sources");
        health_->setStyleSheet("color:#F5B544;");
    } else if (last_.running && dropped > 0 && !recording_.empty()) {
        notice_->setText("This computer cannot encode every frame, so some are being skipped. Sound is unaffected. "
                         "A smaller canvas size in Settings will help.");
        notice_->show();
        health_->setText("Skipping frames");
        health_->setStyleSheet("color:#F5B544;");
    } else if (last_.running) {
        notice_->hide();
        health_->setText("All good");
        health_->setStyleSheet("color:#3DD68C;");
    } else if (controller_.busy()) {
        notice_->setText("Starting or stopping your sources…");
        notice_->show();
        health_->setText("Please wait");
    } else {
        notice_->setText(options_.synthetic ? "Test sources stopped. Choose Add to restart preview."
                                            : "Choose Add to select your screen, camera and audio sources.");
        notice_->show();
        health_->setText("Add sources");
    }
    dropLabel_->setText(recording_.empty() && broadcasting_.empty() ? ""
                                                                    : QString::number(dropped) + " dropped");
    elapsed_->setText(options_.synthetic && last_.running
                          ? "Test sources · " + QString::number(config_.fps) + " fps"
                      : last_.running ? QString::number(config_.fps) + " fps"
                                      : "");
    if (closing_ && !controller_.busy()) {
        persist();
        close();
    }
}

void StudioWindow::persist() {
    if (!options_.persistent)
        return;
    QJsonArray scenes;
    for (auto &entry : scenes_)
        scenes.append(QJsonObject{{"id", entry.id},
                                  {"name", entry.name},
                                  {"group", entry.group},
                                  {"horizontal", layerJson(entry.scene.horizontal)},
                                  {"vertical", layerJson(entry.scene.vertical)}});
    settings_.setValue("scenes", QJsonDocument(scenes).toJson(QJsonDocument::Compact));
    settings_.setValue("recordingFolder", recordDirectory_);
    settings_.setValue("container", container_);
    settings_.setValue("stage", stage_);
    settings_.setValue("programH", programH_);
    settings_.setValue("programV", programV_);
    settings_.sync();
}
void StudioWindow::restore() {
    recordDirectory_ = settings_.value("recordingFolder", recordDirectory_).toString();
    auto container = settings_.value("container", "mkv").toString();
    if (container == "mkv" || container == "mp4" || container == "mov")
        container_ = container;
    stage_ = std::clamp(settings_.value("stage", 2).toInt(), 0, 2);
    auto array = QJsonDocument::fromJson(settings_.value("scenes").toByteArray()).array();
    std::vector<SceneEntry> loaded;
    for (auto value : array) {
        if (loaded.size() >= 64)
            break;
        auto o = value.toObject();
        if (o["name"].toString().isEmpty())
            continue;
        loaded.push_back({o["id"].toString(),
                          o["name"].toString().left(80),
                          o["group"].toString(),
                          {readLayers(o["horizontal"].toArray()), readLayers(o["vertical"].toArray())}});
    }
    if (!loaded.empty())
        scenes_ = std::move(loaded);
    programH_ = std::clamp(settings_.value("programH", 3).toInt(), 0, int(scenes_.size()) - 1);
    programV_ = std::clamp(settings_.value("programV", 3).toInt(), 0, int(scenes_.size()) - 1);
    selected_ = stage_ == 1 ? programV_ : programH_;
}
void StudioWindow::closeEvent(QCloseEvent *event) {
    watchdog().action("close window");
    if (controller_.busy()) {
        if (!closing_ && hasPendingOutputs() &&
            QMessageBox::question(this, "End this show?",
                                  "Stop recording and broadcasting, save your files, and close Framecast?") !=
                QMessageBox::Yes) {
            event->ignore();
            return;
        }
        closing_ = true;
        controller_.stop();
        event->ignore();
        return;
    }
    persist();
    event->accept();
}
void StudioWindow::saveScreenshots(const QString &directory) {
    QDir().mkpath(directory);
    setPanel(4);
    grab().save(QDir(directory).filePath("studio.png"));
    setPanel(7);
    grab().save(QDir(directory).filePath("layer.png"));
    setPanel(0);
    grab().save(QDir(directory).filePath("comments.png"));
    setPanel(4);
    preview_->click();
    QApplication::processEvents();
    grab().save(QDir(directory).filePath("preview.png"));
    preview_->click();
}
} // namespace framecast::ui
