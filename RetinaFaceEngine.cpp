#include "RetinaFaceEngine.h"
#include "logger.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>

RetinaFaceEngine::RetinaFaceEngine()
    : m_ctx(0)
    , m_inputWidth(320)
    , m_inputHeight(320)
    , m_initialized(false)
{
}

RetinaFaceEngine::~RetinaFaceEngine()
{
    release();
}

bool RetinaFaceEngine::init(const std::string& modelPath, int inputWidth, int inputHeight)
{
    m_inputWidth = inputWidth;
    m_inputHeight = inputHeight;

    LOG_INFO(QString("Loading RetinaFace model from: %1").arg(modelPath.c_str()));

    // 1. 打开模型文件
    FILE* fp = fopen(modelPath.c_str(), "rb");
    if (!fp) {
        LOG_ERROR(QString("Model file not found: %1").arg(modelPath.c_str()));
        return false;
    }

    // 2. 获取文件大小
    fseek(fp, 0, SEEK_END);
    size_t modelSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // 3. 读取模型数据
    unsigned char* modelData = new unsigned char[modelSize];
    if (fread(modelData, 1, modelSize, fp) != modelSize) {
        LOG_ERROR("Failed to read model file");
        delete[] modelData;
        fclose(fp);
        return false;
    }
    fclose(fp);

    // 4. 初始化RKNN
    int ret = rknn_init(&m_ctx, (void*)modelData, modelSize, 0, nullptr);
    delete[] modelData;

    if (ret < 0) {
        LOG_ERROR(QString("rknn_init failed: %1").arg(ret));
        return false;
    }

    // 5. 查询输入输出信息
    rknn_input_output_num io_num;
    memset(&io_num, 0, sizeof(io_num));
    ret = rknn_query(m_ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (ret < 0) {
        LOG_ERROR("rknn_query IO_NUM failed");
        return false;
    }

    LOG_INFO(QString("Model inputs: %1, outputs: %2").arg(io_num.n_input).arg(io_num.n_output));

    // 6. 查询输入维度
    rknn_tensor_attr input_attrs[io_num.n_input];
    memset(input_attrs, 0, sizeof(input_attrs));
    for (uint32_t i = 0; i < io_num.n_input; i++) {
        input_attrs[i].index = i;
        ret = rknn_query(m_ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0) {
            LOG_ERROR("rknn_query INPUT_ATTR failed");
            return false;
        }
        LOG_INFO(QString("Input %1 shape: %2x%3x%4x%5")
                    .arg(i)
                    .arg(input_attrs[i].dims[0])
                    .arg(input_attrs[i].dims[1])
                    .arg(input_attrs[i].dims[2])
                    .arg(input_attrs[i].dims[3]));
    }

    // 7. 查询输出维度
    for (uint32_t i = 0; i < io_num.n_output; i++) {
        rknn_tensor_attr attr;
        attr.index = i;
        rknn_query(m_ctx, RKNN_QUERY_OUTPUT_ATTR, &attr, sizeof(attr));
        LOG_INFO(QString("Output %1 dims: [%2,%3,%4,%5], elems: %6")
                    .arg(i)
                    .arg(attr.dims[0])
                    .arg(attr.dims[1])
                    .arg(attr.dims[2])
                    .arg(attr.dims[3])
                    .arg(attr.n_elems));
    }

    m_initialized = true;
    LOG_INFO("RetinaFace engine initialized successfully");
    return true;
}

cv::Mat RetinaFaceEngine::preprocess(const cv::Mat& src, float& scaleX, float& scaleY)
{
    int srcWidth = src.cols;
    int srcHeight = src.rows;

    scaleX = (float)m_inputWidth / srcWidth;
    scaleY = (float)m_inputHeight / srcHeight;

    // 1. 缩放到模型输入尺寸
    cv::Mat resized;
    cv::resize(src, resized, cv::Size(m_inputWidth, m_inputHeight));

    // 2. 转换为 float32
    cv::Mat floatImg;
    resized.convertTo(floatImg, CV_32FC3);

    // 3. RetinaFace 预处理：减去均值 (BGR 顺序的均值)
    // 注意：输入是 RGB 格式，但模型是用 BGR 训练的，所以这里用 BGR 均值
    std::vector<float> mean_vals = {104.0f, 117.0f, 123.0f};  // BGR 均值

    std::vector<cv::Mat> channels(3);
    cv::split(floatImg, channels);

    // channels[0] = R, channels[1] = G, channels[2] = B
    // 减去均值时需要对应正确的通道
    // 模型期望 BGR，所以 channels[2] 减 104, channels[1] 减 117, channels[0] 减 123
    channels[0] = channels[0] - mean_vals[2];  // R - 123
    channels[1] = channels[1] - mean_vals[1];  // G - 117
    channels[2] = channels[2] - mean_vals[0];  // B - 104

    cv::merge(channels, floatImg);

    // 4. 使用 NHWC 格式
    cv::Mat nhwc(1, m_inputHeight * m_inputWidth * 3, CV_32FC1);
    memcpy(nhwc.data, floatImg.data, m_inputHeight * m_inputWidth * 3 * sizeof(float));

    return nhwc;
}

std::vector<FaceInfo> RetinaFaceEngine::detect(const cv::Mat& image)
{
    std::vector<FaceInfo> results;

    if (!m_initialized || image.empty()) {
        LOG_WARNING("detect: not initialized or empty image");
        return results;
    }

    // 1. 预处理
    float scaleX, scaleY;
    cv::Mat inputBlob = preprocess(image, scaleX, scaleY);

    if (inputBlob.empty() || inputBlob.data == nullptr) {
        LOG_ERROR("detect: inputBlob is empty");
        return results;
    }

    // 2. 设置输入
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_FLOAT32;
    inputs[0].size = m_inputWidth * m_inputHeight * 3 * sizeof(float);
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].buf = inputBlob.data;

    int ret = rknn_inputs_set(m_ctx, 1, inputs);
    if (ret < 0) {
        LOG_ERROR("rknn_inputs_set failed");
        return results;
    }

    // 3. 推理
    ret = rknn_run(m_ctx, nullptr);
    if (ret < 0) {
        LOG_ERROR("rknn_run failed");
        return results;
    }

    // 4. 获取输出数量
    rknn_input_output_num io_num;
    memset(&io_num, 0, sizeof(io_num));
    rknn_query(m_ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));

    // 5. 分配输出
    std::vector<rknn_output> outputs(io_num.n_output);
    for (uint32_t i = 0; i < io_num.n_output; i++) {
        outputs[i].want_float = 1;
        outputs[i].is_prealloc = 0;
    }

    ret = rknn_outputs_get(m_ctx, io_num.n_output, outputs.data(), nullptr);
    if (ret < 0) {
        LOG_ERROR("rknn_outputs_get failed");
        return results;
    }

    // 6. 收集输出指针
    std::vector<float*> outputPtrs;
    for (uint32_t i = 0; i < io_num.n_output; i++) {
        outputPtrs.push_back((float*)outputs[i].buf);
    }

    // 7. 后处理
    results = postprocess(outputPtrs.data(), io_num.n_output,
                          image.cols, image.rows, scaleX, scaleY);

    // 8. 释放输出
    rknn_outputs_release(m_ctx, io_num.n_output, outputs.data());

    return results;
}

std::vector<FaceInfo> RetinaFaceEngine::postprocess(float* outputs[], int outputCount,
                                                     int imgWidth, int imgHeight,
                                                     float scaleX, float scaleY)
{
    std::vector<FaceInfo> faces;

    LOG_DEBUG(QString("=== Postprocess Debug ==="));
    LOG_DEBUG(QString("outputCount=%1, imgSize=%2x%3").arg(outputCount).arg(imgWidth).arg(imgHeight));

    if (outputCount < 3) {
        LOG_WARNING(QString("Unexpected output count: %1, expected 3").arg(outputCount));
        return faces;
    }

    // 检查输出指针
    for (int i = 0; i < outputCount; i++) {
        if (outputs[i] == nullptr) {
            LOG_ERROR(QString("Output %1 is null").arg(i));
            return faces;
        }
    }

    int numAnchors = 4200;
    int validFaces = 0;
    float maxScore = 0.0f;

    // 首先找出最大置信度
    for (int i = 0; i < numAnchors; i++) {
        int scoreIdx = i * 2 + 1;
        float score = outputs[1][scoreIdx];
        if (score > maxScore) {
            maxScore = score;
        }
    }
    LOG_DEBUG(QString("Max confidence score: %1").arg(maxScore, 0, 'f', 6));

    // 动态调整阈值
    float scoreThreshold = 0.3f;
    if (maxScore > 0.8f) {
        scoreThreshold = 0.5f;
    } else if (maxScore > 0.5f) {
        scoreThreshold = 0.4f;
    }
    LOG_DEBUG(QString("Using score threshold: %1").arg(scoreThreshold));

    // 如果最大置信度太低，可能没有检测到人脸
    if (maxScore < 0.1f) {
        LOG_DEBUG("Max confidence too low, no faces detected");
        return faces;
    }

    for (int i = 0; i < numAnchors; i++) {
        // 置信度：outputs[1] 格式 [背景分数, 人脸分数]
        int scoreIdx = i * 2 + 1;
        float score = outputs[1][scoreIdx];

        if (score > scoreThreshold) {
            FaceInfo face;
            face.score = score;

            // 边界框坐标 (x1, y1, x2, y2)
            int boxIdx = i * 4;
            float x1 = outputs[0][boxIdx + 0];
            float y1 = outputs[0][boxIdx + 1];
            float x2 = outputs[0][boxIdx + 2];
            float y2 = outputs[0][boxIdx + 3];

            // 将坐标限制在 [0, 1] 范围
            x1 = std::max(0.0f, std::min(1.0f, x1));
            y1 = std::max(0.0f, std::min(1.0f, y1));
            x2 = std::max(0.0f, std::min(1.0f, x2));
            y2 = std::max(0.0f, std::min(1.0f, y2));

            // 转换为像素坐标
            int px1 = (int)(x1 * imgWidth);
            int py1 = (int)(y1 * imgHeight);
            int px2 = (int)(x2 * imgWidth);
            int py2 = (int)(y2 * imgHeight);

            // 确保坐标正确顺序
            if (px1 > px2) std::swap(px1, px2);
            if (py1 > py2) std::swap(py1, py2);

            int x = px1;
            int y = py1;
            int width = px2 - px1;
            int height = py2 - py1;

            // 过滤太小或太大的框
            if (width > 30 && height > 30 && width < imgWidth && height < imgHeight) {
                face.bbox = cv::Rect(x, y, width, height);

                // 解析关键点
                int lmIdx = i * 10;
                for (int k = 0; k < 5; k++) {
                    float lx = outputs[2][lmIdx + k * 2];
                    float ly = outputs[2][lmIdx + k * 2 + 1];
                    lx = std::max(0.0f, std::min(1.0f, lx)) * imgWidth;
                    ly = std::max(0.0f, std::min(1.0f, ly)) * imgHeight;
                    face.landmarks.push_back(cv::Point2f(lx, ly));
                }

                faces.push_back(face);
                validFaces++;

                // 只记录前5个检测到的脸
                if (validFaces <= 5) {
                    LOG_DEBUG(QString("Face %1: score=%2, bbox=[%3,%4,%5,%6]")
                              .arg(validFaces)
                              .arg(score, 0, 'f', 4)
                              .arg(x).arg(y).arg(width).arg(height));
                }
            }
        }
    }

    LOG_DEBUG(QString("Found %1 valid faces before NMS").arg(validFaces));

    // NMS 去重
    if (!faces.empty()) {
        nms(faces, 0.4f);
    }

    LOG_DEBUG(QString("Final %1 faces after NMS").arg(faces.size()));
    LOG_DEBUG(QString("=== Postprocess Debug End ==="));

    return faces;
}

float RetinaFaceEngine::calculateIou(const FaceInfo& a, const FaceInfo& b)
{
    int x1 = std::max(a.bbox.x, b.bbox.x);
    int y1 = std::max(a.bbox.y, b.bbox.y);
    int x2 = std::min(a.bbox.x + a.bbox.width, b.bbox.x + b.bbox.width);
    int y2 = std::min(a.bbox.y + a.bbox.height, b.bbox.y + b.bbox.height);

    int interArea = std::max(0, x2 - x1) * std::max(0, y2 - y1);
    int areaA = a.bbox.width * a.bbox.height;
    int areaB = b.bbox.width * b.bbox.height;

    if (areaA + areaB - interArea <= 0) return 0;
    return (float)interArea / (areaA + areaB - interArea);
}

void RetinaFaceEngine::nms(std::vector<FaceInfo>& faces, float threshold)
{
    if (faces.empty()) return;

    std::sort(faces.begin(), faces.end(),
              [](const FaceInfo& a, const FaceInfo& b) { return a.score > b.score; });

    std::vector<bool> keep(faces.size(), true);

    for (size_t i = 0; i < faces.size(); i++) {
        if (!keep[i]) continue;

        for (size_t j = i + 1; j < faces.size(); j++) {
            if (!keep[j]) continue;

            if (calculateIou(faces[i], faces[j]) > threshold) {
                keep[j] = false;
            }
        }
    }

    std::vector<FaceInfo> filtered;
    for (size_t i = 0; i < faces.size(); i++) {
        if (keep[i]) {
            filtered.push_back(faces[i]);
        }
    }
    faces = filtered;
}

void RetinaFaceEngine::release()
{
    if (m_ctx) {
        rknn_destroy(m_ctx);
        m_ctx = 0;
    }
    m_initialized = false;
}
