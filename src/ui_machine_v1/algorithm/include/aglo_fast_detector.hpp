#pragma once

#include "pp7_detector.hpp"

namespace pp7 {

// Real-time approximation of PP7 for a fixed industrial camera.
// It keeps the bright/dark/chroma responses, but uses one spatial scale,
// sampled robust noise and a single connected-component pass.
struct FastConfig {
    int background_radius = 7;
    int noise_radius = 2;
    float sensitivity = 3.0F;
    float minimum_contrast = 0.025F;
    float chroma_sensitivity = 2.7F;
    float minimum_chroma_contrast = 0.018F;
    int minimum_area = 3;
    int morphology_size = 3;
    int sample_stride = 4;
    int merge_gap = 12;
    int alignment_tolerance = 8;
};

class FastDetector {
public:
    explicit FastDetector(FastConfig config = FastConfig{});

    Result detect(const cv::Mat& bgr_image) const;
    const FastConfig& config() const { return config_; }

private:
    FastConfig config_;
};

}  // namespace pp7
