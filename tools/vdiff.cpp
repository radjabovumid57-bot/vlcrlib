/* vdiff — compare two PNGs for golden-image tests. */
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <algorithm>

#include <stb_image.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: vdiff <expected.png> <actual.png> [--threshold N]\n");
        return 2;
    }
    float threshold = 2.0f;
    float maxOver = -1.0f;
    for (int i = 3; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--threshold")
            threshold = (float)atof(argv[i + 1]);
        else if (std::string(argv[i]) == "--max-over")
            maxOver = (float)atof(argv[i + 1]);
    }

    int aw, ah, ac, bw, bh, bc;
    unsigned char *a = stbi_load(argv[1], &aw, &ah, &ac, 4);
    unsigned char *b = stbi_load(argv[2], &bw, &bh, &bc, 4);
    if (!a || !b) {
        fprintf(stderr, "vdiff: cannot load images\n");
        stbi_image_free(a); stbi_image_free(b);
        return 2;
    }
    if (aw != bw || ah != bh) {
        fprintf(stderr, "vdiff: size mismatch %dx%d vs %dx%d\n", aw, ah, bw, bh);
        stbi_image_free(a); stbi_image_free(b);
        return 1;
    }

    double sum = 0.0;
    int maxDiff = 0;
    size_t over = 0;
    size_t pixels = (size_t)aw * ah;
    for (size_t i = 0; i < pixels * 4; ++i) {
        int d = std::abs((int)a[i] - (int)b[i]);
        sum += d;
        if (d > maxDiff) maxDiff = d;
        if (d > (int)threshold) over++;
    }
    double mean = sum / (double)(pixels * 4);
    double percent = 100.0 * (double)over / (double)(pixels * 4);
    printf("vdiff: %s vs %s\n", argv[1], argv[2]);
    printf("  size=%dx%d mean=%.4f max=%d over_threshold=%.4f%% (threshold %.2f)\n",
           aw, ah, mean, maxDiff, percent, threshold);
    stbi_image_free(a);
    stbi_image_free(b);
    if (mean > threshold) {
        printf("  RESULT: FAIL (mean %.4f > %.2f)\n", mean, threshold);
        return 1;
    }
    if (maxOver >= 0.0f && percent > maxOver) {
        printf("  RESULT: FAIL (%.4f%% of samples over %.2f > %.4f%%)\n",
               percent, threshold, maxOver);
        return 1;
    }
    printf("  RESULT: OK\n");
    return 0;
}
