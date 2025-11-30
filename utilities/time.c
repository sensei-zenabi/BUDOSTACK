#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846  /* Define M_PI if not defined */
#endif

// ---------- Output helpers ----------

#define MAX_OUTPUT_COLS 78
#define LABEL_WIDTH 24

// Print a label/value pair with wrapping so no line exceeds MAX_OUTPUT_COLS.
static void print_wrapped_label(const char *label, const char *content) {
    if (!label) {
        label = "";
    }
    if (!content) {
        content = "";
    }

    char prefix[LABEL_WIDTH + 2];
    int prefix_len = snprintf(prefix, sizeof(prefix), "%-*s", LABEL_WIDTH, label);
    if (prefix_len < 0) {
        return;
    }
    if (prefix_len >= (int)sizeof(prefix)) {
        prefix_len = (int)sizeof(prefix) - 1;
    }

    char indent[LABEL_WIDTH + 2];
    memset(indent, ' ', (size_t)prefix_len);
    indent[prefix_len] = '\0';

    const char *text = content;
    int line_len = 0;
    int first_word = 1;

    fputs(prefix, stdout);
    line_len = prefix_len;

    while (*text) {
        while (*text == ' ') {
            text++;
        }
        if (*text == '\0') {
            break;
        }
        const char *word_start = text;
        size_t word_len = 0;
        while (text[word_len] && text[word_len] != ' ') {
            word_len++;
        }

        if (!first_word && line_len + 1 + (int)word_len > MAX_OUTPUT_COLS) {
            fputc('\n', stdout);
            fputs(indent, stdout);
            line_len = prefix_len;
            first_word = 1;
        }

        if (!first_word) {
            fputc(' ', stdout);
            line_len++;
        }

        fwrite(word_start, 1, word_len, stdout);
        line_len += (int)word_len;
        text += word_len;
        first_word = 0;
    }

    fputc('\n', stdout);
}

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

// ---------- IP and time zone lookup ----------

struct IpLocation {
    char city[64];
    char region[64];
    char country[16];
    char timezone[128];
};

// Fetch IP-based location data from ip-api.com.
static int fetch_ip_location(struct IpLocation *loc) {
    if (!loc) {
        return 0;
    }
    struct HttpResponse resp;
    if (http_get("ip-api.com", "/json", &resp) <= 0) {
        return 0;
    }

    const char *body = get_http_body(resp.body);
    if (!body) {
        return 0;
    }

    int ok = 1;
    ok &= extract_json_value(body, "\"city\"", loc->city, sizeof(loc->city));
    ok &= extract_json_value(body, "\"regionName\"", loc->region,
                             sizeof(loc->region));
    ok &= extract_json_value(body, "\"countryCode\"", loc->country,
                             sizeof(loc->country));
    ok &= extract_json_value(body, "\"timezone\"", loc->timezone,
                             sizeof(loc->timezone));
    return ok;
}

// ---------- NTP time lookup ----------

// Minimal NTP client that queries an NTP server over UDP port 123.
static int fetch_ntp_time(const char *host, time_t *out_time) {
    if (!host || !out_time) {
        return 0;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    struct addrinfo *result = NULL;
    if (getaddrinfo(host, "123", &hints, &result) != 0 || !result) {
        return 0;
    }

    int sockfd = -1;
    for (struct addrinfo *rp = result; rp; rp = rp->ai_next) {
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
        freeaddrinfo(result);
        return 0;
    }

    unsigned char packet[48];
    memset(packet, 0, sizeof(packet));
    packet[0] = 0x1b;  // LI = 0, VN = 3, Mode = 3 (client).

    ssize_t sent = send(sockfd, packet, sizeof(packet), 0);
    if (sent != (ssize_t)sizeof(packet)) {
        close(sockfd);
        freeaddrinfo(result);
        return 0;
    }

    ssize_t received = recv(sockfd, packet, sizeof(packet), 0);
    close(sockfd);
    freeaddrinfo(result);
    if (received < 44) {
        return 0;
    }

    uint32_t seconds;
    memcpy(&seconds, packet + 40, sizeof(seconds));
    seconds = ntohl(seconds);
    const uint32_t seventy_years = 2208988800U; // Seconds from 1900 to 1970.
    if (seconds < seventy_years) {
        return 0;
    }
    *out_time = (time_t)(seconds - seventy_years);
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

// Compute local time and UTC offset for a given IANA time zone using a base
// UTC time (from NTP if available).
static int format_zone_time(const char *tz, time_t base_utc, char *time_buf,
                            size_t time_bufsz, double *offset_hours) {
    if (!tz || !time_buf || time_bufsz == 0) {
        return 0;
    }

    char prev_tz[128];
    const char *current = getenv("TZ");
    if (current) {
        snprintf(prev_tz, sizeof(prev_tz), "%s", current);
    } else {
        prev_tz[0] = '\0';
    }

    if (setenv("TZ", tz, 1) != 0) {
        return 0;
    }
    tzset();

    struct tm local_tm;
    struct tm gm_tm;
    localtime_r(&base_utc, &local_tm);
    gmtime_r(&base_utc, &gm_tm);

    strftime(time_buf, time_bufsz, "%d-%m-%Y %H:%M:%S", &local_tm);

    if (offset_hours) {
        time_t local_sec = mktime(&local_tm);
        time_t gm_sec = mktime(&gm_tm);
        *offset_hours = difftime(local_sec, gm_sec) / 3600.0;
    }

    if (prev_tz[0] != '\0') {
        setenv("TZ", prev_tz, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
    return 1;
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
    time_t base_time = time(NULL);
    time_t ntp_time = 0;
    const char *ntp_hosts[] = { "time.google.com", "time.cloudflare.com",
                                "pool.ntp.org" };
    for (size_t i = 0; i < sizeof(ntp_hosts) / sizeof(ntp_hosts[0]); i++) {
        if (fetch_ntp_time(ntp_hosts[i], &ntp_time)) {
            base_time = ntp_time;
            break;
        }
    }

    struct tm local_tm;
    localtime_r(&base_time, &local_tm);

    char buffer[100];

    // Attempt to fetch remote location and time data.
    struct IpLocation location;
    memset(&location, 0, sizeof(location));
    int has_location = fetch_ip_location(&location);

    // "Time now:" in local time.
    strftime(buffer, sizeof(buffer), "%d-%m-%Y %H:%M:%S", &local_tm);
    double tz_offset = get_tz_offset(base_time);
    char linebuf[512];
    snprintf(linebuf, sizeof(linebuf), "%s (UTC%+0.0f via NTP)", buffer,
             tz_offset);
    print_wrapped_label("Time now:", linebuf);

    if (has_location) {
        snprintf(linebuf, sizeof(linebuf), "%s, %s, %s (tz %s via ip-api.com)",
                 location.city[0] ? location.city : "Unknown city",
                 location.region[0] ? location.region : "Unknown region",
                 location.country[0] ? location.country : "Unknown country",
                 location.timezone[0] ? location.timezone : "Unknown");
        print_wrapped_label("Detected location:", linebuf);
    } else {
        print_wrapped_label("Detected location:",
                            "Unavailable (internet lookup failed)");
    }

    if (has_location && location.timezone[0]) {
        double remote_offset = 0.0;
        char remote_time[64];
        if (format_zone_time(location.timezone, base_time, remote_time,
                             sizeof(remote_time), &remote_offset)) {
            snprintf(linebuf, sizeof(linebuf), "%s (%s, UTC%+.1f)", remote_time,
                     location.timezone, remote_offset);
            print_wrapped_label("Internet time:", linebuf);
        } else {
            print_wrapped_label("Internet time:",
                                "Unavailable (timezone formatting failed)");
        }
    } else {
        print_wrapped_label("Internet time:",
                            "Unavailable (timezone missing)");
    }

    // ISO week number.
    char week[3];
    strftime(week, sizeof(week), "%V", &local_tm);
    snprintf(linebuf, sizeof(linebuf), "%02d", atoi(week));
    print_wrapped_label("Current Week:", linebuf);

    // Days since year start.
    snprintf(linebuf, sizeof(linebuf), "%03d", local_tm.tm_yday);
    print_wrapped_label("Days since year start:", linebuf);

    // Calculate days until year end.
    int year = local_tm.tm_year + 1900;
    int total_days = is_leap(year) ? 366 : 365;
    int days_till_end = total_days - (local_tm.tm_yday + 1);
    snprintf(linebuf, sizeof(linebuf), "%03d", days_till_end);
    print_wrapped_label("Days till year end:", linebuf);
    printf("\n");

    // Regional times header.
    printf("Regional times (NTP-synced base time):\n\n");

    struct Timezone zones[] = {
        { -12, "Etc/GMT+12", "Baker Island" },
        { -11, "Pacific/Pago_Pago", "Pago Pago" },
        { -10, "Pacific/Honolulu", "Honolulu" },
        { -9,  "America/Anchorage", "Anchorage" },
        { -8,  "America/Los_Angeles", "Los Angeles" },
        { -7,  "America/Denver", "Denver" },
        { -6,  "America/Chicago", "Chicago" },
        { -5,  "America/New_York", "New York" },
        { -4,  "America/Halifax", "Halifax" },
        { -3,  "America/Sao_Paulo", "Sao Paulo" },
        { -2,  "America/Noronha", "Fernando de Noronha" },
        { -1,  "Atlantic/Cape_Verde", "Praia" },
        {  0,  "Europe/London", "London" },
        {  1,  "Europe/Paris", "Paris" },
        {  2,  "Africa/Cairo", "Cairo" },
        {  3,  "Europe/Moscow", "Moscow" },
        {  4,  "Asia/Dubai", "Dubai" },
        {  5,  "Asia/Karachi", "Karachi" },
        {  6,  "Asia/Dhaka", "Dhaka" },
        {  7,  "Asia/Bangkok", "Bangkok" },
        {  8,  "Asia/Shanghai", "Beijing" },
        {  9,  "Asia/Tokyo", "Tokyo" },
        { 10,  "Australia/Sydney", "Sydney" },
        { 11,  "Pacific/Guadalcanal", "Honiara" },
        { 12,  "Pacific/Auckland", "Auckland" },
        { 13,  "Pacific/Tongatapu", "Nuku'alofa" },
        { 14,  "Pacific/Kiritimati", "Kiritimati" }
    };

    int numZones = (int)(sizeof(zones) / sizeof(zones[0]));
    char label[96];
    char city_time[64];
    for (int i = 0; i < numZones; i++) {
        double off = 0.0;
        int ok = format_zone_time(zones[i].tzString, base_time, city_time,
                                  sizeof(city_time), &off);
        snprintf(label, sizeof(label), "  UTC%+d %s", zones[i].offset,
                 zones[i].cities);
        if (ok) {
            snprintf(linebuf, sizeof(linebuf), "%s (UTC%+.1f)", city_time,
                     off);
            print_wrapped_label(label, linebuf);
        } else {
            print_wrapped_label(label, "Unavailable (tz data)");
        }
    }

    return 0;
}
