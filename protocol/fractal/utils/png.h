#ifndef PNG_H
#define PNG_H
/**
 * Copyright Fractal Computers, Inc. 2020
 * @file clock.h
 * @brief Helper functions for png to bmp and bmp to png
============================
Usage
============================
*/

/*
============================
Includes
============================
*/

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <time.h>
#endif

#include <libavcodec/avcodec.h>

/*
============================
Public Functions
============================
*/

char* read_file(const char* filename, size_t* char_nb);

int bmp_to_png(unsigned char* bmp, unsigned int size, AVPacket* pkt);

int png_to_bmp_char(char* png, int size, AVPacket* pkt);

int png_to_bmp(char* png, AVPacket* pkt);

#endif