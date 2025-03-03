/*
 * object_recognition.c
 *
 * Object Recognition and Tracking Module with Motion Detection and Clearing Overlay
 *
 * Design Principles:
 *   - Written in plain C (-std=c11) using only standard cross-platform libraries.
 *   - No header files are used; function prototypes are declared as extern in the main file.
 *   - Implements a basic motion detection algorithm by comparing the current frame with the previous frame.
 *   - If the luminance difference (Y channel) of a pixel pair exceeds a threshold, that block is marked with red.
 *   - When no motion is detected, the original pixels are restored, clearing the red overlay.
 *   - All processing is done on the raw YUYV frame.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Motion detection threshold (adjust as needed).
#define MOTION_THRESHOLD 10

// Define the red color to overlay (approximate red in YUYV format).
const unsigned char redY = 76;
const unsigned char redU = 84;
const unsigned char redV = 255;

/*
 * process_frame compares the current frame with the previous frame.
 * It uses two static buffers:
 *   - prev_frame: holds the original (unmodified) data from the previous frame.
 *   - orig_frame: holds a copy of the current frame before overlaying.
 *
 * For each pixel pair (4 bytes in YUYV), if the absolute difference in the Y channel
 * exceeds MOTION_THRESHOLD, the pixel pair is replaced with red. Otherwise, the original pixel
 * value is kept.
 */
void process_frame(unsigned char *frame, size_t frame_size, int frame_width, int frame_height) {
    // Static buffers to store the previous frame and a copy of the current original frame.
    static unsigned char *prev_frame = NULL;
    static unsigned char *orig_frame = NULL;
    static size_t buf_size = 0;

    // On the first call, allocate buffers and initialize prev_frame with the current frame.
    if (prev_frame == NULL || frame_size != buf_size) {
        if (prev_frame) {
            free(prev_frame);
            free(orig_frame);
        }
        buf_size = frame_size;
        prev_frame = malloc(buf_size);
        orig_frame = malloc(buf_size);
        if (!prev_frame || !orig_frame) {
            fprintf(stderr, "Motion detection: Out of memory.\n");
            exit(EXIT_FAILURE);
        }
        // Initialize prev_frame with the first frame (no motion overlay yet).
        memcpy(prev_frame, frame, frame_size);
        // No overlay on first frame.
        return;
    }

    // Copy the current frame into orig_frame to preserve the unmodified data.
    memcpy(orig_frame, frame, frame_size);

    // Process the frame in blocks of 4 bytes (Y0, U, Y1, V).
    for (size_t i = 0; i < frame_size; i += 4) {
        // Compute absolute differences for the Y channel of each pixel in the pair.
        int diff0 = abs((int)orig_frame[i] - (int)prev_frame[i]);
        int diff1 = abs((int)orig_frame[i + 2] - (int)prev_frame[i + 2]);

        if (diff0 > MOTION_THRESHOLD || diff1 > MOTION_THRESHOLD) {
            // Mark this pixel pair as moving: set to red.
            frame[i]     = redY;
            frame[i + 1] = redU;
            frame[i + 2] = redY;
            frame[i + 3] = redV;
        } else {
            // No significant motion: restore original pixel values.
            frame[i]     = orig_frame[i];
            frame[i + 1] = orig_frame[i + 1];
            frame[i + 2] = orig_frame[i + 2];
            frame[i + 3] = orig_frame[i + 3];
        }
    }

    // Update prev_frame with the current original frame for the next comparison.
    memcpy(prev_frame, orig_frame, frame_size);

    // Debug output (optional).
    fprintf(stderr, "Motion tracking: Updated overlay based on motion detection.\n");
}
