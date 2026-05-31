#include "FaceRecognizer.h"
#include <cmath>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>

FaceRecognizer::FaceRecognizer()
    : m_ctx(0), m_initialized(false), m_feature_dim(128),
      m_input_width(112), m_input_height(112)
{
}

FaceRecognizer::~FaceRecognizer()
{
    release();
}

bool FaceRecognizer::init(const std::string& model_path)
{
    FILE* fp = fopen(model_path.c_str(), "rb");
    if (!fp) {
        printf("Failed to open model file: %s\n", model_path.c_str());
        return false;
    }

    fseek(fp, 0, SEEK_END);
    size_t model_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char* model_data = (unsigned char*)malloc(model_size);
    fread(model_data, 1, model_size, fp);
    fclose(fp);

    int ret = rknn_init(&m_ctx, model_data, model_size, 0, NULL);
    free(model_data);

    if (ret != 0) {
        printf("rknn_init failed: %d\n", ret);
        return false;
    }

    m_initialized = true;
    return true;
}

std::vector<float> FaceRecognizer::extractFeature(const cv::Mat& face)
{
    std::vector<float> features;
    if (!m_initialized || face.empty()) return features;

    // 预处理
    cv::Mat resized;
    cv::resize(face, resized, cv::Size(m_input_width, m_input_height));

    cv::Mat rgb;
    if (resized.channels() == 1) {
        cv::cvtColor(resized, rgb, cv::COLOR_GRAY2RGB);
    } else {
        rgb = resized;
    }

    // 设置输入
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].buf = rgb.data;
    inputs[0].size = rgb.total() * rgb.channels();

    int ret = rknn_inputs_set(m_ctx, 1, inputs);
    if (ret != 0) {
        printf("rknn_inputs_set failed: %d\n", ret);
        return features;
    }

    // 运行推理
    ret = rknn_run(m_ctx, NULL);
    if (ret != 0) {
        printf("rknn_run failed: %d\n", ret);
        return features;
    }

    // 获取输出数量
    rknn_input_output_num io_num;
    ret = rknn_query(m_ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    printf("Model has %d inputs, %d outputs\n", io_num.n_input, io_num.n_output);

    // 获取输出 - 使用正确的数量
    rknn_output outputs[io_num.n_output];
    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < io_num.n_output; i++) {
        outputs[i].want_float = 1;
    }

    ret = rknn_outputs_get(m_ctx, io_num.n_output, outputs, NULL);
    if (ret != 0) {
        printf("rknn_outputs_get failed: %d\n", ret);
        return features;
    }

    // 提取特征（只有1个输出）
    float* feat = (float*)outputs[0].buf;
    features.assign(feat, feat + m_feature_dim);

    // L2 归一化
    float norm = 0.0f;
    for (float v : features) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 0) {
        for (float& v : features) v /= norm;
    }

    // 释放输出
    rknn_outputs_release(m_ctx, io_num.n_output, outputs);

    return features;
}

void FaceRecognizer::release()
{
    if (m_ctx) {
        rknn_destroy(m_ctx);
        m_ctx = 0;
    }
    m_initialized = false;
}
