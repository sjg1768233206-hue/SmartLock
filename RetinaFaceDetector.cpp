#include "RetinaFaceDetector.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <cstring>

RetinaFaceDetector::RetinaFaceDetector()
    : m_ctx(0), m_initialized(false), m_model_width(320), m_model_height(320), m_output_num(0)
{
}

RetinaFaceDetector::~RetinaFaceDetector()
{
    release();
}

bool RetinaFaceDetector::init(const std::string& model_path)
{
    // 读取模型文件
    FILE* fp = fopen(model_path.c_str(), "rb");
    if (!fp) {
        printf("Failed to open model file: %s\n", model_path.c_str());
        return false;
    }

    fseek(fp, 0, SEEK_END);
    size_t model_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char* model_data = (unsigned char*)malloc(model_size);
    if (!model_data) {
        fclose(fp);
        return false;
    }

    size_t ret = fread(model_data, 1, model_size, fp);
    fclose(fp);

    if (ret != model_size) {
        free(model_data);
        return false;
    }

    // 初始化 RKNN 上下文
    int rknn_ret = rknn_init(&m_ctx, model_data, model_size, 0, NULL);
    free(model_data);

    if (rknn_ret < 0) {
        printf("rknn_init failed: %d\n", rknn_ret);
        return false;
    }

    // 查询输入输出数量
    rknn_input_output_num io_num;
    rknn_ret = rknn_query(m_ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    if (rknn_ret < 0) {
        printf("rknn_query failed: %d\n", rknn_ret);
        return false;
    }

    m_output_num = io_num.n_output;
    printf("Model has %d inputs, %d outputs\n", io_num.n_input, m_output_num);

    m_initialized = true;
    return true;
}

void RetinaFaceDetector::postprocess(float* boxes, float* scores, float* landmarks,
                                     int img_w, int img_h, float conf_threshold,
                                     std::vector<FaceBox>& results)
{
    const int ANCHOR_NUM = 4200;

    for (int i = 0; i < ANCHOR_NUM; i++) {
        float score = scores[i * 2 + 1];
        if (score > conf_threshold) {
            FaceBox face;
            face.score = score;

            float x1 = boxes[i * 4] * img_w;
            float y1 = boxes[i * 4 + 1] * img_h;
            float x2 = boxes[i * 4 + 2] * img_w;
            float y2 = boxes[i * 4 + 3] * img_h;

            // 确保坐标有效
            if (x1 < 0) x1 = 0;
            if (y1 < 0) y1 = 0;
            if (x2 > img_w) x2 = img_w;
            if (y2 > img_h) y2 = img_h;

            if (x2 > x1 && y2 > y1) {
                face.rect = cv::Rect((int)x1, (int)y1, (int)(x2 - x1), (int)(y2 - y1));

                // 5个关键点
                face.landmarks.clear();
                for (int j = 0; j < 5; j++) {
                    float lx = landmarks[i * 10 + j * 2] * img_w;
                    float ly = landmarks[i * 10 + j * 2 + 1] * img_h;
                    face.landmarks.push_back(cv::Point((int)lx, (int)ly));
                }

                results.push_back(face);
            }
        }
    }
}

bool RetinaFaceDetector::detect(const cv::Mat& frame, std::vector<FaceBox>& faces, float conf_threshold)
{
    if (!m_initialized) {
        printf("Detector not initialized\n");
        return false;
    }

    // 1. 缩放到模型输入尺寸
    cv::Mat resized;
    cv::resize(frame, resized, cv::Size(m_model_width, m_model_height));

    // 2. 设置输入
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].buf = resized.data;
    inputs[0].size = resized.total() * resized.channels();

    int ret = rknn_inputs_set(m_ctx, 1, inputs);
    if (ret != 0) {
        printf("rknn_inputs_set failed: %d\n", ret);
        return false;
    }

    // 3. 运行推理
    ret = rknn_run(m_ctx, NULL);
    if (ret != 0) {
        printf("rknn_run failed: %d\n", ret);
        return false;
    }

    // 4. 准备输出缓冲区
    rknn_output outputs[3];  // RetinaFace 固定有 3 个输出
    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < 3; i++) {
        outputs[i].want_float = 1;
        outputs[i].is_prealloc = 0;
    }

    // 5. 获取输出
    ret = rknn_outputs_get(m_ctx, 3, outputs, NULL);
    if (ret != 0) {
        printf("rknn_outputs_get failed: %d\n", ret);
        return false;
    }

    // 6. 后处理
    faces.clear();
    postprocess((float*)outputs[0].buf, (float*)outputs[1].buf, (float*)outputs[2].buf,
                frame.cols, frame.rows, conf_threshold, faces);

    printf("Detected %zu faces\n", faces.size());

    // 7. 释放输出
    rknn_outputs_release(m_ctx, 3, outputs);

    return !faces.empty();
}

void RetinaFaceDetector::release()
{
    if (m_ctx) {
        rknn_destroy(m_ctx);
        m_ctx = 0;
    }
    m_initialized = false;
}
