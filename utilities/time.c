#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846  /* Define M_PI if not defined */
#endif

// ---------- Networking helpers ----------

#define TIME_RESPONSE_BUF 16384

struct HttpResponse {
    char body[TIME_RESPONSE_BUF];
};

// Perform a simple HTTP GET request over port 80.
static int http_get(const char *host, const char *path, struct HttpResponse *response) {
    if (!host || !path || !response) {
        return -1;
    }

    struct addrinfo hints;
    struct addrinfo *result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int ret = getaddrinfo(host, "80", &hints, &result);
    if (ret != 0 || result == NULL) {
        fprintf(stderr, "Failed to resolve host %s: %s\n", host, gai_strerror(ret));
        return -1;
    }

    int sockfd = -1;
    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) {
            continue;
        }
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(sockfd);
        sockfd = -1;
    }

    if (sockfd == -1) {
        fprintf(stderr, "Unable to connect to %s\n", host);
        freeaddrinfo(result);
        return -1;
    }

    char request[512];
    int written = snprintf(request, sizeof(request),
                           "GET %s HTTP/1.0\r\n"
                           "Host: %s\r\n"
                           "User-Agent: budostack-time/1.0\r\n"
                           "Connection: close\r\n\r\n",
                           path, host);
    if (written < 0 || (size_t)written >= sizeof(request)) {
        fprintf(stderr, "Request too large\n");
        close(sockfd);
        freeaddrinfo(result);
        return -1;
    }

    if (send(sockfd, request, (size_t)written, 0) < 0) {
        fprintf(stderr, "Failed to send HTTP request to %s\n", host);
        close(sockfd);
        freeaddrinfo(result);
        return -1;
    }

    size_t total = 0;
    while (1) {
        ssize_t n = recv(sockfd, response->body + total, TIME_RESPONSE_BUF - 1 - total, 0);
        if (n < 0) {
            fprintf(stderr, "Error reading response from %s\n", host);
            close(sockfd);
            freeaddrinfo(result);
            return -1;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
        if (total >= TIME_RESPONSE_BUF - 1) {
            break;
        }
    }
    response->body[total] = '\0';

    close(sockfd);
    freeaddrinfo(result);
    return (int)total;
}

// Extract a JSON string value for the given key (very small parser).
static int extract_json_value(const char *json, const char *key, char *out, size_t out_size) {
    if (!json || !key || !out || out_size == 0) {
        return 0;
    }

    const char *key_pos = strstr(json, key);
    if (!key_pos) {
        return 0;
    }

    const char *colon = strchr(key_pos, ':');
    if (!colon) {
        return 0;
    }

    const char *start = colon + 1;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    if (*start == '"') {
        start++;
    }

    const char *end = strchr(start, '"');
    if (!end) {
        end = start;
        while (*end && *end != ',' && *end != '\n' && *end != '\r') {
            end++;
        }
    }

    size_t len = (size_t)(end - start);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return 1;
}

// Skip HTTP headers to obtain the body pointer.
static const char *get_http_body(const char *response_text) {
    if (!response_text) {
        return NULL;
    }
    const char *body = strstr(response_text, "\r\n\r\n");
    if (body) {
        return body + 4;
    }
    body = strstr(response_text, "\n\n");
    if (body) {
        return body + 2;
    }
    return response_text;
}

// Format a datetime string from worldtimeapi into "YYYY-MM-DD HH:MM:SS".
static void format_datetime(const char *raw, char *out, size_t out_size) {
    if (!raw || !out || out_size == 0) {
        return;
    }
    const char *t_ptr = strchr(raw, 'T');
    if (!t_ptr || strlen(raw) < 19) {
        snprintf(out, out_size, "%s", raw);
        return;
    }
    // Copy YYYY-MM-DD
    size_t date_len = (size_t)(t_ptr - raw);
    if (date_len > 10) {
        date_len = 10;
    }
    char date_part[11];
    memcpy(date_part, raw, date_len);
    date_part[date_len] = '\0';

    // Copy HH:MM:SS
    const char *time_part = t_ptr + 1;
    char clock_part[9];
    size_t copy_len = 8;
    if (strlen(time_part) < 8) {
        copy_len = strlen(time_part);
    }
    memcpy(clock_part, time_part, copy_len);
    clock_part[copy_len] = '\0';

    snprintf(out, out_size, "%s %s", date_part, clock_part);
}

// ---------- IP and time zone lookup ----------

struct IpLocation {
    char city[64];
    char region[64];
    char country[16];
    char timezone[128];
};

struct RemoteTime {
    char datetime[64];
    char utc_offset[16];
    char timezone[128];
};

// Fetch IP-based location data from ipinfo.io.
static int fetch_ip_location(struct IpLocation *loc) {
    if (!loc) {
        return 0;
    }
    struct HttpResponse resp;
    if (http_get("ipinfo.io", "/json", &resp) <= 0) {
        return 0;
    }

    const char *body = get_http_body(resp.body);
    if (!body) {
        return 0;
    }

    int ok = 1;
    ok &= extract_json_value(body, "\"city\"", loc->city, sizeof(loc->city));
    ok &= extract_json_value(body, "\"region\"", loc->region, sizeof(loc->region));
    ok &= extract_json_value(body, "\"country\"", loc->country, sizeof(loc->country));
    ok &= extract_json_value(body, "\"timezone\"", loc->timezone, sizeof(loc->timezone));
    return ok;
}

// Fetch time data from worldtimeapi.org for the given path (e.g., "/api/ip" or "/api/timezone/Europe/London").
static int fetch_remote_time(const char *path, struct RemoteTime *rtime) {
    if (!path || !rtime) {
        return 0;
    }
    struct HttpResponse resp;
    if (http_get("worldtimeapi.org", path, &resp) <= 0) {
        return 0;
    }

    const char *body = get_http_body(resp.body);
    if (!body) {
        return 0;
    }

    char raw_datetime[80] = {0};
    if (!extract_json_value(body, "\"datetime\"", raw_datetime, sizeof(raw_datetime))) {
        return 0;
    }
    format_datetime(raw_datetime, rtime->datetime, sizeof(rtime->datetime));

    if (!extract_json_value(body, "\"utc_offset\"", rtime->utc_offset, sizeof(rtime->utc_offset))) {
        return 0;
    }
    if (!extract_json_value(body, "\"timezone\"", rtime->timezone, sizeof(rtime->timezone))) {
        return 0;
    }

    return 1;
}

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

    // Attempt to fetch remote location and time data.
    struct IpLocation location;
    memset(&location, 0, sizeof(location));
    struct RemoteTime remote_now;
    memset(&remote_now, 0, sizeof(remote_now));
    int has_location = fetch_ip_location(&location);
    int has_remote_now = fetch_remote_time("/api/ip", &remote_now);

    // "Time now:" in local time.
    strftime(buffer, sizeof(buffer), "%d-%m-%Y %H:%M:%S", &local_tm);
    if (has_remote_now) {
        printf("%-30s %s (UTC%s, via worldtimeapi.org)\n", "Time now:", buffer, remote_now.utc_offset);
    } else {
        double tz_offset = get_tz_offset(now);
        printf("%-30s %s (UTC%+0.0f)\n", "Time now:", buffer, tz_offset);
    }

    if (has_location) {
        printf("%-30s %s, %s, %s (timezone: %s via ipinfo.io)\n", "Detected location:",
               location.city[0] ? location.city : "Unknown city",
               location.region[0] ? location.region : "Unknown region",
               location.country[0] ? location.country : "Unknown country",
               location.timezone[0] ? location.timezone : "Unknown");
    } else {
        printf("%-30s %s\n", "Detected location:", "Unavailable (internet lookup failed)");
    }
    if (has_remote_now) {
        printf("%-30s %s (%s)\n", "Internet time:", remote_now.datetime, remote_now.timezone);
    } else {
        printf("%-30s %s\n", "Internet time:", "Unavailable (worldtimeapi.org unreachable)");
    }

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
    printf("Regional times verified online (worldtimeapi.org):\n\n");

    struct Timezone zones[] = {
        { -11, "Pacific/Pago_Pago", "Pago Pago (American Samoa)" },
        { -10, "Pacific/Honolulu", "Honolulu (USA)" },
        { -9,  "America/Anchorage", "Anchorage (USA)" },
        { -8,  "America/Los_Angeles", "Los Angeles (USA), Vancouver (Canada)" },
        { -7,  "America/Denver", "Denver (USA), Calgary (Canada)" },
        { -6,  "America/Chicago", "Chicago (USA), Winnipeg (Canada)" },
        { -5,  "America/New_York", "New York (USA), Toronto (Canada)" },
        { -4,  "America/Santiago", "Santiago (Chile)" },
        { -3,  "America/Argentina/Buenos_Aires", "Buenos Aires (Argentina)" },
        { -2,  "America/Noronha", "Fernando de Noronha (Brazil)" },
        { -1,  "Atlantic/Cape_Verde", "Praia (Cape Verde)" },
        {  0,  "Europe/London", "London (England)" },
        {  1,  "Europe/Paris", "Paris (France), Berlin (Germany)" },
        {  2,  "Europe/Helsinki", "Helsinki (Finland)" },
        {  3,  "Europe/Moscow", "Moscow (Russia)" },
        {  4,  "Asia/Dubai", "Dubai (UAE)" },
        {  5,  "Asia/Kolkata", "New Delhi (India)" },
        {  6,  "Asia/Dhaka", "Dhaka (Bangladesh)" },
        {  7,  "Asia/Bangkok", "Bangkok (Thailand)" },
        {  8,  "Asia/Shanghai", "Beijing (China), Hong Kong (China)" },
        {  9,  "Asia/Tokyo", "Tokyo (Japan)" },
        { 10,  "Australia/Sydney", "Sydney (Australia)" },
        { 11,  "Pacific/Honiara", "Honiara (Solomon Islands)" },
        { 12,  "Pacific/Auckland", "Auckland (New Zealand)" }
    };

    int numZones = (int)(sizeof(zones) / sizeof(zones[0]));
    char label[150];
    for (int i = 0; i < numZones; i++) {
        struct RemoteTime city_time;
        memset(&city_time, 0, sizeof(city_time));
        char path[160];
        snprintf(path, sizeof(path), "/api/timezone/%s", zones[i].tzString);
        int ok = fetch_remote_time(path, &city_time);
        if (zones[i].offset >= 0) {
            snprintf(label, sizeof(label), "UTC+%d - %s", zones[i].offset, zones[i].cities);
        } else {
            snprintf(label, sizeof(label), "UTC%d - %s", zones[i].offset, zones[i].cities);
        }

        if (ok) {
            printf("    %-45s %s (%s)\n", label, city_time.datetime, city_time.utc_offset);
        } else {
            printf("    %-45s %s\n", label, "Unavailable (worldtimeapi.org unreachable)");
        }
    }

    return 0;
}
