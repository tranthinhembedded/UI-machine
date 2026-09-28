#include "pp7_detector.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace pp7 {
namespace {

cv::Mat boxMean(const cv::Mat& source, int radius) {
    if (source.type() != CV_32FC1) {
        throw std::invalid_argument("boxMean expects CV_32FC1");
    }
    if (radius < 0) {
        throw std::invalid_argument("A PP7 radius cannot be negative");
    }
    if (radius == 0) {
        return source.clone();
    }

    cv::Mat destination;
    const int kernel = 2 * radius + 1;
    cv::boxFilter(source, destination, CV_32F, cv::Size(kernel, kernel),
                  cv::Point(-1, -1), true, cv::BORDER_REFLECT_101);
    return destination;
}

float percentile(std::vector<float>& values, float percent) {
    if (values.empty()) {
        return 0.0F;
    }
    percent = std::clamp(percent, 0.0F, 100.0F);
    const double rank = (static_cast<double>(percent) / 100.0) *
                        static_cast<double>(values.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(rank));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(rank));

    std::nth_element(values.begin(), values.begin() + lower, values.end());
    const float lower_value = values[lower];
    if (upper == lower) {
        return lower_value;
    }
    std::nth_element(values.begin(), values.begin() + upper, values.end());
    const float upper_value = values[upper];
    const float fraction = static_cast<float>(rank - static_cast<double>(lower));
    return lower_value + fraction * (upper_value - lower_value);
}

float percentileMat(const cv::Mat& source, float percent,
                    const cv::Mat& include_mask = cv::Mat()) {
    if (source.type() != CV_32FC1) {
        throw std::invalid_argument("percentileMat expects CV_32FC1");
    }
    if (!include_mask.empty() &&
        (include_mask.type() != CV_8UC1 || include_mask.size() != source.size())) {
        throw std::invalid_argument("Invalid percentile mask");
    }

    std::vector<float> values;
    values.reserve(include_mask.empty()
                       ? source.total()
                       : static_cast<std::size_t>(cv::countNonZero(include_mask)));
    for (int y = 0; y < source.rows; ++y) {
        const float* row = source.ptr<float>(y);
        const unsigned char* mask_row =
            include_mask.empty() ? nullptr : include_mask.ptr<unsigned char>(y);
        for (int x = 0; x < source.cols; ++x) {
            if ((mask_row == nullptr || mask_row[x] != 0) && std::isfinite(row[x])) {
                values.push_back(row[x]);
            }
        }
    }
    return percentile(values, percent);
}

cv::Mat neighborCount(const cv::Mat& mask) {
    if (mask.type() != CV_8UC1) {
        throw std::invalid_argument("neighborCount expects CV_8UC1");
    }
    cv::Mat binary;
    cv::compare(mask, 0, binary, cv::CMP_GT);
    binary /= 255;

    cv::Mat count;
    cv::boxFilter(binary, count, CV_32F, cv::Size(3, 3), cv::Point(-1, -1),
                  false, cv::BORDER_CONSTANT);
    return count;
}

cv::Mat cleanMask(const cv::Mat& mask, int minimum_neighbors) {
    cv::Mat first_count = neighborCount(mask);
    cv::Mat enough_neighbors;
    cv::compare(first_count, minimum_neighbors, enough_neighbors, cv::CMP_GE);

    cv::Mat opened;
    cv::bitwise_and(mask, enough_neighbors, opened);
    cv::Mat second_count = neighborCount(opened);
    cv::Mat cleaned;
    cv::compare(second_count, 2, cleaned, cv::CMP_GE);
    return cleaned;
}

cv::Mat retainLarge(const cv::Mat& mask, int minimum_area) {
    cv::Mat labels, stats, centroids;
    const int count = cv::connectedComponentsWithStats(
        mask, labels, stats, centroids, 8, CV_32S);
    std::vector<unsigned char> retain(static_cast<std::size_t>(count), 0);
    for (int label = 1; label < count; ++label) {
        if (stats.at<int>(label, cv::CC_STAT_AREA) >= minimum_area) {
            retain[static_cast<std::size_t>(label)] = 255;
        }
    }

    cv::Mat output(mask.size(), CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < labels.rows; ++y) {
        const int* label_row = labels.ptr<int>(y);
        unsigned char* output_row = output.ptr<unsigned char>(y);
        for (int x = 0; x < labels.cols; ++x) {
            output_row[x] = retain[static_cast<std::size_t>(label_row[x])];
        }
    }
    return output;
}

cv::Mat qualifyingBorderComponents(const cv::Mat& candidates) {
    const int height = candidates.rows;
    const int width = candidates.cols;
    const int perimeter = std::max(1, 2 * height + 2 * width - 4);

    cv::Mat labels, stats, centroids;
    const int count = cv::connectedComponentsWithStats(
        candidates, labels, stats, centroids, 8, CV_32S);
    std::vector<int> edge_pixels(static_cast<std::size_t>(count), 0);

    auto add_edge = [&](int y, int x) {
        const int label = labels.at<int>(y, x);
        if (label > 0) {
            ++edge_pixels[static_cast<std::size_t>(label)];
        }
    };
    for (int x = 0; x < width; ++x) {
        add_edge(0, x);
        if (height > 1) add_edge(height - 1, x);
    }
    for (int y = 1; y + 1 < height; ++y) {
        add_edge(y, 0);
        if (width > 1) add_edge(y, width - 1);
    }

    std::vector<unsigned char> retain(static_cast<std::size_t>(count), 0);
    const double image_area = static_cast<double>(height) * width;
    for (int label = 1; label < count; ++label) {
        if (edge_pixels[static_cast<std::size_t>(label)] == 0) continue;
        const int component_width = stats.at<int>(label, cv::CC_STAT_WIDTH);
        const int component_height = stats.at<int>(label, cv::CC_STAT_HEIGHT);
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        const double edge_fraction =
            static_cast<double>(edge_pixels[static_cast<std::size_t>(label)]) /
            perimeter;
        const double span_fraction = std::max(
            static_cast<double>(component_height) / height,
            static_cast<double>(component_width) / width);
        const double area_fraction = static_cast<double>(area) / image_area;
        if (edge_fraction >= 0.08 && span_fraction >= 0.40 &&
            area_fraction <= 0.45) {
            retain[static_cast<std::size_t>(label)] = 255;
        }
    }

    cv::Mat output(candidates.size(), CV_8UC1, cv::Scalar(0));
    for (int y = 0; y < labels.rows; ++y) {
        const int* label_row = labels.ptr<int>(y);
        unsigned char* output_row = output.ptr<unsigned char>(y);
        for (int x = 0; x < labels.cols; ++x) {
            output_row[x] = retain[static_cast<std::size_t>(label_row[x])];
        }
    }
    return output;
}

std::vector<float> channelMedians(const cv::Mat& sample) {
    std::vector<cv::Mat> channels;
    cv::split(sample, channels);
    std::vector<float> medians;
    medians.reserve(channels.size());
    for (cv::Mat& channel : channels) {
        medians.push_back(percentileMat(channel, 50.0F));
    }
    return medians;
}

cv::Mat uniformEdgeBackground(const cv::Mat& gray, const cv::Mat& bgr_float) {
    const int height = gray.rows;
    const int width = gray.cols;
    const int depth = std::max(3, std::min(height, width) / 30);
    const bool color = !bgr_float.empty();
    const cv::Mat features = color ? bgr_float : gray;

    enum class Side { Top, Bottom, Left, Right };
    const std::vector<std::pair<Side, cv::Rect>> sides{
        {Side::Top, cv::Rect(0, 0, width, std::min(depth, height))},
        {Side::Bottom,
         cv::Rect(0, std::max(0, height - depth), width, std::min(depth, height))},
        {Side::Left, cv::Rect(0, 0, std::min(depth, width), height)},
        {Side::Right,
         cv::Rect(std::max(0, width - depth), 0, std::min(depth, width), height)},
    };

    cv::Mat background(gray.size(), CV_8UC1, cv::Scalar(0));
    for (const auto& side_and_rect : sides) {
        const Side side = side_and_rect.first;
        const cv::Mat sample = features(side_and_rect.second);
        const std::vector<float> edge_color = channelMedians(sample);

        cv::Mat distance(gray.size(), CV_32FC1, cv::Scalar(0));
        if (color) {
            std::vector<cv::Mat> channels;
            cv::split(features, channels);
            for (std::size_t c = 0; c < channels.size(); ++c) {
                cv::Mat delta = channels[c] - edge_color[c];
                distance += delta.mul(delta);
            }
            cv::sqrt(distance, distance);
        } else {
            cv::absdiff(features, cv::Scalar(edge_color[0]), distance);
        }

        cv::Mat candidates;
        cv::compare(distance, 0.10F, candidates, cv::CMP_LT);
        cv::Mat labels, stats, centroids;
        const int count = cv::connectedComponentsWithStats(
            candidates, labels, stats, centroids, 8, CV_32S);

        std::vector<int> coverage(static_cast<std::size_t>(count), 0);
        if (side == Side::Top || side == Side::Bottom) {
            const int y = side == Side::Top ? 0 : height - 1;
            const int* row = labels.ptr<int>(y);
            for (int x = 0; x < width; ++x) {
                if (row[x] > 0) ++coverage[static_cast<std::size_t>(row[x])];
            }
        } else {
            const int x = side == Side::Left ? 0 : width - 1;
            for (int y = 0; y < height; ++y) {
                const int label = labels.at<int>(y, x);
                if (label > 0) ++coverage[static_cast<std::size_t>(label)];
            }
        }

        const double image_area = static_cast<double>(height) * width;
        for (int label = 1; label < count; ++label) {
            const double edge_coverage =
                static_cast<double>(coverage[static_cast<std::size_t>(label)]) /
                ((side == Side::Top || side == Side::Bottom) ? width : height);
            if (edge_coverage < 0.35) continue;

            const int area = stats.at<int>(label, cv::CC_STAT_AREA);
            const double area_fraction = static_cast<double>(area) / image_area;
            if (area_fraction < 0.01 || area_fraction > 0.85) continue;

            cv::Mat component;
            cv::compare(labels, label, component, cv::CMP_EQ);
            const float region_level = static_cast<float>(cv::mean(gray, component)[0]);
            const cv::Scalar region_color = cv::mean(features, component);
            float minimum = static_cast<float>(region_color[0]);
            float maximum = minimum;
            const int channel_count = features.channels();
            for (int c = 1; c < channel_count; ++c) {
                minimum = std::min(minimum, static_cast<float>(region_color[c]));
                maximum = std::max(maximum, static_cast<float>(region_color[c]));
            }
            const bool neutral = (maximum - minimum) <= 0.12F;
            const bool extreme = region_level >= 0.85F || region_level <= 0.08F;
            if (neutral && extreme) {
                cv::bitwise_or(background, component, background);
            }
        }
    }
    return background;
}

cv::Mat estimateBorder(const cv::Mat& signal, const cv::Mat& bgr_float,
                       const Config& config) {
    const float center = percentileMat(signal, 50.0F);
    cv::Mat absolute;
    cv::absdiff(signal, cv::Scalar(center), absolute);
    const float sigma = 1.4826F * percentileMat(absolute, 50.0F);
    cv::Mat outliers;
    cv::compare(absolute, std::max(0.06F, 4.5F * sigma), outliers, cv::CMP_GT);
    cv::Mat border = qualifyingBorderComponents(outliers);

    if (!bgr_float.empty()) {
        std::vector<cv::Mat> bgr;
        cv::split(bgr_float, bgr);
        std::vector<cv::Mat> opponent_channels{
            bgr[2] - bgr[1], 0.5F * (bgr[2] + bgr[1]) - bgr[0]};
        cv::Mat color_outliers(signal.size(), CV_8UC1, cv::Scalar(0));
        for (cv::Mat& channel : opponent_channels) {
            channel = boxMean(channel, config.presmooth_radius);
            const float channel_center = percentileMat(channel, 50.0F);
            cv::Mat difference;
            cv::absdiff(channel, cv::Scalar(channel_center), difference);
            const float channel_sigma = 1.4826F * percentileMat(difference, 50.0F);
            cv::Mat current;
            cv::compare(difference,
                        std::max(config.border_chroma_threshold,
                                 4.5F * channel_sigma),
                        current, cv::CMP_GT);
            cv::bitwise_or(color_outliers, current, color_outliers);
        }
        cv::Mat color_border = qualifyingBorderComponents(color_outliers);
        cv::bitwise_or(border, color_border, border);
    }

    cv::Mat edge_background = uniformEdgeBackground(signal, bgr_float);
    if (static_cast<double>(cv::countNonZero(border)) / border.total() > 0.85) {
        border.setTo(0);
    }
    if (cv::countNonZero(edge_background) > 0) {
        border = edge_background;
    }
    const cv::Mat border_core = border.clone();

    if (config.border_guard_radius > 0 && cv::countNonZero(border) > 0) {
        cv::Mat border_float;
        border.convertTo(border_float, CV_32F, 1.0 / 255.0);
        cv::Mat expanded = boxMean(border_float, config.border_guard_radius);
        cv::compare(expanded, 0.0F, border, cv::CMP_GT);
    }
    if (static_cast<double>(cv::countNonZero(border)) / border.total() > 0.95) {
        border = border_core;
    }
    if (static_cast<double>(cv::countNonZero(border)) / border.total() > 0.95) {
        border.setTo(0);
    }
    return border;
}

std::pair<cv::Mat, cv::Mat> localResponses(
    const cv::Mat& signal, const cv::Mat& border_mask,
    const std::vector<int>& radii, int noise_radius, float sensitivity,
    float minimum_contrast) {
    cv::Mat positive(signal.size(), CV_32FC1, cv::Scalar(0));
    cv::Mat negative(signal.size(), CV_32FC1, cv::Scalar(0));
    cv::Mat valid;
    cv::bitwise_not(border_mask, valid);

    for (int radius : radii) {
        cv::Mat background = boxMean(signal, radius);
        cv::Mat residual = signal - background;
        cv::Mat absolute;
        cv::absdiff(residual, cv::Scalar(0), absolute);
        cv::Mat local_noise = 1.2533F * boxMean(absolute, noise_radius);
        const float typical_noise = std::max(
            percentileMat(local_noise, 60.0F, valid),
            minimum_contrast / sensitivity);

        cv::Mat effective_noise;
        cv::threshold(local_noise, effective_noise, 2.0F * typical_noise,
                      2.0F * typical_noise, cv::THRESH_TRUNC);
        cv::Mat threshold = sensitivity * effective_noise;
        cv::max(threshold, static_cast<double>(minimum_contrast), threshold);
        cv::Mat current_positive, current_negative;
        cv::divide(residual, threshold, current_positive);
        cv::divide(-residual, threshold, current_negative);
        cv::max(positive, current_positive, positive);
        cv::max(negative, current_negative, negative);
    }
    return {positive, negative};
}

std::vector<Detection> detectionsFromMask(const cv::Mat& mask,
                                          const cv::Mat& response,
                                          int minimum_area) {
    cv::Mat labels, stats, centroids;
    const int count = cv::connectedComponentsWithStats(
        mask, labels, stats, centroids, 8, CV_32S);
    std::vector<float> scores(static_cast<std::size_t>(count), 0.0F);
    for (int y = 0; y < labels.rows; ++y) {
        const int* label_row = labels.ptr<int>(y);
        const float* response_row = response.ptr<float>(y);
        for (int x = 0; x < labels.cols; ++x) {
            const int label = label_row[x];
            if (label > 0) {
                scores[static_cast<std::size_t>(label)] = std::max(
                    scores[static_cast<std::size_t>(label)], response_row[x]);
            }
        }
    }

    std::vector<Detection> output;
    for (int label = 1; label < count; ++label) {
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        if (area < minimum_area) continue;
        const int x = stats.at<int>(label, cv::CC_STAT_LEFT);
        const int y = stats.at<int>(label, cv::CC_STAT_TOP);
        const int width = stats.at<int>(label, cv::CC_STAT_WIDTH);
        const int height = stats.at<int>(label, cv::CC_STAT_HEIGHT);
        output.push_back(
            {x, y, x + width, y + height,
             scores[static_cast<std::size_t>(label)]});
    }
    return output;
}

int axisGap(int a1, int a2, int b1, int b2) {
    if (a2 < b1) return b1 - a2;
    if (b2 < a1) return a1 - b2;
    return 0;
}

bool shouldMerge(const Detection& first, const Detection& second,
                 const Config& config) {
    const int x_gap = axisGap(first.x1, first.x2, second.x1, second.x2);
    const int y_gap = axisGap(first.y1, first.y2, second.y1, second.y2);
    const float first_x = 0.5F * (first.x1 + first.x2);
    const float second_x = 0.5F * (second.x1 + second.x2);
    const float first_y = 0.5F * (first.y1 + first.y2);
    const float second_y = 0.5F * (second.y1 + second.y2);
    const bool vertical =
        std::abs(first_x - second_x) <= config.alignment_tolerance &&
        y_gap <= config.merge_gap;
    const bool horizontal =
        std::abs(first_y - second_y) <= config.alignment_tolerance &&
        x_gap <= config.merge_gap;
    return vertical || horizontal;
}

std::vector<Detection> mergeDetections(const std::vector<Detection>& items,
                                       const Config& config) {
    if (items.empty()) return {};
    std::vector<std::size_t> parents(items.size());
    std::iota(parents.begin(), parents.end(), 0);
    auto find_root = [&](std::size_t index) {
        while (parents[index] != index) {
            parents[index] = parents[parents[index]];
            index = parents[index];
        }
        return index;
    };

    for (std::size_t left = 0; left < items.size(); ++left) {
        for (std::size_t right = left + 1; right < items.size(); ++right) {
            if (!shouldMerge(items[left], items[right], config)) continue;
            const std::size_t left_root = find_root(left);
            const std::size_t right_root = find_root(right);
            if (left_root != right_root) parents[right_root] = left_root;
        }
    }

    std::unordered_map<std::size_t, Detection> groups;
    for (std::size_t index = 0; index < items.size(); ++index) {
        const std::size_t root = find_root(index);
        const auto found = groups.find(root);
        if (found == groups.end()) {
            groups.emplace(root, items[index]);
        } else {
            Detection& merged = found->second;
            merged.x1 = std::min(merged.x1, items[index].x1);
            merged.y1 = std::min(merged.y1, items[index].y1);
            merged.x2 = std::max(merged.x2, items[index].x2);
            merged.y2 = std::max(merged.y2, items[index].y2);
            merged.score = std::max(merged.score, items[index].score);
        }
    }

    std::vector<Detection> merged;
    merged.reserve(groups.size());
    for (const auto& key_and_detection : groups) {
        merged.push_back(key_and_detection.second);
    }
    std::sort(merged.begin(), merged.end(), [](const Detection& a,
                                                const Detection& b) {
        return a.y1 == b.y1 ? a.x1 < b.x1 : a.y1 < b.y1;
    });
    return merged;
}

cv::Mat ensureBgr8(const cv::Mat& image) {
    if (image.empty()) throw std::invalid_argument("PP7 received an empty image");
    if (image.depth() != CV_8U) {
        throw std::invalid_argument("PP7 expects an 8-bit image");
    }
    if (image.channels() == 3) return image;
    if (image.channels() == 1) {
        cv::Mat bgr;
        cv::cvtColor(image, bgr, cv::COLOR_GRAY2BGR);
        return bgr;
    }
    if (image.channels() == 4) {
        cv::Mat bgr;
        cv::cvtColor(image, bgr, cv::COLOR_BGRA2BGR);
        return bgr;
    }
    throw std::invalid_argument("PP7 expects a grayscale, BGR, or BGRA image");
}

}  // namespace

cv::Mat Result::anomalyMask() const {
    cv::Mat output;
    cv::bitwise_or(bright_mask, dark_mask, output);
    cv::bitwise_or(output, chroma_mask, output);
    return output;
}

Result detect(const cv::Mat& image, const Config& config) {
    if (config.background_radii.empty() || config.sensitivity <= 0.0F ||
        config.chroma_sensitivity <= 0.0F || config.minimum_neighbors < 0 ||
        config.min_anomaly_area < 1) {
        throw std::invalid_argument("Invalid PP7 configuration");
    }

    const cv::Mat bgr8 = ensureBgr8(image);
    cv::Mat bgr_float;
    bgr8.convertTo(bgr_float, CV_32FC3, 1.0 / 255.0);
    cv::Mat gray;
    cv::cvtColor(bgr_float, gray, cv::COLOR_BGR2GRAY);
    cv::Mat signal = boxMean(gray, config.presmooth_radius);
    cv::Mat border = estimateBorder(signal, bgr_float, config);

    auto intensity = localResponses(
        signal, border, config.background_radii, config.noise_radius,
        config.sensitivity, config.minimum_contrast);
    cv::Mat bright_response = std::move(intensity.first);
    cv::Mat dark_response = std::move(intensity.second);

    cv::Mat chroma_response(gray.size(), CV_32FC1, cv::Scalar(0));
    std::vector<cv::Mat> bgr;
    cv::split(bgr_float, bgr);
    std::vector<cv::Mat> opponent_channels{
        bgr[2] - bgr[1], 0.5F * (bgr[2] + bgr[1]) - bgr[0]};
    for (cv::Mat& channel : opponent_channels) {
        channel = boxMean(channel, config.presmooth_radius);
        auto color = localResponses(
            channel, border, config.background_radii, config.noise_radius,
            config.chroma_sensitivity, config.minimum_chroma_contrast);
        cv::Mat maximum;
        cv::max(color.first, color.second, maximum);
        cv::max(chroma_response, maximum, chroma_response);
    }

    cv::Mat not_border;
    cv::bitwise_not(border, not_border);
    cv::Mat candidate;
    cv::compare(bright_response, 1.0F, candidate, cv::CMP_GT);
    cv::bitwise_and(candidate, not_border, candidate);
    cv::Mat bright = retainLarge(
        cleanMask(candidate, config.minimum_neighbors), config.min_anomaly_area);

    cv::compare(dark_response, 1.0F, candidate, cv::CMP_GT);
    cv::bitwise_and(candidate, not_border, candidate);
    cv::Mat dark = retainLarge(
        cleanMask(candidate, config.minimum_neighbors), config.min_anomaly_area);

    cv::Mat overlap;
    cv::bitwise_and(bright, dark, overlap);
    cv::Mat comparison, remove;
    cv::compare(dark_response, bright_response, comparison, cv::CMP_GE);
    cv::bitwise_and(overlap, comparison, remove);
    bright.setTo(0, remove);
    cv::compare(bright_response, dark_response, comparison, cv::CMP_GT);
    cv::bitwise_and(overlap, comparison, remove);
    dark.setTo(0, remove);

    cv::compare(chroma_response, 1.0F, candidate, cv::CMP_GT);
    cv::bitwise_and(candidate, not_border, candidate);
    cv::Mat chroma = retainLarge(
        cleanMask(candidate, config.minimum_neighbors), config.min_anomaly_area);
    cv::Mat intensity_mask;
    cv::bitwise_or(bright, dark, intensity_mask);
    cv::bitwise_not(intensity_mask, comparison);
    cv::bitwise_and(chroma, comparison, chroma);

    cv::Mat response;
    cv::max(bright_response, dark_response, response);
    cv::max(response, chroma_response, response);
    cv::Mat anomaly;
    cv::bitwise_or(intensity_mask, chroma, anomaly);
    std::vector<Detection> raw =
        detectionsFromMask(anomaly, response, config.min_anomaly_area);

    Result result;
    result.border_mask = std::move(border);
    result.bright_mask = std::move(bright);
    result.dark_mask = std::move(dark);
    result.chroma_mask = std::move(chroma);
    result.response = std::move(response);
    result.raw_components = static_cast<int>(raw.size());
    result.detections = mergeDetections(raw, config);
    return result;
}

cv::Mat renderAnnotated(const cv::Mat& image, const Result& result) {
    cv::Mat output = ensureBgr8(image).clone();
    const std::vector<std::pair<cv::Mat, cv::Scalar>> masks{
        {result.border_mask, cv::Scalar(11, 158, 245)},
        {result.bright_mask, cv::Scalar(94, 63, 244)},
        {result.dark_mask, cv::Scalar(246, 92, 139)},
        {result.chroma_mask, cv::Scalar(94, 197, 34)},
    };
    for (const auto& mask_and_color : masks) {
        cv::Mat tint(output.size(), output.type(), mask_and_color.second);
        cv::Mat blended;
        cv::addWeighted(output, 0.40, tint, 0.60, 0.0, blended);
        blended.copyTo(output, mask_and_color.first);
    }

    const int line_width = std::max(2, std::min(output.rows, output.cols) / 100);
    for (std::size_t index = 0; index < result.detections.size(); ++index) {
        const Detection& item = result.detections[index];
        cv::rectangle(output, cv::Point(item.x1, item.y1),
                      cv::Point(item.x2 - 1, item.y2 - 1),
                      cv::Scalar(255, 229, 0), line_width, cv::LINE_AA);
        cv::putText(output, "#" + std::to_string(index + 1),
                    cv::Point(item.x1 + 2, std::max(18, item.y1 + 18)),
                    cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(255, 255, 255),
                    2, cv::LINE_AA);
    }
    return output;
}

cv::Mat renderComponents(const Result& result) {
    cv::Mat output(result.border_mask.size(), CV_8UC3, cv::Scalar(91, 83, 72));
    output.setTo(cv::Scalar(11, 158, 245), result.border_mask);
    output.setTo(cv::Scalar(94, 63, 244), result.bright_mask);
    output.setTo(cv::Scalar(246, 92, 139), result.dark_mask);
    output.setTo(cv::Scalar(94, 197, 34), result.chroma_mask);
    return output;
}

}  // namespace pp7
