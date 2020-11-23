/**
 * Copyright Fractal Computers, Inc. 2020
 * @file clock.c
 * @brief This file contains the helper functions for timing code.
============================
Usage
============================

You can use start_timer and get_timer to time specific pieces of code, or to
relate different events across server and client.
*/

#define _CRT_SECURE_NO_WARNINGS  // stupid Windows warnings

#define UNUSED(x) (void)(x)

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/fractal.h"
#include "../utils/logging.h"
#include "clock.h"

#ifdef _WIN32
int GetUTCOffset();
#endif

#if defined(_WIN32)
LARGE_INTEGER frequency;
bool set_frequency = false;
#endif

#define US_IN_MS 1000.0

void start_timer(clock* timer) {
#if defined(_WIN32)
    if (!set_frequency) {
        QueryPerformanceFrequency(&frequency);
        set_frequency = true;
    }
    QueryPerformanceCounter(timer);
#else
    // start timer
    gettimeofday(timer, NULL);
#endif
}

double get_timer(clock timer) {
#if defined(_WIN32)
    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);
    double ret = (double)(end.QuadPart - timer.QuadPart) / frequency.QuadPart;
#else
    // stop timer
    struct timeval t2;
    gettimeofday(&t2, NULL);

    // compute and print the elapsed time in millisec
    double elapsedTime = (t2.tv_sec - timer.tv_sec) * MS_IN_SECOND;  // sec to ms
    elapsedTime += (t2.tv_usec - timer.tv_usec) / US_IN_MS;          // us to ms

    // printf("elapsed time in ms is: %f\n", elapsedTime);

    // standard var to return and convert to seconds since it gets converted to
    // ms in function call
    double ret = elapsedTime / MS_IN_SECOND;
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
    snprintf(buffer, sizeof(buffer), "%02i:%02i:%02i.%03i", time_now.wHour, time_now.wMinute,
             time_now.wSecond, time_now.wMilliseconds);
#else
    struct tm* time_str_tm;
    struct timeval time_now;
    gettimeofday(&time_now, NULL);

    time_str_tm = gmtime(&time_now.tv_sec);
    snprintf(buffer, sizeof(buffer), "%02i:%02i:%02i:%06li", time_str_tm->tm_hour,
             time_str_tm->tm_min, time_str_tm->tm_sec, (long)time_now.tv_usec);
#endif

    //    strftime(buffer, 64, "%Y-%m-%d %H:%M:%S", timeinfo);

    return buffer;
}

int get_utc_offset() {
#if defined(_WIN32)
    return 0;
#else
    time_t t = time(NULL);
    struct tm lt = {0};
    localtime_r(&t, &lt);
    printf("dst flag %d \n \n", lt.tm_isdst);

    return (int)lt.tm_gmtoff / (60 * 60);
#endif
}

int get_dst() {
#if defined(_WIN32)
    return 0;
#else
    time_t t = time(NULL);
    struct tm lt = {0};
    localtime_r(&t, &lt);
    return lt.tm_isdst;
#endif
}

int get_time_data(FractalTimeData* time_data) {
#ifdef _WIN32
    time_data->use_win_name = 1;
    time_data->use_linux_name = 0;

    char* win_tz_name = NULL;
    runcmd("powershell.exe \"$tz = Get-TimeZone; $tz.Id\" ", &win_tz_name);
    strncpy(time_data->win_tz_name, win_tz_name, sizeof(time_data->win_tz_name));
    time_data->win_tz_name[strlen(time_data->win_tz_name) - 1] = '\0';
    free(win_tz_name);

    LOG_INFO("Sending Windows TimeZone %s", time_data->win_tz_name);

    return 0;
#elif __APPLE__
    time_data->use_win_name = 0;
    time_data->use_linux_name = 1;

    time_data->UTC_Offset = GetUTCOffset();
    LOG_INFO("Sending UTC offset %d", time_data->UTC_Offset);
    time_data->DST_flag = GetDST();

    char* response = NULL;
    runcmd(
        "path=$(readlink /etc/localtime); echo "
        "${path#\"/var/db/timezone/zoneinfo\"}",
        &response);
    strncpy(time_data->linux_tz_name, response, sizeof(time_data->linux_tz_name));
    free(response);

    return 0;
#else
    time_data->use_win_name = 0;
    time_data->use_linux_name = 1;

    time_data->UTC_Offset = get_utc_offset();
    LOG_INFO("Sending UTC offset %d", time_data->UTC_Offset);
    time_data->DST_flag = get_dst();

    char* response = NULL;
    runcmd("cat /etc/timezone", &response);
    strncpy(time_data->linux_tz_name, response, sizeof(time_data->linux_tz_name));
    free(response);

    return 0;
#endif
}

void set_timezone_from_iana_name(char* linux_tz_name, char* password) {
    // Two spaces to hide from bash history
    char cmd[2000] = "  echo %s | sudo -S timedatectl set-timezone %s";
    snprintf(cmd, sizeof(cmd), password, linux_tz_name);

    runcmd(cmd, NULL);

    return;
}

void set_timezone_from_windows_name(char* win_tz_name) {
    char cmd[500];
    //    Timezone name must end with no white space
    for (size_t i = 0; win_tz_name[i] != '\0'; i++) {
        if (win_tz_name[i] == '\n') {
            win_tz_name[i] = '\0';
        }
        if (win_tz_name[i] == '\r') {
            win_tz_name[i] = '\0';
        }
    }
    snprintf(cmd, sizeof(cmd), "powershell -command \"Set-TimeZone -Id '%s'\"", win_tz_name);

    char* response = NULL;
    runcmd(cmd, &response);
    LOG_INFO("Timezone powershell command: %s -> %s", cmd, response);
    free(response);
    return;
}

void set_timezone_from_utc(int utc, int DST_flag) {
#ifndef _WIN32
    // TODO come back to this when we have sudo password on linux server
    //    char cmd[5000];
    //    // Negative one because UNIX UTC values are flipped from usual. West
    //    is positive and east is negative. sprintf(cmd,"echo {INSERT PASSWORD
    //    HERE WHEN WE CAN} | sudo -S timedatectl set-timezoneEtc/GMT%d\0",
    //    -1*utc);
    return;
#else
    if (DST_flag > 0) {
        LOG_INFO("DST active");
        utc = utc - 1;
    }
    char* timezone;
    //    Open powershell " here closing " in timezone
    char cmd[5000] = "powershell.exe \"Set-TimeZone -Id \0";
    switch (utc) {
        case -12:
            timezone = " 'Dateline Standard Time' \" \0";
            break;
        case -11:
            timezone = " 'UTC-11' \" \0";
            break;
        case -10:
            timezone = " 'Hawaiian Standard Time' \" \0";
            break;
        case -9:
            timezone = " 'Alaskan Standard Time' \" \0";
            break;
        case -8:
            timezone = " 'Pacific Standard Time' \" \0";
            break;
        case -7:
            timezone = " 'Mountain Standard Time' \" \0";
            break;
        case -6:
            timezone = " 'Central Standard Time' \" \0";
            break;
        case -5:
            timezone = " 'US Eastern Standard Time' \" \0";
            break;
        case -4:
            timezone = " 'Atlantic Standard Time' \" \0";
            break;
        case -3:
            timezone = " ' E. South America Standard Time' \" \0";
            break;
        case -2:
            timezone = " 'Mid-Atlantic Standard Time'  \" \0";
            break;
        case -1:
            timezone = " 'Cape Verde Standard Time'  \" \0";
            break;
        case 0:
            timezone = " 'GMT Standard Time'  \" \0";
            break;
        case 1:
            timezone = " 'W. Europe Standard Time' \" \0";
            break;
        case 2:
            timezone = " 'E. Europe Standard Time' \" \0";
            break;
        case 3:
            timezone = " 'Turkey Standard Time' \" \0";
            break;
        case 4:
            timezone = " 'Arabian Standard Time' \" \0";
            break;
        default:
            LOG_WARNING("Note a valid UTC offset: %d", utc);
            return;
    }
    snprintf(cmd + strlen(cmd), strlen(timezone), timezone);
    char* response = malloc(sizeof(char) * 200);
    runcmd(cmd, &response);
    LOG_INFO("Timezone powershell command: %s ", cmd);
    free(response);
#endif
}
