#pragma once
#include <vector>

// ============================
// RetinaFace Mobile320 Anchors
// RKNN Zoo compatible version
// ============================
extern const int num_anchors;
extern const float anchors[3200][4];
static const std::vector<std::vector<float>> anchors = []()
{
    std::vector<std::vector<float>> a;
    a.reserve(4200 * 2);

    const int steps[3] = {8, 16, 32};

    const float min_sizes[3][2] = {
        {16.0f, 32.0f},
        {64.0f, 128.0f},
        {256.0f, 512.0f}
    };

    for (int k = 0; k < 3; k++)
    {
        int step = steps[k];
        int fm = 320 / step;

        for (int i = 0; i < fm; i++)
        {
            for (int j = 0; j < fm; j++)
            {
                float cx = (j + 0.5f) * step;
                float cy = (i + 0.5f) * step;

                for (int m = 0; m < 2; m++)
                {
                    float s = min_sizes[k][m];

                    // anchor = [cx, cy, w, h]
                    a.push_back({
                        cx,
                        cy,
                        s,
                        s
                    });
                }
            }
        }
    }

    return a;
}();
