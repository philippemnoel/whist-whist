/*
 * General Fractal helper functions and headers.
 *
 * Copyright Fractal Computers, Inc. 2020
 **/

#include "fractal.h"  // header file for this protocol, includes winsock

#include <ctype.h>
#include <stdio.h>

#include "../utils/json.h"
#include "../utils/sysinfo.h"

#define UNUSED(x) (void)(x)

// Print Memory Info

int MultithreadedPrintSystemInfo(void* opaque) {
    UNUSED(opaque);

    LOG_INFO("Hardware information:");

    PrintOSInfo();
    PrintModelInfo();
    PrintCPUInfo();
    PrintRAMInfo();
    PrintMonitors();
    PrintHardDriveInfo();

    return 0;
}

void PrintSystemInfo() {
    SDL_Thread* sysinfo_thread =
        SDL_CreateThread(MultithreadedPrintSystemInfo, "PrintSystemInfo", NULL);
    SDL_DetachThread(sysinfo_thread);
}

struct dynamic_buffer_struct {
    int size;
    int capacity;
    char* buf;
};

typedef struct dynamic_buffer_struct* dynamic_buffer;

dynamic_buffer init_dynamic_buffer() {
    dynamic_buffer db = malloc(sizeof(struct dynamic_buffer_struct));
    db->size = 0;
    db->capacity = 128;
    db->buf = malloc(db->capacity);
    if (!db->buf) {
        LOG_ERROR("Could not malloc size %d!", db->capacity);
        SDL_Delay(50);
        exit(-1);
    }
    return db;
}

void resize_dynamic_buffer(dynamic_buffer db, int new_size) {
    if (new_size > db->capacity) {
        int new_capacity = new_size * 2;
        char* new_buffer = realloc(db->buf, new_capacity);
        if (!new_buffer) {
            LOG_ERROR("Could not realloc from %d to %d!", db->capacity, new_capacity);
            SDL_Delay(50);
            exit(-1);
        } else {
            db->capacity = new_capacity;
            db->size = new_size;
            db->buf = new_buffer;
        }
    } else {
        db->size = new_size;
    }
}

void free_dynamic_buffer(dynamic_buffer db) {
    free(db->buf);
    free(db);
}

int runcmd(const char* cmdline, char** response) {
#ifdef _WIN32
    HANDLE hChildStd_IN_Rd = NULL;
    HANDLE hChildStd_IN_Wr = NULL;
    HANDLE hChildStd_OUT_Rd = NULL;
    HANDLE hChildStd_OUT_Wr = NULL;

    SECURITY_ATTRIBUTES saAttr;

    // Set the bInheritHandle flag so pipe handles are inherited.

    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // Create a pipe for the child process's STDOUT.

    if (response) {
        if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &saAttr, 0)) {
            LOG_ERROR("StdoutRd CreatePipe failed");
            *response = NULL;
            return -1;
        }
        if (!SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0)) {
            LOG_ERROR("Stdout SetHandleInformation failed");
            *response = NULL;
            return -1;
        }
        if (!CreatePipe(&hChildStd_IN_Rd, &hChildStd_IN_Wr, &saAttr, 0)) {
            LOG_ERROR("Stdin CreatePipe failed");
            *response = NULL;
            return -1;
        }
        if (!SetHandleInformation(hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0)) {
            LOG_ERROR("Stdin SetHandleInformation failed");
            *response = NULL;
            return -1;
        }
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (response) {
        si.hStdError = hChildStd_OUT_Wr;
        si.hStdOutput = hChildStd_OUT_Wr;
        si.hStdInput = hChildStd_IN_Rd;
        si.dwFlags |= STARTF_USESTDHANDLES;
    }
    ZeroMemory(&pi, sizeof(pi));

    char cmd_buf[1000];

    while (cmdline[0] == ' ') {
        cmdline++;
    }

    if (strlen((const char*)cmdline) + 1 > sizeof(cmd_buf)) {
        LOG_WARNING("runcmd cmdline too long!");
        if (response) {
            *response = NULL;
        }
        return -1;
    }

    memcpy(cmd_buf, cmdline, strlen((const char*)cmdline) + 1);

    SetEnvironmentVariableW((LPCWSTR)L"UNISON", (LPCWSTR)L"./.unison");

    if (CreateProcessA(NULL, (LPSTR)cmd_buf, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si,
                       &pi)) {
    } else {
        LOG_ERROR("CreateProcessA failed!");
        if (response) {
            *response = NULL;
        }
        return -1;
    }

    if (response) {
        CloseHandle(hChildStd_OUT_Wr);
        CloseHandle(hChildStd_IN_Rd);

        CloseHandle(hChildStd_IN_Wr);

        DWORD dwRead;
        CHAR chBuf[2048];
        BOOL bSuccess = FALSE;

        dynamic_buffer db = init_dynamic_buffer();
        for (;;) {
            bSuccess = ReadFile(hChildStd_OUT_Rd, chBuf, sizeof(chBuf), &dwRead, NULL);
            if (!bSuccess || dwRead == 0) break;

            int original_size = db->size;
            resize_dynamic_buffer(db, original_size + dwRead);
            memcpy(db->buf + original_size, chBuf, dwRead);
            if (!bSuccess) break;
        }

        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        resize_dynamic_buffer(db, db->size + 1);
        db->buf[db->size] = '\0';
        resize_dynamic_buffer(db, db->size - 1);
        int size = db->size;
        *response = db->buf;
        free(db);
        return size;
    } else {
        CloseHandle(hChildStd_OUT_Wr);
        CloseHandle(hChildStd_IN_Rd);
        CloseHandle(hChildStd_IN_Wr);

        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 0;
    }
#else
    if (response == NULL) {
        system(cmdline);
        return 0;
    } else {
        FILE* pPipe;

        /* Run DIR so that it writes its output to a pipe. Open this
         * pipe with read text attribute so that we can read it
         * like a text file.
         */

        char* cmd = malloc(strlen(cmdline) + 128);
        snprintf(cmd, strlen(cmdline) + 128, "%s 2>/dev/null", cmdline);

        if ((pPipe = popen(cmd, "r")) == NULL) {
            LOG_WARNING("Failed to popen %s", cmd);
            free(cmd);
            return -1;
        }
        free(cmd);

        /* Read pipe until end of file, or an error occurs. */

        int current_len = 0;
        dynamic_buffer db = init_dynamic_buffer();

        while (true) {
            char c = (char)fgetc(pPipe);

            resize_dynamic_buffer(db, current_len + 1);

            if (c == EOF) {
                db->buf[current_len] = '\0';
                break;
            } else {
                db->buf[current_len] = c;
                current_len++;
            }
        }

        *response = db->buf;
        free(db);

        /* Close pipe and print return value of pPipe. */
        if (feof(pPipe)) {
            return current_len;
        } else {
            LOG_WARNING("Error: Failed to read the pipe to the end.\n");
            *response = NULL;
            return -1;
        }
    }
#endif
}

char* get_ip() {
    static char ip[128];
    static bool already_obtained_ip = false;
    if (already_obtained_ip) {
        return ip;
    }

    char* buf;
    runcmd("curl ipinfo.io", &buf);

    json_t json;
    if (!parse_json(buf, &json)) {
        LOG_WARNING("curl ipinfo.io did not return an IP: %s", buf);
        return NULL;
    }
    kv_pair_t* kv = get_kv(&json, "ip");

    memcpy(ip, kv->str_value, sizeof(ip));

    free_json(json);

    already_obtained_ip = true;
    return ip;
}

bool read_hexadecimal_private_key(char* hex_string, char* private_key) {
    for (int i = 0; i < 16; i++) {
        if (!isxdigit(hex_string[2 * i]) || !isxdigit(hex_string[2 * i + 1]) ||
            hex_string[32] != '\0') {
            return false;
        }
        sscanf(&hex_string[2 * i], "%2hhx", &(private_key[i]));
    }
    return true;
}

static char aes_private_key[16];
static char* branch = NULL;
static bool is_dev;
static bool already_obtained_vm_type = false;
static clock last_vm_info_check_time;
static bool is_using_stun;
static char* access_token = NULL;
static char* vm_password = NULL;
bool is_trying_staging_protocol_info = false;

void update_webserver_parameters() {
    if (already_obtained_vm_type && GetTimer(last_vm_info_check_time) < 30.0) {
        return;
    }

    bool will_try_staging = false;
    if (is_trying_staging_protocol_info) {
        will_try_staging = true;
    }
    is_trying_staging_protocol_info = false;

    if (!already_obtained_vm_type) {
        // Set Default Values
        is_dev = true;
        memcpy(aes_private_key, DEFAULT_PRIVATE_KEY, sizeof(aes_private_key));
        is_using_stun = false;
    }

    char buf[4800];
    size_t len = sizeof(buf);

    LOG_INFO("GETTING JSON");

    if (!SendJSONGet(will_try_staging ? STAGING_HOST : PRODUCTION_HOST, "/vm/protocol_info", buf,
                     len)) {
        already_obtained_vm_type = true;
        StartTimer(&last_vm_info_check_time);
        return;
    }

    // Find JSON as the data after all HTTP headers, ie after the string
    // "\r\n\r\n"
    char* json_str = NULL;
    for (size_t i = 0; i < len - 4; i++) {
        if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
            json_str = buf + i + 4;
        }
    }

    if (!json_str) {
        already_obtained_vm_type = true;
        StartTimer(&last_vm_info_check_time);
        return;
    }

    // Set Default Values
    is_dev = true;
    memcpy(aes_private_key, DEFAULT_PRIVATE_KEY, sizeof(aes_private_key));
    is_using_stun = false;

    json_t json;
    if (!parse_json(json_str, &json)) {
        LOG_ERROR("Failed to parse JSON from /vm/protocol_info");
        already_obtained_vm_type = true;
        StartTimer(&last_vm_info_check_time);
        return;
    }

    kv_pair_t* dev_value = get_kv(&json, "dev");
    kv_pair_t* branch_value = get_kv(&json, "branch");
    kv_pair_t* private_key = get_kv(&json, "private_key");
    kv_pair_t* using_stun = get_kv(&json, "using_stun");
    kv_pair_t* access_token_value = get_kv(&json, "access_token");
    kv_pair_t* vm_password_value = get_kv(&json, "vm_password");

    if (dev_value && branch_value) {
        if (dev_value->type != JSON_BOOL) {
            free_json(json);
            already_obtained_vm_type = true;
            StartTimer(&last_vm_info_check_time);
            return;
        }

        is_dev = dev_value->bool_value;

        if (branch) {
            free(branch);
        }
        if (branch_value->type == JSON_STRING) {
            branch = clone(branch_value->str_value);
        } else if (branch_value->type == JSON_NULL) {
            branch = clone("[NULL]");
        } else {
            branch = clone("");
        }

        LOG_INFO("Is Dev? %s", dev_value->bool_value ? "true" : "false");
        LOG_INFO("Branch: %s", branch);

        if (private_key && private_key->type == JSON_BOOL) {
            LOG_INFO("Private Key: %s", private_key->str_value);
            read_hexadecimal_private_key(private_key->str_value, aes_private_key);
        }

        if (using_stun && using_stun->type == JSON_BOOL) {
            LOG_INFO("Using Stun: %s", using_stun->bool_value ? "Yes" : "No");
            is_using_stun = using_stun->bool_value;
        }

        if (access_token_value && access_token_value->type == JSON_STRING) {
            if (!access_token) {
                free(access_token);
            }
            access_token = clone(access_token_value->str_value);
        }

        if (vm_password_value && vm_password_value->type == JSON_STRING) {
            vm_password = clone(vm_password_value->str_value);
        }
    } else {
        LOG_WARNING("COULD NOT GET JSON PARAMETERS FROM: %s", json_str);
    }

    free_json(json);
    if (is_dev && !will_try_staging) {
        is_trying_staging_protocol_info = true;
        // This time trying the staging protocol info, if we haven't already
        update_webserver_parameters();
        return;
    } else {
        already_obtained_vm_type = true;
        StartTimer(&last_vm_info_check_time);
    }
}

char* get_branch() {
    if (!already_obtained_vm_type) {
        LOG_ERROR("Webserver parameters not updated!");
    }
    return branch;
}

char* get_private_key() {
    if (!already_obtained_vm_type) {
        LOG_ERROR("Webserver parameters not updated!");
    }
    return aes_private_key;
}

bool get_using_stun() {
    if (!already_obtained_vm_type) {
        LOG_ERROR("Webserver parameters not updated!");
    }
    return is_using_stun;
}

bool is_dev_vm() {
    if (!already_obtained_vm_type) {
        LOG_ERROR("Webserver parameters not updated!");
    }
    return is_dev;
}

char* get_access_token() {
    if (!already_obtained_vm_type) {
        LOG_ERROR("Webserver parameters not updated!");
    }
    return access_token;
}

char* get_vm_password() {
    if (!already_obtained_vm_type) {
        LOG_ERROR("Webserver parameters not updated!");
    }
    if (!vm_password) {
        return "password1234567.";
    }
    return vm_password;
}

int GetFmsgSize(FractalClientMessage* fmsg) {
    if (fmsg->type == MESSAGE_KEYBOARD_STATE || fmsg->type == MESSAGE_TIME) {
        return sizeof(*fmsg);
    } else if (fmsg->type == CMESSAGE_CLIPBOARD) {
        return sizeof(*fmsg) + fmsg->clipboard.size;
    } else {
        return sizeof(fmsg->type) + 40;
    }
}
