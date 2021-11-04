/**
 * Copyright 2021 Fractal Computers, Inc., dba Whist
 * @file clock.c
 * @brief This file contains the helper functions for timing code.
============================
Usage
============================

You can use start_timer and get_timer to time specific pieces of code, or to
relate different events across server and client.
*/

#define _CRT_SECURE_NO_WARNINGS  // stupid Windows warnings

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fractal/core/fractal.h>
#include "clock.h"

#ifdef _WIN32
int get_utc_offset();
#endif

#define US_IN_MS 1000.0

void start_timer(clock* timer) {
#if defined(_WIN32)
    QueryPerformanceCounter(timer);
#else
    // start timer
    gettimeofday(timer, NULL);
#endif
}

double get_timer(clock timer) {
#if defined(_WIN32)
    LARGE_INTEGER end;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&end);
    double ret = (double)(end.QuadPart - timer.QuadPart) / frequency.QuadPart;
#else
    // stop timer
    struct timeval t2;
    gettimeofday(&t2, NULL);

    // compute and print the elapsed time in millisec
    double elapsed_time = (t2.tv_sec - timer.tv_sec) * MS_IN_SECOND;  // sec to ms
    elapsed_time += (t2.tv_usec - timer.tv_usec) / US_IN_MS;          // us to ms

    // LOG_INFO("elapsed time in ms is: %f\n", elapsedTime);

    // standard var to return and convert to seconds since it gets converted to
    // ms in function call
    double ret = elapsed_time / MS_IN_SECOND;
#endif
    return ret;
}

clock create_clock(int timeout_ms) {
    clock out;
#if defined(_WIN32)
    out.QuadPart = timeout_ms;
#else
    out.tv_sec = timeout_ms / (double)MS_IN_SECOND;
    out.tv_usec = (timeout_ms % 1000) * 1000;
#endif
    return out;
}

char* current_time_str() {
    static char buffer[64];
    //    time_t rawtime;
    //
    //    time(&rawtime);
    //    timeinfo = localtime(&rawtime);
#if defined(_WIN32)
    SYSTEMTIME time_now;
    GetSystemTime(&time_now);
    snprintf(buffer, sizeof(buffer), "%02i:%02i:%02i.%06li", time_now.wHour, time_now.wMinute,
             time_now.wSecond, (long)time_now.wMilliseconds);
#else
    struct tm* time_str_tm;
    struct timeval time_now;
    gettimeofday(&time_now, NULL);

    time_str_tm = gmtime(&time_now.tv_sec);
    snprintf(buffer, sizeof(buffer), "%02i:%02i:%02i.%06li", time_str_tm->tm_hour,
             time_str_tm->tm_min, time_str_tm->tm_sec, (long)time_now.tv_usec);
#endif

    //    strftime(buffer, 64, "%Y-%m-%d %H:%M:%S", timeinfo);

    return buffer;
}

int get_utc_offset() {
#if defined(_WIN32)
    char* utc_offset_str;
    runcmd("powershell.exe \"Get-Date -UFormat \\\"%Z\\\"\"", &utc_offset_str);
    if (strlen(utc_offset_str) >= 3) {
        utc_offset_str[3] = '\0';
    }

    int utc_offset = atoi(utc_offset_str);

    free(utc_offset_str);

    return utc_offset;
#else
    time_t t = time(NULL);
    struct tm lt = {0};
    localtime_r(&t, &lt);

    return (int)lt.tm_gmtoff / (60 * 60);
#endif
}

int get_dst() {
#if defined(_WIN32)
    // TODO: Implement DST on Windows if it makes sense
    return 0;
#else
    time_t t = time(NULL);
    struct tm lt = {0};
    localtime_r(&t, &lt);
    return lt.tm_isdst;
#endif
}

int get_time_data(FractalTimeData* time_data) {
    time_data->use_utc_offset = 1;

    time_data->utc_offset = get_utc_offset();
    time_data->dst_flag = get_dst();
    LOG_INFO("Getting UTC offset %d (DST: %d)", time_data->utc_offset, time_data->dst_flag);

#ifdef _WIN32
    time_data->use_win_name = 1;
    time_data->use_linux_name = 0;

    char* win_tz_name = NULL;
    runcmd("powershell.exe \"$tz = Get-TimeZone; $tz.Id\" ", &win_tz_name);
    LOG_DEBUG("Getting Windows TimeZone %s", win_tz_name);
    if (strlen(win_tz_name) >= 1) {
        win_tz_name[strlen(win_tz_name) - 1] = '\0';
    }
    safe_strncpy(time_data->win_tz_name, win_tz_name,
                 min(sizeof(time_data->win_tz_name), strlen(win_tz_name) + 1));
    free(win_tz_name);

    return 0;
#elif __APPLE__
    time_data->use_win_name = 0;
    time_data->use_linux_name = 1;

    char* linux_tz_name = NULL;
    runcmd(
        "path=$(readlink /etc/localtime); echo "
        "${path#\"/var/db/timezone/zoneinfo/\"}",
        &linux_tz_name);
    safe_strncpy(time_data->linux_tz_name, linux_tz_name,
                 min(sizeof(time_data->linux_tz_name), strlen(linux_tz_name) + 1));
    free(linux_tz_name);

    return 0;
#else
    time_data->use_win_name = 0;
    time_data->use_linux_name = 1;
    time_data->use_utc_offset = 1;

    time_data->utc_offset = get_utc_offset();
    LOG_INFO("Sending UTC offset %d", time_data->utc_offset);
    time_data->dst_flag = get_dst();

    char* linux_tz_name = NULL;
    runcmd("cat /etc/timezone", &linux_tz_name);
    safe_strncpy(time_data->linux_tz_name, linux_tz_name,
                 min(sizeof(time_data->linux_tz_name), strlen(linux_tz_name) + 1));
    free(linux_tz_name);

    return 0;
#endif
}
