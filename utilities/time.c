#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846  /* Define M_PI if not defined */
#endif

// Determine if a given year is a leap year.
int is_leap(int year) {
    // A year is a leap year if it is divisible by 400 or if it is divisible by 4 and not by 100.
    return ((year % 400 == 0) || ((year % 4 == 0) && (year % 100 != 0)));
}

// Structure for a time zone slot.
struct Timezone {
    int offset;             // Integer offset: local time = UTC + offset.
    const char *tzString;   // POSIX TZ string to set the time zone.
    const char *cities;     // Display string: one or more well-known cities.
};

// ---------- Astronomical events functions ----------

// Calculate the Julian Ephemeris Date (JDE) for a given event and year.
// event: 0 = March Equinox, 1 = June Solstice, 2 = September Equinox, 3 = December Solstice.
double calc_event_jde(int event, int year) {
    double T = (year - 2000) / 1000.0;
    double T2 = T * T;
    double T3 = T2 * T;
    double T4 = T3 * T;
    double JDE;
    switch (event) {
        case 0: // March Equinox
            JDE = 2451623.80984 + 365242.37404 * T + 0.05169 * T2 - 0.00411 * T3 - 0.00057 * T4;
            break;
        case 1: // June Solstice
            JDE = 2451716.56767 + 365241.62603 * T + 0.00325 * T2 + 0.00888 * T3 - 0.00030 * T4;
            break;
        case 2: // September Equinox
            JDE = 2451810.21715 + 365242.01767 * T - 0.11575 * T2 + 0.00337 * T3 + 0.00078 * T4;
            break;
        case 3: // December Solstice
            JDE = 2451900.05952 + 365242.74049 * T - 0.06223 * T2 - 0.00823 * T3 + 0.00032 * T4;
            break;
        default:
            JDE = 0;
    }
    return JDE;
}

// Convert a Julian Ephemeris Date to Unix time (seconds since 1970-01-01 00:00:00 UTC).
time_t event_time_from_jde(double jde) {
    double seconds = (jde - 2440587.5) * 86400.0;
    return (time_t)(seconds + 0.5);  // Round to nearest second.
}

// For a given event and year, compute the Unix time of the event.
// If the event time for the given year has passed (relative to now), compute it for next year.
time_t get_astronomical_event(int event, int year, time_t now) {
    time_t etime = event_time_from_jde(calc_event_jde(event, year));
    if (difftime(etime, now) <= 0) {
        etime = event_time_from_jde(calc_event_jde(event, year + 1));
    }
    return etime;
}

// Compute the local time zone offset (in hours) from UTC.
double get_tz_offset(time_t now) {
    struct tm local_tm, gm_tm;
    localtime_r(&now, &local_tm);
    gmtime_r(&now, &gm_tm);
    time_t local_sec = mktime(&local_tm);
    time_t gm_sec = mktime(&gm_tm);
    return difftime(local_sec, gm_sec) / 3600.0;
}

// Compute sunrise or sunset (is_sunrise nonzero for sunrise, zero for sunset)
// using a simple solar calculation. This function uses the current date provided
// in the 'base_date' struct and adjusts to the next occurrence if the event has passed.
time_t compute_sun_event(struct tm base_date, double lat, double lon, double tz_offset, int is_sunrise) {
    struct tm date = base_date;
    time_t event_time;
    while (1) {
        int N = date.tm_yday + 1;  // Day of year (1 to 366)
        // Calculate solar declination (in degrees) using an approximate formula.
        double decl_deg = 23.45 * sin(2 * M_PI * (284 + N) / 365.0);
        double decl = decl_deg * M_PI / 180.0;
        double lat_rad = lat * M_PI / 180.0;
        // Zenith angle for sunrise/sunset (includes atmospheric refraction)
        double zenith = 90.833 * M_PI / 180.0;
        // Use the standard sunrise/sunset formula.
        double cos_ha = (cos(zenith) - sin(lat_rad) * sin(decl)) / (cos(lat_rad) * cos(decl));
        if (cos_ha > 1 || cos_ha < -1) {
            // No sunrise or sunset occurs on this day at this location.
            event_time = (time_t)-1;
            break;
        }
        double ha = acos(cos_ha);  // Hour angle in radians.
        // Convert hour angle to time in hours.
        double ha_hours = (ha * 180.0 / M_PI) / 15.0;
        // Approximate solar noon (in local time, in hours). Assumes user is near the center of the time zone.
        double solar_noon = 12.0 - (lon / 15.0) + tz_offset;
        double event_hour;
        if (is_sunrise)
            event_hour = solar_noon - ha_hours;
        else
            event_hour = solar_noon + ha_hours;
        // Set the calculated hour (and minutes) into the date structure.
        date.tm_hour = (int)event_hour;
        date.tm_min = (int)((event_hour - date.tm_hour) * 60);
        date.tm_sec = 0;
        event_time = mktime(&date);
        if (event_time > time(NULL))
            break;
        // If the event has already passed, try the next day.
        date.tm_mday += 1;
        mktime(&date);  // Normalize the date.
    }
    return event_time;
}

// Format a time difference (in seconds) into a string "X days, HH:MM:SS".
void format_time_diff(time_t diff, char *buffer, size_t bufsize) {
    int days = diff / 86400;
    int hours = (diff % 86400) / 3600;
    int minutes = (diff % 3600) / 60;
    int seconds = diff % 60;
    snprintf(buffer, bufsize, "%d days, %02d:%02d:%02d", days, hours, minutes, seconds);
}

// Display astronomical events (equinoxes, solstices, sunrise, and sunset) with remaining time.
void display_astronomy(void) {
    time_t now = time(NULL);
    struct tm local_tm;
    localtime_r(&now, &local_tm);
    char time_str[100];

    // Obtain the local time zone offset.
    double tz_offset = get_tz_offset(now);

    printf("Astronomical Events:\n\n");

    // Compute and display equinoxes and solstices.
    // Event indices: 0 = March Equinox, 1 = June Solstice, 2 = September Equinox, 3 = December Solstice.
    const char* events[] = { "March Equinox", "June Solstice", "Sept. Equinox", "Dec. Solstice" };
    int current_year = local_tm.tm_year + 1900;
    for (int i = 0; i < 4; i++) {
        time_t event_t = get_astronomical_event(i, current_year, now);
        struct tm event_local;
        localtime_r(&event_t, &event_local);
        strftime(time_str, sizeof(time_str), "%d-%m-%Y %H:%M:%S", &event_local);
        time_t diff = event_t - now;
        char diff_str[100];
        format_time_diff(diff, diff_str, sizeof(diff_str));
        printf("%-20s at %s (in %s)\n", events[i], time_str, diff_str);
    }

    // Compute and display sunrise and sunset if location is provided via environment variables.
    char *lat_env = getenv("LATITUDE");
    char *lon_env = getenv("LONGITUDE");
    if (lat_env && lon_env) {
        double lat = atof(lat_env);
        double lon = atof(lon_env);
        time_t sunrise = compute_sun_event(local_tm, lat, lon, tz_offset, 1);
        time_t sunset = compute_sun_event(local_tm, lat, lon, tz_offset, 0);
        if (sunrise != (time_t)-1) {
            struct tm sr_local;
            localtime_r(&sunrise, &sr_local);
            strftime(time_str, sizeof(time_str), "%d-%m-%Y %H:%M:%S", &sr_local);
            time_t diff = sunrise - now;
            char diff_str[100];
            format_time_diff(diff, diff_str, sizeof(diff_str));
            printf("%-20s at %s (in %s)\n", "Sunrise", time_str, diff_str);
        } else {
            printf("Sunrise: no sunrise on this day at this location\n");
        }
        if (sunset != (time_t)-1) {
            struct tm ss_local;
            localtime_r(&sunset, &ss_local);
            strftime(time_str, sizeof(time_str), "%d-%m-%Y %H:%M:%S", &ss_local);
            time_t diff = sunset - now;
            char diff_str[100];
            format_time_diff(diff, diff_str, sizeof(diff_str));
            printf("%-20s at %s (in %s)\n", "Sunset", time_str, diff_str);
        } else {
            printf("Sunset: no sunset on this day at this location\n");
        }
    } else {
        printf("\nLocation not provided (set LATITUDE and LONGITUDE env variables)\n");
    }
}

// ---------- Main function ----------

int main(int argc, char *argv[]) {
    // If the "-s" command-line argument is provided, run the astronomical events display.
    if (argc > 1 && strcmp(argv[1], "-s") == 0) {
        display_astronomy();
        return 0;
    }

    // Regular time display.
    time_t now = time(NULL);
    struct tm local_tm;
    localtime_r(&now, &local_tm);

    char buffer[100];

    // "Time now:" in local time.
    strftime(buffer, sizeof(buffer), "%d-%m-%Y %H:%M:%S", &local_tm);
    printf("%-30s %s\n", "Time now:", buffer);

    // ISO week number.
    char week[3];
    strftime(week, sizeof(week), "%V", &local_tm);
    printf("%-30s %02d\n", "Current Week:", atoi(week));

    // Days since year start (tm_yday starts at 0).
    printf("%-30s %03d\n", "Days since year start:", local_tm.tm_yday);

    // Calculate days until year end.
    int year = local_tm.tm_year + 1900;
    int total_days = is_leap(year) ? 366 : 365;
    int days_till_end = total_days - (local_tm.tm_yday + 1);
    printf("%-30s %03d\n\n", "Days till year end:", days_till_end);

    // Regional times header.
    printf("Regional standard times: (non-DST):\n\n");

    // Array of 24 time zones (UTC -11 to UTC +12).
    struct Timezone zones[] = {
        { -11, "PagoPago11", "Pago Pago (American Samoa)" },
        { -10, "Honolulu10", "Honolulu (USA)" },
        { -9,  "Anchorage9", "Anchorage (USA)" },
        { -8,  "LosAngeles8", "Los Angeles (USA), Vancouver (Canada)" },
        { -7,  "Denver7", "Denver (USA), Calgary (Canada)" },
        { -6,  "Chicago6", "Chicago (USA), Winnipeg (Canada)" },
        { -5,  "NewYork5", "New York (USA), Toronto (Canada)" },
        { -4,  "Santiago4", "Santiago (Chile)" },
        { -3,  "BuenosAires3", "Buenos Aires (Argentina)" },
        { -2,  "FernandoNoronha2", "Fernando de Noronha (Brazil)" },
        { -1,  "Praia1", "Praia (Cape Verde)" },
        {  0,  "London0", "London (England)" },
        {  1,  "Paris-1", "Paris (France), Berlin (Germany)" },
        {  2,  "Helsinki-2", "Helsinki (Finland)" },
        {  3,  "Moscow-3", "Moscow (Russia)" },
        {  4,  "Dubai-4", "Dubai (UAE)" },
        {  5,  "NewDelhi-5", "New Delhi (India)" },
        {  6,  "Dhaka-6", "Dhaka (Bangladesh)" },
        {  7,  "Bangkok-7", "Bangkok (Thailand)" },
        {  8,  "Beijing-8", "Beijing (China), Hong Kong (China)" },
        {  9,  "Tokyo-9", "Tokyo (Japan)" },
        { 10,  "Sydney-10", "Sydney (Australia)" },
        { 11,  "Honiara-11", "Honiara (Solomon Islands)" },
        { 12,  "Auckland-12", "Auckland (New Zealand)" }
    };

    int numZones = sizeof(zones) / sizeof(zones[0]);
    char label[150];
    char time_str[100];
    struct tm tm_city;

    // Iterate through each time zone and display its regional time.
    for (int i = 0; i < numZones; i++) {
        // Set the TZ environment variable to the zone's TZ string.
        setenv("TZ", zones[i].tzString, 1);
        tzset();
        localtime_r(&now, &tm_city);
        strftime(time_str, sizeof(time_str), "%d-%m-%Y %H:%M:%S", &tm_city);

        // Build the label in the format: "UTC±offset - cities".
        if (zones[i].offset >= 0) {
            snprintf(label, sizeof(label), "UTC+%d - %s", zones[i].offset, zones[i].cities);
        } else {
            snprintf(label, sizeof(label), "UTC%d - %s", zones[i].offset, zones[i].cities);
        }
        // Print the label and the regional time.
        printf("    %-45s %s\n", label, time_str);
    }

    return 0;
}
