#include "pp7_fast_detector.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pp7 {
namespace {

float sampledPercentile(const cv::Mat& source, int stride, float percentile) {
    stride = std::max(1, stride);
    std::vector<float> values;
    values.reserve((source.rows / stride + 1) * (source.cols / stride + 1));
    for (int y = 0; y < source.rows; y += stride) {
        const float* row = source.ptr<float>(y);
        for (int x = 0; x < source.cols; x += stride) {
            if (std::isfinite(row[x])) values.push_back(row[x]);
        }
    }
    if (values.empty()) return 0.0F;
    const float fraction = std::clamp(percentile / 100.0F, 0.0F, 1.0F);
    const std::size_t index = static_cast<std::size_t>(
        std::lround(fraction * static_cast<float>(values.size() - 1)));
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values[index];
}

int axisGap(int a1, int a2, int b1, int b2) {
    if (a2 < b1) return b1 - a2;
    if (b2 < a1) return a1 - b2;
    return 0;
}

bool shouldMerge(const Detection& first, const Detection& second,
                 const FastConfig& config) {
    const int x_gap = axisGap(first.x1, first.x2, second.x1, second.x2);
    const int y_gap = axisGap(first.y1, first.y2, second.y1, second.y2);
    const float first_x = 0.5F * (first.x1 + first.x2);
    const float second_x = 0.5F * (second.x1 + second.x2);
    const float first_y = 0.5F * (first.y1 + first.y2);
    const float second_y = 0.5F * (second.y1 + second.y2);
    return (std::abs(first_x - second_x) <= config.alignment_tolerance &&
            y_gap <= config.merge_gap) ||
           (std::abs(first_y - second_y) <= config.alignment_tolerance &&
            x_gap <= config.merge_gap);
}

std::vector<Detection> mergeDetections(const std::vector<Detection>& items,
                                       const FastConfig& config) {
    if (items.empty()) return {};
    std::vector<std::size_t> parent(items.size());
    std::iota(parent.begin(), parent.end(), 0);
    auto findRoot = [&](std::size_t index) {
        while (parent[index] != index) {
            parent[index] = parent[parent[index]];
            index = parent[index];
        }
        return index;
    };

    for (std::size_t left = 0; left < items.size(); ++left) {
        for (std::size_t right = left + 1; right < items.size(); ++right) {
            if (!shouldMerge(items[left], items[right], config)) continue;
            const std::size_t a = findRoot(left);
            const std::size_t b = findRoot(right);
            if (a != b) parent[b] = a;
        }
    }

    std::unordered_map<std::size_t, Detection> groups;
    for (std::size_t index = 0; index < items.size(); ++index) {
        const std::size_t root = findRoot(index);
        auto found = groups.find(root);
        if (found == groups.end()) {
            groups.emplace(root, items[index]);
            continue;
        }
        Detection& value = found->second;
        value.x1 = std::min(value.x1, items[index].x1);
        value.y1 = std::min(value.y1, items[index].y1);
        value.x2 = std::max(value.x2, items[index].x2);
        value.y2 = std::max(value.y2, items[index].y2);
        value.score = std::max(value.score, items[index].score);
    }

    std::vector<Detection> result;
    result.reserve(groups.size());
    for (const auto& item : groups) result.push_back(item.second);
    std::sort(result.begin(), result.end(), [](const Detection& a,
                                               const Detection& b) {
        return a.y1 == b.y1 ? a.x1 < b.x1 : a.y1 < b.y1;
    });
    return result;
}

cv::Mat boxMean(const cv::Mat& source, int radius) {
    cv::Mat output;
    const int size = 2 * radius + 1;
    cv::boxFilter(source, output, CV_32F, cv::Size(size, size),
                  cv::Point(-1, -1), true, cv::BORDER_REFLECT_101);
    return output;
}

}  // namespace

FastDetector::FastDetector(FastConfig config) : config_(std::move(config)) {
    if (config_.background_radius < 1 || config_.noise_radius < 1 ||
        config_.sensitivity <= 0.0F || config_.chroma_sensitivity <= 0.0F ||
        config_.minimum_area < 1 || config_.sample_stride < 1 ||
        config_.morphology_size < 1) {
        throw std::invalid_argument("Invalid PP7-Fast configuration");
    }
    if (config_.morphology_size % 2 == 0) ++config_.morphology_size;
}

Result FastDetector::detect(const cv::Mat& image) const {
    if (image.empty() || image.type() != CV_8UC3) {
        throw std::invalid_argument("PP7-Fast expects a non-empty CV_8UC3 image");
    }

    cv::Mat gray_u8;
    cv::cvtColor(image, gray_u8, cv::COLOR_BGR2GRAY);
    cv::Mat gray;
    gray_u8.convertTo(gray, CV_32F, 1.0 / 255.0);

    cv::Mat local_mean = boxMean(gray, config_.background_radius);
    cv::Mat residual = gray - local_mean;
    cv::Mat absolute_residual;
    cv::absdiff(residual, cv::Scalar(0), absolute_residual);
    cv::Mat local_noise = boxMean(absolute_residual, config_.noise_radius);
    local_noise *= 1.2533F;
    const float typical_noise = std::max(
        sampledPercentile(local_noise, config_.sample_stride, 60.0F),
        config_.minimum_contrast / config_.sensitivity);
    cv::threshold(local_noise, local_noise, 2.0F * typical_noise,
                  2.0F * typical_noise, cv::THRESH_TRUNC);
    cv::Mat intensity_threshold = config_.sensitivity * local_noise;
    cv::max(intensity_threshold,
            static_cast<double>(config_.minimum_contrast), intensity_threshold);

    cv::Mat bright_response, dark_response;
    cv::divide(residual, intensity_threshold, bright_response);
    cv::divide(-residual, intensity_threshold, dark_response);
    cv::max(bright_response, 0.0, bright_response);
    cv::max(dark_response, 0.0, dark_response);

    cv::Mat color_float;
    image.convertTo(color_float, CV_32FC3, 1.0 / 255.0);
    std::vector<cv::Mat> bgr;
    cv::split(color_float, bgr);
    cv::Mat opponent_rg = bgr[2] - bgr[1];
    cv::Mat opponent_yb = 0.5F * (bgr[2] + bgr[1]) - bgr[0];
    cv::Mat rg_delta = opponent_rg - boxMean(opponent_rg, config_.background_radius);
    cv::Mat yb_delta = opponent_yb - boxMean(opponent_yb, config_.background_radius);
    cv::Mat chroma_magnitude;
    cv::magnitude(rg_delta, yb_delta, chroma_magnitude);
    const float typical_chroma = sampledPercentile(
        chroma_magnitude, config_.sample_stride, 60.0F);
    const float chroma_threshold = std::max(
        config_.minimum_chroma_contrast,
        config_.chroma_sensitivity * typical_chroma);
    cv::Mat chroma_response = chroma_magnitude / chroma_threshold;

    cv::Mat bright_candidate, dark_candidate, chroma_candidate;
    cv::compare(bright_response, 1.0, bright_candidate, cv::CMP_GT);
    cv::compare(dark_response, 1.0, dark_candidate, cv::CMP_GT);
    cv::compare(chroma_response, 1.0, chroma_candidate, cv::CMP_GT);
    cv::Mat combined;
    cv::bitwise_or(bright_candidate, dark_candidate, combined);
    cv::bitwise_or(combined, chroma_candidate, combined);

    const cv::Mat kernel = cv::getStructuringElement(
        cv::MORPH_RECT,
        cv::Size(config_.morphology_size, config_.morphology_size));
    cv::morphologyEx(combined, combined, cv::MORPH_OPEN, kernel,
                     cv::Point(-1, -1), 1, cv::BORDER_CONSTANT);

    cv::Mat labels, stats, centroids;
    const int component_count = cv::connectedComponentsWithStats(
        combined, labels, stats, centroids, 8, CV_32S);
    std::vector<unsigned char> keep(static_cast<std::size_t>(component_count), 0);
    for (int label = 1; label < component_count; ++label) {
        if (stats.at<int>(label, cv::CC_STAT_AREA) >= config_.minimum_area) {
            keep[static_cast<std::size_t>(label)] = 255;
        }
    }

    Result result;
    result.border_mask = cv::Mat(image.size(), CV_8UC1, cv::Scalar(0));
    result.bright_mask = cv::Mat(image.size(), CV_8UC1, cv::Scalar(0));
    result.dark_mask = cv::Mat(image.size(), CV_8UC1, cv::Scalar(0));
    result.chroma_mask = cv::Mat(image.size(), CV_8UC1, cv::Scalar(0));
    cv::max(bright_response, dark_response, result.response);
    cv::max(result.response, chroma_response, result.response);

    std::vector<float> component_score(static_cast<std::size_t>(component_count),
                                       0.0F);
    for (int y = 0; y < image.rows; ++y) {
        const int* label_row = labels.ptr<int>(y);
        const float* bright_row = bright_response.ptr<float>(y);
        const float* dark_row = dark_response.ptr<float>(y);
        const float* chroma_row = chroma_response.ptr<float>(y);
        unsigned char* bright_mask_row = result.bright_mask.ptr<unsigned char>(y);
        unsigned char* dark_mask_row = result.dark_mask.ptr<unsigned char>(y);
        unsigned char* chroma_mask_row = result.chroma_mask.ptr<unsigned char>(y);
        for (int x = 0; x < image.cols; ++x) {
            const int label = label_row[x];
            if (label <= 0 || keep[static_cast<std::size_t>(label)] == 0) continue;
            const float bright_value = bright_row[x];
            const float dark_value = dark_row[x];
            const float chroma_value = chroma_row[x];
            const float maximum = std::max({bright_value, dark_value, chroma_value});
            component_score[static_cast<std::size_t>(label)] = std::max(
                component_score[static_cast<std::size_t>(label)], maximum);
            if (chroma_value >= bright_value && chroma_value >= dark_value) {
                chroma_mask_row[x] = 255;
            } else if (bright_value >= dark_value) {
                bright_mask_row[x] = 255;
            } else {
                dark_mask_row[x] = 255;
            }
        }
    }

    std::vector<Detection> raw;
    for (int label = 1; label < component_count; ++label) {
        if (keep[static_cast<std::size_t>(label)] == 0) continue;
        const int x = stats.at<int>(label, cv::CC_STAT_LEFT);
        const int y = stats.at<int>(label, cv::CC_STAT_TOP);
        const int width = stats.at<int>(label, cv::CC_STAT_WIDTH);
        const int height = stats.at<int>(label, cv::CC_STAT_HEIGHT);
        raw.push_back({x, y, x + width, y + height,
                       component_score[static_cast<std::size_t>(label)]});
    }
    result.raw_components = static_cast<int>(raw.size());
    result.detections = mergeDetections(raw, config_);
    return result;
}

}  // namespace pp7
