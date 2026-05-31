#ifndef RETINAFACEDETECTOR_H
#define RETINAFACEDETECTOR_H

#include <opencv2/opencv.hpp>
#include <vector>
#include "rknn_api.h"

struct FaceBox {
    cv::Rect rect;
    float score;
    std::vector<cv::Point> landmarks;  // 5个关键点
};

class RetinaFaceDetector
{
public:
    RetinaFaceDetector();
    ~RetinaFaceDetector();

    bool init(const std::string& model_path);
    bool detect(const cv::Mat& frame, std::vector<FaceBox>& faces, float conf_threshold = 0.5);
    void release();

private:
    rknn_context m_ctx;
    bool m_initialized;
    int m_model_width;
    int m_model_height;
    int m_output_num;

    void postprocess(float* boxes, float* scores, float* landmarks,
                     int img_w, int img_h, float conf_threshold,
                     std::vector<FaceBox>& results);
};

#endif // RETINAFACEDETECTOR_H
