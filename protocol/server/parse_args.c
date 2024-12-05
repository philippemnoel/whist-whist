/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file parse_args.c
 * @brief This file contains the code that parses the args for
the server protocol.
============================
Usage
============================

server_parse_args() checks and parses the args for server/main.c
*/

/*
============================
Includes
============================
*/

#include <whist/core/whist.h>
#include <whist/core/whist_string.h>
#include <whist/logging/error_monitor.h>
#include "whist/utils/command_line.h"
#include "parse_args.h"
#include "network.h"
#include "state.h"

/*
============================
Command-line options
============================
*/

static const char* private_key_option;
COMMAND_LINE_STRING_OPTION(private_key_option, 'k', "private-key", 32,
                           "Pass in the RSA Private Key as a hexadecimal string.")

static int connect_timeout_option = 60;
COMMAND_LINE_INT_OPTION(connect_timeout_option, 't', "connect-timeout", -1, 3600,
                        "Tell the server to give up on an initial connection "
                        "after TIME seconds.  If TIME is -1, disable auto exit "
                        "completely.  Default: 60.")

static int disconnect_timeout_option = 60;
COMMAND_LINE_INT_OPTION(disconnect_timeout_option, 'T', "disconnect-timeout", -1, 3600,
                        "Tell the server to shut down after one successful connection "
                        "after TIME seconds. If TIME is -1, disable auto exit completely. "
                        "Default: 60.")

static const char* identifier_option;
COMMAND_LINE_STRING_OPTION(identifier_option, 'i', "identifier", WHIST_IDENTIFIER_MAXLEN,
                           "Pass in the unique identifier for this "
                           "server as a hexadecimal string.")

/*
============================
Public Function Implementations
============================
*/

int server_parse_args(whist_server_config* config, int argc, char* argv[]) {
    int ret = whist_parse_command_line(argc, (const char**)argv, NULL);
    if (ret != WHIST_SUCCESS) {
        printf("Failed to parse command line!\n");
        return -1;
    }

    config->wait_time_to_exit = connect_timeout_option;
    config->disconnect_time_to_exit = disconnect_timeout_option;

    safe_strncpy(config->identifier, identifier_option, sizeof(config->identifier));

    if (private_key_option) {
        if (!read_hexadecimal_private_key(private_key_option, config->binary_aes_private_key,
                                          config->hex_aes_private_key)) {
            printf("Invalid private key \"%s\".\n", private_key_option);
            return -1;
        }
    } else {
        memcpy(config->binary_aes_private_key, DEFAULT_BINARY_PRIVATE_KEY,
               sizeof(config->binary_aes_private_key));
        memcpy(config->hex_aes_private_key, DEFAULT_HEX_PRIVATE_KEY,
               sizeof(config->hex_aes_private_key));
    }

    return 0;
}
