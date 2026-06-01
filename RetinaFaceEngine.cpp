#include "RetinaFaceEngine.h"
#include "logger.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include "rknn_box_priors.h"

RetinaFaceEngine::RetinaFaceEngine()
{
    m_ctx = 0;
    m_inputW = 320;
    m_inputH = 320;
    m_init = false;
}

RetinaFaceEngine::~RetinaFaceEngine()
{
    release();
}

// ================= INIT =================
bool RetinaFaceEngine::init(const std::string& modelPath, int inputW, int inputH)
{
    m_inputW = inputW;
    m_inputH = inputH;

    FILE* fp = fopen(modelPath.c_str(), "rb");
    if (!fp) return false;

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char* model = new unsigned char[size];
    fread(model, 1, size, fp);
    fclose(fp);

    int ret = rknn_init(&m_ctx, model, size, 0, nullptr);
    delete[] model;

    if (ret < 0) return false;

    LOG_INFO("RetinaFace model loaded successfully");
    m_init = true;
    return true;
}

// ================= PREPROCESS =================
PreprocessInfo RetinaFaceEngine::preprocess(const cv::Mat& img)
{
    PreprocessInfo info;
    int w = m_inputW;
    int h = m_inputH;

    cv::Mat rgb;
    cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB);

    info.scale = std::min((float)w / img.cols, (float)h / img.rows);
    int nw = img.cols * info.scale;
    int nh = img.rows * info.scale;

    cv::Mat resized;
    cv::resize(rgb, resized, cv::Size(nw, nh));

    info.canvas = cv::Mat(h, w, CV_8UC3, cv::Scalar(114, 114, 114));
    info.offset_x = (w - nw) / 2;
    info.offset_y = (h - nh) / 2;
    resized.copyTo(info.canvas(cv::Rect(info.offset_x, info.offset_y, nw, nh)));

    return info;
}

// ================= DETECT =================
std::vector<FaceInfo> RetinaFaceEngine::detect(const cv::Mat& img)
{
    std::vector<FaceInfo> out;
    if (!m_init || img.empty()) return out;

    PreprocessInfo preInfo = preprocess(img);

    rknn_input in[1]{};
    in[0].index = 0;
    in[0].type = RKNN_TENSOR_UINT8;
    in[0].fmt = RKNN_TENSOR_NHWC;
    in[0].size = m_inputW * m_inputH * 3;
    in[0].buf = preInfo.canvas.data;

    rknn_inputs_set(m_ctx, 1, in);
    rknn_run(m_ctx, nullptr);

    rknn_input_output_num io{};
    rknn_query(m_ctx, RKNN_QUERY_IN_OUT_NUM, &io, sizeof(io));

    std::vector<rknn_output> outs(io.n_output);
    for (auto &o : outs) o.want_float = 1;

    if (rknn_outputs_get(m_ctx, io.n_output, outs.data(), nullptr) < 0)
        return out;

    std::vector<float*> ptr;
    for (auto &o : outs)
        ptr.push_back((float*)o.buf);

    out = postprocess(ptr.data(), io.n_output, img.cols, img.rows,
                      preInfo.scale, preInfo.offset_x, preInfo.offset_y);

    rknn_outputs_release(m_ctx, io.n_output, outs.data());
    return out;
}

// ================= POSTPROCESS =================
std::vector<FaceInfo> RetinaFaceEngine::postprocess(
    float* outputs[],
    int outputCount,
    int imgW,
    int imgH,
    float scale,
    int offset_x,
    int offset_y)
{
    std::vector<FaceInfo> faces;
    if (outputCount < 3) return faces;

    float* loc = outputs[0];
    float* conf = outputs[1];
    float* landm = outputs[2];

    const float (*priors)[4] = nullptr;
    int num = 0;

    if (m_inputW == 320)
    {
        priors = BOX_PRIORS_320;
        num = 4200;
    }
    else if (m_inputW == 640)
    {
        priors = BOX_PRIORS_640;
        num = 16800;
    }
    else
    {
        return faces;
    }

    const float variances[2] = {0.1f, 0.2f};
    const float CONFIDENCE_THRESHOLD = 0.2f;  // 可调整

    for (int i = 0; i < num; i++)
    {
        float score = conf[i * 2 + 1];
        if (score < CONFIDENCE_THRESHOLD) continue;

        // 解码
        float cx = priors[i][0];
        float cy = priors[i][1];
        float w = priors[i][2];
        float h = priors[i][3];

        float dx = loc[i * 4 + 0];
        float dy = loc[i * 4 + 1];
        float dw = loc[i * 4 + 2];
        float dh = loc[i * 4 + 3];

        cx += dx * variances[0] * w;
        cy += dy * variances[0] * h;
        w *= exp(dw * variances[1]);
        h *= exp(dh * variances[1]);

        float x1 = cx - w * 0.5f;
        float y1 = cy - h * 0.5f;
        float x2 = cx + w * 0.5f;
        float y2 = cy + h * 0.5f;

        if (x2 <= x1 || y2 <= y1) continue;

        // 坐标映射到原图
        float real_x1 = (x1 * m_inputW - offset_x) / scale;
        float real_y1 = (y1 * m_inputH - offset_y) / scale;
        float real_x2 = (x2 * m_inputW - offset_x) / scale;
        float real_y2 = (y2 * m_inputH - offset_y) / scale;

        // 边界裁剪
        real_x1 = std::max(0.0f, std::min((float)imgW, real_x1));
        real_y1 = std::max(0.0f, std::min((float)imgH, real_y1));
        real_x2 = std::max(0.0f, std::min((float)imgW, real_x2));
        real_y2 = std::max(0.0f, std::min((float)imgH, real_y2));

        if (real_x2 <= real_x1 || real_y2 <= real_y1) continue;

        FaceInfo f;
        f.score = score;
        f.bbox = cv::Rect(
            (int)real_x1,
            (int)real_y1,
            (int)(real_x2 - real_x1),
            (int)(real_y2 - real_y1)
        );

        // 关键点映射
        for (int k = 0; k < 5; k++)
        {
            float lx = landm[i * 10 + k * 2];
            float ly = landm[i * 10 + k * 2 + 1];

            lx = cx + lx * variances[0] * w;
            ly = cy + ly * variances[0] * h;

            float real_lx = (lx * m_inputW - offset_x) / scale;
            float real_ly = (ly * m_inputH - offset_y) / scale;

            real_lx = std::max(0.0f, std::min((float)imgW, real_lx));
            real_ly = std::max(0.0f, std::min((float)imgH, real_ly));

            f.landmarks.emplace_back(real_lx, real_ly);
        }

        faces.push_back(f);
    }

    if (!faces.empty())
        nms(faces, 0.4f);

    return faces;
}

// ================= IOU =================
float RetinaFaceEngine::iou(const FaceInfo& a, const FaceInfo& b)
{
    int x1 = std::max(a.bbox.x, b.bbox.x);
    int y1 = std::max(a.bbox.y, b.bbox.y);
    int x2 = std::min(a.bbox.x + a.bbox.width, b.bbox.x + b.bbox.width);
    int y2 = std::min(a.bbox.y + a.bbox.height, b.bbox.y + b.bbox.height);

    int inter = std::max(0, x2 - x1) * std::max(0, y2 - y1);
    int ua = a.bbox.area() + b.bbox.area() - inter;
    return ua <= 0 ? 0 : (float)inter / ua;
}

// ================= NMS =================
void RetinaFaceEngine::nms(std::vector<FaceInfo>& faces, float thr)
{
    std::sort(faces.begin(), faces.end(),
              [](auto& a, auto& b) { return a.score > b.score; });

    std::vector<int> keep(faces.size(), 1);

    for (size_t i = 0; i < faces.size(); i++)
    {
        if (!keep[i]) continue;

        for (size_t j = i + 1; j < faces.size(); j++)
        {
            if (!keep[j]) continue;

            if (iou(faces[i], faces[j]) > thr)
                keep[j] = 0;
        }
    }

    std::vector<FaceInfo> tmp;
    for (size_t i = 0; i < faces.size(); i++)
        if (keep[i]) tmp.push_back(faces[i]);

    faces = tmp;
}

// ================= RELEASE =================
void RetinaFaceEngine::release()
{
    if (m_ctx) {
        rknn_destroy(m_ctx);
        m_ctx = 0;
    }
    m_init = false;
}
