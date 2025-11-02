// Define POSIX version for system calls
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

// If M_PI is not defined by math.h, define it.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Constant: Unix time for the J2000 epoch (2000 Jan 1 12:00:00 UT)
const time_t J2000 = 946728000;

// Conversion factor from kilometers to astronomical units (IAU 2012 value)
static const double KM_PER_AU = 149597870.7;

// Structure to hold planetary orbital parameters:
// a  : semi-major axis in astronomical units (AU)
// e  : orbital eccentricity
// T  : orbital period in days
// M0 : mean anomaly at J2000 (in radians)
typedef struct {
    char symbol;        // Character symbol for the planet
    const char *name;   // Planet name for table output
    const char *command;// Horizons COMMAND identifier
    double a;           // Semi-major axis (AU)
    double e;           // Eccentricity
    double T;           // Orbital period in days
    double M0;          // Mean anomaly at J2000 (radians)
} Planet;

typedef struct {
    double x_au;
    double y_au;
    double z_au;
    double distance_au;
    double true_anomaly_deg;
    int valid;
} PlanetEphemeris;

static double solveKepler(double M, double e);

// Eight planets (Mercury, Venus, Earth, Mars, Jupiter, Saturn, Uranus, Neptune)
// Note: The orbital elements are approximated for a simple realistic model.
#define NUM_PLANETS 8
Planet planets[NUM_PLANETS] = {
    //   symbol, name,       COMMAND, a (AU),    e,         T (days),   M0 (rad)
    { 'm', "Mercury", "199", 0.387, 0.2056,   87.969,  3.049  },  // Mercury (lowercase 'm')
    { 'V', "Venus",   "299", 0.723, 0.0068,  224.701,  0.875  },  // Venus
    { 'E', "Earth",   "399", 1.000, 0.0167,  365.256,  6.240  },  // Earth
    { 'M', "Mars",    "499", 1.524, 0.0934,  686.980,  0.338  },  // Mars (uppercase 'M')
    { 'J', "Jupiter", "599", 5.203, 0.0484, 4332.59,  0.349  },  // Jupiter
    { 'S', "Saturn",  "699", 9.537, 0.0542,10759.22,  5.534  },  // Saturn
    { 'U', "Uranus",  "799",19.191, 0.0472,30685.4,  2.482  },  // Uranus
    { 'N', "Neptune", "899",30.070, 0.0086,60190.0,  4.471  }   // Neptune
};

// Populate the ephemeris data with fallback analytic values using Kepler's equation.
static void populate_fallback_ephemeris(const Planet *planet, double days_since_J2000,
                                        PlanetEphemeris *ephem)
{
    double M = planet->M0 + (2 * M_PI / planet->T) * days_since_J2000;
    M = fmod(M, 2 * M_PI);
    if (M < 0) {
        M += 2 * M_PI;
    }

    double E = solveKepler(M, planet->e);
    double f_angle = 2 * atan2(sqrt(1 + planet->e) * sin(E / 2),
                               sqrt(1 - planet->e) * cos(E / 2));
    double r = planet->a * (1 - planet->e * cos(E));

    ephem->x_au = r * cos(f_angle);
    ephem->y_au = r * sin(f_angle);
    ephem->z_au = 0.0;
    ephem->distance_au = r;
    ephem->true_anomaly_deg = f_angle * 180.0 / M_PI;
    if (ephem->true_anomaly_deg < 0.0) {
        ephem->true_anomaly_deg += 360.0;
    }
    ephem->valid = 0;
}

// Attempt to fetch heliocentric state vectors from the Horizons API.
static int fetch_ephemeris_from_horizons(const Planet *planet, double julian_date,
                                         PlanetEphemeris *ephem)
{
    char command[512];
    int written = snprintf(command, sizeof(command),
                           "curl -s 'https://ssd.jpl.nasa.gov/api/horizons.api?format=text&"
                           "COMMAND=%s&OBJ_DATA=NO&MAKE_EPHEM=YES&EPHEM_TYPE=VECTORS&CENTER=500@0&TLIST=%.6f'",
                           planet->command, julian_date);
    if (written < 0 || (size_t)written >= sizeof(command)) {
        return -1;
    }

    FILE *fp = popen(command, "r");
    if (!fp) {
        return -1;
    }

    char line[1024];
    double x_km = 0.0;
    double y_km = 0.0;
    double z_km = 0.0;
    int found_x = 0;
    int found_y = 0;
    int found_z = 0;

    while (fgets(line, sizeof(line), fp)) {
        char *location;
        if (!found_x && (location = strstr(line, " X =")) != NULL) {
            x_km = strtod(location + 4, NULL);
            found_x = 1;
        } else if (!found_y && (location = strstr(line, " Y =")) != NULL) {
            y_km = strtod(location + 4, NULL);
            found_y = 1;
        } else if (!found_z && (location = strstr(line, " Z =")) != NULL) {
            z_km = strtod(location + 4, NULL);
            found_z = 1;
        }

        if (found_x && found_y && found_z) {
            break;
        }
    }

    int status = pclose(fp);
    if (!found_x || !found_y || !found_z || status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }

    ephem->x_au = x_km / KM_PER_AU;
    ephem->y_au = y_km / KM_PER_AU;
    ephem->z_au = z_km / KM_PER_AU;
    ephem->distance_au = sqrt(ephem->x_au * ephem->x_au +
                              ephem->y_au * ephem->y_au +
                              ephem->z_au * ephem->z_au);
    double angle = atan2(ephem->y_au, ephem->x_au);
    if (angle < 0.0) {
        angle += 2 * M_PI;
    }
    ephem->true_anomaly_deg = angle * 180.0 / M_PI;
    ephem->valid = 1;
    return 0;
}

// Function to solve Kepler's equation M = E - e*sin(E) using Newton-Raphson iteration.
// Returns the eccentric anomaly E given the mean anomaly M and eccentricity e.
static double solveKepler(double M, double e) {
    double E = M; // initial guess
    for (int i = 0; i < 10; i++) {
        double f = E - e * sin(E) - M;
        double fprime = 1 - e * cos(E);
        double delta = f / fprime;
        E -= delta;
        if (fabs(delta) < 1e-6)
            break;
    }
    return E;
}

int main(int argc, char *argv[]) {
    // Parse command-line argument for number of orbits to visualize.
    // Minimum 2, maximum NUM_PLANETS (8). Default is 8 if no valid argument is given.
    int num_orbits = NUM_PLANETS;
    if (argc >= 2) {
        int temp = atoi(argv[1]);
        if (temp < 2)
            num_orbits = 2;
        else if (temp > NUM_PLANETS)
            num_orbits = NUM_PLANETS;
        else
            num_orbits = temp;
    }
    
    // Set the number of planets to display as the number of orbits.
    int display_planet_count = num_orbits;
    // Set max_au based on the outermost displayed planet plus a little margin.
    double max_au = planets[display_planet_count - 1].a * 1.2;
    
    // Dynamically obtain the terminal size.
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        ws.ws_col = 80; // fallback default width
    }
    int width = ws.ws_col;
    if (width <= 0)
        width = 80;
    // Maintain the previous choice of making the drawing area half as tall as it
    // is wide, but use a square scaling factor so that an 8x8 font keeps the
    // orbits circular.
    int height = width / 2;
    if (height <= 0)
        height = 40;

    double usable_span = fmin(width, height) - 2.0;
    if (usable_span < 2.0)
        usable_span = 2.0;
    double scale = (usable_span / 2.0) / max_au;
    double center_x = (width - 1) / 2.0;
    double center_y = (height - 1) / 2.0;
    int center_ix = (int)lround(center_x);
    int center_iy = (int)lround(center_y);
    
    // Get the current system time and compute elapsed days since J2000.
    time_t now = time(NULL);
    char iso_time[64] = "unavailable";
    struct tm tm_utc;
    if (gmtime_r(&now, &tm_utc) != NULL) {
        if (strftime(iso_time, sizeof(iso_time), "%Y-%m-%dT%H:%M:%SZ", &tm_utc) == 0) {
            strncpy(iso_time, "unavailable", sizeof(iso_time) - 1);
            iso_time[sizeof(iso_time) - 1] = '\0';
        }
    }
    double days_since_J2000 = difftime(now, J2000) / 86400.0;
    double julian_date = (double)now / 86400.0 + 2440587.5;

    PlanetEphemeris ephemerides[NUM_PLANETS];
    for (int p = 0; p < NUM_PLANETS; p++) {
        populate_fallback_ephemeris(&planets[p], days_since_J2000, &ephemerides[p]);
        fetch_ephemeris_from_horizons(&planets[p], julian_date, &ephemerides[p]);
    }
    
    // Allocate a 2D screen buffer and fill it with spaces.
    char screen[height][width];
    for (int i = 0; i < height; i++)
        for (int j = 0; j < width; j++)
            screen[i][j] = ' ';
    
    // --- Draw orbital paths for each displayed planet ---
    // For each planet from index 0 up to display_planet_count, sample the elliptical orbit.
    for (int p = 0; p < display_planet_count; p++) {
        // The polar equation for an ellipse with one focus at the Sun:
        // r = a*(1 - e^2) / (1 + e*cos(f))
        for (double f_angle = 0.0; f_angle < 2 * M_PI; f_angle += 0.035) {
            double r = planets[p].a * (1 - planets[p].e * planets[p].e) /
                       (1 + planets[p].e * cos(f_angle));
            // Convert polar to Cartesian coordinates.
            double x = r * cos(f_angle);
            double y = r * sin(f_angle);
            // Map to screen coordinates.
            int ix = (int)lround(center_x + x * scale);
            int iy = (int)lround(center_y - y * scale);
            if (ix >= 0 && ix < width && iy >= 0 && iy < height) {
                if (screen[iy][ix] == ' ')
                    screen[iy][ix] = '.';
            }
        }
    }
    
    // --- Draw the Sun at the center (at the focus, 0,0) ---
    if (center_iy >= 0 && center_iy < height &&
        center_ix >= 0 && center_ix < width)
    {
        screen[center_iy][center_ix] = 'O';
    }
    
    // --- Compute and draw each displayed planet's current position ---
    for (int p = 0; p < display_planet_count; p++) {
        double x = ephemerides[p].x_au;
        double y = ephemerides[p].y_au;
        int ix = (int)lround(center_x + x * scale);
        int iy = (int)lround(center_y - y * scale);
        if (ix >= 0 && ix < width && iy >= 0 && iy < height)
            screen[iy][ix] = planets[p].symbol;
    }
    
    // --- Print the final frame (visualization) ---
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            putchar(screen[i][j]);
        }
        putchar('\n');
    }
    putchar('\n');
    fflush(stdout);
    
    // --- Print planetary statistics below the visualization ---
    printf("Planetary Statistics:\n");
    printf("%-10s %-6s %-20s %-12s %-20s %-15s %-18s %-8s\n", "Name", "Symbol", "Semi-Major Axis (AU)", "Eccentricity", "Orbital Period (days)", "Distance (AU)", "True Anomaly (deg)", "Source");
    printf("---------------------------------------------------------------------------------------------------------------------------\n");

    for (int p = 0; p < display_planet_count; p++) {
        double distance = ephemerides[p].distance_au;
        double f_deg = ephemerides[p].true_anomaly_deg;
        printf("%-10s %-6c %-20.3f %-12.4f %-20.3f %-15.3f %-18.1f %-8s\n",
               planets[p].name, planets[p].symbol, planets[p].a,
               planets[p].e, planets[p].T, distance, f_deg,
               ephemerides[p].valid ? "API" : "Model");
    }

    int display_api_count = 0;
    for (int p = 0; p < display_planet_count; p++) {
        if (ephemerides[p].valid)
            display_api_count++;
    }

    printf("\nData timestamp (UTC): %s\n", iso_time);
    printf("Julian Date (approx UTC): %.6f\n", julian_date);
    printf("Horizons API results used for %d/%d displayed planets.\n",
           display_api_count, display_planet_count);
    if (display_api_count < display_planet_count) {
        printf("Fallback orbital model used where API data was unavailable.\n");
    }
    
    return 0;
}
