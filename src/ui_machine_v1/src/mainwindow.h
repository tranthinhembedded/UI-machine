#pragma once

#include "visionruntime.h"

#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QRectF>
#include <QVector>

class QCloseEvent;
class QComboBox;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QResizeEvent;
class QMouseEvent;
class QPaintEvent;

class ImageView final : public QLabel {
    Q_OBJECT

public:
    explicit ImageView(const QString &objectName, const QString &emptyText,
                       int minimumHeight, QWidget *parent = nullptr);
    void setImage(const QImage &image);
    void clearImage(const QString &text);
    void setRoi(const QRectF &normalizedRoi);
    QRectF roi() const { return roi_; }
    void setRoiVisible(bool visible);
    void setRoiEditing(bool enabled);

signals:
    void roiChanged(const QRectF &normalizedRoi);
    void roiEditFinished(const QRectF &normalizedRoi);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void fitImage();
    QRectF imageDisplayRect() const;
    QPointF normalizedToWidget(const QPointF &point) const;
    QPointF widgetToNormalized(const QPointF &point) const;
    QVector<QPointF> handlePositions() const;
    QImage source_;
    QRectF roi_{0.0, 0.0, 1.0, 1.0};
    bool roiVisible_ = false;
    bool roiEditing_ = false;
    int activeHandle_ = -1;
};

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void startInspection();
    void stopInspection();
    void updateDevices(const QStringList &devices);
    void updateState(const QString &state, const QString &message);
    void updatePacket(const AnalysisPacket &packet);
    void selectHistory(QListWidgetItem *current, QListWidgetItem *previous);
    void followLatest();
    void toggleContext();
    void toggleFullscreen();
    void toggleMaximized();
    void syncWindowButtons();
    void toggleRoiEditing(bool enabled);
    void resetRoi();
    void updateRoi(const QRectF &normalizedRoi);
    void saveRoi(const QRectF &normalizedRoi);

private:
    struct EventRecord {
        quint64 id = 0;
        quint64 frameNumber = 0;
        int regions = 0;
        QDateTime time;
        QImage crop;
        QImage context;
    };

    void buildUi();
    QWidget *buildHeader();
    QWidget *buildControls();
    QWidget *buildMonitor();
    QWidget *buildInspector();
    void resetSession();
    void addEvent(const AnalysisPacket &packet);
    void showEvent(const EventRecord &event);
    const EventRecord *findEvent(quint64 id) const;
    void setBadge(QLabel *badge, const QString &text, const QString &tone);
    void setMessage(const QString &text, bool bad = false);

    VisionRuntime runtime_;
    QVector<EventRecord> eventRecords_;
    quint64 selectedEventId_ = 0;
    bool following_ = true;
    bool contextVisible_ = false;
    QRectF processingRoi_{0.0, 0.0, 1.0, 1.0};

    QComboBox *deviceBox_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QPushButton *roiButton_ = nullptr;
    QPushButton *resetRoiButton_ = nullptr;
    QPushButton *fullscreenButton_ = nullptr;
    QPushButton *maximizeButton_ = nullptr;
    QLabel *controlStatus_ = nullptr;
    QLabel *runBadge_ = nullptr;
    QLabel *fpsValue_ = nullptr;
    QLabel *latencyValue_ = nullptr;
    QLabel *processedValue_ = nullptr;
    QLabel *configLabel_ = nullptr;
    QLabel *message_ = nullptr;
    ImageView *frameView_ = nullptr;
    ImageView *cropView_ = nullptr;
    ImageView *contextView_ = nullptr;
    QLabel *followLabel_ = nullptr;
    QLabel *eventTitle_ = nullptr;
    QLabel *eventTime_ = nullptr;
    QLabel *eventDetail_ = nullptr;
    QLabel *eventCount_ = nullptr;
    QListWidget *events_ = nullptr;
    QPushButton *contextButton_ = nullptr;
    QPushButton *followButton_ = nullptr;
};
