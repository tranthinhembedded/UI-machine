#pragma once

#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QRectF>
#include <QStringList>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

#include <opencv2/core.hpp>

struct AnalysisPacket {
    QImage frame;
    QImage crop;
    QImage context;
    quint64 frameNumber = 0;
    quint64 processedTotal = 0;
    quint64 droppedTotal = 0;
    int regionCount = 0;
    double cameraFps = 0.0;
    double detectorFps = 0.0;
    double processingMs = 0.0;
    QDateTime capturedAt;
};

Q_DECLARE_METATYPE(AnalysisPacket)

class VisionRuntime final : public QObject {
    Q_OBJECT

public:
    explicit VisionRuntime(QObject *parent = nullptr);
    ~VisionRuntime() override;

    bool isRunning() const noexcept { return running_.load(); }

public slots:
    void refreshDevices();
    void start(unsigned int cameraIndex);
    void stop();
    void setProcessingRoi(const QRectF &normalizedRoi);

signals:
    void devicesChanged(const QStringList &devices);
    void stateChanged(const QString &state, const QString &message);
    void packetReady(const AnalysisPacket &packet);

private:
    struct CapturedFrame {
        quint64 number = 0;
        cv::Mat image;
        std::chrono::steady_clock::time_point capturedAt;
        QDateTime wallTime;
    };

    void captureLoop(unsigned int cameraIndex);
    void processingLoop();
    void pushFrame(CapturedFrame frame);
    bool popFrame(CapturedFrame &frame);
    void clearQueue();

    std::atomic_bool running_{false};
    std::atomic<quint64> capturedTotal_{0};
    std::atomic<quint64> processedTotal_{0};
    std::atomic<quint64> droppedTotal_{0};
    std::thread captureThread_;
    std::thread processingThread_;
    std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    std::deque<CapturedFrame> queue_;
    std::mutex roiMutex_;
    QRectF processingRoi_{0.0, 0.0, 1.0, 1.0};
    static constexpr std::size_t QueueCapacity = 3;
};
