#pragma once

#include <stdint.h>
#include <stddef.h>

// Motion detection using block-based frame differencing
// Decodes JPEG at 1/8 scale for efficiency

class MotionDetector {
public:
    // Decoded frame dimensions (1/8 of typical VGA)
    static constexpr int DECODE_WIDTH = 80;
    static constexpr int DECODE_HEIGHT = 60;
    static constexpr int BLOCK_SIZE = 8;
    static constexpr int BLOCKS_X = DECODE_WIDTH / BLOCK_SIZE;   // 10
    static constexpr int BLOCKS_Y = DECODE_HEIGHT / BLOCK_SIZE;  // 7
    static constexpr int TOTAL_BLOCKS = BLOCKS_X * BLOCKS_Y;     // 70

    MotionDetector();

    // Configure detection parameters
    // sensitivity: 1-100 (higher = more sensitive, default 50)
    // cooldown_ms: minimum time between motion triggers (default 2000)
    // threshold_percent: percentage of blocks that must change (default 10)
    void configure(uint8_t sensitivity, uint16_t cooldown_ms, uint8_t threshold_percent);

    // Process a JPEG frame and check for motion
    // Returns true if motion was detected (respecting cooldown)
    bool processFrame(const uint8_t* jpeg_data, size_t jpeg_size);

    // Check if motion was detected on last processFrame call
    bool isMotionDetected() const { return motion_detected; }

    // Get timestamp of last motion detection
    uint32_t getLastMotionTime() const { return last_motion_time; }

    // Get percentage of blocks that changed on last frame
    uint8_t getChangedBlockPercent() const { return changed_percent; }

    // Check if detector has a valid previous frame for comparison
    bool isReady() const { return has_previous_frame; }

private:
    // Decode JPEG to grayscale at reduced scale
    bool decodeJpegToGray(const uint8_t* jpeg_data, size_t jpeg_size);

    // Compare blocks between current and previous frame
    int compareBlocks();

    // Calculate average brightness of a block
    uint8_t blockAverage(const uint8_t* frame, int block_x, int block_y);

    // Frame buffers (grayscale, 1 byte per pixel)
    uint8_t current_frame[DECODE_WIDTH * DECODE_HEIGHT];
    uint8_t previous_frame[DECODE_WIDTH * DECODE_HEIGHT];

    // Configuration
    uint8_t sensitivity;        // 1-100
    uint16_t cooldown_ms;       // Minimum ms between triggers
    uint8_t threshold_percent;  // % of blocks that must change

    // State
    bool has_previous_frame;
    bool motion_detected;
    uint32_t last_motion_time;
    uint8_t changed_percent;

    // JPEG decoder workspace
    uint8_t decode_work[3100];  // TJpgDec work buffer
};

// Global instance
extern MotionDetector motionDetector;
