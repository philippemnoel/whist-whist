/*
 * Clipboard thread handling.
 *
 * Copyright Fractal Computers, Inc. 2020
 **/
#include <stdio.h>

#include "../core/fractal.h"
#include "clipboard.h"

int UpdateClipboardThread(void* opaque);

extern char filename[300];
extern char username[50];
bool updating_set_clipboard;
bool updating_clipboard;
bool pending_update_clipboard;
clock last_clipboard_update;
SDL_sem* clipboard_semaphore;
ClipboardData* clipboard;
SEND_FMSG* send_fmsg;
SDL_Thread* thread;
bool connected;
char* server_ip;
ClipboardData* clipboard;

bool isUpdatingClipboard() { return updating_clipboard; }

bool updateSetClipboard(ClipboardData* cb) {
    if (updating_clipboard) {
        LOG_INFO("Tried to SetClipboard, but clipboard is updating\n");
        return false;
    }

    updating_clipboard = true;
    updating_set_clipboard = true;
    clipboard = cb;

    SDL_SemPost(clipboard_semaphore);

    return true;
}

bool pendingUpdateClipboard() { return pending_update_clipboard; }

void initUpdateClipboard(SEND_FMSG* send_fmsg_local, char* server_ip_local) {
    connected = true;

    server_ip = server_ip_local;
    send_fmsg = send_fmsg_local;

    updating_clipboard = false;
    pending_update_clipboard = false;
    StartTimer((clock*)&last_clipboard_update);
    clipboard_semaphore = SDL_CreateSemaphore(0);

    thread =
        SDL_CreateThread(UpdateClipboardThread, "UpdateClipboardThread", NULL);

    pending_update_clipboard = true;
    StartTrackingClipboardUpdates();
}

void destroyUpdateClipboard() {
    connected = false;
    SDL_SemPost(clipboard_semaphore);
}

int UpdateClipboardThread(void* opaque) {
    opaque;

    while (connected) {
        SDL_SemWait(clipboard_semaphore);

        if (!connected) {
            break;
        }

        // ClipboardData* clipboard = GetClipboard();

        if (updating_set_clipboard) {
            LOG_INFO("Trying to set clipboard!\n");
            // ClipboardData cb; TODO: unused, still needed?
            if (clipboard->type == CLIPBOARD_FILES) {
                char cmd[1000] = "";

#ifndef _WIN32
                char* prefix = "UNISON=./.unison;";
#else
                char* prefix = "";
#endif

#ifdef _WIN32
                char* exc = "unison";
#elif __APPLE__
                char* exc = "./mac_unison";
#else  // Linux
                char* exc = "./linux_unison";
#endif

                snprintf(
                    cmd,
                    sizeof(cmd),
                    "%s %s -follow \"Path *\" -ui text -ignorearchives -confirmbigdel=false -batch \
						 -sshargs \"-o UserKnownHostsFile=ssh_host_ecdsa_key.pub -l %s -i sshkey\" \
                         \"ssh://%s/%s/get_clipboard/\" \
                         %s \
                         -force \"ssh://%s/%s/get_clipboard/\"",
                    prefix, exc, username, server_ip, filename, SET_CLIPBOARD,
                    server_ip, filename
                );

                /*
                strcat( cmd, "-follow \"Path *\" -ui text -sshargs \"-o
                UserKnownHostsFile=ssh_host_ecdsa_key.pub -l " ); strcat( cmd,
                username ); strcat( cmd, " -i sshkey\" " ); strcat( cmd, "
                \"ssh://" ); strcat( cmd, (char*)server_ip ); strcat( cmd, "/"
                ); strcat( cmd, filename ); strcat( cmd, "/get_clipboard/" );
                strcat( cmd, "\" " );
                strcat( cmd, SET_CLIPBOARD );
                strcat( cmd, " -force " );
                strcat( cmd, " \"ssh://" );
                strcat( cmd, (char*)server_ip );
                strcat( cmd, "/" );
                strcat( cmd, filename );
                strcat( cmd, "/get_clipboard/" );
                strcat( cmd, "\" " );
                strcat( cmd, " -ignorearchives -confirmbigdel=false -batch" );
                */

                LOG_INFO("COMMAND: %s\n", cmd);
                runcmd(cmd);
            }
            SetClipboard(clipboard);
        } else {
            clock clipboard_time;
            StartTimer(&clipboard_time);

            if (clipboard->type == CLIPBOARD_FILES) {
                char cmd[1000] = "";

#ifndef _WIN32
                char* prefix = "UNISON=./.unison;";
#else
                char* prefix = "";
#endif

#ifdef _WIN32
                char* exc = "unison";
#elif __APPLE__
                char* exc = "./mac_unison";
#else  // Linux
                char* exc = "./linux_unison";
#endif

                snprintf(
                    cmd,
                    sizeof( cmd ),
                    "%s %s -follow \"Path *\" -ui text -ignorearchives -confirmbigdel=false -batch \
						 -sshargs \"-o UserKnownHostsFile=ssh_host_ecdsa_key.pub -l %s -i sshkey\" \
                         %s \
                         \"ssh://%s/%s/set_clipboard/\" \
                         -force %s",
                    prefix, exc, username, GET_CLIPBOARD, server_ip, filename, GET_CLIPBOARD
                );

                /*
                strncat(cmd,
                       "-follow \"Path *\" -ui text -sshargs \"-o "
                       "UserKnownHostsFile=ssh_host_ecdsa_key.pub -l ", sizeof(cmd));
                strncat(cmd, username, sizeof( cmd ) );
                strncat(cmd, " -i sshkey\" ", sizeof( cmd ) );
                strncat(cmd, GET_CLIPBOARD, sizeof( cmd ) );
                strncat(cmd, " \"ssh://", sizeof( cmd ) );
                strncat(cmd, (char*)server_ip, sizeof( cmd ) );
                strncat(cmd, "/", sizeof( cmd ) );
                strncat(cmd, filename, sizeof( cmd ) );
                strncat(cmd, "/set_clipboard", sizeof( cmd ) );
                strncat(cmd, "/\" -force ", sizeof( cmd ) );
                strncat(cmd, GET_CLIPBOARD, sizeof( cmd ) );
                strncat(cmd, " -ignorearchives -confirmbigdel=false -batch", sizeof( cmd ) );
                */

                LOG_INFO("COMMAND: %s\n", cmd);
                runcmd(cmd);
            }

            FractalClientMessage* fmsg =
                malloc(sizeof(FractalClientMessage) + sizeof(ClipboardData) +
                       clipboard->size);
            fmsg->type = CMESSAGE_CLIPBOARD;
            memcpy(&fmsg->clipboard, clipboard,
                   sizeof(ClipboardData) + clipboard->size);
            send_fmsg(fmsg);
            free(fmsg);

            // If it hasn't been 500ms yet, then wait 500ms to prevent too much
            // spam
            const int spam_time_ms = 500;
            if (GetTimer(clipboard_time) < spam_time_ms / 1000.0) {
                SDL_Delay(max(
                    (int)(spam_time_ms - 1000 * GetTimer(clipboard_time)), 1));
            }
        }

        LOG_INFO("Updated clipboard!\n");
        updating_clipboard = false;
    }

    return 0;
}

void updateClipboard() {
    if (updating_clipboard) {
        pending_update_clipboard = true;
    } else {
        LOG_INFO("Pushing update to clipboard\n");
        pending_update_clipboard = false;
        updating_clipboard = true;
        updating_set_clipboard = false;
        clipboard = GetClipboard();
        SDL_SemPost(clipboard_semaphore);
    }
}
