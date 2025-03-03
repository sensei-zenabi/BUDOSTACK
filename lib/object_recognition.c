/*
 * object_recognition.c
 *
 * Object Recognition and Tracking Module with:
 *   - A running-average background model for motion detection.
 *   - Configurable background learning rates to reduce trailing artifacts.
 *   - A morphological opening (3×3 erosion + dilation) to remove noise.
 *   - Flood-fill detection, keeping only the two largest regions.
 *   - Kalman filter tracking (constant-velocity model).
 *
 * Design Principles:
 *   - Written in plain C (-std=c11) using only standard cross-platform libraries.
 *   - Single file, no headers.
 *   - All modifications are commented, with no original functionality removed.
 *
 * References:
 *   [1] Mathematical morphology techniques for image processing.
 *   [2] Running average (background modeling) for motion detection.
 *   [3] Standard Kalman filter theory in embedded tracking literature.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* --------------------------------------------------------------------------
 * ADJUSTABLE DEFINES FOR NOISE REDUCTION & BACKGROUND LEARNING
 * -------------------------------------------------------------------------- */
#define MOTION_THRESHOLD 3          // Base threshold for deciding "motion" vs. "no motion" (in Y)
#define BG_MOTION_DIFF_THRESHOLD 20.0f  // If diff > this, use faster alpha to reduce trailing
#define BG_ALPHA_NO_MOTION 0.95f     // Slow update for background when diff is small
#define BG_ALPHA_MOTION 0.70f        // Faster update for background when diff is large

#define CROSSHAIR_SIZE 10            // Crosshair half-length in pixels
#define MIN_BLOCKS_FOR_OBJECT 100    // Minimum # of blocks for a valid region
#define MAX_TRACKS 3                 // Max concurrent tracks
#define MATCH_DISTANCE_THRESHOLD 10   // Grid distance threshold to match detection to track

// Define the red color (for motion overlay) in YUYV format.
const unsigned char redY = 76;
const unsigned char redU = 84;
const unsigned char redV = 255;

/* Color structure for YUYV markers */
typedef struct {
    unsigned char Y;
    unsigned char U;
    unsigned char V;
} Color;

Color marker_colors[] = {
    {76, 84, 255},    // red
    {145, 54, 34},    // approximated green-ish
    {41, 240, 110},   // approximated blue-ish
    {210, 16, 146},   // approximated yellow-ish
    {107, 202, 222},  // approximated magenta-ish
    {170, 166, 16}    // approximated cyan-ish
};
const int num_marker_colors = sizeof(marker_colors) / sizeof(marker_colors[0]);

/* Forward declarations to avoid implicit function warnings */
void kalman_update_track(int det_center_x, int det_center_y);
void clear_stale_kalman_tracks(void);

/*
 * set_pixel sets the pixel at (x,y) to the specified color in YUYV format.
 */
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

/*
 * draw_crosshair draws horizontal and vertical lines of length CROSSHAIR_SIZE.
 */
void draw_crosshair(unsigned char *frame, int frame_width, int frame_height,
                    int center_x, int center_y, Color color) {
    for (int x = center_x - CROSSHAIR_SIZE; x <= center_x + CROSSHAIR_SIZE; x++) {
        set_pixel(frame, frame_width, frame_height, x, center_y, color);
    }
    for (int y = center_y - CROSSHAIR_SIZE; y <= center_y + CROSSHAIR_SIZE; y++) {
        set_pixel(frame, frame_width, frame_height, center_x, y, color);
    }
}

/*
 * KalmanTrack structure: each track is [x, y, vx, vy] in grid coordinates.
 */
typedef struct {
    int id;
    float state[4];      // [x, y, vx, vy]
    float cov[4][4];     // 4x4 covariance matrix
    int missed_frames;
    int updated;         // flag: 1 if updated in this frame
} KalmanTrack;

static KalmanTrack ktracks[MAX_TRACKS];
static int ktrack_count = 0;
static int next_ktrack_id = 0;

/*
 * kalman_predict: constant-velocity prediction step
 */
static void kalman_predict(KalmanTrack* t) {
    float F[4][4] = {
      {1, 0, 1, 0},
      {0, 1, 0, 1},
      {0, 0, 1, 0},
      {0, 0, 0, 1}
    };
    float Q[4][4] = {
      {0.1f, 0,    0,    0},
      {0,    0.1f, 0,    0},
      {0,    0,    0.1f, 0},
      {0,    0,    0,    0.1f}
    };
    float new_state[4] = {0};
    // x' = F*x
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            new_state[i] += F[i][j] * t->state[j];
        }
    }
    memcpy(t->state, new_state, sizeof(new_state));

    // P' = F*P*F^T + Q
    float new_cov[4][4] = {0};
    float temp[4][4] = {0};
    // temp = F*P
    for (int i = 0; i < 4; i++){
       for (int j = 0; j < 4; j++){
           for (int k = 0; k < 4; k++){
              temp[i][j] += F[i][k] * t->cov[k][j];
           }
       }
    }
    // new_cov = temp*F^T
    for (int i = 0; i < 4; i++){
       for (int j = 0; j < 4; j++){
           for (int k = 0; k < 4; k++){
              new_cov[i][j] += temp[i][k] * F[j][k];
           }
       }
    }
    // add Q
    for (int i = 0; i < 4; i++){
       for (int j = 0; j < 4; j++){
          t->cov[i][j] = new_cov[i][j] + Q[i][j];
       }
    }
}

/*
 * kalman_update: measurement update for z = [meas_x, meas_y]
 */
static void kalman_update(KalmanTrack* t, int meas_x, int meas_y) {
    float H[2][4] = {
      {1, 0, 0, 0},
      {0, 1, 0, 0}
    };
    float R[2][2] = {
      {1, 0},
      {0, 1}
    };
    // y_innov = z - H*x
    float y_innov[2];
    y_innov[0] = meas_x - t->state[0];
    y_innov[1] = meas_y - t->state[1];

    // S = H*P*H^T + R
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

    // invert S (2x2)
    float detS = S[0][0]*S[1][1] - S[0][1]*S[1][0];
    if (detS == 0) return; // avoid div-by-zero
    float invS[2][2];
    invS[0][0] =  S[1][1] / detS;
    invS[0][1] = -S[0][1] / detS;
    invS[1][0] = -S[1][0] / detS;
    invS[1][1] =  S[0][0] / detS;

    // K = P*H^T*invS
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

    // x = x + K*y_innov
    for (int i = 0; i < 4; i++){
       for (int j = 0; j < 2; j++){
          t->state[i] += K[i][j] * y_innov[j];
       }
    }

    // P = (I - K*H)*P
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

/* Global background arrays for the Y channel (float) */
static float *backgroundY = NULL;

/*
 * process_frame:
 *   1. Initializes or updates the background model for the Y channel (with 2 learning rates).
 *   2. Creates a binary motion mask by comparing the current frame's Y to the background.
 *   3. Applies morphological opening (3×3 erosion, then 3×3 dilation) to remove noise.
 *   4. Flood-fill detection, storing up to MAX_TRACKS largest regions.
 *   5. Only keep top two largest detections.
 *   6. Kalman update for each detection or create new track if unmatched.
 *   7. Kalman predict for any track not updated.
 *   8. Remove stale tracks.
 *   9. Draw crosshairs for each active track.
 */
void process_frame(unsigned char *frame, size_t frame_size, int frame_width, int frame_height) {
    static unsigned char *orig_frame = NULL;
    static size_t buf_size = 0;

    if (orig_frame == NULL || frame_size != buf_size) {
        if (orig_frame) {
            free(orig_frame);
        }
        buf_size = frame_size;
        orig_frame = malloc(buf_size);
        if (!orig_frame) {
            fprintf(stderr, "Motion detection: Out of memory.\n");
            exit(EXIT_FAILURE);
        }
        memcpy(orig_frame, frame, frame_size);

        // Initialize background for Y
        if (backgroundY) {
            free(backgroundY);
        }
        int grid_width = frame_width / 2;
        int grid_height = frame_height;
        int grid_size = grid_width * grid_height;
        backgroundY = malloc(grid_size * sizeof(float));
        if (!backgroundY) {
            fprintf(stderr, "Motion detection: Out of memory for background.\n");
            exit(EXIT_FAILURE);
        }
        // Initialize background from first frame
        for (int i = 0; i < grid_size; i++) {
            int base = i * 4;
            float y1 = (float)frame[base];
            float y2 = (float)frame[base + 2];
            backgroundY[i] = (y1 + y2) / 2.0f;
        }
        return;
    }

    memcpy(orig_frame, frame, frame_size);

    int grid_width = frame_width / 2;
    int grid_height = frame_height;
    int grid_size = grid_width * grid_height;

    /* Step 1 & 2: Update background model, then compare to form motion mask. */
    int *motion_mask = malloc(grid_size * sizeof(int));
    if (!motion_mask) {
        fprintf(stderr, "Motion detection: Out of memory for motion mask.\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < grid_size; i++) {
        int base = i * 4;
        // Current Y for this 2-pixel block
        float y1 = (float)orig_frame[base];
        float y2 = (float)orig_frame[base + 2];
        float currentY = (y1 + y2) / 2.0f;

        // Decide which alpha to use
        float diff = fabsf(currentY - backgroundY[i]);
        float alpha = BG_ALPHA_NO_MOTION; 
        if (diff > BG_MOTION_DIFF_THRESHOLD) {
            // Large difference => use faster alpha to reduce trailing
            alpha = BG_ALPHA_MOTION;
        }

        // Update background (running average)
        backgroundY[i] = alpha * backgroundY[i] + (1.0f - alpha) * currentY;

        // Check if above motion threshold
        if (diff > MOTION_THRESHOLD) {
            // Mark motion on the frame visually
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

    /* Step 3: Morphological opening (3×3 erosion -> 3×3 dilation). */
    int *eroded_mask = malloc(grid_size * sizeof(int));
    if (!eroded_mask) {
        fprintf(stderr, "Motion detection: Out of memory for eroded mask.\n");
        free(motion_mask);
        exit(EXIT_FAILURE);
    }
    memset(eroded_mask, 0, grid_size * sizeof(int));

    // Erosion
    for (int y = 1; y < grid_height - 1; y++){
        for (int x = 1; x < grid_width - 1; x++){
            int idx = y * grid_width + x;
            int all_one = 1;
            for (int ny = y - 1; ny <= y + 1; ny++) {
                for (int nx = x - 1; nx <= x + 1; nx++) {
                    int nidx = ny * grid_width + nx;
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
    free(motion_mask);

    // Dilation
    int *dilated_mask = malloc(grid_size * sizeof(int));
    if (!dilated_mask) {
        fprintf(stderr, "Motion detection: Out of memory for dilated mask.\n");
        free(eroded_mask);
        exit(EXIT_FAILURE);
    }
    memset(dilated_mask, 0, grid_size * sizeof(int));

    for (int y = 1; y < grid_height - 1; y++) {
        for (int x = 1; x < grid_width - 1; x++) {
            int idx = y * grid_width + x;
            if (eroded_mask[idx] == 1) {
                dilated_mask[idx] = 1;
            } else {
                int found = 0;
                for (int ny = y - 1; ny <= y + 1 && !found; ny++) {
                    for (int nx = x - 1; nx <= x + 1; nx++) {
                        int nidx = ny * grid_width + nx;
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
    free(eroded_mask);

    /* Step 4: Flood-fill to find connected regions. */
    typedef struct {
        int center_x;
        int center_y;
        int count;
    } Detection;
    Detection detections[MAX_TRACKS];
    int detection_count = 0;

    int *stack = malloc(grid_size * sizeof(int));
    if (!stack) {
        fprintf(stderr, "Motion detection: Out of memory for flood fill stack.\n");
        free(dilated_mask);
        exit(EXIT_FAILURE);
    }
    for (int y = 0; y < grid_height; y++) {
        for (int x = 0; x < grid_width; x++) {
            int idx = y * grid_width + x;
            if (dilated_mask[idx] == 1) {
                int stack_top = 0;
                stack[stack_top++] = idx;
                dilated_mask[idx] = 0;
                int count = 0, min_x = x, max_x = x, min_y = y, max_y = y;
                while (stack_top > 0) {
                    int current = stack[--stack_top];
                    int cx = current % grid_width;
                    int cy = current / grid_width;
                    count++;
                    if (cx < min_x) min_x = cx;
                    if (cx > max_x) max_x = cx;
                    if (cy < min_y) min_y = cy;
                    if (cy > max_y) max_y = cy;
                    // 4-neighbor
                    int neighbors[4][2] = { {cx-1,cy}, {cx+1,cy}, {cx,cy-1}, {cx,cy+1} };
                    for (int k = 0; k < 4; k++) {
                        int nx = neighbors[k][0];
                        int ny = neighbors[k][1];
                        if (nx >= 0 && nx < grid_width && ny >= 0 && ny < grid_height) {
                            int nidx = ny * grid_width + nx;
                            if (dilated_mask[nidx] == 1) {
                                stack[stack_top++] = nidx;
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
    free(stack);
    free(dilated_mask);

    /* Step 5: Keep only the top 2 largest detections. */
    if (detection_count > 2) {
        // Simple bubble sort to rank by count
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

    /* Step 6: Update Kalman tracks with each detection. */
    for (int i = 0; i < detection_count; i++) {
        kalman_update_track(detections[i].center_x, detections[i].center_y);
    }

    /* Step 7: Predict for tracks not updated. */
    for (int i = 0; i < ktrack_count; i++) {
        if (!ktracks[i].updated) {
            kalman_predict(&ktracks[i]);
        }
    }

    /* Step 8: Remove stale tracks. */
    clear_stale_kalman_tracks();

    /* Step 9: Draw crosshairs at predicted positions. */
    for (int i = 0; i < ktrack_count; i++) {
        int pred_center_x = (int)(ktracks[i].state[0] * 2) + 1;
        int pred_center_y = (int)(ktracks[i].state[1]);
        Color marker = marker_colors[i % num_marker_colors];
        draw_crosshair(frame, frame_width, frame_height, pred_center_x, pred_center_y, marker);
    }

    fprintf(stderr, "Motion tracking (Kalman): %d object(s) detected and tracked.\n", ktrack_count);
}

/*
 * kalman_update_track: match detection to existing track or create a new one.
 */
void kalman_update_track(int det_center_x, int det_center_y) {
    int best_match = -1;
    float best_distance = 1e9f;
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

/*
 * clear_stale_kalman_tracks: remove tracks missed for more than MAX_MISSED frames.
 */
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
    // Reset updated flags
    for (int i = 0; i < ktrack_count; i++) {
        ktracks[i].updated = 0;
    }
}

/*
 * End of object_recognition.c
 *
 * Usage:
 *   - Adjust #defines BG_MOTION_DIFF_THRESHOLD, BG_ALPHA_MOTION, and BG_ALPHA_NO_MOTION
 *     to control how quickly the background "forgets" old pixel values in regions of strong motion
 *     vs. regions of small differences.
 *   - Increase MOTION_THRESHOLD if flicker remains, or decrease if you're missing subtle movements.
 *   - Morphological opening size (3×3) can be increased to 5×5 for even stronger noise removal,
 *     but that might merge nearby objects.
 *
 * This setup helps avoid long motion trails by using a faster background update
 * in regions where there's clearly a large difference from the background.
 */
