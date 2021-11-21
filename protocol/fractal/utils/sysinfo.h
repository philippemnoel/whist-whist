#ifndef SYSINFO_H
#define SYSINFO_H
/**
 * Copyright 2021 Whist Technologies, Inc.
 * @file sysinfo.h
 * @brief This file contains all code that logs client hardware specifications.
============================
Usage
============================

Call the respective functions to log a device's OS, model, CPU, RAM, etc.
*/

/*
============================
Defines
============================
*/

#define BYTES_IN_GB (1024.0 * 1024.0 * 1024.0)

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Prints the OS name,
 */
void print_os_info();

/**
 * @brief                          Prints the OS model and build version
 */
void print_model_info();

/**
 * @brief                          Prints the monitors
 */
void print_monitors();

/**
 * @brief                          Prints the total RAM and used RAM
 */
void print_ram_info();

/**
 * @brief                          Prints the CPU specs, vendor and usage
 */
void print_cpu_info();

/**
 * @brief                          Prints the total storage and the available
 * storage
 */
void print_hard_drive_info();

/**
 * @brief                          Prints the memory
 */
void print_memory_info();

#endif  // SYS_INFO
