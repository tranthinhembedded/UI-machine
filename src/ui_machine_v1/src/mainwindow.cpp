#include "mainwindow.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineF>
#include <QListWidget>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QShortcut>
#include <QSizePolicy>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr auto StyleSheet = R"QSS(
* { color: #EDF4F5; }
QMainWindow, QWidget#root { background: #0E181C; }
QFrame#header { background: #101D22; border: none; border-bottom: 1px solid #40575F; }
QLabel#brandLogo { background: transparent; border: none; }
QLabel#brandName { color: white; font-size: 18px; font-weight: 700; }
QLabel#brandSub, QLabel#headerMeta { color: #B6C5C9; font-size: 12px; }
QLabel#pageTitle { color: white; font-size: 22px; font-weight: 650; }
QLabel#pageSub, QLabel[class="muted"] { color: #A7B8BD; font-size: 12px; }
QFrame[class="card"], QFrame#controls {
  background: #17262C; border: 1px solid #40575F; border-radius: 4px;
}
QLabel[class="sectionTitle"] { color: #F4F8F8; font-size: 15px; font-weight: 650; }
QLabel[class="fieldLabel"] { color: #D5E0E2; font-size: 12px; font-weight: 600; }
QComboBox {
  background: #1D3037; color: white; border: 1px solid #60757C; border-radius: 3px;
  min-height: 40px; padding: 0 12px; font-size: 13px;
}
QComboBox:hover { border-color: #82979E; }
QComboBox:focus, QPushButton:focus { border: 2px solid #14B8B4; }
QComboBox::drop-down { border: none; width: 28px; }
QComboBox QAbstractItemView {
  background: #1D3037; color: white; border: 1px solid #60757C;
  selection-background-color: #0D6264; selection-color: white;
}
QPushButton {
  background: #24373E; color: #F3F7F8; border: 1px solid #61767D; border-radius: 3px;
  min-height: 40px; padding: 0 16px; font-size: 13px; font-weight: 650;
}
QPushButton:hover { background: #30474F; border-color: #81969D; }
QPushButton:pressed { background: #15262C; }
QPushButton:disabled { color: #73858A; background: #1A292E; border-color: #35494F; }
QPushButton#startButton { background: #087F7D; color: white; border-color: #14B8B4; }
QPushButton#startButton:hover { background: #0A9491; }
QPushButton#stopButton { background: #5A292D; color: #FFDADB; border-color: #A94B50; }
QPushButton#stopButton:hover { background: #743238; border-color: #E05257; }
QPushButton#headerButton { min-height: 36px; padding: 0 14px; }
QPushButton#windowButton, QPushButton#closeButton {
  min-width: 38px; max-width: 38px; min-height: 36px; padding: 0;
  font-size: 18px; font-weight: 700;
}
QPushButton#closeButton { background: #51272B; color: #FFDADB; border-color: #8E444A; }
QPushButton#closeButton:hover { background: #B33B44; color: white; border-color: #E05257; }
QLabel#imageWell, QLabel#contextWell { background: #081419; color: #D6E0E2; border: none; }
QLabel#cropWell { background: #24373D; color: #A7B8BD; border: 1px solid #40575F; }
QLabel[class="statLabel"] { color: #A7B8BD; font-size: 11px; }
QLabel[class="statValue"] { color: white; font-size: 24px; font-weight: 650; }
QLabel#message {
  background: #17272D; color: #E7EFF0; border: 1px solid #40575F;
  border-left: 4px solid #14B8B4; border-radius: 3px; padding: 10px 12px;
}
QLabel#message[tone="bad"] { color: #FFD2D4; border-left-color: #E05257; background: #352225; }
QLabel[class="badge"] { border-radius: 3px; padding: 5px 9px; font-size: 11px; font-weight: 650; }
QLabel[class="badge"][tone="neutral"] { color: #C2CED1; background: #24363C; border: 1px solid #536970; }
QLabel[class="badge"][tone="good"] { color: #8DF2A9; background: #153D2C; border: 1px solid #368A58; }
QLabel[class="badge"][tone="warn"] { color: #FFD07B; background: #493716; border: 1px solid #9A7228; }
QLabel[class="badge"][tone="bad"] { color: #FFB4B8; background: #4A2529; border: 1px solid #A64B51; }
QLabel#operationState { min-width: 180px; min-height: 38px; font-size: 15px; font-weight: 700; }
QListWidget { background: #17262C; color: #E9F0F1; border: none; outline: none; padding: 4px; }
QListWidget::item { border: 1px solid transparent; border-bottom-color: #31474E; padding: 8px 10px; min-height: 42px; }
QListWidget::item:selected { color: white; background: #0D5558; border: 1px solid #14B8B4; }
QScrollBar:vertical { background: #132329; width: 12px; }
QScrollBar::handle:vertical { background: #526970; min-height: 28px; border-radius: 3px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QSplitter::handle { background: #0E181C; }
)QSS";

QLabel *label(const QString &text, const char *cssClass = nullptr) {
    auto *result = new QLabel(text);
    if (cssClass) result->setProperty("class", cssClass);
    return result;
}

QFrame *separator() {
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background:#40575F;border:none;"));
    return line;
}

struct Card {
    QFrame *frame;
    QVBoxLayout *layout;
};

Card makeCard() {
    auto *frame = new QFrame;
    frame->setProperty("class", "card");
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    return {frame, layout};
}

QWidget *sectionHeader(const QString &title, QWidget *trailing) {
    auto *widget = new QWidget;
    auto *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->addWidget(label(title, "sectionTitle"));
    layout->addStretch();
    layout->addWidget(trailing);
    return widget;
}

void repolish(QWidget *widget) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

} // namespace

ImageView::ImageView(const QString &objectName, const QString &emptyText,
                     int minimumHeight, QWidget *parent)
    : QLabel(emptyText, parent) {
    setObjectName(objectName);
    setAlignment(Qt::AlignCenter);
    setWordWrap(true);
    setMinimumHeight(minimumHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
}

void ImageView::setImage(const QImage &image) {
    if (image.isNull()) return;
    source_ = image;
    fitImage();
}

void ImageView::clearImage(const QString &text) {
    source_ = {};
    setPixmap(QPixmap());
    setText(text);
}

void ImageView::setRoi(const QRectF &normalizedRoi) {
    const QRectF safe = normalizedRoi.normalized().intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    if (safe.width() < 0.03 || safe.height() < 0.03) return;
    roi_ = safe;
    update();
}

void ImageView::setRoiVisible(bool visible) {
    roiVisible_ = visible;
    update();
}

void ImageView::setRoiEditing(bool enabled) {
    roiEditing_ = enabled;
    activeHandle_ = -1;
    setCursor(enabled ? Qt::OpenHandCursor : Qt::ArrowCursor);
    update();
}

void ImageView::resizeEvent(QResizeEvent *event) {
    QLabel::resizeEvent(event);
    fitImage();
}

void ImageView::paintEvent(QPaintEvent *event) {
    QLabel::paintEvent(event);
    if (!roiVisible_ || source_.isNull()) return;

    const QRectF imageRect = imageDisplayRect();
    const QRectF roiRect(normalizedToWidget(roi_.topLeft()),
                         normalizedToWidget(roi_.bottomRight()));
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath shade;
    shade.setFillRule(Qt::OddEvenFill);
    shade.addRect(imageRect);
    shade.addRect(roiRect);
    painter.fillPath(shade, QColor(0, 0, 0, 135));

    painter.setPen(QPen(QColor(QStringLiteral("#14B8B4")), roiEditing_ ? 3.0 : 2.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(roiRect);

    const qreal radius = roiEditing_ ? 8.0 : 5.0;
    painter.setPen(QPen(QColor(QStringLiteral("#E8FFFF")), 2.0));
    painter.setBrush(QColor(QStringLiteral("#087F7D")));
    for (const QPointF &point : handlePositions()) painter.drawEllipse(point, radius, radius);

    painter.setPen(Qt::white);
    painter.setBrush(QColor(8, 20, 25, 190));
    const QString caption = QStringLiteral(" ROI %1 × %2% ")
        .arg(qRound(roi_.width() * 100.0))
        .arg(qRound(roi_.height() * 100.0));
    const QRectF captionRect(roiRect.left() + 6.0, roiRect.top() + 6.0,
                             112.0, 24.0);
    painter.drawRoundedRect(captionRect, 3.0, 3.0);
    painter.drawText(captionRect, Qt::AlignCenter, caption);
}

void ImageView::mousePressEvent(QMouseEvent *event) {
    if (!roiEditing_ || event->button() != Qt::LeftButton || source_.isNull()) {
        QLabel::mousePressEvent(event);
        return;
    }
    const QVector<QPointF> handles = handlePositions();
    qreal closest = 18.0;
    activeHandle_ = -1;
    for (int index = 0; index < handles.size(); ++index) {
        const qreal distance = QLineF(event->position(), handles[index]).length();
        if (distance <= closest) {
            closest = distance;
            activeHandle_ = index;
        }
    }
    if (activeHandle_ >= 0) {
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void ImageView::mouseMoveEvent(QMouseEvent *event) {
    if (!roiEditing_ || source_.isNull()) {
        QLabel::mouseMoveEvent(event);
        return;
    }
    if (activeHandle_ < 0) {
        bool nearHandle = false;
        for (const QPointF &handle : handlePositions()) {
            if (QLineF(event->position(), handle).length() <= 18.0) {
                nearHandle = true;
                break;
            }
        }
        setCursor(nearHandle ? Qt::OpenHandCursor : Qt::ArrowCursor);
        return;
    }

    QPointF point = widgetToNormalized(event->position());
    point.setX(std::clamp(point.x(), 0.0, 1.0));
    point.setY(std::clamp(point.y(), 0.0, 1.0));
    constexpr qreal MinimumSize = 0.03;
    QRectF next = roi_;
    switch (activeHandle_) {
    case 0:
        next.setLeft(std::min(point.x(), next.right() - MinimumSize));
        next.setTop(std::min(point.y(), next.bottom() - MinimumSize));
        break;
    case 1:
        next.setRight(std::max(point.x(), next.left() + MinimumSize));
        next.setTop(std::min(point.y(), next.bottom() - MinimumSize));
        break;
    case 2:
        next.setRight(std::max(point.x(), next.left() + MinimumSize));
        next.setBottom(std::max(point.y(), next.top() + MinimumSize));
        break;
    case 3:
        next.setLeft(std::min(point.x(), next.right() - MinimumSize));
        next.setBottom(std::max(point.y(), next.top() + MinimumSize));
        break;
    default:
        return;
    }
    setRoi(next);
    emit roiChanged(roi_);
    event->accept();
}

void ImageView::mouseReleaseEvent(QMouseEvent *event) {
    if (activeHandle_ >= 0 && event->button() == Qt::LeftButton) {
        activeHandle_ = -1;
        setCursor(Qt::OpenHandCursor);
        emit roiEditFinished(roi_);
        event->accept();
        return;
    }
    QLabel::mouseReleaseEvent(event);
}

void ImageView::fitImage() {
    if (source_.isNull() || width() < 2 || height() < 2) return;
    setText(QString());
    setPixmap(QPixmap::fromImage(source_).scaled(
        size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QRectF ImageView::imageDisplayRect() const {
    if (source_.isNull()) return {};
    const QSize scaled = source_.size().scaled(size(), Qt::KeepAspectRatio);
    return QRectF((width() - scaled.width()) / 2.0,
                  (height() - scaled.height()) / 2.0,
                  scaled.width(), scaled.height());
}

QPointF ImageView::normalizedToWidget(const QPointF &point) const {
    const QRectF rect = imageDisplayRect();
    return {rect.left() + point.x() * rect.width(),
            rect.top() + point.y() * rect.height()};
}

QPointF ImageView::widgetToNormalized(const QPointF &point) const {
    const QRectF rect = imageDisplayRect();
    if (rect.width() <= 0.0 || rect.height() <= 0.0) return {};
    return {(point.x() - rect.left()) / rect.width(),
            (point.y() - rect.top()) / rect.height()};
}

QVector<QPointF> ImageView::handlePositions() const {
    return {normalizedToWidget(roi_.topLeft()),
            normalizedToWidget(roi_.topRight()),
            normalizedToWidget(roi_.bottomRight()),
            normalizedToWidget(roi_.bottomLeft())};
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("TNM Vision — PP7-Fast"));
    setMinimumSize(980, 680);
    resize(1280, 800);
    setStyleSheet(QString::fromUtf8(StyleSheet));
    QSettings settings;
    const QRectF savedRoi = settings.value(
        QStringLiteral("processing/roi"), QRectF(0.0, 0.0, 1.0, 1.0)).toRectF();
    const QRectF safeRoi = savedRoi.normalized().intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    if (safeRoi.width() >= 0.05 && safeRoi.height() >= 0.05)
        processingRoi_ = safeRoi;

    buildUi();
    frameView_->setRoiVisible(true);
    frameView_->setRoi(processingRoi_);
    runtime_.setProcessingRoi(processingRoi_);

    connect(&runtime_, &VisionRuntime::devicesChanged,
            this, &MainWindow::updateDevices);
    connect(&runtime_, &VisionRuntime::stateChanged,
            this, &MainWindow::updateState);
    connect(&runtime_, &VisionRuntime::packetReady,
            this, &MainWindow::updatePacket, Qt::QueuedConnection);
    connect(frameView_, &ImageView::roiChanged,
            this, &MainWindow::updateRoi);
    connect(frameView_, &ImageView::roiEditFinished,
            this, &MainWindow::saveRoi);

    auto *fullscreen = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(fullscreen, &QShortcut::activated, this, &MainWindow::toggleFullscreen);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, [this] {
        if (isFullScreen()) {
            showNormal();
            QTimer::singleShot(0, this, &MainWindow::syncWindowButtons);
        }
    });
    QTimer::singleShot(0, &runtime_, &VisionRuntime::refreshDevices);
    QTimer::singleShot(0, this, &MainWindow::syncWindowButtons);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    auto *root = new QWidget;
    root->setObjectName(QStringLiteral("root"));
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(buildHeader());

    auto *body = new QWidget;
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(14, 12, 14, 10);
    layout->setSpacing(12);
    layout->addWidget(buildControls());

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(12);
    splitter->addWidget(buildMonitor());
    QWidget *inspector = buildInspector();
    inspector->setMinimumWidth(340);
    splitter->addWidget(inspector);
    splitter->setStretchFactor(0, 5);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({790, 440});
    layout->addWidget(splitter, 1);

    auto *footer = new QHBoxLayout;
    configLabel_ = label(QStringLiteral("PP7-Fast · tối đa 640 px · camera 20 FPS"),
                         "muted");
    footer->addWidget(configLabel_);
    footer->addStretch();
    footer->addWidget(label(QStringLiteral("TNM Vision · Xử lý hoàn toàn trên thiết bị"),
                            "muted"));
    layout->addLayout(footer);
    rootLayout->addWidget(body, 1);
    setCentralWidget(root);
}

QWidget *MainWindow::buildHeader() {
    auto *header = new QFrame;
    header->setObjectName(QStringLiteral("header"));
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(22, 9, 22, 9);
    layout->setSpacing(11);

    auto *logo = new QLabel;
    logo->setObjectName(QStringLiteral("brandLogo"));
    logo->setPixmap(QPixmap(QStringLiteral(":/assets/logo.png")).scaled(
        46, 46, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setFixedSize(46, 46);
    logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);

    auto *names = new QVBoxLayout;
    names->setSpacing(1);
    auto *brand = new QLabel(QStringLiteral("TNM Vision"));
    brand->setObjectName(QStringLiteral("brandName"));
    auto *sub = new QLabel(QStringLiteral("Kiểm tra bề mặt vải"));
    sub->setObjectName(QStringLiteral("brandSub"));
    names->addWidget(brand);
    names->addWidget(sub);
    layout->addLayout(names);

    auto *rule = new QFrame;
    rule->setFrameShape(QFrame::VLine);
    rule->setFixedHeight(42);
    rule->setStyleSheet(QStringLiteral("color:#4A6067;"));
    layout->addWidget(rule);

    auto *headline = new QVBoxLayout;
    headline->setSpacing(1);
    auto *title = new QLabel(QStringLiteral("Kiểm tra trực tiếp"));
    title->setObjectName(QStringLiteral("pageTitle"));
    auto *description = new QLabel(QStringLiteral(
        "Quan sát bề mặt vải và đối chiếu vùng nghi ngờ trong cùng một màn hình."));
    description->setObjectName(QStringLiteral("pageSub"));
    headline->addWidget(title);
    headline->addWidget(description);
    layout->addLayout(headline);
    layout->addStretch();

    auto *station = new QLabel(QStringLiteral("Trạm kiểm tra / 01"));
    station->setObjectName(QStringLiteral("headerMeta"));
    layout->addWidget(station);
    auto *localBadge = label(QStringLiteral("Camera trực tiếp"), "badge");
    localBadge->setProperty("tone", "good");
    layout->addWidget(localBadge);
    fullscreenButton_ = new QPushButton(QStringLiteral("Toàn màn hình"));
    fullscreenButton_->setObjectName(QStringLiteral("headerButton"));
    connect(fullscreenButton_, &QPushButton::clicked,
            this, &MainWindow::toggleFullscreen);
    layout->addWidget(fullscreenButton_);

    auto *minimizeButton = new QPushButton(QStringLiteral("—"));
    minimizeButton->setObjectName(QStringLiteral("windowButton"));
    minimizeButton->setToolTip(QStringLiteral("Thu nhỏ cửa sổ"));
    connect(minimizeButton, &QPushButton::clicked, this, &QWidget::showMinimized);
    layout->addWidget(minimizeButton);

    maximizeButton_ = new QPushButton(QStringLiteral("□"));
    maximizeButton_->setObjectName(QStringLiteral("windowButton"));
    connect(maximizeButton_, &QPushButton::clicked,
            this, &MainWindow::toggleMaximized);
    layout->addWidget(maximizeButton_);

    auto *closeButton = new QPushButton(QStringLiteral("×"));
    closeButton->setObjectName(QStringLiteral("closeButton"));
    closeButton->setToolTip(QStringLiteral("Đóng ứng dụng"));
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    layout->addWidget(closeButton);
    return header;
}

QWidget *MainWindow::buildControls() {
    auto *controls = new QFrame;
    controls->setObjectName(QStringLiteral("controls"));
    auto *layout = new QHBoxLayout(controls);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(10);

    auto *source = new QVBoxLayout;
    source->setSpacing(3);
    source->addWidget(label(QStringLiteral("Nguồn camera"), "fieldLabel"));
    deviceBox_ = new QComboBox;
    deviceBox_->setMinimumWidth(240);
    deviceBox_->addItem(QStringLiteral("Đang tìm camera…"));
    source->addWidget(deviceBox_);
    layout->addLayout(source);

    refreshButton_ = new QPushButton(QStringLiteral("Làm mới"));
    roiButton_ = new QPushButton(QStringLiteral("Chỉnh ROI"));
    resetRoiButton_ = new QPushButton(QStringLiteral("Toàn ảnh"));
    roiButton_->setCheckable(true);
    startButton_ = new QPushButton(QStringLiteral("Bắt đầu kiểm tra"));
    stopButton_ = new QPushButton(QStringLiteral("Dừng kiểm tra"));
    startButton_->setObjectName(QStringLiteral("startButton"));
    stopButton_->setObjectName(QStringLiteral("stopButton"));
    startButton_->setEnabled(false);
    stopButton_->setEnabled(false);
    connect(refreshButton_, &QPushButton::clicked,
            &runtime_, &VisionRuntime::refreshDevices);
    connect(roiButton_, &QPushButton::toggled,
            this, &MainWindow::toggleRoiEditing);
    connect(resetRoiButton_, &QPushButton::clicked,
            this, &MainWindow::resetRoi);
    connect(startButton_, &QPushButton::clicked, this, &MainWindow::startInspection);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopInspection);
    layout->addWidget(refreshButton_, 0, Qt::AlignBottom);
    layout->addWidget(roiButton_, 0, Qt::AlignBottom);
    layout->addWidget(resetRoiButton_, 0, Qt::AlignBottom);
    layout->addWidget(startButton_, 0, Qt::AlignBottom);
    layout->addWidget(stopButton_, 0, Qt::AlignBottom);
    layout->addStretch();

    controlStatus_ = label(QStringLiteral("Chưa chạy"), "badge");
    controlStatus_->setObjectName(QStringLiteral("operationState"));
    controlStatus_->setAlignment(Qt::AlignCenter);
    setBadge(controlStatus_, QStringLiteral("Chưa chạy"), QStringLiteral("neutral"));
    layout->addWidget(controlStatus_, 0, Qt::AlignBottom);
    return controls;
}

QWidget *MainWindow::buildMonitor() {
    auto *result = new QWidget;
    auto *outer = new QVBoxLayout(result);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(9);

    Card card = makeCard();
    runBadge_ = label(QStringLiteral("Chưa chạy"), "badge");
    runBadge_->setAlignment(Qt::AlignCenter);
    setBadge(runBadge_, QStringLiteral("Chưa chạy"), QStringLiteral("neutral"));
    card.layout->addWidget(sectionHeader(QStringLiteral("Khung camera"), runBadge_));
    card.layout->addWidget(separator());
    frameView_ = new ImageView(QStringLiteral("imageWell"),
        QStringLiteral("SẴN SÀNG QUAN SÁT\n\nChọn camera và bắt đầu kiểm tra.\n"
                       "Vùng nghi ngờ sẽ xuất hiện ở khung bên cạnh."), 300);
    card.layout->addWidget(frameView_, 1);
    card.layout->addWidget(separator());

    auto *stats = new QHBoxLayout;
    stats->setContentsMargins(16, 9, 16, 9);
    stats->setSpacing(26);
    auto addStat = [stats](const QString &title, const QString &value,
                           const QString &unit = QString()) {
        auto *box = new QVBoxLayout;
        box->setSpacing(1);
        box->addWidget(label(title, "statLabel"));
        auto *row = new QHBoxLayout;
        row->setSpacing(4);
        auto *number = label(value, "statValue");
        row->addWidget(number);
        row->addWidget(label(unit, "muted"), 0, Qt::AlignBottom);
        row->addStretch();
        box->addLayout(row);
        stats->addLayout(box);
        return number;
    };
    fpsValue_ = addStat(QStringLiteral("Tốc độ xử lý thực tế"), QStringLiteral("—"),
                        QStringLiteral("FPS"));
    latencyValue_ = addStat(QStringLiteral("Thời gian / khung"), QStringLiteral("—"),
                            QStringLiteral("ms"));
    processedValue_ = addStat(QStringLiteral("Khung đã kiểm tra"), QStringLiteral("0"));
    stats->addStretch();
    card.layout->addLayout(stats);
    outer->addWidget(card.frame, 1);

    message_ = new QLabel(QStringLiteral("Chọn camera rồi nhấn Bắt đầu kiểm tra."));
    message_->setObjectName(QStringLiteral("message"));
    message_->setWordWrap(true);
    message_->setMinimumHeight(46);
    outer->addWidget(message_);
    outer->addWidget(label(QStringLiteral(
        "▣  Khung xanh: vùng nghi ngờ cần kiểm tra, chưa phải kết luận lỗi."), "muted"));
    return result;
}

QWidget *MainWindow::buildInspector() {
    Card card = makeCard();
    followLabel_ = label(QStringLiteral("Theo ảnh mới nhất"), "muted");
    card.layout->addWidget(sectionHeader(QStringLiteral("Vùng nghi ngờ"), followLabel_));
    card.layout->addWidget(separator());

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(14, 11, 14, 9);
    layout->setSpacing(8);
    cropView_ = new ImageView(QStringLiteral("cropWell"),
        QStringLiteral("Khi phát hiện vùng nghi ngờ,\nảnh cắt sẽ hiển thị tại đây."), 138);
    cropView_->setMaximumHeight(205);
    layout->addWidget(cropView_);

    auto *detail = new QHBoxLayout;
    eventTitle_ = label(QStringLiteral("Chưa có ảnh sự kiện"), "sectionTitle");
    eventTime_ = label(QStringLiteral("—"), "muted");
    detail->addWidget(eventTitle_);
    detail->addStretch();
    detail->addWidget(eventTime_);
    layout->addLayout(detail);
    eventDetail_ = label(QStringLiteral("Ảnh được giữ lại để người vận hành đối chiếu."),
                         "muted");
    eventDetail_->setWordWrap(true);
    layout->addWidget(eventDetail_);

    contextView_ = new ImageView(QStringLiteral("contextWell"), QString(), 110);
    contextView_->setMaximumHeight(165);
    contextView_->setVisible(false);
    layout->addWidget(contextView_);

    auto *actions = new QHBoxLayout;
    contextButton_ = new QPushButton(QStringLiteral("Xem toàn cảnh"));
    followButton_ = new QPushButton(QStringLiteral("Theo ảnh mới nhất"));
    contextButton_->setEnabled(false);
    followButton_->setEnabled(false);
    connect(contextButton_, &QPushButton::clicked, this, &MainWindow::toggleContext);
    connect(followButton_, &QPushButton::clicked, this, &MainWindow::followLatest);
    actions->addWidget(contextButton_);
    actions->addWidget(followButton_);
    layout->addLayout(actions);
    card.layout->addWidget(content);
    card.layout->addWidget(separator());

    auto *history = new QWidget;
    auto *historyLayout = new QHBoxLayout(history);
    historyLayout->setContentsMargins(14, 8, 14, 8);
    historyLayout->addWidget(label(QStringLiteral("Ảnh sự kiện gần đây"), "fieldLabel"));
    historyLayout->addStretch();
    eventCount_ = label(QStringLiteral("0 ảnh"), "muted");
    historyLayout->addWidget(eventCount_);
    card.layout->addWidget(history);
    card.layout->addWidget(separator());

    events_ = new QListWidget;
    events_->setMinimumHeight(105);
    events_->setMaximumHeight(180);
    connect(events_, &QListWidget::currentItemChanged,
            this, &MainWindow::selectHistory);
    card.layout->addWidget(events_);
    card.layout->addWidget(separator());
    auto *retention = label(QStringLiteral(
        "Giữ tối đa 24 ảnh trong RAM · Lấy mẫu tối đa 1 ảnh / 2 giây.\n"
        "Số ảnh sự kiện không phải số lỗi vải độc lập."), "muted");
    retention->setWordWrap(true);
    retention->setContentsMargins(14, 7, 14, 8);
    card.layout->addWidget(retention);
    return card.frame;
}

void MainWindow::startInspection() {
    if (deviceBox_->currentIndex() < 0 || !deviceBox_->currentData().isValid()) {
        setMessage(QStringLiteral("Không có camera hợp lệ để bắt đầu."), true);
        return;
    }
    resetSession();
    runtime_.start(deviceBox_->currentData().toUInt());
}

void MainWindow::stopInspection() {
    setBadge(controlStatus_, QStringLiteral("Đang dừng"), QStringLiteral("warn"));
    setBadge(runBadge_, QStringLiteral("Đang dừng"), QStringLiteral("warn"));
    setMessage(QStringLiteral("Đang đóng luồng camera và kết thúc xử lý…"));
    runtime_.stop();
}

void MainWindow::updateDevices(const QStringList &devices) {
    const int oldIndex = deviceBox_->currentIndex();
    deviceBox_->clear();
    for (int index = 0; index < devices.size(); ++index)
        deviceBox_->addItem(devices[index], index);
    if (devices.isEmpty()) {
        deviceBox_->addItem(QStringLiteral("Không tìm thấy camera"));
        deviceBox_->setCurrentIndex(0);
    } else if (oldIndex >= 0 && oldIndex < devices.size()) {
        deviceBox_->setCurrentIndex(oldIndex);
    }
    startButton_->setEnabled(!devices.isEmpty());
}

void MainWindow::updateState(const QString &state, const QString &message) {
    const bool active = state == QStringLiteral("starting") ||
                        state == QStringLiteral("running");
    deviceBox_->setEnabled(!active);
    refreshButton_->setEnabled(!active);
    startButton_->setEnabled(!active && deviceBox_->currentData().isValid());
    stopButton_->setEnabled(active);

    QString title = QStringLiteral("Chưa chạy");
    QString tone = QStringLiteral("neutral");
    if (state == QStringLiteral("starting")) {
        title = QStringLiteral("Đang khởi động");
        tone = QStringLiteral("warn");
    } else if (state == QStringLiteral("running")) {
        title = QStringLiteral("Đang kiểm tra");
        tone = QStringLiteral("good");
    } else if (state == QStringLiteral("stopped")) {
        title = QStringLiteral("Đã dừng");
    } else if (state == QStringLiteral("error")) {
        title = QStringLiteral("Lỗi nguồn / xử lý");
        tone = QStringLiteral("bad");
    }
    setBadge(controlStatus_, title, tone);
    setBadge(runBadge_, title, tone);
    setMessage(message, state == QStringLiteral("error"));
}

void MainWindow::updatePacket(const AnalysisPacket &packet) {
    frameView_->setImage(packet.frame);
    fpsValue_->setText(packet.processedTotal > 1
                           ? QString::number(packet.detectorFps, 'f', 1)
                           : QStringLiteral("—"));
    latencyValue_->setText(QString::number(qRound(packet.processingMs)));
    processedValue_->setText(QLocale().toString(packet.processedTotal));
    configLabel_->setText(QStringLiteral(
        "PP7-Fast · ROI %1×%2% · CAM %3 FPS · DROP %4 · tối đa 640 px")
        .arg(qRound(processingRoi_.width() * 100.0))
        .arg(qRound(processingRoi_.height() * 100.0))
        .arg(packet.cameraFps, 0, 'f', 1)
        .arg(packet.droppedTotal));
    if (packet.droppedTotal > 0) {
        setMessage(QStringLiteral(
            "Bộ xử lý đang chậm hơn camera; đã bỏ %1 khung cũ để giữ dữ liệu hiện tại.")
            .arg(packet.droppedTotal), true);
    } else {
        setMessage(packet.regionCount > 0
            ? QStringLiteral("Đã phát hiện %1 vùng nghi ngờ. Hãy đối chiếu ảnh bên phải.")
                  .arg(packet.regionCount)
            : QStringLiteral("Đang chạy PP7-Fast trên ảnh camera. Chưa có vùng nghi ngờ."));
    }
    if (!packet.crop.isNull()) addEvent(packet);
}

void MainWindow::selectHistory(QListWidgetItem *current, QListWidgetItem *) {
    if (!current) return;
    const quint64 id = current->data(Qt::UserRole).toULongLong();
    if (const EventRecord *event = findEvent(id)) {
        following_ = false;
        showEvent(*event);
    }
}

void MainWindow::followLatest() {
    following_ = true;
    if (!eventRecords_.isEmpty()) showEvent(eventRecords_.first());
}

void MainWindow::toggleContext() {
    contextVisible_ = !contextVisible_;
    contextView_->setVisible(contextVisible_);
    contextButton_->setText(contextVisible_ ? QStringLiteral("Ẩn toàn cảnh")
                                            : QStringLiteral("Xem toàn cảnh"));
}

void MainWindow::toggleFullscreen() {
    isFullScreen() ? showNormal() : showFullScreen();
    QTimer::singleShot(0, this, &MainWindow::syncWindowButtons);
}

void MainWindow::toggleMaximized() {
    if (isFullScreen() || isMaximized())
        showNormal();
    else
        showMaximized();
    QTimer::singleShot(0, this, &MainWindow::syncWindowButtons);
}

void MainWindow::syncWindowButtons() {
    if (!fullscreenButton_ || !maximizeButton_) return;
    fullscreenButton_->setText(isFullScreen()
        ? QStringLiteral("Thoát toàn màn hình")
        : QStringLiteral("Toàn màn hình"));
    maximizeButton_->setText((isFullScreen() || isMaximized())
        ? QStringLiteral("❐") : QStringLiteral("□"));
    maximizeButton_->setToolTip((isFullScreen() || isMaximized())
        ? QStringLiteral("Khôi phục cửa sổ")
        : QStringLiteral("Phóng to cửa sổ"));
}

void MainWindow::toggleRoiEditing(bool enabled) {
    frameView_->setRoiEditing(enabled);
    roiButton_->setText(enabled ? QStringLiteral("Xong ROI")
                                : QStringLiteral("Chỉnh ROI"));
    if (enabled) {
        setMessage(QStringLiteral(
            "Kéo bốn điểm tròn ở các góc để chọn vùng PP7 cần xử lý. "
            "Thay đổi được lưu tự động."));
    } else {
        setMessage(QStringLiteral(
            "Đã lưu vùng xử lý. PP7 chỉ phân tích phần ảnh nằm trong khung ROI."));
    }
}

void MainWindow::resetRoi() {
    const QRectF fullImage(0.0, 0.0, 1.0, 1.0);
    frameView_->setRoi(fullImage);
    updateRoi(fullImage);
    saveRoi(fullImage);
    setMessage(QStringLiteral("Đã đặt vùng xử lý về toàn bộ khung hình."));
}

void MainWindow::updateRoi(const QRectF &normalizedRoi) {
    const QRectF safe = normalizedRoi.normalized().intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    if (safe.width() < 0.03 || safe.height() < 0.03) return;
    processingRoi_ = safe;
    runtime_.setProcessingRoi(processingRoi_);
}

void MainWindow::saveRoi(const QRectF &normalizedRoi) {
    updateRoi(normalizedRoi);
    QSettings settings;
    settings.setValue(QStringLiteral("processing/roi"), processingRoi_);
}

void MainWindow::resetSession() {
    eventRecords_.clear();
    events_->clear();
    selectedEventId_ = 0;
    following_ = true;
    contextVisible_ = false;
    frameView_->clearImage(QStringLiteral("ĐANG KHỞI ĐỘNG…"));
    cropView_->clearImage(QStringLiteral(
        "Khi phát hiện vùng nghi ngờ,\nảnh cắt sẽ hiển thị tại đây."));
    contextView_->clearImage(QString());
    contextView_->setVisible(false);
    contextButton_->setText(QStringLiteral("Xem toàn cảnh"));
    contextButton_->setEnabled(false);
    followButton_->setEnabled(false);
    eventTitle_->setText(QStringLiteral("Chưa có ảnh sự kiện"));
    eventTime_->setText(QStringLiteral("—"));
    eventCount_->setText(QStringLiteral("0 ảnh"));
    fpsValue_->setText(QStringLiteral("—"));
    latencyValue_->setText(QStringLiteral("—"));
    processedValue_->setText(QStringLiteral("0"));
}

void MainWindow::addEvent(const AnalysisPacket &packet) {
    EventRecord event;
    event.id = packet.frameNumber;
    event.frameNumber = packet.frameNumber;
    event.regions = packet.regionCount;
    event.time = packet.capturedAt;
    event.crop = packet.crop;
    event.context = packet.context;
    eventRecords_.prepend(event);

    auto *item = new QListWidgetItem(QStringLiteral("Ảnh #%1    %2 vùng nghi ngờ    %3")
        .arg(event.frameNumber)
        .arg(event.regions)
        .arg(event.time.toString(QStringLiteral("HH:mm:ss"))));
    item->setData(Qt::UserRole, QVariant::fromValue(event.id));
    item->setSizeHint(QSize(0, 48));
    events_->insertItem(0, item);
    while (eventRecords_.size() > 24) {
        eventRecords_.removeLast();
        delete events_->takeItem(events_->count() - 1);
    }
    eventCount_->setText(QStringLiteral("%1 ảnh").arg(eventRecords_.size()));
    if (following_) showEvent(eventRecords_.first());
}

void MainWindow::showEvent(const EventRecord &event) {
    selectedEventId_ = event.id;
    cropView_->setImage(event.crop);
    contextView_->setImage(event.context);
    eventTitle_->setText(QStringLiteral("Ảnh #%1").arg(event.frameNumber));
    eventTime_->setText(event.time.toString(QStringLiteral("HH:mm:ss")));
    eventDetail_->setText(QStringLiteral(
        "%1 vùng · Hiển thị ảnh cắt của vùng nghi ngờ lớn nhất").arg(event.regions));
    followLabel_->setText(following_ ? QStringLiteral("Theo ảnh mới nhất")
                                    : QStringLiteral("Đang giữ ảnh đã chọn"));
    followButton_->setEnabled(!following_);
    contextButton_->setEnabled(true);
    if (following_) {
        events_->blockSignals(true);
        for (int row = 0; row < events_->count(); ++row) {
            if (events_->item(row)->data(Qt::UserRole).toULongLong() == event.id) {
                events_->setCurrentRow(row);
                break;
            }
        }
        events_->blockSignals(false);
    }
}

const MainWindow::EventRecord *MainWindow::findEvent(quint64 id) const {
    const auto found = std::find_if(eventRecords_.cbegin(), eventRecords_.cend(),
        [id](const EventRecord &event) { return event.id == id; });
    return found == eventRecords_.cend() ? nullptr : &(*found);
}

void MainWindow::setBadge(QLabel *badge, const QString &text, const QString &tone) {
    badge->setText(text);
    badge->setProperty("tone", tone);
    repolish(badge);
}

void MainWindow::setMessage(const QString &text, bool bad) {
    message_->setText(text);
    message_->setProperty("tone", bad ? "bad" : "normal");
    repolish(message_);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    runtime_.stop();
    event->accept();
}
