#ifndef RETINAFACEENGINE_H
#define RETINAFACEENGINE_H

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include "rknn_api.h"

struct FaceInfo {
    cv::Rect bbox;
    float score;
    std::vector<cv::Point2f> landmarks;
};

// PreprocessInfo 只在这里定义一次
struct PreprocessInfo {
    cv::Mat canvas;
    float scale;
    int offset_x;
    int offset_y;
};

class RetinaFaceEngine {
public:
    RetinaFaceEngine();
    ~RetinaFaceEngine();

    bool init(const std::string& modelPath, int inputW = 320, int inputH = 320);
    std::vector<FaceInfo> detect(const cv::Mat& img);
    void release();

private:
    PreprocessInfo preprocess(const cv::Mat& img);

    std::vector<FaceInfo> postprocess(
        float* outputs[],
        int outputCount,
        int imgW,
        int imgH,
        float scale,
        int offset_x,
        int offset_y);

    float iou(const FaceInfo& a, const FaceInfo& b);
    void nms(std::vector<FaceInfo>& faces, float thr);

private:
    rknn_context m_ctx;
    int m_inputW;
    int m_inputH;
    bool m_init;
};

#endif // RETINAFACEENGINE_H
