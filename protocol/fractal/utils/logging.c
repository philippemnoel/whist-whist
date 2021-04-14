/**
 * Copyright Fractal Computers, Inc. 2020
 * @file logging.c
 * @brief This file contains the logging macros and utils to send Winlogon
 *        status and to send the logs to the webserver.
============================
Usage
============================
We have several levels of logging.
- NO_LOG: self explanatory
- ERROR_LEVEL: only log errors. Errors are problems that must be addressed,
               as they indicate a fundamental probem with the protocol,
               but they represent a status that can be recovered from.
- WARNING_LEVEL: log warnings and above (warnings and errors). Warnings are when
                 something did not work as expected, but do not necessarily imply
                 that the protocol is at fault, as it may be an issue with
                 their environment (No audio device found, packet loss during handshake, etc).
- INFO_LEVEL: log info and above. Info is just for logs that provide additional
              information on the state of the protocol. e.g. decode time
- DEBUG_LEVEL: log debug and above. For use when actively debugging a problem,
               but for things that don't need to be logged regularly
The log level defaults to DEBUG_LEVEL, but it can also be passed as a compiler
flag, as it is in the root CMakesList.txt, which sets it to DEBUG_LEVEL for
Debug builds and WARNING_LEVEL for release builds.
Note that these macros do not need an additional \n character at the end of your
format strings.
*/

#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>

#ifdef _WIN32
#define _NO_CVCONST_H
#include <Windows.h>
#include <DbgHelp.h>
#include <process.h>
#define strtok_r strtok_s
#else
#include <execinfo.h>
#include <signal.h>
#endif

#include <fractal/core/fractal.h>
#include "../network/network.h"
#include "logging.h"
#include <sentry.h>

char* get_logger_history();
int get_logger_history_len();
void init_backtrace_handler();

extern int connection_id;
extern char sentry_environment[FRACTAL_ARGS_MAXLEN + 1];
extern bool using_sentry;

// logger Semaphores and Mutexes
static volatile SDL_sem* logger_semaphore;
static volatile SDL_mutex* logger_mutex;

// logger queue

typedef struct LoggerQueueItem {
    int id;
    bool log;
    char buf[LOGGER_BUF_SIZE];
} LoggerQueueItem;
static volatile LoggerQueueItem logger_queue[LOGGER_QUEUE_SIZE];
static volatile LoggerQueueItem logger_queue_cache[LOGGER_QUEUE_SIZE];
static volatile int logger_queue_index = 0;
static volatile int logger_queue_size = 0;
static volatile int logger_global_id = 0;

// logger global variables
SDL_Thread* mprintf_thread = NULL;
static volatile bool run_multithreaded_printf;
int multi_threaded_printf(void* opaque);
void mprintf(bool log, const char* fmt_str, va_list args);
clock mprintf_timer;
FILE* mprintf_log_file = NULL;
FILE* mprintf_log_connection_file = NULL;
int log_connection_log_id;
char* log_directory = NULL;

// This is written to in MultiThreaderPrintf
#define LOG_CACHE_SIZE 1000000
char logger_history[LOG_CACHE_SIZE];
int logger_history_len;

char* get_logger_history() { return logger_history; }
int get_logger_history_len() { return logger_history_len; }

void start_connection_log();

// Initializes the logger and starts a connection log
void init_logger(char* log_dir) {
    init_backtrace_handler();

    if (using_sentry) {
        sentry_options_t* options = sentry_options_new();
        // sentry_options_set_debug(options, true);  // if sentry is playing up uncomment this
        sentry_options_set_dsn(options, SENTRY_DSN);
        // These are used by sentry to classify events and so we can keep track of version specific
        // issues.
        char release[200];
        snprintf(release, sizeof(release), "fractal-protocol@%s", fractal_git_revision());
        sentry_options_set_release(options, release);
        sentry_options_set_environment(options, sentry_environment);
        sentry_init(options);
    }

    logger_history_len = 0;
    char f[1000] = "";
    if (log_dir) {
        size_t dir_len = strlen(log_dir);
        log_directory = (char*)safe_malloc(dir_len + 2);
        strncpy(log_directory, log_dir, dir_len + 1);
#if defined(_WIN32)
        log_directory[dir_len] = '\\';
#else
        log_directory[dir_len] = '/';
#endif
        log_directory[dir_len + 1] = '\0';
        strcat(f, log_directory);
        
        if (strcmp(sentry_environment, "production") == 0) {
            strcat(f, "log-prod.txt");
        }
        else if strcmp(sentry_environment, "staging") == 0) {
            strcat(f, "log-staging.txt");
        } else {
            strcat(f, "log-dev.txt");
        }

#if defined(_WIN32)
        CreateDirectoryA(log_directory, 0);
#else
        mkdir(log_directory, 0755);
#endif
        printf("Trying to open up %s \n", f);
        mprintf_log_file = fopen(f, "ab");
        if (mprintf_log_file == NULL) {
            printf("Couldn't open up logfile\n");
        }
    }

    run_multithreaded_printf = true;
    logger_mutex = safe_SDL_CreateMutex();
    logger_semaphore = SDL_CreateSemaphore(0);
    mprintf_thread =
        SDL_CreateThread((SDL_ThreadFunction)multi_threaded_printf, "MultiThreadedPrintf", NULL);
    LOG_INFO("Writing logs to %s", f);
    //    start_timer(&mprintf_timer);
}

// Sets up logs for a new connection, overwriting previous
void start_connection_log() {
    safe_SDL_LockMutex((SDL_mutex*)logger_mutex);

    if (mprintf_log_connection_file) {
        fclose(mprintf_log_connection_file);
    }
    char log_connection_directory[1000] = "";
    strcat(log_connection_directory, log_directory);
    strcat(log_connection_directory, "log_connection.txt");
    mprintf_log_connection_file = fopen(log_connection_directory, "w+b");
    log_connection_log_id = logger_global_id;

    safe_SDL_UnlockMutex((SDL_mutex*)logger_mutex);

    LOG_INFO("Beginning connection log");
}

void destroy_logger() {
    // Wait for any remaining printfs to execute
    SDL_Delay(50);
    if (using_sentry) {
        sentry_shutdown();
    }
    run_multithreaded_printf = false;
    SDL_SemPost((SDL_sem*)logger_semaphore);

    SDL_WaitThread(mprintf_thread, NULL);
    mprintf_thread = NULL;

    if (mprintf_log_file) {
        fclose(mprintf_log_file);
    }
    mprintf_log_file = NULL;

    logger_history[0] = '\0';
    logger_history_len = 0;
    if (log_directory) {
        free(log_directory);
    }
}

void sentry_send_bread_crumb(char* tag, const char* fmt_str, ...) {
    if (!using_sentry) return;

        // in the current sentry-native beta version, breadcrumbs prevent sentry
        //  from sending Windows events to the dashboard, so we only log
        //  breadcrumbs for OSX and Linux
#ifndef _WIN32
    va_list args;
    va_start(args, fmt_str);
    char sentry_str[LOGGER_BUF_SIZE];
    sprintf(sentry_str, fmt_str, args);
    sentry_value_t crumb = sentry_value_new_breadcrumb("default", sentry_str);
    sentry_value_set_by_key(crumb, "category", sentry_value_new_string("protocol-logs"));
    sentry_value_set_by_key(crumb, "level", sentry_value_new_string(tag));
    sentry_add_breadcrumb(crumb);
    va_end(args);
#endif
}

void sentry_send_event(const char* fmt_str, ...) {
    if (!using_sentry) return;

    va_list args;
    va_start(args, fmt_str);
    char sentry_str[LOGGER_BUF_SIZE];
    sprintf(sentry_str, fmt_str, args);
    va_end(args);
    sentry_value_t event = sentry_value_new_message_event(
        /*   level */ SENTRY_LEVEL_ERROR,
        /*  logger */ "client-logs",
        /* message */ sentry_str);
    sentry_capture_event(event);
}

int multi_threaded_printf(void* opaque) {
    UNUSED(opaque);

    while (true) {
        // Wait until signaled by printf to begin running
        SDL_SemWait((SDL_sem*)logger_semaphore);

        if (!run_multithreaded_printf) {
            break;
        }

        int cache_size = 0;

        // Clear the queue into the cache,
        // And then let go of the mutex so that printf can continue accumulating
        safe_SDL_LockMutex((SDL_mutex*)logger_mutex);
        cache_size = logger_queue_size;
        for (int i = 0; i < logger_queue_size; i++) {
            safe_strncpy((char*)logger_queue_cache[i].buf,
                         (const char*)logger_queue[logger_queue_index].buf, LOGGER_BUF_SIZE);
            logger_queue_cache[i].log = logger_queue[logger_queue_index].log;
            logger_queue_cache[i].id = logger_queue[logger_queue_index].id;
            logger_queue[logger_queue_index].buf[0] = '\0';
            logger_queue_index++;
            logger_queue_index %= LOGGER_QUEUE_SIZE;
            if (i != 0) {
                SDL_SemWait((SDL_sem*)logger_semaphore);
            }
        }
        logger_queue_size = 0;
        safe_SDL_UnlockMutex((SDL_mutex*)logger_mutex);

        // Print all of the data into the cache
        // int last_printf = -1;
        for (int i = 0; i < cache_size; i++) {
            if (logger_queue_cache[i].log) {
                if (mprintf_log_file) {
                    fprintf(mprintf_log_file, "%s", logger_queue_cache[i].buf);
                }
                if (mprintf_log_connection_file &&
                    logger_queue_cache[i].id >= log_connection_log_id) {
                    fprintf(mprintf_log_connection_file, "%s", logger_queue_cache[i].buf);
                }
            }
            // if (i + 6 < cache_size) {
            //  printf("%s%s%s%s%s%s%s", logger_queue_cache[i].buf,
            // logger_queue_cache[i+1].buf, logger_queue_cache[i+2].buf,
            // logger_queue_cache[i+3].buf, logger_queue_cache[i+4].buf,
            // logger_queue_cache[i+5].buf,  logger_queue_cache[i+6].buf);
            //    last_printf = i + 6;
            //} else if (i > last_printf) {
            logger_queue_cache[i].buf[LOGGER_BUF_SIZE - 5] = '.';
            logger_queue_cache[i].buf[LOGGER_BUF_SIZE - 4] = '.';
            logger_queue_cache[i].buf[LOGGER_BUF_SIZE - 3] = '.';
            logger_queue_cache[i].buf[LOGGER_BUF_SIZE - 2] = '\n';
            logger_queue_cache[i].buf[LOGGER_BUF_SIZE - 1] = '\0';
            fprintf(stdout, "%s", logger_queue_cache[i].buf);

            int chars_written =
                sprintf(&logger_history[logger_history_len], "%s", logger_queue_cache[i].buf);
            logger_history_len += chars_written;

            // Shift buffer over if too large;
            if ((unsigned long)logger_history_len >
                sizeof(logger_history) - sizeof(logger_queue_cache[i].buf) - 10) {
                int new_len = sizeof(logger_history) / 3;
                for (i = 0; i < new_len; i++) {
                    logger_history[i] = logger_history[logger_history_len - new_len + i];
                }
                logger_history_len = new_len;
            }
            //}
        }
        if (mprintf_log_file) {
            fflush(mprintf_log_file);
        }
        if (mprintf_log_connection_file) {
            fflush(mprintf_log_connection_file);
        }

#define MAX_LOG_FILE_SIZE (5 * BYTES_IN_KILOBYTE * BYTES_IN_KILOBYTE)

        // If the log file is large enough, cache it
        if (mprintf_log_file) {
            fseek(mprintf_log_file, 0L, SEEK_END);
            int sz = ftell(mprintf_log_file);

            // If it's larger than 5MB, start a new file and store the old one
            if (sz > MAX_LOG_FILE_SIZE) {
                fclose(mprintf_log_file);

                char f[1000] = "";
                strcat(f, log_directory);
                strcat(f, "log.txt");
                char fp[1000] = "";
                strcat(fp, log_directory);
                strcat(fp, "log_prev.txt");
#if defined(_WIN32)
                WCHAR wf[1000];
                WCHAR wfp[1000];
                mbstowcs(wf, f, sizeof(wf));
                mbstowcs(wfp, fp, sizeof(wfp));
                DeleteFileW(wfp);
                MoveFileW(wf, wfp);
                DeleteFileW(wf);
#endif
                mprintf_log_file = fopen(f, "ab");
            }
        }

        // If the log file is large enough, cache it
        if (mprintf_log_connection_file) {
            fseek(mprintf_log_connection_file, 0L, SEEK_END);
            long sz = ftell(mprintf_log_connection_file);

            // If it's larger than 5MB, start a new file and store the old one
            if (sz > MAX_LOG_FILE_SIZE) {
                long buf_len = MAX_LOG_FILE_SIZE / 2;

                char* original_buf = safe_malloc(buf_len);
                char* buf = original_buf;
                fseek(mprintf_log_connection_file, -buf_len, SEEK_END);
                fread(buf, buf_len, 1, mprintf_log_connection_file);

                while (buf_len > 0 && buf[0] != '\n') {
                    buf++;
                    buf_len--;
                }

                char f[1000] = "";
                strcat(f, log_directory);
                strcat(f, "log_connection.txt");
                mprintf_log_connection_file = freopen(f, "wb", mprintf_log_connection_file);
                fwrite(buf, buf_len, 1, mprintf_log_connection_file);
                fflush(mprintf_log_connection_file);

                free(original_buf);
            }
        }
    }
    return 0;
}

/**
 * This function escapes certain escape sequences in a log. It allocates heap
 * memory that must be later freed
 * @param old_string a string with sequences we want escaped, e.g "some json
 * stuff \r\n\r\n"
 * @return the same string, but possible longer e.g "some json stuff
 * \\r\\n\\r\\n"
 */
char* escape_string(char* old_string, bool escape_all) {
    size_t old_string_len = strlen(old_string);
    char* new_string = safe_malloc(2 * (old_string_len + 1));
    int new_str_len = 0;
    for (size_t i = 0; i < old_string_len; i++) {
        switch (old_string[i]) {
            case '\b':
                new_string[new_str_len++] = '\\';
                new_string[new_str_len++] = 'b';
                break;
            case '\f':
                new_string[new_str_len++] = '\\';
                new_string[new_str_len++] = 'f';
                break;
            case '\r':
                new_string[new_str_len++] = '\\';
                new_string[new_str_len++] = 'r';
                break;
            case '\t':
                new_string[new_str_len++] = '\\';
                new_string[new_str_len++] = 't';
                break;
            case '\"':
                if (escape_all) {
                    new_string[new_str_len++] = '\\';
                    new_string[new_str_len++] = '\"';
                    break;
                }
                // fall through
            case '\\':
                if (escape_all) {
                    new_string[new_str_len++] = '\\';
                    new_string[new_str_len++] = '\\';
                    break;
                }
                // fall through
            case '\n':
                if (escape_all) {
                    new_string[new_str_len++] = '\\';
                    new_string[new_str_len++] = 'n';
                    break;
                }
                // fall through
            default:
                new_string[new_str_len++] = old_string[i];
                break;
        }
    }
    new_string[new_str_len++] = '\0';
    return new_string;
}

// Our vararg function that gets called from LOG_INFO, LOG_WARNING, etc macros
void internal_logging_printf(const char* fmt_str, ...) {
    va_list args;
    va_start(args, fmt_str);

    // Map to mprintf
    mprintf(WRITE_MPRINTF_TO_LOG, fmt_str, args);
    va_end(args);
}

// Core multithreaded printf function, that accepts va_list and log boolean
void mprintf(bool log, const char* fmt_str, va_list args) {
    if (mprintf_thread == NULL) {
        printf("initLogger has not been called! Printing below...\n");
        vprintf(fmt_str, args);
        return;
    }

    safe_SDL_LockMutex((SDL_mutex*)logger_mutex);

    int index = (logger_queue_index + logger_queue_size) % LOGGER_QUEUE_SIZE;
    char* buf = NULL;
    if (logger_queue_size < LOGGER_QUEUE_SIZE - 2) {
        logger_queue[index].log = log;
        logger_queue[index].id = logger_global_id++;
        buf = (char*)logger_queue[index].buf;

        if (buf[0] != '\0') {
            char old_msg[LOGGER_BUF_SIZE];
            memcpy(old_msg, buf, LOGGER_BUF_SIZE);
            int chars_written =
                snprintf(buf, LOGGER_BUF_SIZE, "OLD MESSAGE: %s\nTRYING TO OVERWRITE WITH: %s\n",
                         old_msg, logger_queue[index].buf);
            if (!(chars_written > 0 && chars_written <= LOGGER_BUF_SIZE)) {
                buf[0] = '\0';
            }
            logger_queue_size++;
            SDL_SemPost((SDL_sem*)logger_semaphore);
        } else {
            // Get the length of the formatted string with args replaced.
            // After calls to function which invoke VA args, the args are
            // undefined so we copy
            va_list args_copy;
            va_copy(args_copy, args);
            int len = vsnprintf(NULL, 0, fmt_str, args) + 1;

            // print to a temp buf so we can split on \n
            char* temp_buf = safe_malloc(sizeof(char) * (len + 1));
            vsnprintf(temp_buf, len, fmt_str, args_copy);
            // use strtok_r over strtok due to thread safety
            char* strtok_context = NULL;  // strtok_r context var
            // Log the first line out of the loop because we log it with
            // the full log formatting time | type | file | log_msg
            // subsequent lines start with | followed by 4 spaces
            char* line = strtok_r(temp_buf, "\n", &strtok_context);
            char* san_line = escape_string(line, false);
            snprintf(buf, LOGGER_BUF_SIZE, "%s \n", san_line);
            free(san_line);
            logger_queue_size++;
            SDL_SemPost((SDL_sem*)logger_semaphore);

            index = (logger_queue_index + logger_queue_size) % LOGGER_QUEUE_SIZE;
            buf = (char*)logger_queue[index].buf;
            line = strtok_r(NULL, "\n", &strtok_context);
            while (line != NULL) {
                san_line = escape_string(line, false);
                snprintf(buf, LOGGER_BUF_SIZE, "|    %s \n", san_line);
                free(san_line);
                logger_queue[index].log = log;
                logger_queue[index].id = logger_global_id++;
                logger_queue_size++;
                SDL_SemPost((SDL_sem*)logger_semaphore);
                index = (logger_queue_index + logger_queue_size) % LOGGER_QUEUE_SIZE;
                buf = (char*)logger_queue[index].buf;
                line = strtok_r(NULL, "\n", &strtok_context);
            }

            va_end(args_copy);
        }

    } else if (logger_queue_size == LOGGER_QUEUE_SIZE - 2) {
        buf = (char*)logger_queue[index].buf;
        safe_strncpy(buf, "Buffer maxed out!!!\n", LOGGER_BUF_SIZE);
        logger_queue[index].log = log;
        logger_queue[index].id = logger_global_id++;
        logger_queue_size++;
        SDL_SemPost((SDL_sem*)logger_semaphore);
    }

    safe_SDL_UnlockMutex((SDL_mutex*)logger_mutex);
}

SDL_mutex* crash_handler_mutex;

void print_stacktrace() {
    safe_SDL_LockMutex(crash_handler_mutex);

#ifdef _WIN32
    unsigned int i;
    void* stack[100];
    unsigned short frames;
    char buf[sizeof(SYMBOL_INFO) + 256 * sizeof(char)];
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)buf;
    HANDLE process;

    process = GetCurrentProcess();

    SymInitialize(process, NULL, TRUE);

    frames = CaptureStackBackTrace(0, 100, stack, NULL);
    memset(symbol, 0, sizeof(buf));
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

    for (i = 0; i < frames; i++) {
        SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);

        fprintf(stderr, "%i: %s - 0x%0llx\n", frames - i - 1, symbol->Name, symbol->Address);
    }
#else
#define HANDLER_ARRAY_SIZE 100

    void* array[HANDLER_ARRAY_SIZE];
    size_t size;

    // get void*'s for all entries on the stack
    size = backtrace(array, HANDLER_ARRAY_SIZE);

    // print out all the frames to stderr
    backtrace_symbols_fd(array, size, STDERR_FILENO);

    // and to the log
    int fd = fileno(mprintf_log_file);
    backtrace_symbols_fd(array, size, fd);

    fprintf(stderr, "addr2line -e build64/FractalServer");
    for (size_t i = 0; i < size; i++) {
        fprintf(stderr, " %p", array[i]);
    }
    fprintf(stderr, "\n\n");
#endif

    safe_SDL_UnlockMutex(crash_handler_mutex);
}

#ifdef _WIN32
LONG WINAPI windows_exception_handler(EXCEPTION_POINTERS* ExceptionInfo) {  // NOLINT
    SDL_Delay(250);
    fprintf(stderr, "\n");
    switch (ExceptionInfo->ExceptionRecord->ExceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:
            fprintf(stderr, "Error: EXCEPTION_ACCESS_VIOLATION\n");
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            fprintf(stderr, "Error: EXCEPTION_ARRAY_BOUNDS_EXCEEDED\n");
            break;
        case EXCEPTION_BREAKPOINT:
            fprintf(stderr, "Error: EXCEPTION_BREAKPOINT\n");
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            fprintf(stderr, "Error: EXCEPTION_DATATYPE_MISALIGNMENT\n");
            break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            fprintf(stderr, "Error: EXCEPTION_FLT_DENORMAL_OPERAND\n");
            break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            fprintf(stderr, "Error: EXCEPTION_FLT_DIVIDE_BY_ZERO\n");
            break;
        case EXCEPTION_FLT_INEXACT_RESULT:
            fprintf(stderr, "Error: EXCEPTION_FLT_INEXACT_RESULT\n");
            break;
        case EXCEPTION_FLT_INVALID_OPERATION:
            fprintf(stderr, "Error: EXCEPTION_FLT_INVALID_OPERATION\n");
            break;
        case EXCEPTION_FLT_OVERFLOW:
            fprintf(stderr, "Error: EXCEPTION_FLT_OVERFLOW\n");
            break;
        case EXCEPTION_FLT_STACK_CHECK:
            fprintf(stderr, "Error: EXCEPTION_FLT_STACK_CHECK\n");
            break;
        case EXCEPTION_FLT_UNDERFLOW:
            fprintf(stderr, "Error: EXCEPTION_FLT_UNDERFLOW\n");
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            fprintf(stderr, "Error: EXCEPTION_ILLEGAL_INSTRUCTION\n");
            break;
        case EXCEPTION_IN_PAGE_ERROR:
            fprintf(stderr, "Error: EXCEPTION_IN_PAGE_ERROR\n");
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            fprintf(stderr, "Error: EXCEPTION_INT_DIVIDE_BY_ZERO\n");
            break;
        case EXCEPTION_INT_OVERFLOW:
            fprintf(stderr, "Error: EXCEPTION_INT_OVERFLOW\n");
            break;
        case EXCEPTION_INVALID_DISPOSITION:
            fprintf(stderr, "Error: EXCEPTION_INVALID_DISPOSITION\n");
            break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            fprintf(stderr, "Error: EXCEPTION_NONCONTINUABLE_EXCEPTION\n");
            break;
        case EXCEPTION_PRIV_INSTRUCTION:
            fprintf(stderr, "Error: EXCEPTION_PRIV_INSTRUCTION\n");
            break;
        case EXCEPTION_SINGLE_STEP:
            fprintf(stderr, "Error: EXCEPTION_SINGLE_STEP\n");
            break;
        case EXCEPTION_STACK_OVERFLOW:
            fprintf(stderr, "Error: EXCEPTION_STACK_OVERFLOW\n");
            break;
        default:
            fprintf(stderr, "Error: Unrecognized Exception\n");
            break;
    }
    fflush(stderr);
    /* If this is a stack overflow then we can't walk the stack, so just show
      where the error happened */
    if (EXCEPTION_STACK_OVERFLOW != ExceptionInfo->ExceptionRecord->ExceptionCode) {
        print_stacktrace();
    } else {
    }

    return EXCEPTION_EXECUTE_HANDLER;
}
#else
void crash_handler(int sig) {
    fprintf(stderr, "\nError: signal %d:\n", sig);
    print_stacktrace();
    SDL_Delay(100);
    exit(-1);
}
#endif

void init_backtrace_handler() {
    crash_handler_mutex = safe_SDL_CreateMutex();
#ifdef _WIN32
    SetUnhandledExceptionFilter(windows_exception_handler);
#else
    signal(SIGSEGV, crash_handler);
#endif
}

char* get_version() {
    static char* version = NULL;

    if (version) {
        return version;
    }

#ifdef _WIN32
    char* version_filepath = "C:\\Program Files\\Fractal\\version";
#else
    char* version_filepath = "./version";
#endif

    size_t length;
    FILE* f = fopen(version_filepath, "r");

    if (f) {
        fseek(f, 0, SEEK_END);
        length = ftell(f);
        fseek(f, 0, SEEK_SET);
        static char buf[200];
        version = buf;
        int bytes = (int)fread(version, 1, min(length, (long)sizeof(buf) - 1),
                               f);  // cast for compiler warning
        for (int i = 0; i < bytes; i++) {
            if (version[i] == '\n') {
                version[i] = '\0';
                break;
            }
        }
        version[bytes] = '\0';
        fclose(f);
    } else {
        version = "NONE";
    }

    return version;
}

void save_connection_id(int connection_id_int) {
    char connection_id_filename[1000] = "";
    strcat(connection_id_filename, log_directory);
    strcat(connection_id_filename, "connection_id.txt");
    FILE* connection_id_file = fopen(connection_id_filename, "wb");
    fprintf(connection_id_file, "%d", connection_id_int);
    fclose(connection_id_file);

    // send connection id to sentry as a tag, client also does this
    if (using_sentry) {
        char str_connection_id[100];
        sprintf(str_connection_id, "%d", connection_id_int);
        sentry_set_tag("connection_id", str_connection_id);
    }
}

typedef struct UpdateStatusData {
    bool is_connected;
    char* host;
    char* identifier;
    char* hex_aes_private_key;
} UpdateStatusData;

int32_t multithreaded_update_server_status(void* data) {
    UpdateStatusData* d = data;

    char json[1000];
    snprintf(json, sizeof(json),
             "{\n"
             "  \"available\" : %s,\n"
             "  \"identifier\" : %s,\n"
             "  \"private_key\" : \"%s\"\n"
             "}",
             d->is_connected ? "false" : "true", d->identifier, d->hex_aes_private_key);
    send_post_request(d->host, "/container/ping", json, NULL, 0);

    free(d);
    return 0;
}

void update_server_status(bool is_connected, char* host, char* identifier,
                          char* hex_aes_private_key) {
    LOG_INFO("Update Status: %s", is_connected ? "Connected" : "Disconnected");
    UpdateStatusData* d = safe_malloc(sizeof(UpdateStatusData));
    d->is_connected = is_connected;
    d->host = host;
    d->identifier = identifier;
    d->hex_aes_private_key = hex_aes_private_key;
    SDL_Thread* update_status =
        SDL_CreateThread(multithreaded_update_server_status, "update_server_status", d);
    SDL_DetachThread(update_status);
}
