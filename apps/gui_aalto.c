/*
   Enhanced Rotating 3D Cube Animation with Realistic Enhancements
   -----------------------------------------------------------------
   This program renders a 3D rotating cube with improved realism by
   implementing the following enhancements:
   
   1. Diffuse Lighting & Face Shading:
      - Each face is filled using a simple Lambertian lighting model.
      - A fixed light direction is used to compute brightness for visible faces.
      - A scanline polygon fill algorithm fills visible faces with an intensity
        corresponding to the computed light.
   
   2. Hidden Surface Removal (Back‑Face Culling):
      - Faces not oriented toward the viewer (based on their normals) are culled.
   
   3. Anti‑Aliased Wireframe Edges:
      - Wireframe edges are drawn using an adaptation of Xiaolin Wu’s algorithm
        for anti‑aliased line drawing.
   
   4. Refined Perspective Projection:
      - The projection function uses a focal length parameter for improved realism.
   
   5. Improved Timing:
      - C11 thread sleep (thrd_sleep) is used for smoother, more accurate frame delays.
   
   6. Color Enhancements:
      - ANSI escape codes (256‑color mode) are used to set foreground colors based on
        pixel intensity, and an ASCII gradient is used to represent brightness.
   
   7. Text Overlay:
      - The text "AALTO" is overlaid onto the cube using an auxiliary overlay buffer,
        so the characters are actually printed.
   
   Design Principles:
     - Single‑file plain C (‑std=c11) using only standard cross‑platform libraries.
     - No header files; all functionality is contained in one source file.
     - Comments explain design decisions and enhance clarity.
   
   Compile with: cc -std=c11 -O2 -o cube cube.c -lm -pthread
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <threads.h>  // C11 thread support for thrd_sleep

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 24
#define FRAME_DELAY (1.0 / 15.0)
#define ASPECT_RATIO 0.5
#define CUBE_SIZE 0.9
#define FOCAL_LENGTH 55.0

// Gradient for mapping intensity (0.0 to 1.0) to an ASCII character.
// The characters progress from "dim" (space) to "bright" (@).
const char *gradient = " .:-=+*#%@";  // 10 levels (indices 0..9)

// Structure for 3D points.
typedef struct { double x, y, z; } Point3D;
// Structure for 2D (projected) points (using double for sub‑pixel accuracy).
typedef struct { double x, y; } Point2D;
// Structure representing a face defined by 4 vertex indices.
typedef struct { int v[4]; } Face;

// Cube vertices (centered at origin); cube spans from -CUBE_SIZE to +CUBE_SIZE.
Point3D cube_vertices[8] = {
    {-CUBE_SIZE, -CUBE_SIZE, -CUBE_SIZE},
    { CUBE_SIZE, -CUBE_SIZE, -CUBE_SIZE},
    { CUBE_SIZE,  CUBE_SIZE, -CUBE_SIZE},
    {-CUBE_SIZE,  CUBE_SIZE, -CUBE_SIZE},
    {-CUBE_SIZE, -CUBE_SIZE,  CUBE_SIZE},
    { CUBE_SIZE, -CUBE_SIZE,  CUBE_SIZE},
    { CUBE_SIZE,  CUBE_SIZE,  CUBE_SIZE},
    {-CUBE_SIZE,  CUBE_SIZE,  CUBE_SIZE}
};

// Cube edges for wireframe drawing.
int cube_edges[12][2] = {
    {0,1}, {1,2}, {2,3}, {3,0},
    {4,5}, {5,6}, {6,7}, {7,4},
    {0,4}, {1,5}, {2,6}, {3,7}
};

// Cube faces defined by 4 vertex indices each.
// Vertices are ordered (when viewed from outside) to enable proper normal calculation.
Face cube_faces[6] = {
    { {0,1,2,3} }, // Front face (z = -CUBE_SIZE)
    { {5,4,7,6} }, // Back face (z =  CUBE_SIZE) -- order reversed for correct normal
    { {0,4,7,3} }, // Left face (x = -CUBE_SIZE)
    { {1,2,6,5} }, // Right face (x =  CUBE_SIZE)
    { {3,7,6,2} }, // Top face (y =  CUBE_SIZE)
    { {0,1,5,4} }  // Bottom face (y = -CUBE_SIZE)
};

// Clears the terminal screen and resets cursor and attributes.
void clear_screen() {
    printf("\033[2J\033[H\033[0m");
}

// Improved frame delay using C11's thrd_sleep.
void wait_frame(double seconds) {
    struct timespec ts;
    ts.tv_sec = (time_t) seconds;
    ts.tv_nsec = (long)((seconds - ts.tv_sec) * 1e9);
    thrd_sleep(&ts, NULL);
}

// Rotates a 3D point by given angles (in radians) about the X, Y, and Z axes.
Point3D rotate_point(Point3D p, double ax, double ay, double az) {
    Point3D r = p;
    double cosx = cos(ax), sinx = sin(ax);
    double cosy = cos(ay), siny = sin(ay);
    double cosz = cos(az), sinz = sin(az);
    
    // Rotate around X-axis.
    double y = r.y * cosx - r.z * sinx;
    double z = r.y * sinx + r.z * cosx;
    r.y = y; r.z = z;
    
    // Rotate around Y-axis.
    double x = r.x * cosy + r.z * siny;
    z = -r.x * siny + r.z * cosy;
    r.x = x; r.z = z;
    
    // Rotate around Z-axis.
    x = r.x * cosz - r.y * sinz;
    y = r.x * sinz + r.y * cosz;
    r.x = x; r.y = y;
    
    return r;
}

// Projects a 3D point to 2D using perspective projection with a focal length.
Point2D project_point(Point3D p, double distance) {
    Point2D proj;
    double factor = FOCAL_LENGTH / (p.z + distance);
    proj.x = p.x * factor + SCREEN_WIDTH / 2;
    proj.y = -p.y * factor * ASPECT_RATIO + SCREEN_HEIGHT / 2;
    return proj;
}

// Returns the cross product of two 3D vectors.
Point3D cross_product(Point3D a, Point3D b) {
    Point3D r;
    r.x = a.y * b.z - a.z * b.y;
    r.y = a.z * b.x - a.x * b.z;
    r.z = a.x * b.y - a.y * b.x;
    return r;
}

// Normalizes a 3D vector.
Point3D normalize(Point3D v) {
    double mag = sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    if (mag == 0) return v;
    v.x /= mag; v.y /= mag; v.z /= mag;
    return v;
}

// Computes the dot product of two 3D vectors.
double dot_product(Point3D a, Point3D b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

// Maps an intensity value (0.0 to 1.0) to an ASCII character from the gradient.
char intensity_to_char(double intensity) {
    int levels = 9;  // indices 0 to 9 for our 10-character gradient
    int index = (int)(intensity * levels + 0.5);
    if (index < 0) index = 0;
    if (index > levels) index = levels;
    return gradient[index];
}

// Maps an intensity value (0.0 to 1.0) to an ANSI 256‑color code (232 to 255).
int intensity_to_color_code(double intensity) {
    int color = 232 + (int)(intensity * (255 - 232));
    if (color < 232) color = 232;
    if (color > 255) color = 255;
    return color;
}

// Sets a pixel in the frame buffer (if within bounds) to the given intensity
// if it is higher than the current value.
void plot_pixel(double frame[SCREEN_HEIGHT][SCREEN_WIDTH], int x, int y, double intensity) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT)
        return;
    if (intensity > frame[y][x])
        frame[y][x] = intensity;
}

// Fills a convex polygon (given as an array of projected 2D points) in the frame
// buffer using a simple scanline fill algorithm.
void fill_polygon(double frame[SCREEN_HEIGHT][SCREEN_WIDTH], Point2D pts[], int n, double intensity) {
    int min_y = SCREEN_HEIGHT, max_y = 0;
    for (int i = 0; i < n; i++) {
        int y = (int)round(pts[i].y);
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
    }
    if (min_y < 0) min_y = 0;
    if (max_y >= SCREEN_HEIGHT) max_y = SCREEN_HEIGHT - 1;
    
    for (int y = min_y; y <= max_y; y++) {
        double intersections[10];
        int count = 0;
        for (int i = 0; i < n; i++) {
            Point2D p1 = pts[i];
            Point2D p2 = pts[(i+1) % n];
            if ((p1.y < y && p2.y >= y) || (p2.y < y && p1.y >= y)) {
                double t = (y - p1.y) / (p2.y - p1.y);
                double x = p1.x + t * (p2.x - p1.x);
                intersections[count++] = x;
            }
        }
        // Sort intersections.
        for (int i = 0; i < count - 1; i++) {
            for (int j = i + 1; j < count; j++) {
                if (intersections[i] > intersections[j]) {
                    double temp = intersections[i];
                    intersections[i] = intersections[j];
                    intersections[j] = temp;
                }
            }
        }
        for (int i = 0; i < count; i += 2) {
            if (i+1 >= count) break;
            int x_start = (int)round(intersections[i]);
            int x_end = (int)round(intersections[i+1]);
            if (x_start < 0) x_start = 0;
            if (x_end >= SCREEN_WIDTH) x_end = SCREEN_WIDTH - 1;
            for (int x = x_start; x <= x_end; x++) {
                if (intensity > frame[y][x])
                    frame[y][x] = intensity;
            }
        }
    }
}

// Draws an anti-aliased line using an adaptation of Xiaolin Wu's algorithm.
// The line is drawn into the frame buffer by updating pixel intensities.
void draw_line_aa(double frame[SCREEN_HEIGHT][SCREEN_WIDTH], Point2D p0, Point2D p1) {
    int steep = fabs(p1.y - p0.y) > fabs(p1.x - p0.x);
    if (steep) {
        double temp = p0.x; p0.x = p0.y; p0.y = temp;
        temp = p1.x; p1.x = p1.y; p1.y = temp;
    }
    if (p0.x > p1.x) {
        Point2D temp = p0;
        p0 = p1;
        p1 = temp;
    }
    double dx = p1.x - p0.x;
    double dy = p1.y - p0.y;
    double gradient_val = (dx == 0) ? 1.0 : dy / dx;
    
    // Handle the first endpoint.
    double xend = round(p0.x);
    double yend = p0.y + gradient_val * (xend - p0.x);
    double xgap = 1.0 - fabs(p0.x - xend);
    double xpxl1 = xend;
    double ypxl1 = floor(yend);
    double intery = yend + gradient_val;
    
    if (steep) {
        plot_pixel(frame, (int)ypxl1, (int)xpxl1, (1.0 - (yend - floor(yend))) * xgap);
        plot_pixel(frame, (int)ypxl1 + 1, (int)xpxl1, (yend - floor(yend)) * xgap);
    } else {
        plot_pixel(frame, (int)xpxl1, (int)ypxl1, (1.0 - (yend - floor(yend))) * xgap);
        plot_pixel(frame, (int)xpxl1, (int)ypxl1 + 1, (yend - floor(yend)) * xgap);
    }
    
    // Handle the second endpoint.
    xend = round(p1.x);
    yend = p1.y + gradient_val * (xend - p1.x);
    xgap = fabs(p1.x - round(p1.x));
    double xpxl2 = xend;
    double ypxl2 = floor(yend);
    
    if (steep) {
        plot_pixel(frame, (int)ypxl2, (int)xpxl2, (1.0 - (yend - floor(yend))) * xgap);
        plot_pixel(frame, (int)ypxl2 + 1, (int)xpxl2, (yend - floor(yend)) * xgap);
    } else {
        plot_pixel(frame, (int)xpxl2, (int)ypxl2, (1.0 - (yend - floor(yend))) * xgap);
        plot_pixel(frame, (int)xpxl2, (int)ypxl2 + 1, (yend - floor(yend)) * xgap);
    }
    
    // Main loop.
    if (steep) {
        for (int x = (int)(xpxl1 + 1); x < (int)xpxl2; x++) {
            int y = (int)floor(intery);
            double frac = intery - floor(intery);
            plot_pixel(frame, y, x, 1.0 - frac);
            plot_pixel(frame, y + 1, x, frac);
            intery += gradient_val;
        }
    } else {
        for (int x = (int)(xpxl1 + 1); x < (int)xpxl2; x++) {
            int y = (int)floor(intery);
            double frac = intery - floor(intery);
            plot_pixel(frame, x, y, 1.0 - frac);
            plot_pixel(frame, x, y + 1, frac);
            intery += gradient_val;
        }
    }
}

int main(void) {
    // Frame buffer: each pixel holds a brightness intensity in the range [0, 1].
    double frame[SCREEN_HEIGHT][SCREEN_WIDTH];
    // Overlay buffer for text; initialized with '\0'.
    char overlay[SCREEN_HEIGHT][SCREEN_WIDTH+1];
    
    double angle_x = 0, angle_y = 0, angle_z = 0;
    double distance = 4.0;  // Distance from viewer to cube
    
    while (1) {
        // Initialize frame and overlay buffers.
        for (int i = 0; i < SCREEN_HEIGHT; i++) {
            for (int j = 0; j < SCREEN_WIDTH; j++) {
                frame[i][j] = 0.0;
                overlay[i][j] = '\0';
            }
            overlay[i][SCREEN_WIDTH] = '\0';
        }
        
        // Rotate and project all cube vertices.
        Point3D rotated[8];
        Point2D projected[8];
        for (int i = 0; i < 8; i++) {
            rotated[i] = rotate_point(cube_vertices[i], angle_x, angle_y, angle_z);
            projected[i] = project_point(rotated[i], distance);
        }
        
        // Fill visible faces with shading.
        // Define a fixed light direction and normalize it.
        Point3D light_dir = {0.5, 0.5, -1.0};
        light_dir = normalize(light_dir);
        
        for (int f = 0; f < 6; f++) {
            Point3D v[4];
            Point2D proj[4];
            for (int i = 0; i < 4; i++) {
                int idx = cube_faces[f].v[i];
                v[i] = rotated[idx];
                proj[i] = projected[idx];
            }
            // Compute face normal using the cross product of two edges.
            Point3D edge1 = { v[1].x - v[0].x, v[1].y - v[0].y, v[1].z - v[0].z };
            Point3D edge2 = { v[2].x - v[0].x, v[2].y - v[0].y, v[2].z - v[0].z };
            Point3D normal = cross_product(edge1, edge2);
            normal = normalize(normal);
            
            // Back-face culling: skip face if its normal is not facing the camera.
            // In this projection, if normal.z >= 0 the face is culled.
            if (normal.z >= 0)
                continue;
            
            // Compute light intensity using Lambert's cosine law.
            double intensity = dot_product(normal, light_dir);
            if (intensity < 0) intensity = 0;
            
            // Fill the face polygon with the computed intensity.
            fill_polygon(frame, proj, 4, intensity);
        }
        
        // Draw anti-aliased wireframe edges on top of the face shading.
        for (int i = 0; i < 12; i++) {
            int v0 = cube_edges[i][0];
            int v1 = cube_edges[i][1];
            draw_line_aa(frame, projected[v0], projected[v1]);
        }
        
        // Overlay "AALTO" text at the cube's center.
        // The variable "text" is now used to fill the overlay buffer.
        const char *text = "AALTO";
        int text_len = 5;  // Length of "AALTO"
        int text_start = SCREEN_WIDTH / 2 - text_len / 2;
        int text_row = SCREEN_HEIGHT / 2;
        for (int k = 0; k < text_len; k++) {
            int x = text_start + k;
            if (x >= 0 && x < SCREEN_WIDTH && text_row >= 0 && text_row < SCREEN_HEIGHT)
                overlay[text_row][x] = text[k];
        }
        
        // Render the frame buffer to the terminal.
        clear_screen();
        for (int i = 0; i < SCREEN_HEIGHT; i++) {
            for (int j = 0; j < SCREEN_WIDTH; j++) {
                // If an overlay character is set, print it directly.
                if (overlay[i][j] != '\0') {
                    // Use bright white for overlay text.
                    printf("\033[38;5;15m%c", overlay[i][j]);
                } else {
                    double intensity = frame[i][j];
                    char ch = intensity_to_char(intensity);
                    int color_code = intensity_to_color_code(intensity);
                    // Print each pixel with its corresponding ANSI color.
                    printf("\033[38;5;%dm%c", color_code, ch);
                }
            }
            // Reset colors at the end of each line.
            printf("\033[0m\n");
        }
        fflush(stdout);
        
        // Update rotation angles for animation.
        angle_x += 0.03;
        angle_y += 0.02;
        angle_z += 0.04;
        
        // Wait for the next frame.
        wait_frame(FRAME_DELAY);
    }
    
    return 0;
}
