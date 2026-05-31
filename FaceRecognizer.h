#ifndef FACERECOGNIZER_H
#define FACERECOGNIZER_H

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include "rknn_api.h"

class FaceRecognizer
{
public:
    FaceRecognizer();
    ~FaceRecognizer();

    bool init(const std::string& model_path);
    std::vector<float> extractFeature(const cv::Mat& face);
    void release();

private:
    rknn_context m_ctx;
    bool m_initialized;
    int m_feature_dim;  // 特征维度，MobileFaceNet 是 128
    int m_input_width;
    int m_input_height;
};

#endif // FACERECOGNIZER_H
