#ifndef QOI_FORMAT_CODEC_QOI_H_
#define QOI_FORMAT_CODEC_QOI_H_

#include "utils.h"
#include <cmath>

constexpr uint8_t QOI_OP_INDEX_TAG = 0x00;
constexpr uint8_t QOI_OP_DIFF_TAG  = 0x40;
constexpr uint8_t QOI_OP_LUMA_TAG  = 0x80;
constexpr uint8_t QOI_OP_RUN_TAG   = 0xc0; 
constexpr uint8_t QOI_OP_RGB_TAG   = 0xfe;
constexpr uint8_t QOI_OP_RGBA_TAG  = 0xff;
constexpr uint8_t QOI_PADDING[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u};
constexpr uint8_t QOI_MASK_2 = 0xc0;

void update_history(uint8_t history[64][4], uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    int hash = QoiColorHash(r, g, b, a);
    history[hash][0] = r;
    history[hash][1] = g;
    history[hash][2] = b;
    history[hash][3] = a;
}

/**
 * @brief encode the raw pixel data of an image to qoi format.
 */
bool QoiEncode(uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace) {
    QoiWriteChar('q');
    QoiWriteChar('o');
    QoiWriteChar('i');
    QoiWriteChar('f');
    QoiWriteU32(width);
    QoiWriteU32(height);
    QoiWriteU8(channels);
    QoiWriteU8(colorspace);

    uint8_t history[64][4] = {0};
    uint8_t pre_r = 0, pre_g = 0, pre_b = 0, pre_a = 255;
    int run = 0;

    for (uint32_t i = 0; i < width * height; ++i) {
        uint8_t r = QoiReadU8();
        uint8_t g = QoiReadU8();
        uint8_t b = QoiReadU8();
        uint8_t a = 255;
        if (channels == 4) a = QoiReadU8();

        if (i == 0) {
            if (channels == 3) {
                QoiWriteU8(QOI_OP_RGB_TAG);
                QoiWriteU8(r); QoiWriteU8(g); QoiWriteU8(b);
            } else {
                QoiWriteU8(QOI_OP_RGBA_TAG);
                QoiWriteU8(r); QoiWriteU8(g); QoiWriteU8(b); QoiWriteU8(a);
            }
            update_history(history, r, g, b, a);
            pre_r = r; pre_g = g; pre_b = b; pre_a = a;
            continue;
        }

        if (r == pre_r && g == pre_g && b == pre_b && a == pre_a && run < 63) {
            run++;
        } else {
            if (run > 0) {
                QoiWriteU8(QOI_OP_RUN_TAG | (run - 1));
            }
            run = 0;

            bool encoded = false;

            // 1. INDEX
            for (int j = 0; j < 64; ++j) {
                if (history[j][0] == r && history[j][1] == g && history[j][2] == b && history[j][3] == a) {
                    QoiWriteU8(QOI_OP_INDEX_TAG | j);
                    encoded = true;
                    break;
                }
            }

            if (!encoded) {
                // 2. LUMA (Prioritize over DIFF for better compression)
                int luma_idx = -1;
                int prev_luma_idx = QoiColorHash(pre_r, pre_g, pre_b, pre_a);
                
                auto check_luma = [&](int idx) {
                    uint8_t ref_r = history[idx][0];
                    uint8_t ref_g = history[idx][1];
                    uint8_t ref_b = history[idx][2];
                    uint8_t ref_a = history[idx][3];
                    int dr = r - ref_r;
                    if (std::abs(dr) > 64) return false;
                    uint8_t luma = (ref_r * 5 + ref_g * 4 + ref_b * 2) / 11;
                    if (g - ref_g != (dr * luma) / 255) return false;
                    if (b - ref_b != (dr * (255 - luma)) / 255) return false;
                    if (channels == 4 && std::abs(a - ref_a) > 64) return false;
                    return true;
                };

                if (check_luma(prev_luma_idx)) {
                    luma_idx = prev_luma_idx;
                } else {
                    for (int j = 0; j < 64; ++j) {
                        if (check_luma(j)) {
                            luma_idx = j;
                            break;
                        }
                    }
                }

                if (luma_idx != -1) {
                    QoiWriteU8(QOI_OP_LUMA_TAG | luma_idx);
                    QoiWriteU8(static_cast<uint8_t>(r - history[luma_idx][0]));
                    if (channels == 4) {
                        QoiWriteU8(static_cast<uint8_t>(a - history[luma_idx][3]));
                    }
                    encoded = true;
                }
            }

            if (!encoded) {
                // 3. DIFF
                int diff_idx = -1;
                int prev_idx = QoiColorHash(pre_r, pre_g, pre_b, pre_a);
                if (std::abs(r - history[prev_idx][0]) <= 64 &&
                    std::abs(g - history[prev_idx][1]) <= 64 &&
                    std::abs(b - history[prev_idx][2]) <= 64 &&
                    (channels == 3 || std::abs(a - history[prev_idx][3]) <= 64)) {
                    diff_idx = prev_idx;
                } else {
                    for (int j = 0; j < 64; ++j) {
                        if (std::abs(r - history[j][0]) <= 64 &&
                            std::abs(g - history[j][1]) <= 64 &&
                            std::abs(b - history[j][2]) <= 64 &&
                            (channels == 3 || std::abs(a - history[j][3]) <= 64)) {
                            diff_idx = j;
                            break;
                        }
                    }
                }

                if (diff_idx != -1) {
                    QoiWriteU8(QOI_OP_DIFF_TAG | diff_idx);
                    QoiWriteU8(static_cast<uint8_t>(r - history[diff_idx][0]));
                    QoiWriteU8(static_cast<uint8_t>(g - history[diff_idx][1]));
                    QoiWriteU8(static_cast<uint8_t>(b - history[diff_idx][2]));
                    if (channels == 4) {
                        QoiWriteU8(static_cast<uint8_t>(a - history[diff_idx][3]));
                    }
                    encoded = true;
                }
            }

            if (!encoded) {
                // 4. RGB/RGBA
                if (channels == 3) {
                    QoiWriteU8(QOI_OP_RGB_TAG);
                    QoiWriteU8(r); QoiWriteU8(g); QoiWriteU8(b);
                } else {
                    QoiWriteU8(QOI_OP_RGBA_TAG);
                    QoiWriteU8(r); QoiWriteU8(g); QoiWriteU8(b); QoiWriteU8(a);
                }
            }

            update_history(history, r, g, b, a);
            pre_r = r; pre_g = g; pre_b = b; pre_a = a;
        }
    }
    if (run > 0) {
        QoiWriteU8(QOI_OP_RUN_TAG | (run - 1));
    }
    for (int i = 0; i < 8; ++i) QoiWriteU8(QOI_PADDING[i]);
    return true;
}

bool QoiDecode(uint32_t &width, uint32_t &height, uint8_t &channels, uint8_t &colorspace) {
    uint8_t m1 = QoiReadU8();
    uint8_t m2 = QoiReadU8();
    uint8_t m3 = QoiReadU8();
    uint8_t m4 = QoiReadU8();
    if (m1 != 'q' || m2 != 'o' || m3 != 'i' || m4 != 'f') return false;

    width = QoiReadU32();
    height = QoiReadU32();
    channels = QoiReadU8();
    colorspace = QoiReadU8();

    uint8_t history[64][4] = {0};
    uint8_t pre_r = 0, pre_g = 0, pre_b = 0, pre_a = 255;

    uint32_t pixels_decoded = 0;
    while (pixels_decoded < width * height) {
        uint8_t tag = QoiReadU8();
        if ((tag & 0xc0) == 0xc0 && tag < 0xfe) {
            int run_len = (tag & 0x3f) + 1;
            for (int j = 0; j < run_len; ++j) {
                QoiWriteU8(pre_r); QoiWriteU8(pre_g); QoiWriteU8(pre_b);
                if (channels == 4) QoiWriteU8(pre_a);
                pixels_decoded++;
            }
        } else if ((tag & 0xc0) == 0x00) {
            int idx = tag & 0x3f;
            uint8_t r = history[idx][0];
            uint8_t g = history[idx][1];
            uint8_t b = history[idx][2];
            uint8_t a = history[idx][3];
            QoiWriteU8(r); QoiWriteU8(g); QoiWriteU8(b);
            if (channels == 4) QoiWriteU8(a);
            update_history(history, r, g, b, a);
            pre_r = r; pre_g = g; pre_b = b; pre_a = a;
            pixels_decoded++;
        } else if ((tag & 0xc0) == 0x40) {
            int idx = tag & 0x3f;
            uint8_t r = history[idx][0] + QoiReadU8();
            uint8_t g = history[idx][1] + QoiReadU8();
            uint8_t b = history[idx][2] + QoiReadU8();
            uint8_t a = 255;
            if (channels == 4) a = history[idx][3] + QoiReadU8();
            QoiWriteU8(r); QoiWriteU8(g); QoiWriteU8(b);
            if (channels == 4) QoiWriteU8(a);
            update_history(history, r, g, b, a);
            pre_r = r; pre_g = g; pre_b = b; pre_a = a;
            pixels_decoded++;
        } else if ((tag & 0xc0) == 0x80) {
            int idx = tag & 0x3f;
            uint8_t dr = QoiReadU8();
            uint8_t ref_r = history[idx][0];
            uint8_t ref_g = history[idx][1];
            uint8_t ref_b = history[idx][2];
            uint8_t ref_a = history[idx][3];
            uint8_t luma = (ref_r * 5 + ref_g * 4 + ref_b * 2) / 11;
            uint8_t r = ref_r + dr;
            uint8_t g = ref_g + (dr * luma) / 255;
            uint8_t b = ref_b + (dr * (255 - luma)) / 255;
            uint8_t a = 255;
            if (channels == 4) a = ref_a + QoiReadU8();
            QoiWriteU8(r); QoiWriteU8(g); QoiWriteU8(b);
            if (channels == 4) QoiWriteU8(a);
            update_history(history, r, g, b, a);
            pre_r = r; pre_g = g; pre_b = b; pre_a = a;
            pixels_decoded++;
        } else if (tag == 0xfe) {
            uint8_t r = QoiReadU8();
            uint8_t g = QoiReadU8();
            uint8_t b = QoiReadU8();
            uint8_t a = 255;
            QoiWriteU8(r); QoiWriteU8(g); QoiWriteU8(b);
            if (channels == 4) QoiWriteU8(a);
            update_history(history, r, g, b, a);
            pre_r = r; pre_g = g; pre_b = b; pre_a = a;
            pixels_decoded++;
        } else if (tag == 0xff) {
            uint8_t r = QoiReadU8();
            uint8_t g = QoiReadU8();
            uint8_t b = QoiReadU8();
            uint8_t a = QoiReadU8();
            QoiWriteU8(r); QoiWriteU8(g); QoiWriteU8(b);
            QoiWriteU8(a);
            update_history(history, r, g, b, a);
            pre_r = r; pre_g = g; pre_b = b; pre_a = a;
            pixels_decoded++;
        } else {
            return false;
        }
    }

    for (int i = 0; i < 8; ++i) {
        if (QoiReadU8() != QOI_PADDING[i]) return false;
    }
    return true;
}

#endif // QOI_FORMAT_CODEC_QOI_H_
