#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

// Determine if a given year is a leap year.
int is_leap(int year) {
    // A year is a leap year if it is divisible by 400 or if
    // it is divisible by 4 and not by 100.
    return ((year % 400 == 0) || ((year % 4 == 0) && (year % 100 != 0)));
}

// Structure for a time zone slot.
struct Timezone {
    int offset;             // Integer offset: local time = UTC + offset.
    const char *tzString;   // POSIX TZ string to set the time zone.
    const char *cities;     // Display string: one or more well-known cities.
};

int main(void) {
    // Obtain current time.
    time_t now = time(NULL);
    struct tm local_tm;
    // Convert current time to local representation.
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

    // Array of 24 time zones (UTC -11 to UTC +12). Each entry
    // provides a TZ string (for setting the system time conversion),
    // and one or more city names to display.
    // For zones with multiple well-known candidates, cities are comma‑separated.
    //
    // Note: Some chosen cities approximate their official time zones to an integer offset.
    // For example, New Delhi normally uses UTC+5:30, but here it is approximated as UTC+5.
    struct Timezone zones[] = {
        // UTC -11
        { -11, "PagoPago11", "Pago Pago (American Samoa)" },
        // UTC -10
        { -10, "Honolulu10", "Honolulu (USA)" },
        // UTC -9
        { -9,  "Anchorage9", "Anchorage (USA)" },
        // UTC -8 (displaying two well-known cities)
        { -8,  "LosAngeles8", "Los Angeles (USA), Vancouver (Canada)" },
        // UTC -7
        { -7,  "Denver7", "Denver (USA), Calgary (Canada)" },
        // UTC -6
        { -6,  "Chicago6", "Chicago (USA), Winnipeg (Canada)" },
        // UTC -5
        { -5,  "NewYork5", "New York (USA), Toronto (Canada)" },
        // UTC -4
        { -4,  "Santiago4", "Santiago (Chile)" },
        // UTC -3
        { -3,  "BuenosAires3", "Buenos Aires (Argentina)" },
        // UTC -2
        { -2,  "FernandoNoronha2", "Fernando de Noronha (Brazil)" },
        // UTC -1
        { -1,  "Praia1", "Praia (Cape Verde)" },
        // UTC +0
        {  0,  "London0", "London (England)" },
        // UTC +1
        {  1,  "Paris-1", "Paris (France), Berlin (Germany)" },
        // UTC +2
        {  2,  "Helsinki-2", "Helsinki (Finland)" },
        // UTC +3
        {  3,  "Moscow-3", "Moscow (Russia)" },
        // UTC +4
        {  4,  "Dubai-4", "Dubai (UAE)" },
        // UTC +5 (approximation; India officially uses UTC+5:30)
        {  5,  "NewDelhi-5", "New Delhi (India)" },
        // UTC +6
        {  6,  "Dhaka-6", "Dhaka (Bangladesh)" },
        // UTC +7
        {  7,  "Bangkok-7", "Bangkok (Thailand)" },
        // UTC +8
        {  8,  "Beijing-8", "Beijing (China), Hong Kong (China)" },
        // UTC +9
        {  9,  "Tokyo-9", "Tokyo (Japan)" },
        // UTC +10
        { 10,  "Sydney-10", "Sydney (Australia)" },
        // UTC +11
        { 11,  "Honiara-11", "Honiara (Solomon Islands)" },
        // UTC +12
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
        // Print the label and time with a fixed field width for the label.
        printf("    %-45s %s\n", label, time_str);
    }
    
    return 0;
}
