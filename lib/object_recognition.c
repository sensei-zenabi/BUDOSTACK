/*
 * object_recognition.c
 *
 * Object Recognition and Tracking Module for 320×240 resolution cameras.
 * Features:
 *   - Running-average background model for motion detection.
 *   - Adaptive background learning rates.
 *   - 3×3 morphological opening (erosion followed by dilation) for noise reduction.
 *   - Flood-fill detection to capture the largest connected regions.
 *   - Kalman filter tracking (constant-velocity model) with improvements for fast movements.
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
 *   4. Kalman filter modifications (dt, Q, and innovation-based covariance reset) tuned for 320×240.
 *
 * References:
 *   [1] Mathematical morphology techniques for image processing.
 *   [2] Running average for background modeling.
 *   [3] Kalman filter theory in embedded tracking literature.
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
#define BG_ALPHA_MOTION 0.001f         // Faster update for background when diff is large

// NEW DEFINES FOR ADAPTIVE THRESHOLD
#define ENABLE_ADAPTIVE_THRESHOLD 1    // Set to 0 to disable adaptive threshold
#define ADAPTIVE_FACTOR 0.2f           // Fraction of local background used as an additional threshold

// Drawing and detection parameters (tuned for 320×240)
#define CROSSHAIR_SIZE 10              // Crosshair size (may be scaled for a smaller resolution)
#define MIN_BLOCKS_FOR_OBJECT 50       // Lowered minimum number of connected blocks (from 250)
#define MAX_TRACKS 1
#define MATCH_DISTANCE_THRESHOLD 320   // Maximum allowed distance to match a detection

// Define the red color (for motion overlay) in YUYV format.
const unsigned char redY = 76;
const unsigned char redU = 84;
const unsigned char redV = 255;

typedef struct {
    unsigned char Y;
    unsigned char U;
    unsigned char V;
} Color;

Color marker_colors[] = {
    {41, 240, 110},    // red
    {41, 240, 110},    // approximated green-ish
    {41, 240, 110},    // approximated blue-ish
    {41, 240, 110},    // approximated yellow-ish
    {41, 240, 110},    // approximated magenta-ish
    {41, 240, 110}     // approximated cyan-ish
};
const int num_marker_colors = sizeof(marker_colors) / sizeof(marker_colors[0]);

/* Function prototypes */
void kalman_update_track(int det_center_x, int det_center_y);
void clear_stale_kalman_tracks(void);

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
static int *flood_fill_stack = NULL;          // Stack for flood-fill detection

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
    for (int x = center_x - CROSSHAIR_SIZE; x <= center_x + CROSSHAIR_SIZE; x++) {
        set_pixel(frame, frame_width, frame_height, x, center_y, color);
    }
    for (int y = center_y - CROSSHAIR_SIZE; y <= center_y + CROSSHAIR_SIZE; y++) {
        set_pixel(frame, frame_width, frame_height, center_x, y, color);
    }
}

/* --------------------------------------------------------------------------
 * Kalman Filter Structures and Globals
 * -------------------------------------------------------------------------- */
typedef struct {
    int id;
    float state[4];      // [x, y, vx, vy]
    float cov[4][4];
    int missed_frames;
    int updated;
} KalmanTrack;

static KalmanTrack ktracks[MAX_TRACKS];
static int ktrack_count = 0;
static int next_ktrack_id = 0;

/* --------------------------------------------------------------------------
 * Kalman Filter Functions (with dt, tuned Q, and covariance reset on large innovation)
 * -------------------------------------------------------------------------- */
static void kalman_predict(KalmanTrack* t) {
    // dt should be dynamically computed; for now, we use a constant.
    float dt = 1.0f;

    // State transition matrix with dt
    float F[4][4] = {
      {1, 0, dt, 0},
      {0, 1, 0, dt},
      {0, 0, 1,  0},
      {0, 0, 0,  1}
    };

    // Process noise (Q): tuned to allow responsiveness at 320×240 scale.
    float Q[4][4] = {
      {0.5f, 0,    0,    0},
      {0,    0.5f, 0,    0},
      {0,    0,    0.5f, 0},
      {0,    0,    0,    0.5f}
    };

    // Predict new state: state = F * state
    float new_state[4] = {0};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            new_state[i] += F[i][j] * t->state[j];
        }
    }
    memcpy(t->state, new_state, sizeof(new_state));

    // Predict new covariance: cov = F * cov * F^T + Q
    float new_cov[4][4] = {0};
    float temp[4][4] = {0};
    for (int i = 0; i < 4; i++){
       for (int j = 0; j < 4; j++){
           for (int k = 0; k < 4; k++){
              temp[i][j] += F[i][k] * t->cov[k][j];
           }
       }
    }
    for (int i = 0; i < 4; i++){
       for (int j = 0; j < 4; j++){
           for (int k = 0; k < 4; k++){
              new_cov[i][j] += temp[i][k] * F[j][k];
           }
       }
    }
    for (int i = 0; i < 4; i++){
       for (int j = 0; j < 4; j++){
          t->cov[i][j] = new_cov[i][j] + Q[i][j];
       }
    }
}

static void kalman_update(KalmanTrack* t, int meas_x, int meas_y) {
    float H[2][4] = {
      {1, 0, 0, 0},
      {0, 1, 0, 0}
    };
    float R[2][2] = {
      {1, 0},
      {0, 1}
    };
    float y_innov[2];
    y_innov[0] = meas_x - t->state[0];
    y_innov[1] = meas_y - t->state[1];

    // Reinitialize covariance if innovation is very large (object jumped quickly)
    float innovation_norm = sqrtf(y_innov[0]*y_innov[0] + y_innov[1]*y_innov[1]);
    if (innovation_norm > MATCH_DISTANCE_THRESHOLD) {
        for (int i = 0; i < 4; i++){
            for (int j = 0; j < 4; j++){
                t->cov[i][j] = (i == j) ? 10.0f : 0.0f;
            }
        }
    }

    // Measurement update calculation
    float S[2][2] = {0};
    float HP[2][4] = {0};
    for (int i = 0; i < 2; i++){
       for (int j = 0; j < 4; j++){
          for (int k = 0; k < 4; k++){
             HP[i][j] += H[i][k] * t->cov[k][j];
          }
       }
    }
    for (int i = 0; i < 2; i++){
       for (int j = 0; j < 2; j++){
          for (int k = 0; k < 4; k++){
              S[i][j] += HP[i][k] * H[j][k];
          }
       }
    }
    for (int i = 0; i < 2; i++){
       for (int j = 0; j < 2; j++){
          S[i][j] += R[i][j];
       }
    }

    float detS = S[0][0]*S[1][1] - S[0][1]*S[1][0];
    if (detS == 0) return;
    float invS[2][2];
    invS[0][0] =  S[1][1] / detS;
    invS[0][1] = -S[0][1] / detS;
    invS[1][0] = -S[1][0] / detS;
    invS[1][1] =  S[0][0] / detS;

    float PHt[4][2] = {0};
    for (int i = 0; i < 4; i++){
       for (int j = 0; j < 2; j++){
          for (int k = 0; k < 4; k++){
             PHt[i][j] += t->cov[i][k] * H[j][k];
          }
       }
    }
    float K[4][2] = {0};
    for (int i = 0; i < 4; i++){
       for (int j = 0; j < 2; j++){
          for (int k = 0; k < 2; k++){
             K[i][j] += PHt[i][k] * invS[k][j];
          }
       }
    }

    // Update state with measurement innovation
    for (int i = 0; i < 4; i++){
       for (int j = 0; j < 2; j++){
          t->state[i] += K[i][j] * y_innov[j];
       }
    }

    // Update covariance: P = (I - K*H) * P
    float KH[4][4] = {0};
    for (int i = 0; i < 4; i++){
       for (int j = 0; j < 4; j++){
          for (int k = 0; k < 2; k++){
             KH[i][j] += K[i][k] * H[k][j];
          }
       }
    }
    float I_KH[4][4] = {0};
    for (int i = 0; i < 4; i++){
       for (int j = 0; j < 4; j++){
          I_KH[i][j] = (i == j) ? (1 - KH[i][j]) : -KH[i][j];
       }
    }
    float newP[4][4] = {0};
    for (int i = 0; i < 4; i++){
       for (int j = 0; j < 4; j++){
          for (int k = 0; k < 4; k++){
             newP[i][j] += I_KH[i][k] * t->cov[k][j];
          }
       }
    }
    memcpy(t->cov, newP, sizeof(newP));
}

/* --------------------------------------------------------------------------
 * process_frame:
 *
 * This function:
 *   1. Initializes or updates the background model (Y channel).
 *   2. Generates a motion mask by comparing the frame to the background.
 *   3. Applies 3×3 erosion followed by 3×3 dilation (morphological opening).
 *   4. Performs flood-fill detection to extract connected regions.
 *   5. Keeps the largest detections (up to MAX_TRACKS).
 *   6. Updates/creates Kalman tracks and predicts positions.
 *   7. Draws crosshairs at the predicted positions.
 *
 * Improvements:
 *   - Uses static buffers for efficiency.
 *   - All buffer sizes are based on GRID_SIZE computed from CAM_WIDTH/HEIGHT.
 * -------------------------------------------------------------------------- */
void process_frame(unsigned char *frame, size_t frame_size, int frame_width, int frame_height) {
    // On the first call, allocate the static buffers
    if (orig_frame == NULL) {
        orig_frame = malloc(frame_size);
        backgroundY = malloc(GRID_SIZE * sizeof(float));
        motion_mask = malloc(GRID_SIZE * sizeof(int));
        eroded_mask = malloc(GRID_SIZE * sizeof(int));
        dilated_mask = malloc(GRID_SIZE * sizeof(int));
        flood_fill_stack = malloc(GRID_SIZE * sizeof(int));
        if (!orig_frame || !backgroundY || !motion_mask || !eroded_mask || !dilated_mask || !flood_fill_stack) {
            fprintf(stderr, "Initialization: Out of memory.\n");
            exit(EXIT_FAILURE);
        }
        // Initialize background from first frame
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
            frame[base]   = redY;
            frame[base+1] = redU;
            frame[base+2] = redY;
            frame[base+3] = redV;
            motion_mask[i] = 1;
        } else {
            // Restore original pixel
            frame[base]   = orig_frame[base];
            frame[base+1] = orig_frame[base+1];
            frame[base+2] = orig_frame[base+2];
            frame[base+3] = orig_frame[base+3];
            motion_mask[i] = 0;
        }
    }

    /* Step 3: Morphological Opening (3×3 erosion then 3×3 dilation) */
    memset(eroded_mask, 0, GRID_SIZE * sizeof(int));
    for (int y = 1; y < GRID_HEIGHT - 1; y++){
        for (int x = 1; x < GRID_WIDTH - 1; x++){
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
                if (!all_one) break;
            }
            eroded_mask[idx] = all_one;
        }
    }

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

    /* Step 4: Flood-fill to detect connected regions */
    typedef struct {
        int center_x;
        int center_y;
        int count;
    } Detection;
    Detection detections[MAX_TRACKS];
    int detection_count = 0;
    int stack_top = 0;

    // Copy dilated_mask into a temporary working buffer (reuse dilated_mask)
    // for flood-fill so that the original mask remains intact if needed.
    for (int i = 0; i < GRID_SIZE; i++) {
        // We can reuse dilated_mask because we no longer need its original values after dilation.
        // (Alternatively, you could copy if needed.)
    }
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            int idx = y * GRID_WIDTH + x;
            if (dilated_mask[idx] == 1) {
                stack_top = 0;
                flood_fill_stack[stack_top++] = idx;
                dilated_mask[idx] = 0;
                int count = 0, min_x = x, max_x = x, min_y = y, max_y = y;
                while (stack_top > 0) {
                    int current = flood_fill_stack[--stack_top];
                    int cx = current % GRID_WIDTH;
                    int cy = current / GRID_WIDTH;
                    count++;
                    if (cx < min_x) min_x = cx;
                    if (cx > max_x) max_x = cx;
                    if (cy < min_y) min_y = cy;
                    if (cy > max_y) max_y = cy;
                    int neighbors[4][2] = { {cx-1,cy}, {cx+1,cy}, {cx,cy-1}, {cx,cy+1} };
                    for (int k = 0; k < 4; k++) {
                        int nx = neighbors[k][0];
                        int ny = neighbors[k][1];
                        if (nx >= 0 && nx < GRID_WIDTH && ny >= 0 && ny < GRID_HEIGHT) {
                            int nidx = ny * GRID_WIDTH + nx;
                            if (dilated_mask[nidx] == 1) {
                                flood_fill_stack[stack_top++] = nidx;
                                dilated_mask[nidx] = 0;
                            }
                        }
                    }
                }
                if (count >= MIN_BLOCKS_FOR_OBJECT && detection_count < MAX_TRACKS) {
                    detections[detection_count].center_x = (min_x + max_x) / 2;
                    detections[detection_count].center_y = (min_y + max_y) / 2;
                    detections[detection_count].count = count;
                    detection_count++;
                }
            }
        }
    }

    /* Step 5: (Optional) Keep only the largest detections if more than MAX_TRACKS.
       (For now, MAX_TRACKS==1, so this loop is trivial.) */
    if (detection_count > 2) {
        for (int i = 0; i < detection_count - 1; i++) {
            for (int j = 0; j < detection_count - i - 1; j++) {
                if (detections[j].count < detections[j+1].count) {
                    Detection temp = detections[j];
                    detections[j] = detections[j+1];
                    detections[j+1] = temp;
                }
            }
        }
        detection_count = 2;
    }

    /* Step 6: Update Kalman tracks with detected object centers */
    for (int i = 0; i < detection_count; i++) {
        kalman_update_track(detections[i].center_x, detections[i].center_y);
    }

    /* Step 7: Predict for tracks not updated */
    for (int i = 0; i < ktrack_count; i++) {
        if (!ktracks[i].updated) {
            kalman_predict(&ktracks[i]);
        }
    }

    /* Step 8: Remove stale tracks */
    clear_stale_kalman_tracks();

    /* Step 9: Draw crosshairs at predicted positions */
    for (int i = 0; i < ktrack_count; i++) {
        // The predicted x coordinate is scaled to the full frame width.
        int pred_center_x = (int)(ktracks[i].state[0] * 2) + 1;
        int pred_center_y = (int)(ktracks[i].state[1]);
        Color marker = marker_colors[i % num_marker_colors];
        draw_crosshair(frame, frame_width, frame_height, pred_center_x, pred_center_y, marker);
    }

    fprintf(stderr, "Motion tracking (Kalman): %d object(s) detected and tracked.\n", ktrack_count);
}

/* --------------------------------------------------------------------------
 * kalman_update_track: Associates a detection with an existing track or
 * creates a new track if no match is found.
 * -------------------------------------------------------------------------- */
void kalman_update_track(int det_center_x, int det_center_y) {
    float best_distance = 1e9f;
    int best_match = -1;
    for (int i = 0; i < ktrack_count; i++) {
        float dx = det_center_x - ktracks[i].state[0];
        float dy = det_center_y - ktracks[i].state[1];
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist < best_distance) {
            best_distance = dist;
            best_match = i;
        }
    }
    if (best_match != -1 && best_distance <= MATCH_DISTANCE_THRESHOLD) {
        kalman_update(&ktracks[best_match], det_center_x, det_center_y);
        ktracks[best_match].updated = 1;
    } else {
        if (ktrack_count < MAX_TRACKS) {
            KalmanTrack *t = &ktracks[ktrack_count];
            t->id = next_ktrack_id++;
            t->state[0] = det_center_x;
            t->state[1] = det_center_y;
            t->state[2] = 0;
            t->state[3] = 0;
            for (int i = 0; i < 4; i++){
                for (int j = 0; j < 4; j++){
                    t->cov[i][j] = (i == j) ? 1.0f : 0.0f;
                }
            }
            t->missed_frames = 0;
            t->updated = 1;
            ktrack_count++;
        }
    }
}

/* --------------------------------------------------------------------------
 * clear_stale_kalman_tracks: Removes tracks that have not been updated
 * for several frames.
 * -------------------------------------------------------------------------- */
void clear_stale_kalman_tracks(void) {
    const int MAX_MISSED = 3;
    for (int i = 0; i < ktrack_count; ) {
        if (!ktracks[i].updated) {
            ktracks[i].missed_frames++;
        } else {
            ktracks[i].missed_frames = 0;
        }
        if (ktracks[i].missed_frames > MAX_MISSED) {
            ktracks[i] = ktracks[ktrack_count - 1];
            ktrack_count--;
        } else {
            i++;
        }
    }
    for (int i = 0; i < ktrack_count; i++) {
        ktracks[i].updated = 0;
    }
}

/*
 * End of object_recognition.c
 *
 * Usage Notes:
 *   - The adaptive threshold (ADAPTIVE_FACTOR) scales the motion threshold based on
 *     the local background; this helps with both dark and bright scenes.
 *   - The parameters (such as MIN_BLOCKS_FOR_OBJECT and CROSSHAIR_SIZE) have been
 *     tuned for a 320×240 camera. Adjust these values if your scene characteristics change.
 *   - Kalman filter modifications (using dt, increased Q, and covariance reset on large
 *     innovations) are aimed at allowing the filter to quickly track fast-moving objects.
 *
 * References:
 *   [1] Mathematical morphology for image processing.
 *   [2] Running average for background modeling.
 *   [3] Kalman filter theory in embedded tracking.
 *   [4] Adaptive thresholding for uneven illumination.
 */
