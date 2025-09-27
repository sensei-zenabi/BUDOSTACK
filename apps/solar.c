// Define POSIX version for system calls
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/ioctl.h>

// If M_PI is not defined by math.h, define it.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Constant: Unix time for the J2000 epoch (2000 Jan 1 12:00:00 UT)
const time_t J2000 = 946728000;

// Structure to hold planetary orbital parameters:
// a  : semi-major axis in astronomical units (AU)
// e  : orbital eccentricity
// T  : orbital period in days
// M0 : mean anomaly at J2000 (in radians)
typedef struct {
    char symbol;   // Character symbol for the planet
    double a;      // Semi-major axis (AU)
    double e;      // Eccentricity
    double T;      // Orbital period in days
    double M0;     // Mean anomaly at J2000 (radians)
} Planet;

// Eight planets (Mercury, Venus, Earth, Mars, Jupiter, Saturn, Uranus, Neptune)
// Note: The orbital elements are approximated for a simple realistic model.
#define NUM_PLANETS 8
Planet planets[NUM_PLANETS] = {
    //   symbol,    a (AU),    e,         T (days),   M0 (rad)
    { 'm',    0.387, 0.2056,   87.969,  3.049  },  // Mercury (lowercase 'm')
    { 'V',    0.723, 0.0068,  224.701,  0.875  },  // Venus
    { 'E',    1.000, 0.0167,  365.256,  6.240  },  // Earth
    { 'M',    1.524, 0.0934,  686.980,  0.338  },  // Mars (uppercase 'M')
    { 'J',    5.203, 0.0484, 4332.59,  0.349  },  // Jupiter
    { 'S',    9.537, 0.0542,10759.22,  5.534  },  // Saturn
    { 'U',   19.191, 0.0472,30685.4,  2.482  },  // Uranus
    { 'N',   30.070, 0.0086,60190.0,  4.471  }   // Neptune
};

// Function to solve Kepler's equation M = E - e*sin(E) using Newton-Raphson iteration.
// Returns the eccentric anomaly E given the mean anomaly M and eccentricity e.
double solveKepler(double M, double e) {
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
    double days_since_J2000 = difftime(now, J2000) / 86400.0;
    
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
        // Compute the mean anomaly: M = M0 + (2*pi/T)*(days since J2000).
        double M = planets[p].M0 + (2 * M_PI / planets[p].T) * days_since_J2000;
        // Normalize M to [0, 2*pi].
        M = fmod(M, 2 * M_PI);
        if (M < 0)
            M += 2 * M_PI;
        
        // Solve Kepler's equation for the eccentric anomaly.
        double E = solveKepler(M, planets[p].e);
        
        // Compute the true anomaly.
        double f_angle = 2 * atan2(sqrt(1 + planets[p].e) * sin(E / 2),
                                   sqrt(1 - planets[p].e) * cos(E / 2));
        
        // Compute the heliocentric distance: r = a * (1 - e*cos(E)).
        double r = planets[p].a * (1 - planets[p].e * cos(E));
        
        // Convert polar coordinates (r, f_angle) to Cartesian (x,y).
        double x = r * cos(f_angle);
        double y = r * sin(f_angle);
        
        // Map the coordinates to screen positions.
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
    // Define the full names of the planets.
    const char *planet_names[NUM_PLANETS] = {"Mercury", "Venus", "Earth", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune"};
    printf("Planetary Statistics:\n");
    printf("%-10s %-6s %-20s %-12s %-20s %-15s %-18s\n", "Name", "Symbol", "Semi-Major Axis (AU)", "Eccentricity", "Orbital Period (days)", "Distance (AU)", "True Anomaly (deg)");
    printf("---------------------------------------------------------------------------------------------------------------\n");
    
    for (int p = 0; p < display_planet_count; p++) {
        // Recompute the mean anomaly for this planet.
        double M = planets[p].M0 + (2 * M_PI / planets[p].T) * days_since_J2000;
        M = fmod(M, 2 * M_PI);
        if (M < 0)
            M += 2 * M_PI;
        // Solve for the eccentric anomaly.
        double E = solveKepler(M, planets[p].e);
        // Compute the true anomaly.
        double f_angle = 2 * atan2(sqrt(1 + planets[p].e) * sin(E / 2),
                                   sqrt(1 - planets[p].e) * cos(E / 2));
        // Compute the heliocentric distance.
        double r = planets[p].a * (1 - planets[p].e * cos(E));
        // Convert true anomaly to degrees.
        double f_deg = f_angle * 180 / M_PI;
        // Print the formatted statistics.
        printf("%-10s %-6c %-20.3f %-12.4f %-20.3f %-15.3f %-18.1f\n",
               planet_names[p], planets[p].symbol, planets[p].a,
               planets[p].e, planets[p].T, r, f_deg);
    }
    
    return 0;
}
