#include "motion_detect.h"
#include <Arduino.h>
#include <string.h>

// Use ROM-based TJpgDec decoder
#if defined(ESP32)
#include "esp32/rom/tjpgd.h"
#else
#include "rom/tjpgd.h"
#endif

// Global instance
MotionDetector motionDetector;

// Context for JPEG decoder callbacks
struct JpegDecodeContext {
    const uint8_t* input_buf;
    size_t input_size;
    size_t input_pos;
    uint8_t* output_buf;
    int output_width;
    int output_height;
};

// TJpgDec input callback - read JPEG data
static unsigned int jpeg_input_cb(JDEC* jd, uint8_t* buff, unsigned int nbyte) {
    JpegDecodeContext* ctx = (JpegDecodeContext*)jd->device;

    size_t remaining = ctx->input_size - ctx->input_pos;
    if (nbyte > remaining) nbyte = remaining;

    if (buff) {
        memcpy(buff, ctx->input_buf + ctx->input_pos, nbyte);
    }
    ctx->input_pos += nbyte;

    return nbyte;
}

// TJpgDec output callback - write decoded pixels (RGB888 -> grayscale)
static unsigned int jpeg_output_cb(JDEC* jd, void* bitmap, JRECT* rect) {
    JpegDecodeContext* ctx = (JpegDecodeContext*)jd->device;
    uint8_t* rgb = (uint8_t*)bitmap;

    // Clamp to output buffer bounds
    int x_start = rect->left;
    int x_end = rect->right;
    int y_start = rect->top;
    int y_end = rect->bottom;

    if (x_end >= ctx->output_width) x_end = ctx->output_width - 1;
    if (y_end >= ctx->output_height) y_end = ctx->output_height - 1;

    // Convert RGB888 to grayscale and store
    int rgb_width = rect->right - rect->left + 1;
    for (int y = y_start; y <= y_end; y++) {
        for (int x = x_start; x <= x_end; x++) {
            int rgb_idx = ((y - rect->top) * rgb_width + (x - rect->left)) * 3;
            int out_idx = y * ctx->output_width + x;

            if (out_idx < ctx->output_width * ctx->output_height) {
                // Luminance: Y = 0.299*R + 0.587*G + 0.114*B
                // Using integer math: Y = (77*R + 150*G + 29*B) >> 8
                uint8_t r = rgb[rgb_idx];
                uint8_t g = rgb[rgb_idx + 1];
                uint8_t b = rgb[rgb_idx + 2];
                ctx->output_buf[out_idx] = (77 * r + 150 * g + 29 * b) >> 8;
            }
        }
    }

    return 1;  // Continue decoding
}

MotionDetector::MotionDetector() {
    sensitivity = 50;
    cooldown_ms = 2000;
    threshold_percent = 10;
    has_previous_frame = false;
    motion_detected = false;
    last_motion_time = 0;
    changed_percent = 0;
    memset(current_frame, 0, sizeof(current_frame));
    memset(previous_frame, 0, sizeof(previous_frame));
}

void MotionDetector::configure(uint8_t sens, uint16_t cooldown, uint8_t threshold) {
    sensitivity = constrain(sens, 1, 100);
    cooldown_ms = cooldown;
    threshold_percent = constrain(threshold, 1, 100);
}

bool MotionDetector::decodeJpegToGray(const uint8_t* jpeg_data, size_t jpeg_size) {
    JDEC jd;
    JpegDecodeContext ctx;

    ctx.input_buf = jpeg_data;
    ctx.input_size = jpeg_size;
    ctx.input_pos = 0;
    ctx.output_buf = current_frame;
    ctx.output_width = DECODE_WIDTH;
    ctx.output_height = DECODE_HEIGHT;

    // Prepare decoder
    JRESULT res = jd_prepare(&jd, jpeg_input_cb, decode_work, sizeof(decode_work), &ctx);
    if (res != JDR_OK) {
        log_e("JPEG prepare failed: %d", res);
        return false;
    }

    // Calculate scale factor
    // Scale: 0=1/1, 1=1/2, 2=1/4, 3=1/8
    // We want output around 80x60, so use 1/8 for VGA (640x480 -> 80x60)
    int scale = 0;
    if (jd.width >= 640 || jd.height >= 480) scale = 3;      // 1/8
    else if (jd.width >= 320 || jd.height >= 240) scale = 2; // 1/4
    else if (jd.width >= 160 || jd.height >= 120) scale = 1; // 1/2

    // Decompress
    res = jd_decomp(&jd, jpeg_output_cb, scale);
    if (res != JDR_OK) {
        log_e("JPEG decompress failed: %d", res);
        return false;
    }

    return true;
}

uint8_t MotionDetector::blockAverage(const uint8_t* frame, int block_x, int block_y) {
    int sum = 0;
    int start_x = block_x * BLOCK_SIZE;
    int start_y = block_y * BLOCK_SIZE;

    for (int y = 0; y < BLOCK_SIZE && (start_y + y) < DECODE_HEIGHT; y++) {
        for (int x = 0; x < BLOCK_SIZE && (start_x + x) < DECODE_WIDTH; x++) {
            sum += frame[(start_y + y) * DECODE_WIDTH + (start_x + x)];
        }
    }

    return sum / (BLOCK_SIZE * BLOCK_SIZE);
}

int MotionDetector::compareBlocks() {
    // Block difference threshold based on sensitivity
    // Higher sensitivity = lower threshold = more sensitive to small changes
    int block_threshold = 255 * (100 - sensitivity) / 100;
    if (block_threshold < 5) block_threshold = 5;  // Minimum threshold

    int changed = 0;

    for (int by = 0; by < BLOCKS_Y; by++) {
        for (int bx = 0; bx < BLOCKS_X; bx++) {
            uint8_t avg_current = blockAverage(current_frame, bx, by);
            uint8_t avg_previous = blockAverage(previous_frame, bx, by);

            int diff = abs((int)avg_current - (int)avg_previous);
            if (diff > block_threshold) {
                changed++;
            }
        }
    }

    return changed;
}

bool MotionDetector::processFrame(const uint8_t* jpeg_data, size_t jpeg_size) {
    motion_detected = false;

    // Decode current frame
    if (!decodeJpegToGray(jpeg_data, jpeg_size)) {
        return false;
    }

    // Need at least one previous frame to compare
    if (!has_previous_frame) {
        memcpy(previous_frame, current_frame, sizeof(current_frame));
        has_previous_frame = true;
        return false;
    }

    // Compare blocks
    int changed_blocks = compareBlocks();
    changed_percent = (changed_blocks * 100) / TOTAL_BLOCKS;

    // Check if motion threshold exceeded and cooldown elapsed
    uint32_t now = millis();
    bool cooldown_ok = (now - last_motion_time) >= cooldown_ms;

    if (changed_percent >= threshold_percent && cooldown_ok) {
        motion_detected = true;
        last_motion_time = now;
        log_i("Motion detected! %d%% blocks changed", changed_percent);
    }

    // Save current frame as previous for next comparison
    memcpy(previous_frame, current_frame, sizeof(current_frame));

    return motion_detected;
}
