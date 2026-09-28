#pragma once

#include <opencv2/core.hpp>

#include <vector>

namespace pp7 {

struct Config {
    std::vector<int> background_radii{5, 15};
    int noise_radius = 4;
    int presmooth_radius = 1;
    float sensitivity = 3.2F;
    float minimum_contrast = 0.025F;
    float chroma_sensitivity = 2.8F;
    float minimum_chroma_contrast = 0.012F;
    float border_chroma_threshold = 0.04F;
    int border_guard_radius = 15;
    int min_anomaly_area = 20;
    int minimum_neighbors = 3;
    int merge_gap = 40;
    int alignment_tolerance = 20;
};

struct Detection {
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;  // exclusive
    int y2 = 0;  // exclusive
    float score = 0.0F;
};

struct Result {
    cv::Mat border_mask;  // CV_8UC1, values 0/255
    cv::Mat bright_mask;  // CV_8UC1, values 0/255
    cv::Mat dark_mask;    // CV_8UC1, values 0/255
    cv::Mat chroma_mask;  // CV_8UC1, values 0/255
    cv::Mat response;     // CV_32FC1
    std::vector<Detection> detections;
    int raw_components = 0;

    cv::Mat anomalyMask() const;
};

// Input must be an 8-bit BGR or grayscale OpenCV image.
Result detect(const cv::Mat& image, const Config& config = Config{});

// Paints the PP7 masks and bounding boxes on a BGR image.
cv::Mat renderAnnotated(const cv::Mat& image, const Result& result);

// Creates a color map: border/orange, bright/red, dark/purple, chroma/green.
cv::Mat renderComponents(const Result& result);

}  // namespace pp7
