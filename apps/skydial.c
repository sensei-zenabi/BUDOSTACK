/*
This improved sky-dial application calculates and displays in the terminal 
the apparent positions (azimuth and altitude) of the Sun and Moon along with 
other metadata including the Moon's illuminated percentage. The default observer 
location is Jyväskylä, Finland (latitude 62.2426° N, longitude 25.7473° E), but a 
user may provide alternate coordinates via command-line parameters.

Compile with:
    cc -std=c11 -D_POSIX_C_SOURCE=200112L -lm -o skydial skydial.c

Usage:
    ./skydial
    ./skydial <lat> <lon>
    
The program uses simple approximations for the Sun and Moon positions. The sky-dial 
is rendered as a single ASCII frame with improved details including a refined circle 
outline and internal cross lines for better orientation.
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>

#define PI 3.14159265358979323846

// Canvas dimensions for the sky-dial.
#define WIDTH 41
#define HEIGHT 21

// Default observer location (Jyväskylä, Finland)
#define DEFAULT_LAT 62.2426   // degrees
#define DEFAULT_LON 25.7473   // degrees

// Tolerance for drawing the circle perimeter.
// Smaller tolerance makes the circle outline thinner.
#define CIRCLE_TOLERANCE 0.04

// Function prototypes.
double deg2rad(double deg);
double rad2deg(double rad);
double normalize_angle(double angle);
double julian_date(struct tm *t);
void calc_sun(double jd, double *ra, double *dec);
void calc_moon(double jd, double *ra, double *dec);
double calc_moon_phase(double jd);
void equatorial_to_horizontal(double ra, double dec, double lat, double lon, double jd, double *az, double *alt);
static void plot_object_on_canvas(char canvas[HEIGHT][WIDTH+1], int center_x, int center_y,
                                  int radius_x, int radius_y, double az, double alt, char symbol);
void draw_skydial(double sun_az, double sun_alt, double moon_az, double moon_alt);

// Convert degrees to radians.
double deg2rad(double deg) {
    return deg * PI / 180.0;
}

// Convert radians to degrees.
double rad2deg(double rad) {
    return rad * 180.0 / PI;
}

// Normalize angle to [0, 360).
double normalize_angle(double angle) {
    double result = fmod(angle, 360.0);
    if (result < 0)
        result += 360.0;
    return result;
}

// Compute Julian Date from a UTC time structure.
double julian_date(struct tm *t) {
    int year = t->tm_year + 1900;
    int month = t->tm_mon + 1;
    int day = t->tm_mday;
    int hour = t->tm_hour;
    int minute = t->tm_min;
    int second = t->tm_sec;

    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    int A = year / 100;
    int B = 2 - A + (A / 4);
    double day_fraction = (hour + minute / 60.0 + second / 3600.0) / 24.0;
    double JD = floor(365.25 * (year + 4716)) +
                floor(30.6001 * (month + 1)) +
                day + day_fraction + B - 1524.5;
    return JD;
}

/*
Approximate apparent right ascension (ra) and declination (dec) of the Sun in degrees.
Uses simplified formulas.
*/
void calc_sun(double jd, double *ra, double *dec) {
    double d = jd - 2451545.0;  // days since J2000

    double M = normalize_angle(357.529 + 0.98560028 * d); // mean anomaly in degrees
    double L = normalize_angle(280.459 + 0.98564736 * d);  // mean longitude
    double lambda = L + 1.915 * sin(deg2rad(M)) + 0.020 * sin(deg2rad(2 * M));

    double epsilon = 23.439 - 0.00000036 * d; // obliquity of the ecliptic
    double lambda_rad = deg2rad(lambda);
    double epsilon_rad = deg2rad(epsilon);

    // Compute right ascension and declination in radians.
    double ra_rad = atan2(cos(epsilon_rad) * sin(lambda_rad), cos(lambda_rad));
    double dec_rad = asin(sin(epsilon_rad) * sin(lambda_rad));

    *ra = normalize_angle(rad2deg(ra_rad));
    *dec = rad2deg(dec_rad);
}

/*
Approximate apparent right ascension (ra) and declination (dec) of the Moon in degrees.
Uses a simplified and low-accuracy algorithm.
*/
void calc_moon(double jd, double *ra, double *dec) {
    double d = jd - 2451545.0;

    double L0 = normalize_angle(218.316 + 13.176396 * d); // Moon's mean longitude
    double M_moon = normalize_angle(134.963 + 13.064993 * d); // Moon's mean anomaly
    double L = L0 + 6.289 * sin(deg2rad(M_moon));
    
    double epsilon = 23.439 - 0.00000036 * d; 
    double L_rad = deg2rad(L);
    double epsilon_rad = deg2rad(epsilon);

    double ra_rad = atan2(cos(epsilon_rad) * sin(L_rad), cos(L_rad));
    double dec_rad = asin(sin(epsilon_rad) * sin(L_rad));

    *ra = normalize_angle(rad2deg(ra_rad));
    *dec = rad2deg(dec_rad);
}

/*
Compute the fraction of the Moon's disc that is illuminated based on the difference 
between the Moon's and Sun's ecliptic longitudes.
This simplified algorithm calculates the phase angle (difference in degrees) and then 
computes the fraction as: (1 - cos(phase_angle))/2.
For example:
    New moon: phase_angle = 0 => 0% illumination
    Full moon: phase_angle = 180 => 100% illumination
*/
double calc_moon_phase(double jd) {
    double d = jd - 2451545.0;
    double M = normalize_angle(357.529 + 0.98560028 * d);
    double L = normalize_angle(280.459 + 0.98564736 * d);
    // Sun's ecliptic longitude.
    double lambda_sun = L + 1.915 * sin(deg2rad(M)) + 0.020 * sin(deg2rad(2 * M));
    
    double L0 = normalize_angle(218.316 + 13.176396 * d);
    double M_moon = normalize_angle(134.963 + 13.064993 * d);
    // Moon's ecliptic longitude.
    double lambda_moon = L0 + 6.289 * sin(deg2rad(M_moon));
    
    double diff = fabs(normalize_angle(lambda_moon - lambda_sun));
    if (diff > 180.0)
        diff = 360.0 - diff;
    
    double phase = (1 - cos(deg2rad(diff))) / 2.0;
    return phase;
}

/*
Convert equatorial coordinates (ra, dec in degrees) to horizontal coordinates
(azimuth and altitude in degrees) for a given observer location and time.
*/
void equatorial_to_horizontal(double ra, double dec, double lat, double lon, double jd, double *az, double *alt) {
    double ra_rad = deg2rad(ra);
    double dec_rad = deg2rad(dec);
    double lat_rad = deg2rad(lat);

    double d = jd - 2451545.0;
    double GMST = normalize_angle(280.46061837 + 360.98564736629 * d);
    double LST = normalize_angle(GMST + lon);
    double LST_rad = deg2rad(LST);

    double HA = LST_rad - ra_rad;
    if (HA < -PI)
        HA += 2 * PI;
    if (HA > PI)
        HA -= 2 * PI;

    double sin_alt = sin(dec_rad) * sin(lat_rad) + cos(dec_rad) * cos(lat_rad) * cos(HA);
    double altitude_rad = asin(sin_alt);

    double cos_az = (sin(dec_rad) - sin(altitude_rad) * sin(lat_rad)) / (cos(altitude_rad) * cos(lat_rad));
    if (cos_az > 1.0) cos_az = 1.0;
    if (cos_az < -1.0) cos_az = -1.0;
    double azimuth_rad = acos(cos_az);
    if (sin(HA) > 0)
        azimuth_rad = 2 * PI - azimuth_rad;

    *az = normalize_angle(rad2deg(azimuth_rad));
    *alt = rad2deg(altitude_rad);
}

/*
Plot a celestial object (if above horizon) onto the ASCII canvas.
The marker is placed by converting its azimuth and altitude to a position within the dial.
*/
static void plot_object_on_canvas(char canvas[HEIGHT][WIDTH+1], int center_x, int center_y,
                                  int radius_x, int radius_y, double az, double alt, char symbol)
{
    // Only plot objects above the horizon.
    if (alt < 0)
        return;
    // Map altitude (0 at horizon, 90 at zenith) to a normalized radius (0 at zenith, 1 at horizon).
    double norm_radius = (90.0 - alt) / 90.0;
    double az_rad = deg2rad(az);
    // Compute offsets taking terminal aspect ratio into account.
    double dx = norm_radius * radius_x * sin(az_rad);
    double dy = norm_radius * radius_y * cos(az_rad);
    int plot_x = center_x + (int)round(dx);
    int plot_y = center_y - (int)round(dy);
    if (plot_x >= 0 && plot_x < WIDTH && plot_y >= 0 && plot_y < HEIGHT)
        canvas[plot_y][plot_x] = symbol;
}

/*
Draw the sky-dial: a circular dial with an improved outline and internal cross axes.
The dial edge, cross lines and cardinal directions help the viewer orient the plot.
Celestial objects (Moon and Sun) are then overlayed.
*/
void draw_skydial(double sun_az, double sun_alt, double moon_az, double moon_alt) {
    char canvas[HEIGHT][WIDTH+1];
    for (int y = 0; y < HEIGHT; y++) {
        memset(canvas[y], ' ', WIDTH);
        canvas[y][WIDTH] = '\0';
    }

    int center_x = WIDTH / 2;
    int center_y = HEIGHT / 2;
    int radius_x = (WIDTH - 2) / 2;
    int radius_y = (HEIGHT - 2) / 2;

    // Draw the circular dial outline using '*' characters.
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            double dx = (double)(x - center_x) / radius_x;
            double dy = (double)(y - center_y) / radius_y;
            double distance = sqrt(dx * dx + dy * dy);
            if (fabs(distance - 1.0) < CIRCLE_TOLERANCE)
                canvas[y][x] = '*';
        }
    }

    // Draw internal cross axes for better orientation.
    // Vertical axis.
    for (int y = 0; y < HEIGHT; y++) {
        double dy = (double)(y - center_y) / radius_y;
        if (fabs(dy) < 1.0)
            canvas[y][center_x] = '|';
    }
    // Horizontal axis.
    for (int x = 0; x < WIDTH; x++) {
        double dx = (double)(x - center_x) / radius_x;
        if (fabs(dx) < 1.0)
            canvas[center_y][x] = '-';
    }
    // Mark the very center with a '+'.
    canvas[center_y][center_x] = '+';

    // Overlay compass directions to override cross axes.
    if (center_y - radius_y >= 0 && center_x < WIDTH)
        canvas[center_y - radius_y][center_x] = 'N';
    if (center_y + radius_y < HEIGHT && center_x < WIDTH)
        canvas[center_y + radius_y][center_x] = 'S';
    if (center_x + radius_x < WIDTH && center_y < HEIGHT)
        canvas[center_y][center_x + radius_x] = 'E';
    if (center_x - radius_x >= 0 && center_y < HEIGHT)
        canvas[center_y][center_x - radius_x] = 'W';

    // Plot celestial markers. Moon is plotted first so that the Sun (if visible) appears on top.
    plot_object_on_canvas(canvas, center_x, center_y, radius_x, radius_y, moon_az, moon_alt, 'M');
    plot_object_on_canvas(canvas, center_x, center_y, radius_x, radius_y, sun_az, sun_alt, 'S');

    // Output the dial.
    for (int i = 0; i < HEIGHT; i++) {
        printf("%s\n", canvas[i]);
    }
}

int main(int argc, char **argv) {
    double lat = DEFAULT_LAT;
    double lon = DEFAULT_LON;

    if (argc == 3) {
        lat = atof(argv[1]);
        lon = atof(argv[2]);
    } else if (argc != 1) {
        fprintf(stderr, "Usage: %s [lat lon]\n", argv[0]);
        return EXIT_FAILURE;
    }

    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    if (!utc) {
        perror("gmtime");
        return EXIT_FAILURE;
    }

    double jd = julian_date(utc);

    double sun_ra, sun_dec;
    calc_sun(jd, &sun_ra, &sun_dec);

    double moon_ra, moon_dec;
    calc_moon(jd, &moon_ra, &moon_dec);

    double sun_az, sun_alt;
    equatorial_to_horizontal(sun_ra, sun_dec, lat, lon, jd, &sun_az, &sun_alt);

    double moon_az, moon_alt;
    equatorial_to_horizontal(moon_ra, moon_dec, lat, lon, jd, &moon_az, &moon_alt);

    // Compute the illuminated fraction of the Moon.
    double moon_phase = calc_moon_phase(jd);

    // Clear the terminal screen.
    printf("\033[2J\033[H");

    // Display header metadata and computed data.
    printf("Sky-Dial: Celestial Positions\n");
    printf("UTC Time: %04d-%02d-%02d %02d:%02d:%02d\n",
           utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
           utc->tm_hour, utc->tm_min, utc->tm_sec);
    printf("Location: lat %.4f°, lon %.4f°\n", lat, lon);
    printf("\n");
    printf("Computed Positions (Horizontal Coordinates):\n");
    printf(" Sun:  Azimuth = %.2f°, Altitude = %.2f°\n", sun_az, sun_alt);
    printf(" Moon: Azimuth = %.2f°, Altitude = %.2f°\n", moon_az, moon_alt);
    printf("\n");
    printf("Moon Illumination: %.2f%%\n", moon_phase * 100);
    printf("\n");

    draw_skydial(sun_az, sun_alt, moon_az, moon_alt);

    printf("\nLegend:\n");
    printf("  N, E, S, W  - Compass directions (dial edge)\n");
    printf("  *           - Dial perimeter (horizon)\n");
    printf("  |, -        - Internal cross axes (azimuth directions)\n");
    printf("  +           - Zenith (center)\n");
    printf("  S           - Sun (if above horizon)\n");
    printf("  M           - Moon (if above horizon)\n");
    printf("  (Objects below horizon are not displayed.)\n");

    return EXIT_SUCCESS;
}
