#ifndef RETINAFACE_ENGINE_H
#define RETINAFACE_ENGINE_H

#include <rknn_api.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <memory>

struct FaceInfo {
    cv::Rect bbox;                        // 人脸框 {x, y, width, height}
    std::vector<cv::Point2f> landmarks;   // 5个关键点（左眼、右眼、鼻子、左嘴角、右嘴角）
    float score;                          // 置信度
};

class RetinaFaceEngine {
public:
    RetinaFaceEngine();
    ~RetinaFaceEngine();

    // 初始化，加载模型
    bool init(const std::string& modelPath, int inputWidth = 640, int inputHeight = 640);

    // 检测人脸
    std::vector<FaceInfo> detect(const cv::Mat& image);

    // 释放资源
    void release();

    // 是否已初始化
    bool isInitialized() const { return m_initialized; }

private:
    // 预处理：缩放、颜色转换、归一化、HWC转CHW
    cv::Mat preprocess(const cv::Mat& src, float& scaleX, float& scaleY);

    // 后处理：解析输出、NMS
    std::vector<FaceInfo> postprocess(float* outputs[], int outputCount,
                                       int imgWidth, int imgHeight,
                                       float scaleX, float scaleY);

    // NMS去重
    void nms(std::vector<FaceInfo>& faces, float threshold);

    // 计算IoU
    float calculateIou(const FaceInfo& a, const FaceInfo& b);

private:
    rknn_context m_ctx;           // RKNN上下文
    int m_inputWidth;              // 输入宽度
    int m_inputHeight;             // 输入高度
    bool m_initialized;            // 初始化标志
};

#endif // RETINAFACE_ENGINE_H
