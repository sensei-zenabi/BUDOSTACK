/*
 * object_recognition.c
 *
 * Object Recognition and Tracking Module with Improved Multiple Object Detection
 * and Center Marker Calculation.
 *
 * Design Principles:
 *   - Written in plain C (-std=c11) using only standard cross-platform libraries.
 *   - No header files are used; all function prototypes and definitions are in this file.
 *   - Implements basic motion detection by comparing the current frame with the previous frame.
 *   - For each two-pixel block (4 bytes in YUYV), if the luminance difference exceeds a threshold,
 *     the block is marked with red.
 *   - Enhancements:
 *       * A binary motion mask is generated for each block.
 *       * A morphological dilation step fills gaps in the motion mask so that moving objects are more
 *         likely to form a single connected region.
 *       * A flood-fill algorithm computes the bounding box for each connected motion region.
 *       * Only regions with a minimum number of blocks (to filter out noise) are considered.
 *       * A colored crosshair marker is drawn at the center of the bounding box of each detected object.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MOTION_THRESHOLD 10
#define CROSSHAIR_SIZE 5             // Crosshair half-length in pixels
#define MIN_BLOCKS_FOR_OBJECT 100      // Minimum number of blocks to consider a region as a valid object

// Define the red color to overlay (approximate red in YUYV format).
const unsigned char redY = 76;
const unsigned char redU = 84;
const unsigned char redV = 255;

/* Define a Color structure for YUYV markers */
typedef struct {
    unsigned char Y;
    unsigned char U;
    unsigned char V;
} Color;

/* Array of marker colors (in YUYV format) for different objects.
   Red is used for motion overlay; additional colors will be cycled through for crosshair markers. */
Color marker_colors[] = {
    {76, 84, 255},    // red
    {145, 54, 34},    // approximated green-ish
    {41, 240, 110},   // approximated blue-ish
    {210, 16, 146},   // approximated yellow-ish
    {107, 202, 222},  // approximated magenta-ish
    {170, 166, 16}    // approximated cyan-ish
};
const int num_marker_colors = sizeof(marker_colors) / sizeof(marker_colors[0]);

/*
 * set_pixel sets the pixel in the frame at (x,y) to the specified color.
 * Because of the YUYV format (each 4 bytes holds two pixels), this function computes
 * the correct offset and writes the marker color to both pixels in the pair.
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
 * draw_crosshair draws a crosshair marker centered at (center_x, center_y) in pixel coordinates.
 * It draws horizontal and vertical lines (if within frame bounds) with a length defined by CROSSHAIR_SIZE.
 */
void draw_crosshair(unsigned char *frame, int frame_width, int frame_height, int center_x, int center_y, Color color) {
    for (int x = center_x - CROSSHAIR_SIZE; x <= center_x + CROSSHAIR_SIZE; x++) {
        set_pixel(frame, frame_width, frame_height, x, center_y, color);
    }
    for (int y = center_y - CROSSHAIR_SIZE; y <= center_y + CROSSHAIR_SIZE; y++) {
        set_pixel(frame, frame_width, frame_height, center_x, y, color);
    }
}

/*
 * process_frame compares the current frame with the previous frame.
 * It performs the following steps:
 *   1. Generate a binary motion mask by comparing the luminance (Y channel) differences of each two-pixel block.
 *   2. Apply a morphological dilation to fill in gaps in the motion mask.
 *   3. Use a flood-fill algorithm on the dilated mask to group connected motion blocks.
 *   4. For each connected region with at least MIN_BLOCKS_FOR_OBJECT blocks, compute the bounding box and
 *      determine its center.
 *   5. Draw a colored crosshair marker at the computed center.
 *
 * Processing is done on the raw YUYV frame while preserving the original frame data.
 */
void process_frame(unsigned char *frame, size_t frame_size, int frame_width, int frame_height) {
    static unsigned char *prev_frame = NULL;
    static unsigned char *orig_frame = NULL;
    static size_t buf_size = 0;

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
        memcpy(prev_frame, frame, frame_size);
        return;
    }

    // Preserve the current frame before modifications.
    memcpy(orig_frame, frame, frame_size);

    // Define grid dimensions: each block corresponds to 4 bytes (2 pixels horizontally).
    int grid_width = frame_width / 2;
    int grid_height = frame_height;
    int grid_size = grid_width * grid_height;

    // Allocate and compute the binary motion mask.
    int *motion_mask = malloc(grid_size * sizeof(int));
    if (!motion_mask) {
        fprintf(stderr, "Motion detection: Out of memory for motion mask.\n");
        exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i < frame_size; i += 4) {
        int block_index = i / 4;
        int diff0 = abs((int)orig_frame[i] - (int)prev_frame[i]);
        int diff1 = abs((int)orig_frame[i + 2] - (int)prev_frame[i + 2]);
        if (diff0 > MOTION_THRESHOLD || diff1 > MOTION_THRESHOLD) {
            frame[i]     = redY;
            frame[i + 1] = redU;
            frame[i + 2] = redY;
            frame[i + 3] = redV;
            motion_mask[block_index] = 1;
        } else {
            // Restore original pixel values.
            frame[i]     = orig_frame[i];
            frame[i + 1] = orig_frame[i + 1];
            frame[i + 2] = orig_frame[i + 2];
            frame[i + 3] = orig_frame[i + 3];
            motion_mask[block_index] = 0;
        }
    }

    // ------------------------------------------------------------------
    // Morphological Dilation: Fill gaps in the motion mask.
    // ------------------------------------------------------------------
    int *dilated_mask = malloc(grid_size * sizeof(int));
    if (!dilated_mask) {
        fprintf(stderr, "Motion detection: Out of memory for dilated mask.\n");
        free(motion_mask);
        exit(EXIT_FAILURE);
    }
    for (int y = 0; y < grid_height; y++) {
        for (int x = 0; x < grid_width; x++) {
            int idx = y * grid_width + x;
            if (motion_mask[idx] == 1) {
                dilated_mask[idx] = 1;
            } else {
                int found = 0;
                if (x > 0 && motion_mask[y * grid_width + (x - 1)] == 1) found = 1;
                if (x < grid_width - 1 && motion_mask[y * grid_width + (x + 1)] == 1) found = 1;
                if (y > 0 && motion_mask[(y - 1) * grid_width + x] == 1) found = 1;
                if (y < grid_height - 1 && motion_mask[(y + 1) * grid_width + x] == 1) found = 1;
                dilated_mask[idx] = found;
            }
        }
    }
    free(motion_mask);

    // ------------------------------------------------------------------
    // Connected Component Analysis via Flood-Fill using Bounding Boxes.
    // ------------------------------------------------------------------
    int object_count = 0;
    // Allocate a stack for flood-fill (stores indices into dilated_mask).
    int *stack = malloc(grid_size * sizeof(int));
    if (!stack) {
        fprintf(stderr, "Motion detection: Out of memory for flood fill stack.\n");
        free(dilated_mask);
        exit(EXIT_FAILURE);
    }
    // Process each block in the grid.
    for (int y = 0; y < grid_height; y++) {
        for (int x = 0; x < grid_width; x++) {
            int idx = y * grid_width + x;
            if (dilated_mask[idx] == 1) {
                // Start a new flood-fill for a connected region.
                int stack_top = 0;
                stack[stack_top++] = idx;
                dilated_mask[idx] = 0;
                int count = 0;
                int min_x = x, max_x = x, min_y = y, max_y = y;
                while (stack_top > 0) {
                    int current = stack[--stack_top];
                    int cx = current % grid_width;
                    int cy = current / grid_width;
                    count++;
                    if (cx < min_x) min_x = cx;
                    if (cx > max_x) max_x = cx;
                    if (cy < min_y) min_y = cy;
                    if (cy > max_y) max_y = cy;
                    // Check 4-connected neighbors.
                    int neighbors[4][2] = {
                        {cx - 1, cy},
                        {cx + 1, cy},
                        {cx, cy - 1},
                        {cx, cy + 1}
                    };
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
                // Only consider regions with enough blocks to reduce noise.
                if (count >= MIN_BLOCKS_FOR_OBJECT) {
                    // Compute the center of the bounding box.
                    int center_grid_x = (min_x + max_x) / 2;
                    int center_grid_y = (min_y + max_y) / 2;
                    // Convert grid coordinates to pixel coordinates.
                    int center_x = center_grid_x * 2 + 1;
                    int center_y = center_grid_y;
                    Color marker = marker_colors[object_count % num_marker_colors];
                    draw_crosshair(frame, frame_width, frame_height, center_x, center_y, marker);
                    object_count++;
                }
            }
        }
    }
    free(stack);
    free(dilated_mask);

    // Update prev_frame with the unmodified current frame.
    memcpy(prev_frame, orig_frame, frame_size);

    // Debug output (optional).
    fprintf(stderr, "Motion tracking: %d object(s) detected and marked.\n", object_count);
}

/*
 * End of object_recognition.c
 *
 * The modifications above use morphological dilation and bounding box analysis to improve the detection
 * of moving objects. This results in a single crosshair marker placed at the approximate center of each object,
 * rather than multiple markers along the moving edges.
 */
