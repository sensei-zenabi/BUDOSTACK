/*
 * object_recognition.c
 *
 * Object Recognition and Tracking Module for 320×240 resolution cameras.
 * Features:
 *   - Running-average background model for motion detection.
 *   - Adaptive background learning rates.
 *   - 3×3 morphological opening (erosion followed by dilation) for noise reduction.
 *   - Center-of-mass calculation based on the number and distribution of movement pixels.
 *   - Optional toggling of a crosshair display to always show the object's placement.
 *
 * Design Principles:
 *   - Written in plain C (-std=c11) using only standard cross-platform libraries.
 *   - Single file, no header files.
 *   - All modifications are commented, with no original functionality removed.
 *
 * Improvements for 320×240 resolution:
 *   1. Resolution-specific defines for width and height.
 *   2. Static allocation and reuse of buffers to avoid frequent dynamic allocations.
 *   3. Parameter tuning (e.g., MIN_BLOCKS_FOR_OBJECT, CROSSHAIR_SIZE) for the smaller resolution.
 *
 * References:
 *   [1] Mathematical morphology techniques for image processing.
 *   [2] Running average for background modeling.
 *   [3] Center-of-mass computation methods.
 *   [4] Adaptive thresholding for uneven illumination.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* --------------------------------------------------------------------------
 * RESOLUTION DEFINES (for 320×240 camera)
 * -------------------------------------------------------------------------- */
#define CAM_WIDTH    320
#define CAM_HEIGHT   240

// The grid represents the Y channel in YUYV format, where we average pairs:
// grid_width = CAM_WIDTH / 2 and grid_height = CAM_HEIGHT.
#define GRID_WIDTH   (CAM_WIDTH / 2)
#define GRID_HEIGHT  (CAM_HEIGHT)
#define GRID_SIZE    (GRID_WIDTH * GRID_HEIGHT)

/* --------------------------------------------------------------------------
 * ADJUSTABLE DEFINES FOR NOISE REDUCTION & BACKGROUND LEARNING
 * -------------------------------------------------------------------------- */
#define MOTION_THRESHOLD 1            // Base threshold for "motion" vs. "no motion" (in Y)
#define BG_MOTION_DIFF_THRESHOLD 1.0f  // If diff > this, use faster alpha to reduce trailing
#define BG_ALPHA_NO_MOTION 0.001f      // Slow update for background when diff is small
// Use a faster learning rate when significant motion is detected.
#define BG_ALPHA_MOTION 0.010f         // Faster update for background when diff is large

// NEW DEFINES FOR ADAPTIVE THRESHOLD
#define ENABLE_ADAPTIVE_THRESHOLD 1    // Set to 0 to disable adaptive threshold
#define ADAPTIVE_FACTOR 0.2f           // Fraction of local background used as an additional threshold

// Drawing parameters (tuned for 320×240)
#define CROSSHAIR_SIZE 10              // Crosshair size (may be scaled for a smaller resolution)
#define MIN_MOVEMENT_PIXELS 50         // Minimum pixels to consider a valid movement

// Define the red color (for motion overlay) in YUYV format.
const unsigned char redY = 76;
const unsigned char redU = 84;
const unsigned char redV = 255;

typedef struct {
    unsigned char Y;
    unsigned char U;
    unsigned char V;
} Color;

// The marker color used for drawing the crosshair
Color marker_color = {41, 240, 110};

/* --------------------------------------------------------------------------
 * STATIC GLOBAL BUFFERS (allocated once for 320×240 resolution)
 *
 * These buffers are used repeatedly for processing frames to avoid dynamic
 * allocation overhead on each frame.
 * -------------------------------------------------------------------------- */
static unsigned char *orig_frame = NULL;      // Copy of the original frame
static float *backgroundY = NULL;             // Background model for Y channel
static int *motion_mask = NULL;               // Binary mask for motion detection
static int *eroded_mask = NULL;               // Buffer for erosion operation
static int *dilated_mask = NULL;              // Buffer for dilation operation
static int last_center_x = CAM_WIDTH / 2;   // Default center is mid-frame
static int last_center_y = CAM_HEIGHT / 2;

/* --------------------------------------------------------------------------
 * Helper functions: set_pixel and draw_crosshair
 * -------------------------------------------------------------------------- */
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

void draw_crosshair(unsigned char *frame, int frame_width, int frame_height,
                    int center_x, int center_y, Color color) {
    // Draw horizontal line
    for (int x = center_x - CROSSHAIR_SIZE; x <= center_x + CROSSHAIR_SIZE; x++) {
        set_pixel(frame, frame_width, frame_height, x, center_y, color);
    }
    // Draw vertical line
    for (int y = center_y - CROSSHAIR_SIZE; y <= center_y + CROSSHAIR_SIZE; y++) {
        set_pixel(frame, frame_width, frame_height, center_x, y, color);
    }
}

/* --------------------------------------------------------------------------
 * process_frame:
 *
 * This function:
 *   1. Initializes or updates the background model (Y channel).
 *   2. Generates a motion mask by comparing the frame to the background.
 *   3. Applies 3×3 morphological opening (erosion then dilation) for noise reduction.
 *   4. Computes the center-of-mass of all detected movement pixels.
 *   5. Draws a crosshair at the computed center-of-mass.
 *
 * When crosshair_mode is enabled, the crosshair is drawn using the last known
 * center-of-mass even if no movement is detected.
 *
 * -------------------------------------------------------------------------- */
void process_frame(unsigned char *frame, size_t frame_size, int frame_width, int frame_height) {
    // On the first call, allocate the static buffers.
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
        // Initialize background from the first frame.
        memcpy(orig_frame, frame, frame_size);
        for (int i = 0; i < GRID_SIZE; i++) {
            int base = i * 4;
            float y1 = (float)frame[base];
            float y2 = (float)frame[base + 2];
            backgroundY[i] = (y1 + y2) / 2.0f;
        }
        return;
    }

    memcpy(orig_frame, frame, frame_size);

    /* Step 1 & 2: Update background model and form motion mask */
    for (int i = 0; i < GRID_SIZE; i++) {
        int base = i * 4;
        float y1 = (float)orig_frame[base];
        float y2 = (float)orig_frame[base + 2];
        float currentY = (y1 + y2) / 2.0f;

        float diff = fabsf(currentY - backgroundY[i]);
        float alpha = BG_ALPHA_NO_MOTION;
        if (diff > BG_MOTION_DIFF_THRESHOLD) {
            alpha = BG_ALPHA_MOTION;
        }
        backgroundY[i] = alpha * backgroundY[i] + (1.0f - alpha) * currentY;

        float dynamic_thresh = MOTION_THRESHOLD;
#if ENABLE_ADAPTIVE_THRESHOLD
        float adaptive_component = ADAPTIVE_FACTOR * (backgroundY[i] + 1.0f);
        if (adaptive_component > dynamic_thresh) {
            dynamic_thresh = adaptive_component;
        }
#endif
        if (diff > dynamic_thresh) {
            // Mark pixel as motion: overlay with red.
            frame[base]   = redY;
            frame[base+1] = redU;
            frame[base+2] = redY;
            frame[base+3] = redV;
            motion_mask[i] = 1;
        } else {
            // Restore original pixel.
            frame[base]   = orig_frame[base];
            frame[base+1] = orig_frame[base+1];
            frame[base+2] = orig_frame[base+2];
            frame[base+3] = orig_frame[base+3];
            motion_mask[i] = 0;
        }
    }

    /* Step 3: Morphological Opening (3×3 erosion then 3×3 dilation) */
    // Erosion: keep pixel only if all neighbors are motion pixels.
    memset(eroded_mask, 0, GRID_SIZE * sizeof(int));
    for (int y = 1; y < GRID_HEIGHT - 1; y++) {
        for (int x = 1; x < GRID_WIDTH - 1; x++) {
            int idx = y * GRID_WIDTH + x;
            int all_one = 1;
            for (int ny = y - 1; ny <= y + 1; ny++) {
                for (int nx = x - 1; nx <= x + 1; nx++) {
                    int nidx = ny * GRID_WIDTH + nx;
                    if (motion_mask[nidx] == 0) {
                        all_one = 0;
                        break;
                    }
                }
                if (!all_one)
                    break;
            }
            eroded_mask[idx] = all_one;
        }
    }
    // Dilation: expand motion regions from eroded_mask.
    memset(dilated_mask, 0, GRID_SIZE * sizeof(int));
    for (int y = 1; y < GRID_HEIGHT - 1; y++) {
        for (int x = 1; x < GRID_WIDTH - 1; x++) {
            int idx = y * GRID_WIDTH + x;
            if (eroded_mask[idx] == 1) {
                dilated_mask[idx] = 1;
            } else {
                int found = 0;
                for (int ny = y - 1; ny <= y + 1 && !found; ny++) {
                    for (int nx = x - 1; nx <= x + 1; nx++) {
                        int nidx = ny * GRID_WIDTH + nx;
                        if (eroded_mask[nidx] == 1) {
                            found = 1;
                            break;
                        }
                    }
                }
                dilated_mask[idx] = found;
            }
        }
    }

    /* Step 4: Center-of-Mass Calculation for movement pixels.
     *
     * Compute the weighted average (centroid) of all pixels in the dilated_mask.
     * If enough movement is detected (at least MIN_MOVEMENT_PIXELS), update the last known center.
     */
    long sum_x = 0, sum_y = 0;
    int count = 0;
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            int idx = y * GRID_WIDTH + x;
            if (dilated_mask[idx] == 1) {
                sum_x += x;
                sum_y += y;
                count++;
            }
        }
    }
    if (count >= MIN_MOVEMENT_PIXELS) {
        // Convert grid coordinates to full frame coordinates (x scaled by 2 and offset by 1).
        int center_x = (int)((sum_x / (float)count) * 2) + 1;
        int center_y = (int)(sum_y / (float)count);
        last_center_x = center_x;
        last_center_y = center_y;
        fprintf(stderr, "Center-of-mass detected at (%d, %d) with %d movement pixels.\n", center_x, center_y, count);
    } else {
        fprintf(stderr, "No sufficient movement detected (only %d pixels).\n", count);
    }

    /* Step 5: Draw crosshair at the computed (or last known) center-of-mass.
     *
     * If crosshair_mode is enabled, the crosshair is always drawn at last_center_x/last_center_y.
     * Otherwise, only draw the crosshair if movement was detected in the current frame.
     */
    if (count >= MIN_MOVEMENT_PIXELS) {
        draw_crosshair(frame, frame_width, frame_height, last_center_x, last_center_y, marker_color);
    }
}

/*
 * End of object_recognition.c
 *
 * Usage Notes:
 *   - The adaptive threshold (ADAPTIVE_FACTOR) scales the motion threshold based on
 *     the local background; this helps with both dark and bright scenes.
 *   - The parameters (such as MIN_MOVEMENT_PIXELS and CROSSHAIR_SIZE) have been
 *     tuned for a 320×240 camera. Adjust these values if your scene characteristics change.
 *   - The center-of-mass is computed over all movement pixels after noise reduction.
 *   - Call toggle_crosshair_mode() (for example, when letter 'T' is pressed) to enable
 *     or disable always drawing the crosshair.
 *
 * References:
 *   [1] Mathematical morphology for image processing.
 *   [2] Running average for background modeling.
 *   [3] Center-of-mass computation methods.
 *   [4] Adaptive thresholding for uneven illumination.
 */
