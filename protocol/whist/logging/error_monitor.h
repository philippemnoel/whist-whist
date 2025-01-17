#ifndef ERROR_MONITOR_H
#define ERROR_MONITOR_H
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file error_monitor.h
 * @brief This file contains the functions to configure and report breadcrumbs
 *        and error events to tools such as Sentry.
============================
Usage
============================
The error monitor needs to have an environment configured before it can be
started up. This environment should be development/staging/production, and is
passed in as a command-line parameter. Once the value is known, we may call
`whist_error_monitor_set_environment()` to configure it.

If no environment is set, the error monitor will fail to initialize. This is
because we don't want it to complain in personal setups and manual connections
when developing, but it's important to note the side-effect that we will not
report to the error monitoring service if environment isn't passed in.

To initialize the error monitor, we call `error_monitor_initialize()`. After
doing so, we can configure the error logging metadata with
`whist_error_monitor_set_username()` and `whist_error_monitor_set_connection_id()`.

At this point, calling `whist_error_monitor_log_breadcrumb()` and
`whist_error_monitor_log_error()` will report a trace of non-error events and report
a detailed breakdown for error-level events to the error monitor service,
respectively. Importantly, *you should almost never be calling this function by
itself*. Instead, simply use the `LOG_*()` functions from logging.h, which will
automatically send error monitor breadcrumbs and error reports as needed.

Because of this integration with our logging setup, we run into race conditions
in `whist_error_monitor_shutdown()`, which may cause us to fail to report our last
few breadcrumbs and error events. In order to avoid this, we must call
`whist_error_monitor_shutdown()` after calling `destroy_logger()`, to allow for any
pending error monitor log calls to be handled. Eventually, we should set up
a more robust solution for synchronizing these calls.
*/

/*
============================
Includes
============================
*/

#include <whist/core/whist.h>

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Configures the error monitor to use the
 *                                 provided environment.
 *
 * @param environment              The environment to set: should be one of
 *                                 development/staging/production.
 *
 * @note                           This should only be called before calling
 *                                 `error_monitor_initialize()`.
 */
void whist_error_monitor_set_environment(const char *environment);

/**
 * @brief                          Get the error_monitor_environment name, if initialided
 *
 * @returns                        A newly-allocated string with a copy of the
 *                                 error_monitor_environment string, if it is set.
 *                                 Otherwise, returns NULL.
 */
char *get_error_monitor_environment(void);

/**
 * @brief                          Check if the error monitor enviroment name was set.
 *
 * @returns                       The error monitor environment's name if it is set,
 *                                NULL otherwise.
 */
bool whist_error_monitor_environment_set(void);

/**
 * @brief                          Configures the error monitor to report
 *                                 the provided session-id on logs.
 *
 * @param session_id               The session id to set.
 *
 * @note                           This should only be called before calling
 *                                 `error_monitor_initialize()`.
 */
void whist_error_monitor_set_session_id(const char *session_id);

/**
 * @brief                          Get the session ID.

 * @returns                        A newly-allocated string with a copy of the session ID.
 */
char *get_error_monitor_session_id(void);

/**
 * @brief                          Configures the connection id tag for error
 *                                 monitor reports.
 *
 * @param id                       The connection id to set. Setting -1 will
 *                                 set the connection id to "waiting".
 *
 * @note                           This should only be called after calling
 *                                 `error_monitor_initialize()`.
 */
void whist_error_monitor_set_connection_id(int id);

/**
 * @brief                         Check if the error monitor is initialized.
 *
 * @returns                       True if the error monitor is initialized,
 *                                false otherwise.
 */
bool whist_error_monitor_is_initialized(void);

/**
 * @brief                          Initializes the error monitor.
 *
 * @param is_client                True if this is the client protocol, else
 *                                 false.
 *
 * @note                           This will do nothing if the environment
 *                                 has not already been set.
 */
void whist_error_monitor_initialize(bool is_client);

/**
 * @brief                          Shuts down the error monitor.
 *
 * @note                           This should only be called after calling
 *                                 logging.h's `destroy_logger()`. See the
 *                                 "Usage" section above for details.
 */
void whist_error_monitor_shutdown(void);

/**
 * @brief                          Logs a breadcrumb to the error monitor.
 *
 * @param tag                      The level of the breadcrumb; for example,
 *                                 "WARNING" or "INFO".
 *
 * @param message                  The message associated with the breadcrumb.
 *
 * @note                           This is automatically called from the
 *                                 `LOG_*` functions from logging.h, and should
 *                                 almost never be called alone.
 */
void whist_error_monitor_log_breadcrumb(const char *tag, const char *message);

/**
 * @brief                          Logs an error event to the error monitor.
 *
 * @param message                  The message associated with the error event.
 *
 * @note                           This is automatically called from the
 *                                 `LOG_*` functions from logging.h, and should
 *                                 almost never be called alone.
 */
void whist_error_monitor_log_error(const char *message);

#endif  // ERROR_MONITOR_H
