/*
 * object_recognition.c
 *
 * This module detects and tracks objects in video frames from a 320×240 camera.
 * It updates a background model, detects moving regions, reduces noise, and
 * calculates the center position of detected motion. A filled square marker is
 * drawn at the calculated (and filtered) position.
 *
 * Design modification:
 *   - A new struct 'Position' is defined to hold x and y coordinates.
 *   - The process_frame() function now returns a Position containing the center
 *     of the detected motion.
 *   - Motion overlay uses a dark red-to-bright red gradient based on the movement magnitude.
 *   - The center-of-motion is now computed as the arithmetic average (geometrical center)
 *     of all motion detection points. This should yield a center even when only edges are detected.
 *
 * Implementation details:
 *   - Uses a running average to update the background model.
 *   - Detects motion by comparing current frame brightness against the background.
 *   - Applies a 3×3 erosion followed by a 3×3 dilation to reduce noise.
 *   - Calculates the center as the average of all grid cells marked as motion.
 *   - Applies a low pass filter to stabilize the marker.
 *   - Written in plain C (-std=c11) with only standard libraries.
 *   - All code is contained in one file.
 *
 * Optimizations for 320×240 resolution:
 *   1. Resolution-specific constants.
 *   2. Static allocation of buffers to avoid repeated memory allocation.
 *   3. Tuned parameters (e.g., motion threshold, marker size) for this resolution.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Camera resolution settings for a 320×240 camera */
#define CAM_WIDTH    320
#define CAM_HEIGHT   240

// For YUYV format, only the Y channel is used from paired pixels.
// GRID_WIDTH is half of CAM_WIDTH, and GRID_HEIGHT equals CAM_HEIGHT.
#define GRID_WIDTH   (CAM_WIDTH / 2)
#define GRID_HEIGHT  (CAM_HEIGHT)
#define GRID_SIZE    (GRID_WIDTH * GRID_HEIGHT)

/* Parameters for motion detection and background update */
#define MOTION_THRESHOLD 1            // Base threshold for detecting motion in the Y channel
#define BG_MOTION_DIFF_THRESHOLD 1.0f // Difference above which the background is updated faster
#define BG_ALPHA_NO_MOTION 0.001f      // Slow update rate when there is little change
#define BG_ALPHA_MOTION 0.010f         // Fast update rate when there is significant change

// Adaptive threshold: adjust the motion threshold based on local brightness.
#define ENABLE_ADAPTIVE_THRESHOLD 1    // Set to 0 to disable adaptive thresholding
#define ADAPTIVE_FACTOR 0.2f           // Fraction to adjust threshold based on background brightness

/* Drawing parameters tuned for 320×240 resolution */
#define MIN_MOVEMENT_PIXELS 25         // Minimum number of motion pixels required

// Low pass filter constant for stabilizing the marker position (0 < alpha <= 1)
#define CROSSHAIR_LPF_ALPHA 0.5f

// Marker size for the filled square (default is 3x3 pixels)
#define MARKER_SIZE 6

// Define the fixed red color for overlay (U and V remain constant for the red hue)
#define RED_U 1
#define RED_V 255

// New defines mapping movement magnitude to gradient colors.
// A diff of MOTION_THRESHOLD (1) is considered dark red, and diff equals BRIGHT_RED_MOVEMENT_LEVEL is bright red.
#define DARK_RED_MOVEMENT_LEVEL MOTION_THRESHOLD
#define BRIGHT_RED_MOVEMENT_LEVEL 30.0f

// Y channel values for gradient: dark red and bright red.
#define DARK_RED_Y_VALUE 1
#define BRIGHT_RED_Y_VALUE 100

typedef struct {
    unsigned char Y;
    unsigned char U;
    unsigned char V;
} Color;

/* Color used for drawing the marker */
Color marker_color = {41, 240, 110};

/* 
 * Static buffers for processing frames.
 * Allocated once to avoid repeated dynamic allocation.
 */
static unsigned char *orig_frame = NULL;   // Copy of the input frame
static float *backgroundY = NULL;          // Background model (Y channel)
static int *motion_mask = NULL;            // Binary mask for detected motion
static int *eroded_mask = NULL;            // Buffer for the erosion step
static int *dilated_mask = NULL;           // Buffer for the dilation step
static int last_center_x = CAM_WIDTH / 2;   // Last known x-coordinate of marker
static int last_center_y = CAM_HEIGHT / 2;  // Last known y-coordinate of marker

/**********************************************************
 * Position structure
 *
 * Holds x and y coordinates to return from process_frame.
 **********************************************************/
typedef struct {
    int x;
    int y;
} Position;

/**********************************************************
 * set_pixel
 *
 * Updates the color of a single pixel at (x, y) in the YUYV frame.
 **********************************************************/
void set_pixel(unsigned char *frame, int frame_width, int frame_height, int x, int y, Color color) {
    if (x < 0 || x >= frame_width || y < 0 || y >= frame_height)
        return;
    int pair_index = x / 2;
    int row_pairs = frame_width / 2;
    int offset = (y * row_pairs + pair_index) * 4;
    frame[offset]     = color.Y;
    frame[offset + 1] = color.U;
    frame[offset + 2] = color.Y;
    frame[offset + 3] = color.V;
}

/**********************************************************
 * draw_marker
 *
 * Draws a filled square marker of size MARKER_SIZE x MARKER_SIZE,
 * centered at (center_x, center_y) using the given color.
 **********************************************************/
void draw_marker(unsigned char *frame, int frame_width, int frame_height,
                 int center_x, int center_y, Color color) {
    int half = MARKER_SIZE / 2;
    for (int y = center_y - half; y <= center_y + half; y++) {
        for (int x = center_x - half; x <= center_x + half; x++) {
            set_pixel(frame, frame_width, frame_height, x, y, color);
        }
    }
}

/**********************************************************
 * process_frame
 *
 * Processes a video frame by performing the following steps:
 *   1. On first call, allocate buffers and initialize the background.
 *   2. For each grid cell:
 *         - Calculate the average brightness (Y channel) from paired pixels.
 *         - Update the background model.
 *         - Compare current brightness to background; if difference (diff)
 *           exceeds threshold, mark the cell as motion and overlay a red gradient.
 *   3. Apply 3×3 erosion followed by 3×3 dilation for noise reduction.
 *   4. Calculate the center of motion by computing the simple arithmetic
 *      average of all grid cells marked as motion.
 *   5. Convert grid coordinates to frame coordinates and low pass filter the center.
 *   6. Draw the marker at the computed center and return its position.
 *
 * Assumes the frame is in YUYV format.
 **********************************************************/
Position process_frame(unsigned char *frame, size_t frame_size, int frame_width, int frame_height) {
    int i, x, y;
    
    // Allocate buffers on first call.
    if (orig_frame == NULL) {
        orig_frame = malloc(frame_size);
        backgroundY = malloc(GRID_SIZE * sizeof(float));
        motion_mask = malloc(GRID_SIZE * sizeof(int));
        eroded_mask = malloc(GRID_SIZE * sizeof(int));
        dilated_mask = malloc(GRID_SIZE * sizeof(int));
        if (!orig_frame || !backgroundY || !motion_mask || !eroded_mask || !dilated_mask) {
            fprintf(stderr, "Initialization: Out of memory.\n");
            exit(EXIT_FAILURE);
        }
        memcpy(orig_frame, frame, frame_size);
        for (i = 0; i < GRID_SIZE; i++) {
            int base = i * 4;
            float y1 = (float)frame[base];
            float y2 = (float)frame[base + 2];
            backgroundY[i] = (y1 + y2) / 2.0f;
        }
        return (Position){CAM_WIDTH / 2, CAM_HEIGHT / 2};
    }
    
    memcpy(orig_frame, frame, frame_size);
    
    /* --- Step 1 & 2: Update background and mark motion ---
       For each grid cell, compute the average brightness and update the background.
       If the difference (diff) exceeds the threshold, mark the cell as motion.
       Also overlay a red gradient based on the diff value.
    */
    for (i = 0; i < GRID_SIZE; i++) {
        int base = i * 4;
        float y1 = (float)orig_frame[base];
        float y2 = (float)orig_frame[base + 2];
        float currentY = (y1 + y2) / 2.0f;
        float diff = fabsf(currentY - backgroundY[i]);
        float alpha = BG_ALPHA_NO_MOTION;
        if (diff > BG_MOTION_DIFF_THRESHOLD)
            alpha = BG_ALPHA_MOTION;
        backgroundY[i] = alpha * backgroundY[i] + (1.0f - alpha) * currentY;
        
        float dynamic_thresh = MOTION_THRESHOLD;
#if ENABLE_ADAPTIVE_THRESHOLD
        float adaptive_component = ADAPTIVE_FACTOR * (backgroundY[i] + 1.0f);
        if (adaptive_component > dynamic_thresh)
            dynamic_thresh = adaptive_component;
#endif
        if (diff > dynamic_thresh) {
            float ratio = (diff - DARK_RED_MOVEMENT_LEVEL) / (BRIGHT_RED_MOVEMENT_LEVEL - DARK_RED_MOVEMENT_LEVEL);
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;
            unsigned char newY = DARK_RED_Y_VALUE + (unsigned char)(ratio * (BRIGHT_RED_Y_VALUE - DARK_RED_Y_VALUE));
            frame[base]   = newY;
            frame[base+1] = RED_U;
            frame[base+2] = newY;
            frame[base+3] = RED_V;
            motion_mask[i] = 1;
        } else {
            frame[base]   = orig_frame[base];
            frame[base+1] = orig_frame[base+1];
            frame[base+2] = orig_frame[base+2];
            frame[base+3] = orig_frame[base+3];
            motion_mask[i] = 0;
        }
    }
    
    /* --- Step 3: Noise reduction via 3×3 erosion and dilation ---
       Erosion: A cell remains marked only if all its 3×3 neighbors are marked.
       Dilation: Expand motion regions by checking adjacent cells.
    */
    memset(eroded_mask, 0, GRID_SIZE * sizeof(int));
    for (y = 1; y < GRID_HEIGHT - 1; y++) {
        for (x = 1; x < GRID_WIDTH - 1; x++) {
            int idx = y * GRID_WIDTH + x;
            int all_one = 1;
            for (int ny = y - 1; ny <= y + 1; ny++) {
                for (int nx = x - 1; nx <= x + 1; nx++) {
                    int nidx = ny * GRID_WIDTH + nx;
                    if (motion_mask[nidx] == 0) { all_one = 0; break; }
                }
                if (!all_one) break;
            }
            eroded_mask[idx] = all_one;
        }
    }
    memset(dilated_mask, 0, GRID_SIZE * sizeof(int));
    for (y = 1; y < GRID_HEIGHT - 1; y++) {
        for (x = 1; x < GRID_WIDTH - 1; x++) {
            int idx = y * GRID_WIDTH + x;
            if (eroded_mask[idx] == 1) {
                dilated_mask[idx] = 1;
            } else {
                int found = 0;
                for (int ny = y - 1; ny <= y + 1 && !found; ny++) {
                    for (int nx = x - 1; nx <= x + 1; nx++) {
                        int nidx = ny * GRID_WIDTH + nx;
                        if (eroded_mask[nidx] == 1) { found = 1; break; }
                    }
                }
                dilated_mask[idx] = found;
            }
        }
    }
    
    /* --- Step 4: Compute geometric center ---
         Simply average the grid coordinates of all cells marked as motion.
         This yields the center-of-motion based on the detected movement.
    */
    long sum_x = 0, sum_y = 0;
    int count = 0;
    for (y = 0; y < GRID_HEIGHT; y++) {
        for (x = 0; x < GRID_WIDTH; x++) {
            int idx = y * GRID_WIDTH + x;
            if (dilated_mask[idx] == 1) {
                sum_x += x;
                sum_y += y;
                count++;
            }
        }
    }
    if (count >= MIN_MOVEMENT_PIXELS) {
        int measured_center_x = (int)((sum_x / (float)count) * 2) + 1;
        int measured_center_y = (int)(sum_y / (float)count);
        last_center_x = (int)(CROSSHAIR_LPF_ALPHA * measured_center_x +
                              (1.0f - CROSSHAIR_LPF_ALPHA) * last_center_x);
        last_center_y = (int)(CROSSHAIR_LPF_ALPHA * measured_center_y +
                              (1.0f - CROSSHAIR_LPF_ALPHA) * last_center_y);
        fprintf(stderr, "Center-of-motion at (%d, %d) with %d pixels.\n", measured_center_x, measured_center_y, count);
    } else {
        fprintf(stderr, "Insufficient motion detected (only %d pixels).\n", count);
    }
    
    /* --- Step 5: Draw the marker and return the center position --- */
    draw_marker(frame, frame_width, frame_height, last_center_x, last_center_y, marker_color);
    return (Position){last_center_x, last_center_y};
}

/*
 * End of object_recognition.c
 *
 * Design notes:
 * - The red gradient overlay is computed based on the brightness difference from the background.
 * - After noise reduction, the geometric center is computed as the arithmetic average
 *   of all motion detection points.
 * - This simple averaging is intended to yield the center of the moving object even
 *   when only its edges are detected.
 * - All code is written in plain C (-std=c11) using standard libraries.
 */
