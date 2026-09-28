#include "visionruntime.h"

#include "pp7_fast_detector.hpp"

#include <MvCameraControl.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

QString mvsError(const QString &operation, int code) {
    return QStringLiteral("%1 (mã MVS 0x%2)")
        .arg(operation)
        .arg(static_cast<unsigned int>(code), 8, 16, QLatin1Char('0'));
}

QString deviceName(const MV_CC_DEVICE_INFO *info, unsigned int index) {
    if (!info) return QStringLiteral("[%1] Thiết bị không xác định").arg(index);
    if (info->nTLayerType == MV_GIGE_DEVICE) {
        const auto &gigE = info->SpecialInfo.stGigEInfo;
        const unsigned int ip = gigE.nCurrentIp;
        return QStringLiteral("[%1] %2 · %3.%4.%5.%6")
            .arg(index)
            .arg(QString::fromLocal8Bit(
                reinterpret_cast<const char *>(gigE.chModelName)))
            .arg((ip >> 24) & 0xff)
            .arg((ip >> 16) & 0xff)
            .arg((ip >> 8) & 0xff)
            .arg(ip & 0xff);
    }
    const auto &usb = info->SpecialInfo.stUsb3VInfo;
    return QStringLiteral("[%1] %2 · USB3")
        .arg(index)
        .arg(QString::fromLocal8Bit(
            reinterpret_cast<const char *>(usb.chModelName)));
}

class CameraHandle final {
public:
    ~CameraHandle() { close(); }
    void *value() const { return handle_; }
    void **address() { return &handle_; }
    void markOpen() { open_ = true; }
    void markGrabbing() { grabbing_ = true; }

    void close() {
        if (grabbing_ && handle_) MV_CC_StopGrabbing(handle_);
        if (open_ && handle_) MV_CC_CloseDevice(handle_);
        if (handle_) MV_CC_DestroyHandle(handle_);
        grabbing_ = false;
        open_ = false;
        handle_ = nullptr;
    }

private:
    void *handle_ = nullptr;
    bool open_ = false;
    bool grabbing_ = false;
};

bool convertToBgr(void *handle, const MV_FRAME_OUT_INFO_EX &info,
                  unsigned char *source, std::vector<unsigned char> &conversion,
                  cv::Mat &output) {
    const int width = static_cast<int>(info.nWidth);
    const int height = static_cast<int>(info.nHeight);
    if (!source || width <= 0 || height <= 0) return false;

    if (info.enPixelType == PixelType_Gvsp_Mono8) {
        cv::cvtColor(cv::Mat(height, width, CV_8UC1, source), output,
                     cv::COLOR_GRAY2BGR);
        return true;
    }
    if (info.enPixelType == PixelType_Gvsp_BGR8_Packed) {
        output = cv::Mat(height, width, CV_8UC3, source).clone();
        return true;
    }
    if (info.enPixelType == PixelType_Gvsp_RGB8_Packed) {
        cv::cvtColor(cv::Mat(height, width, CV_8UC3, source), output,
                     cv::COLOR_RGB2BGR);
        return true;
    }

    conversion.resize(static_cast<std::size_t>(width) * height * 3);
    MV_CC_PIXEL_CONVERT_PARAM parameter{};
    parameter.nWidth = info.nWidth;
    parameter.nHeight = info.nHeight;
    parameter.enSrcPixelType = info.enPixelType;
    parameter.pSrcData = source;
    parameter.nSrcDataLen = info.nFrameLen;
    parameter.enDstPixelType = PixelType_Gvsp_BGR8_Packed;
    parameter.pDstBuffer = conversion.data();
    parameter.nDstBufferSize = static_cast<unsigned int>(conversion.size());
    if (MV_CC_ConvertPixelType(handle, &parameter) != MV_OK) return false;
    output = cv::Mat(height, width, CV_8UC3, conversion.data()).clone();
    return true;
}

QImage toQImage(const cv::Mat &bgr) {
    if (bgr.empty()) return {};
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows,
                  static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
}

std::vector<cv::Rect> mapDetections(const std::vector<pp7::Detection> &items,
                                    const cv::Rect &roi,
                                    const cv::Size &processed) {
    const double scaleX = static_cast<double>(roi.width) / processed.width;
    const double scaleY = static_cast<double>(roi.height) / processed.height;
    std::vector<cv::Rect> result;
    result.reserve(items.size());
    for (const pp7::Detection &item : items) {
        const int x1 = std::clamp(
            roi.x + static_cast<int>(std::lround(item.x1 * scaleX)),
            roi.x, roi.x + roi.width - 1);
        const int y1 = std::clamp(
            roi.y + static_cast<int>(std::lround(item.y1 * scaleY)),
            roi.y, roi.y + roi.height - 1);
        const int x2 = std::clamp(
            roi.x + static_cast<int>(std::lround(item.x2 * scaleX)),
            x1 + 1, roi.x + roi.width);
        const int y2 = std::clamp(
            roi.y + static_cast<int>(std::lround(item.y2 * scaleY)),
            y1 + 1, roi.y + roi.height);
        result.emplace_back(x1, y1, x2 - x1, y2 - y1);
    }
    return result;
}

void drawDetections(cv::Mat &image, const std::vector<cv::Rect> &regions) {
    const int thickness = std::max(2, std::min(image.cols, image.rows) / 500);
    for (std::size_t index = 0; index < regions.size(); ++index) {
        cv::rectangle(image, regions[index], cv::Scalar(255, 229, 0),
                      thickness, cv::LINE_AA);
        cv::putText(image, "#" + std::to_string(index + 1),
                    cv::Point(regions[index].x + 5,
                              std::max(28, regions[index].y + 28)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.75, cv::Scalar(255, 255, 255),
                    2, cv::LINE_AA);
    }
}

cv::Mat displaySize(const cv::Mat &source, int maximumWidth = 1280) {
    if (source.cols <= maximumWidth) return source;
    cv::Mat resized;
    const double scale = static_cast<double>(maximumWidth) / source.cols;
    cv::resize(source, resized, cv::Size(), scale, scale, cv::INTER_AREA);
    return resized;
}

cv::Rect pixelRoi(const QRectF &normalized, const cv::Size &size) {
    const QRectF safe = normalized.normalized().intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    const int x1 = std::clamp(static_cast<int>(std::floor(safe.left() * size.width)),
                              0, size.width - 1);
    const int y1 = std::clamp(static_cast<int>(std::floor(safe.top() * size.height)),
                              0, size.height - 1);
    const int x2 = std::clamp(static_cast<int>(std::ceil(safe.right() * size.width)),
                              x1 + 1, size.width);
    const int y2 = std::clamp(static_cast<int>(std::ceil(safe.bottom() * size.height)),
                              y1 + 1, size.height);
    return {x1, y1, x2 - x1, y2 - y1};
}

} // namespace

VisionRuntime::VisionRuntime(QObject *parent) : QObject(parent) {
    qRegisterMetaType<AnalysisPacket>("AnalysisPacket");
    cv::setUseOptimized(true);
    cv::setNumThreads(std::max(1u, std::thread::hardware_concurrency() / 2));
}

VisionRuntime::~VisionRuntime() {
    stop();
}

void VisionRuntime::refreshDevices() {
    if (running_.load()) return;
    MV_CC_DEVICE_INFO_LIST devices{};
    const int code = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &devices);
    QStringList names;
    if (code == MV_OK) {
        for (unsigned int index = 0; index < devices.nDeviceNum; ++index)
            names.append(deviceName(devices.pDeviceInfo[index], index));
    }
    emit devicesChanged(names);
    if (code != MV_OK) {
        emit stateChanged(QStringLiteral("error"),
                          mvsError(QStringLiteral("Không thể tìm camera"), code));
    } else if (names.isEmpty()) {
        emit stateChanged(QStringLiteral("idle"),
                          QStringLiteral("Chưa tìm thấy camera GigE/USB."));
    } else {
        emit stateChanged(QStringLiteral("idle"),
                          QStringLiteral("Đã tìm thấy %1 camera.").arg(names.size()));
    }
}

void VisionRuntime::start(unsigned int cameraIndex) {
    stop();
    clearQueue();
    capturedTotal_.store(0);
    processedTotal_.store(0);
    droppedTotal_.store(0);
    running_.store(true);
    emit stateChanged(QStringLiteral("starting"),
                      QStringLiteral("Đang mở camera và khởi tạo PP7-Fast…"));
    processingThread_ = std::thread(&VisionRuntime::processingLoop, this);
    captureThread_ = std::thread(&VisionRuntime::captureLoop, this, cameraIndex);
}

void VisionRuntime::stop() {
    const bool hadWork = running_.exchange(false) || captureThread_.joinable() ||
                         processingThread_.joinable();
    queueCondition_.notify_all();
    if (captureThread_.joinable()) captureThread_.join();
    if (processingThread_.joinable()) processingThread_.join();
    clearQueue();
    if (hadWork)
        emit stateChanged(QStringLiteral("stopped"),
                          QStringLiteral("Đã dừng kiểm tra. Ảnh cuối vẫn được giữ lại."));
}

void VisionRuntime::setProcessingRoi(const QRectF &normalizedRoi) {
    QRectF safe = normalizedRoi.normalized().intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    if (safe.width() < 0.05 || safe.height() < 0.05) return;
    std::lock_guard<std::mutex> lock(roiMutex_);
    processingRoi_ = safe;
}

void VisionRuntime::captureLoop(unsigned int cameraIndex) {
    CameraHandle camera;
    try {
        MV_CC_DEVICE_INFO_LIST devices{};
        int code = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &devices);
        if (code != MV_OK)
            throw std::runtime_error(mvsError(QStringLiteral("Lỗi tìm camera"), code)
                                         .toStdString());
        if (cameraIndex >= devices.nDeviceNum)
            throw std::runtime_error("Camera đã chọn không còn khả dụng");

        MV_CC_DEVICE_INFO *info = devices.pDeviceInfo[cameraIndex];
        code = MV_CC_CreateHandle(camera.address(), info);
        if (code != MV_OK)
            throw std::runtime_error(mvsError(QStringLiteral("Không tạo được kết nối"), code)
                                         .toStdString());
        code = MV_CC_OpenDevice(camera.value());
        if (code != MV_OK)
            throw std::runtime_error(mvsError(QStringLiteral("Không mở được camera"), code)
                                         .toStdString());
        camera.markOpen();

        if (info->nTLayerType == MV_GIGE_DEVICE) {
            const int packetSize = MV_CC_GetOptimalPacketSize(camera.value());
            if (packetSize > 0)
                MV_CC_SetIntValue(camera.value(), "GevSCPSPacketSize",
                                  static_cast<unsigned int>(packetSize));
        }
        MV_CC_SetEnumValue(camera.value(), "TriggerMode", 0);
        MV_CC_SetBoolValue(camera.value(), "AcquisitionFrameRateEnable", true);
        MV_CC_SetFloatValue(camera.value(), "AcquisitionFrameRate", 20.0F);
        MV_CC_SetEnumValue(camera.value(), "ExposureAuto", 2);
        MV_CC_SetEnumValue(camera.value(), "GainAuto", 2);
        MV_CC_SetEnumValue(camera.value(), "BalanceWhiteAuto", 1);

        MVCC_INTVALUE payload{};
        code = MV_CC_GetIntValue(camera.value(), "PayloadSize", &payload);
        if (code != MV_OK || payload.nCurValue == 0)
            throw std::runtime_error(mvsError(QStringLiteral("Không đọc được PayloadSize"), code)
                                         .toStdString());

        std::vector<unsigned char> raw(static_cast<std::size_t>(payload.nCurValue));
        std::vector<unsigned char> conversion;
        code = MV_CC_StartGrabbing(camera.value());
        if (code != MV_OK)
            throw std::runtime_error(mvsError(QStringLiteral("Không bắt đầu thu hình"), code)
                                         .toStdString());
        camera.markGrabbing();
        emit stateChanged(QStringLiteral("running"),
                          QStringLiteral("Camera đã kết nối. Đang kiểm tra bề mặt vải."));

        while (running_.load()) {
            MV_FRAME_OUT_INFO_EX frameInfo{};
            code = MV_CC_GetOneFrameTimeout(
                camera.value(), raw.data(), static_cast<unsigned int>(raw.size()),
                &frameInfo, 200);
            if (code != MV_OK) continue;
            cv::Mat bgr;
            if (!convertToBgr(camera.value(), frameInfo, raw.data(), conversion, bgr))
                continue;
            const quint64 number = capturedTotal_.fetch_add(1) + 1;
            pushFrame({number, std::move(bgr), Clock::now(),
                       QDateTime::currentDateTime()});
        }
    } catch (const std::exception &error) {
        if (running_.exchange(false)) {
            queueCondition_.notify_all();
            emit stateChanged(QStringLiteral("error"), QString::fromUtf8(error.what()));
        }
    }
}

void VisionRuntime::processingLoop() {
    pp7::FastConfig config;
    config.background_radius = 7;
    config.sensitivity = 3.0F;
    config.minimum_area = 3;
    pp7::FastDetector detector(config);

    quint64 lastCaptured = 0;
    quint64 lastProcessed = 0;
    double cameraFps = 0.0;
    double detectorFps = 0.0;
    auto statisticsAt = Clock::now();
    auto lastEventAt = Clock::time_point::min();
    CapturedFrame captured;

    while (popFrame(captured)) {
        try {
            QRectF normalizedRoi;
            {
                std::lock_guard<std::mutex> lock(roiMutex_);
                normalizedRoi = processingRoi_;
            }
            const cv::Rect roi = pixelRoi(normalizedRoi, captured.image.size());
            constexpr int ProcessWidth = 640;
            const int width = std::min(ProcessWidth, roi.width);
            const int height = std::max(1, static_cast<int>(std::lround(
                static_cast<double>(roi.height) * width / roi.width)));
            cv::Mat analysis;
            cv::resize(captured.image(roi), analysis, cv::Size(width, height),
                       0.0, 0.0, cv::INTER_AREA);

            const auto begin = Clock::now();
            const pp7::Result result = detector.detect(analysis);
            const auto finished = Clock::now();
            const double processingMs =
                std::chrono::duration<double, std::milli>(finished - begin).count();
            const quint64 processed = processedTotal_.fetch_add(1) + 1;
            const auto regions = mapDetections(result.detections, roi, analysis.size());

            const double interval =
                std::chrono::duration<double>(finished - statisticsAt).count();
            if (interval >= 1.0) {
                const quint64 currentCaptured = capturedTotal_.load();
                cameraFps = (currentCaptured - lastCaptured) / interval;
                detectorFps = (processed - lastProcessed) / interval;
                lastCaptured = currentCaptured;
                lastProcessed = processed;
                statisticsAt = finished;
            }

            cv::Mat annotated = captured.image.clone();
            drawDetections(annotated, regions);
            const cv::Mat display = displaySize(annotated);

            AnalysisPacket packet;
            packet.frame = toQImage(display);
            packet.frameNumber = captured.number;
            packet.processedTotal = processed;
            packet.droppedTotal = droppedTotal_.load();
            packet.regionCount = static_cast<int>(regions.size());
            packet.cameraFps = cameraFps;
            packet.detectorFps = detectorFps;
            packet.processingMs = processingMs;
            packet.capturedAt = captured.wallTime;

            if (!regions.empty() &&
                (lastEventAt == Clock::time_point::min() ||
                 std::chrono::duration<double>(finished - lastEventAt).count() >= 2.0)) {
                lastEventAt = finished;
                const auto largest = std::max_element(
                    regions.begin(), regions.end(), [](const cv::Rect &a, const cv::Rect &b) {
                        return a.area() < b.area();
                    });
                const cv::Rect bounds(0, 0, captured.image.cols, captured.image.rows);
                const int paddingX = std::max(24, largest->width / 5);
                const int paddingY = std::max(24, largest->height / 2);
                const cv::Rect padded(largest->x - paddingX,
                                      largest->y - paddingY,
                                      largest->width + 2 * paddingX,
                                      largest->height + 2 * paddingY);
                packet.crop = toQImage(captured.image(padded & bounds));
                packet.context = packet.frame;
            }
            emit packetReady(packet);
        } catch (const std::exception &error) {
            running_.store(false);
            emit stateChanged(QStringLiteral("error"), QString::fromUtf8(error.what()));
            queueCondition_.notify_all();
            break;
        }
    }
}

void VisionRuntime::pushFrame(CapturedFrame frame) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (!running_.load()) return;
    if (queue_.size() >= QueueCapacity) {
        queue_.pop_front();
        droppedTotal_.fetch_add(1);
    }
    queue_.push_back(std::move(frame));
    queueCondition_.notify_one();
}

bool VisionRuntime::popFrame(CapturedFrame &frame) {
    std::unique_lock<std::mutex> lock(queueMutex_);
    queueCondition_.wait(lock, [this] {
        return !running_.load() || !queue_.empty();
    });
    if (!running_.load()) return false;
    frame = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

void VisionRuntime::clearQueue() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.clear();
}
