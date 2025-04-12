// Compile using: gcc -std=c11 -D_POSIX_C_SOURCE=200112L -o earth earth.c
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

// Declare POSIX functions if not provided in time.h.
extern int setenv(const char *name, const char *value, int overwrite);
extern int unsetenv(const char *name);
extern void tzset(void);
extern struct tm *localtime_r(const time_t *timep, struct tm *result);
extern struct tm *gmtime_r(const time_t *timep, struct tm *result);

// Define M_PI if not provided.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Canvas dimensions and drawing tolerance.
#define WIDTH 80
#define HEIGHT 40
#define TOLERANCE 0.04

// Convert degrees to radians.
double deg2rad(double deg) {
    return deg * (M_PI / 180.0);
}

// Place a single character on the canvas if within bounds.
void place_marker(char canvas[HEIGHT][WIDTH+1], int x, int y, char ch) {
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
        canvas[y][x] = ch;
}

// Write a null-terminated text string into the canvas starting at (x,y).
void write_text(char canvas[HEIGHT][WIDTH+1], int x, int y, const char *text) {
    if (y < 0 || y >= HEIGHT)
        return;
    for (int i = 0; text[i] != '\0' && (x + i) < WIDTH; i++) {
        canvas[y][x + i] = text[i];
    }
}

// Helper function that mimics timegm() by temporarily forcing TZ to UTC.
// Note: Not thread-safe.
time_t my_timegm(struct tm *tm) {
    time_t ret;
    char *old_tz = getenv("TZ");

    setenv("TZ", "UTC0", 1);
    tzset();

    ret = mktime(tm);

    if (old_tz)
        setenv("TZ", old_tz, 1);
    else
        unsetenv("TZ");
    tzset();

    return ret;
}

// Calculate the sun's angle based on current local time.
// Midnight (0 secs) corresponds to 0 deg, 86400 secs (full day) to 360 deg.
double calculate_sun_angle(void) {
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    int seconds_since_midnight = lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec;
    return (seconds_since_midnight / 86400.0) * 360.0;
}

// Calculate the moon's angle based on a reference new moon.
// Returns an angle in degrees (0 deg to 360 deg).
double calculate_moon_angle(void) {
    struct tm ref = {0};
    ref.tm_year = 2000 - 1900;
    ref.tm_mon = 0;      // January (0-based)
    ref.tm_mday = 6;
    ref.tm_hour = 18;
    ref.tm_min = 14;
    ref.tm_sec = 0;
    time_t ref_time = my_timegm(&ref);
    time_t now = time(NULL);
    double diff_seconds = difftime(now, ref_time);
    double lunar_period = 29.53 * 86400.0;
    double phase = fmod(diff_seconds, lunar_period) / lunar_period;
    return phase * 360.0;
}

int main(int argc, char *argv[]) {
    double sun_angle, moon_angle;
    // Use command-line angles if provided; otherwise, compute in real time.
    if (argc > 1) {
        sun_angle = 45.0;
        moon_angle = 225.0;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-sun") == 0 && i + 1 < argc)
                sun_angle = atof(argv[++i]);
            else if (strcmp(argv[i], "-moon") == 0 && i + 1 < argc)
                moon_angle = atof(argv[++i]);
        }
    } else {
        sun_angle = calculate_sun_angle();
        moon_angle = calculate_moon_angle();
    }

    // Prepare the ASCII canvas.
    char canvas[HEIGHT][WIDTH+1];
    for (int y = 0; y < HEIGHT; y++) {
        memset(canvas[y], ' ', WIDTH);
        canvas[y][WIDTH] = '\0';
    }

    // Ellipse (Earth) parameters.
    int cx = WIDTH / 2;
    int cy = HEIGHT / 2;
    int rx = (WIDTH / 2) - 2;
    int ry = (HEIGHT / 2) - 2;

    // Draw the Earth's outline (an ellipse adjusted for character proportions).
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            double dx = ((double)(x - cx)) / rx;
            double dy = ((double)(y - cy)) / ry;
            double dist = dx * dx + dy * dy;
            if (fabs(dist - 1.0) < TOLERANCE)
                canvas[y][x] = '.';
        }
    }

    // Define city markers with approximate angular positions.
    // Note: New Delhi marker changed from 'N' to 'D' to avoid conflict with the north indicator.
    struct {
        char mark;
        double angle;
    } cities[] = {
        {'H', 25.0},    // Helsinki ~25 deg East
        {'T', 139.0},   // Tokyo ~139 deg East
        {'L', 245.0},   // Las Vegas ~245 deg (115 deg West)
        {'D', 77.0},    // New Delhi (using D for Delhi) ~77 deg East
        {'C', 18.0},    // Cape Town ~18 deg East
        {'S', 151.0},   // Sydney ~151 deg East
        {'B', 100.0}    // Bangkok ~100 deg East
    };
    const int num_cities = sizeof(cities) / sizeof(cities[0]);
    // Corresponding city names.
    const char *city_names[] = {
        "Helsinki",
        "Tokyo",
        "Las Vegas",
        "New Delhi",
        "Cape Town",
        "Sydney",
        "Bangkok"
    };
    int marker_x, marker_y;
    double rad;

    // Place city markers on the ellipse.
    for (int i = 0; i < num_cities; i++) {
        rad = deg2rad(cities[i].angle);
        marker_x = cx + (int)round(rx * sin(rad));
        marker_y = cy - (int)round(ry * cos(rad));
        place_marker(canvas, marker_x, marker_y, cities[i].mark);
    }

    // Offset so the Sun and Moon appear slightly above the surface.
    double marker_offset = 1.05;
    // Place the Sun marker ('@').
    rad = deg2rad(sun_angle);
    marker_x = cx + (int)round(rx * marker_offset * sin(rad));
    marker_y = cy - (int)round(ry * marker_offset * cos(rad));
    place_marker(canvas, marker_x, marker_y, '@');
    // Place the Moon marker ('*').
    rad = deg2rad(moon_angle);
    marker_x = cx + (int)round(rx * marker_offset * sin(rad));
    marker_y = cy - (int)round(ry * marker_offset * cos(rad));
    place_marker(canvas, marker_x, marker_y, '*');

    // Use '^' as the north indicator.
    if (cy - ry - 1 >= 0)
        place_marker(canvas, cx, cy - ry - 1, '^');

    // Gather extra statistics.
    time_t now = time(NULL);
    struct tm local_tm, utc_tm;
    localtime_r(&now, &local_tm);
    gmtime_r(&now, &utc_tm);
    
    char local_date[16], local_time[16], utc_time[16];
    strftime(local_date, sizeof(local_date), "%F", &local_tm);
    strftime(local_time, sizeof(local_time), "%T", &local_tm);
    strftime(utc_time, sizeof(utc_time), "%T", &utc_tm);

    // Compute the moon phase fraction.
    struct tm ref = {0};
    ref.tm_year = 2000 - 1900;
    ref.tm_mon = 0;
    ref.tm_mday = 6;
    ref.tm_hour = 18;
    ref.tm_min = 14;
    ref.tm_sec = 0;
    time_t ref_time = my_timegm(&ref);
    double diff_seconds = difftime(now, ref_time);
    double lunar_period = 29.53 * 86400.0;
    double moon_phase_fraction = fmod(diff_seconds, lunar_period) / lunar_period;
    int illum_percent = (moon_phase_fraction <= 0.5) ? (int)(moon_phase_fraction * 2 * 100)
                                                    : (int)((1 - moon_phase_fraction) * 2 * 100);
    const char *phase_desc;
    if (moon_phase_fraction < 0.05 || moon_phase_fraction > 0.95)
        phase_desc = "New Moon";
    else if (fabs(moon_phase_fraction - 0.5) < 0.05)
        phase_desc = "Full Moon";
    else
        phase_desc = (moon_phase_fraction < 0.5) ? "Waxing" : "Waning";

    // Block 1: City descriptions with angles.
    // Format: "H: Helsinki (25.0 deg)"
    char city_info[num_cities][50];
    for (int i = 0; i < num_cities; i++) {
        snprintf(city_info[i], sizeof(city_info[i]), "%c: %s (%.1f deg)", 
                 cities[i].mark, city_names[i], cities[i].angle);
    }
    int block1_lines = num_cities;
    int desc1_x = cx - rx / 2;   // Left section of the ellipse.
    int desc1_y = cy - block1_lines / 2;
    for (int i = 0; i < block1_lines; i++) {
        write_text(canvas, desc1_x-3, desc1_y + i, city_info[i]);
    }

    // Block 2: Additional statistics.
    char info[6][40];
    snprintf(info[0], sizeof(info[0]), "Local Date: %s", local_date);
    snprintf(info[1], sizeof(info[1]), "Local Time: %s", local_time);
    snprintf(info[2], sizeof(info[2]), "UTC Time:   %s", utc_time);
    snprintf(info[3], sizeof(info[3]), "Sun Angle:  %.1f deg", sun_angle);
    snprintf(info[4], sizeof(info[4]), "Moon Angle: %.1f deg", moon_angle);
    snprintf(info[5], sizeof(info[5]), "Moon: %s, %d%%", phase_desc, illum_percent);
    int block2_lines = 6;
    int desc2_x = cx + rx / 8;   // Right section inside the Earth.
    int desc2_y = cy - block2_lines / 2;
    for (int i = 0; i < block2_lines; i++) {
        write_text(canvas, desc2_x, desc2_y + i, info[i]);
    }

    // Block 3: GPS (Bearing) Markers along an inner circle.
    // Instead of clock hours, we now display directional bearings (in 30 deg increments)
    // using a three-digit format. For example: "000", "030", "060", ..., "330".
    double inner_factor = 0.75;  // Scale factor for inner markers.
    char bearing[4];  // To hold a three-digit number plus null terminator.
    for (int i = 0; i < 12; i++) {
        int degrees = i * 30;
        snprintf(bearing, sizeof(bearing), "%03d", degrees);
        double angle = deg2rad(degrees);
        int tick_x = cx + (int)round(rx * inner_factor * sin(angle));
        int tick_y = cy - (int)round(ry * inner_factor * cos(angle));
        // Adjust horizontal position to center the text label.
        int len = (int)strlen(bearing);
        tick_x -= len / 2;
        write_text(canvas, tick_x, tick_y, bearing);
    }

    // Finally, print the canvas.
    for (int y = 0; y < HEIGHT; y++) {
        puts(canvas[y]);
    }

    return 0;
}
