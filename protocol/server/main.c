#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#include <windows.h>

#include "../include/fractal.h"
#include "../include/videocapture.h"
#include "../include/wasapicapture.h"
#include "../include/videoencode.h"
#include "../include/audioencode.h"
#include "../include/dxgicapture.h"
#include "../include/desktop.h"
#include "../include/input.h"

#pragma comment (lib, "ws2_32.lib")

#define USE_GPU 0
#define USE_MONITOR 0
#define ENCODE_TYPE NVENC_ENCODE
#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080

volatile static bool connected;
volatile static double max_mbps;
volatile static int gop_size = 48;
volatile static DesktopContext desktopContext = { 0 };

volatile int server_width = DEFAULT_WIDTH;
volatile int server_height = DEFAULT_HEIGHT;
volatile bool update_device = true;
volatile FractalCursorID last_cursor;

char buf[LARGEST_FRAME_SIZE];

#define VIDEO_BUFFER_SIZE 25
#define MAX_VIDEO_INDEX 500
struct RTPPacket video_buffer[VIDEO_BUFFER_SIZE][MAX_VIDEO_INDEX];
int video_buffer_packet_len[VIDEO_BUFFER_SIZE][MAX_VIDEO_INDEX];

#define AUDIO_BUFFER_SIZE 100
#define MAX_AUDIO_INDEX 3
struct RTPPacket audio_buffer[AUDIO_BUFFER_SIZE][MAX_AUDIO_INDEX];
int audio_buffer_packet_len[AUDIO_BUFFER_SIZE][MAX_AUDIO_INDEX];

SDL_mutex* packet_mutex;

struct SocketContext PacketSendContext = { 0 };

static int ReplayPacket(struct SocketContext* context, struct RTPPacket* packet, int len) {
    if (len > sizeof(struct RTPPacket)) {
        mprintf("Len too long!\n");
        return -1;
    }

    packet->is_a_nack = true;

    SDL_LockMutex(packet_mutex);
    int sent_size = sendp(context, packet, len);
    SDL_UnlockMutex(packet_mutex);

    if (sent_size < 0) {
        mprintf("Could not replay packet!\n");
        return -1;
    }

    return 0;
}

static int SendPacket(struct SocketContext* context, FractalPacketType type, uint8_t* data, int len, int id) {
    if (id <= 0) {
        mprintf("IDs must be positive!\n");
        return -1;
    }

    int payload_size;
    int curr_index = 0, i = 0;

    clock packet_timer;
    StartTimer(&packet_timer);

    int num_indices = len / MAX_PAYLOAD_SIZE + (len % MAX_PAYLOAD_SIZE == 0 ? 0 : 1);

    while (curr_index < len) {
        if (i != 0 && i % 40 == 0) {
            SDL_Delay(1);
        }

        struct RTPPacket l_packet = { 0 };
        int l_len = 0;

        int* packet_len = &l_len;
        struct RTPPacket* packet = &l_packet;
        if (type == PACKET_AUDIO) {
            if (i >= MAX_AUDIO_INDEX) {
                mprintf("Audio index too long!\n");
                return -1;
            }
            else {
                packet = &audio_buffer[id % AUDIO_BUFFER_SIZE][i];
                packet_len = &audio_buffer_packet_len[id % AUDIO_BUFFER_SIZE][i];
            }
        }
        else if (type == PACKET_VIDEO) {
            if (i >= MAX_VIDEO_INDEX) {
                mprintf("Video index too long!\n");
                return -1;
            }
            else {
                packet = &video_buffer[id % VIDEO_BUFFER_SIZE][i];
                packet_len = &video_buffer_packet_len[id % VIDEO_BUFFER_SIZE][i];
            }
        }
        payload_size = min(MAX_PAYLOAD_SIZE, (len - curr_index));

        memcpy(packet->data, data + curr_index, payload_size);
        packet->type = type;
        packet->index = i;
        packet->payload_size = payload_size;
        packet->id = id;
        packet->num_indices = num_indices;
        packet->is_a_nack = false;
        int packet_size = sizeof(*packet) - sizeof(packet->data) + packet->payload_size;
        packet->hash = Hash((char*)packet + sizeof(packet->hash), packet_size - sizeof(packet->hash));

        SDL_LockMutex(packet_mutex);
        *packet_len = packet_size;
        int sent_size = sendp(context, packet, packet_size);
        SDL_UnlockMutex(packet_mutex);

        if (sent_size < 0) {
            int error = WSAGetLastError();
            mprintf("Unexpected Packet Error: %d\n", error);
            return -1;
        }

        i++;
        curr_index += payload_size;
    }

    return 0;
}


static int32_t SendVideo(void* opaque) {
    SDL_Delay(150);

    mprintf("Initializing desktop...\n");

    char* desktop_name = InitDesktop();

    mprintf("Desktop initialized\n");

    struct SocketContext socketContext = *(struct SocketContext*) opaque;

    // Init DXGI Device
    struct CaptureDevice rdevice;
    struct CaptureDevice* device = NULL;

    struct FractalCursorTypes *types = (struct FractalCursorTypes *) malloc(sizeof(struct FractalCursorTypes));
    memset(types, 0, sizeof(struct FractalCursorTypes));
    LoadCursors(types);

    // Open desktop
    bool defaultFound = (strcmp("Default", desktop_name) == 0);
    if (!defaultFound) {
        OpenNewDesktop(NULL, false, true);
    }
    else {
        desktopContext.ready = true;
        mprintf("DESKTOP INITIALY READY\n");
    }

    // Init FFMPEG Encoder
    int current_bitrate = STARTING_BITRATE;
    encoder_t* encoder = NULL;

    bool update_encoder = false;

    double worst_fps = 40.0;
    int ideal_bitrate = current_bitrate;
    int bitrate_tested_frames = 0;
    int bytes_tested_frames = 0;

    clock previous_frame_time;
    StartTimer(&previous_frame_time);
    int previous_frame_size = 0;

    int consecutive_capture_screen_errors = 0;

    int defaultCounts = 1;
    HRESULT hr;

    clock world_timer;
    StartTimer(&world_timer);

    int id = 1;
    int frames_since_first_iframe = 0;
    update_device = true;
    while (connected) {
        if (!defaultFound) {
            desktopContext = OpenNewDesktop(NULL, true, false);

            if (strcmp("Default", desktopContext.desktop_name) == 0) {
                mprintf("Default found in capture loop\n");
                desktopContext = OpenNewDesktop("default", true, true);

                update_device = true;

                defaultFound = true;
                desktopContext.ready = true;
            }
            else {
                mprintf("Default not yet found\n");
            }
        }

        if (update_device) {
            if (device) {
                DestroyCaptureDevice(device);
                device = NULL;
            }

            device = &rdevice;
            int result = CreateCaptureDevice(device, server_width, server_height);
            if (result < 0) {
                mprintf("Failed to create capture device\n");
                device = NULL;
                connected = false;
            }

            update_encoder = true;
            update_device = false;
        }

        if (update_encoder) {
            if (encoder) {
                destroy_video_encoder(encoder);
            }
            encoder = create_video_encoder(device->width, device->height, device->width, device->height, device->width * current_bitrate, gop_size, ENCODE_TYPE);
            update_encoder = false;
            frames_since_first_iframe = 0;
        }

        int accumulated_frames = CaptureScreen(device);
        if (accumulated_frames < 0) {
            mprintf("Failed to capture screen\n");
            int width = device->width;
            int height = device->height;
            DestroyCaptureDevice(device);
            InitDesktop();
            CreateCaptureDevice(device, width, height);
            continue;
        }

        clock server_frame_timer;
        StartTimer(&server_frame_timer);

        // Only if we have a frame to render
        if (accumulated_frames > 0) {
            if (accumulated_frames > 1) {
                mprintf("Accumulated Frames: %d\n", accumulated_frames);
            }

            consecutive_capture_screen_errors = 0;

            clock t;
            StartTimer(&t);
            video_encoder_encode(encoder, device->frame_data);
            //mprintf("Encode Time: %f\n", GetTimer(t));

            bitrate_tested_frames++;
            bytes_tested_frames += encoder->packet.size;

            if (encoder->packet.size != 0) {
                double delay = -1.0;

                if (previous_frame_size > 0) {
                    double frame_time = GetTimer(previous_frame_time);
                    StartTimer(&previous_frame_time);
                    double mbps = previous_frame_size * 8.0 / 1024.0 / 1024.0 / frame_time;
                    // previousFrameSize * 8.0 / 1024.0 / 1024.0 / IdealTime = max_mbps
                    // previousFrameSize * 8.0 / 1024.0 / 1024.0 / max_mbps = IdealTime
                    double transmit_time = previous_frame_size * 8.0 / 1024.0 / 1024.0 / max_mbps;

                    double average_frame_size = 1.0 * bytes_tested_frames / bitrate_tested_frames;
                    double current_trasmit_time = previous_frame_size * 8.0 / 1024.0 / 1024.0 / max_mbps;
                    double current_fps = 1.0 / current_trasmit_time;

                    delay = transmit_time - frame_time;
                    delay = min(delay, 0.004);

                    //mprintf("Size: %d, MBPS: %f, VS MAX MBPS: %f, Time: %f, Transmit Time: %f, Delay: %f\n", previous_frame_size, mbps, max_mbps, frame_time, transmit_time, delay);

                    if ((current_fps < worst_fps || ideal_bitrate > current_bitrate) && bitrate_tested_frames > 20) {
                        // Rather than having lower than the worst acceptable fps, find the ratio for what the bitrate should be
                        double ratio_bitrate = current_fps / worst_fps;
                        int new_bitrate = (int)(ratio_bitrate * current_bitrate);
                        if (abs(new_bitrate - current_bitrate) / new_bitrate > 0.05) {
                            mprintf("Updating bitrate from %d to %d\n", current_bitrate, new_bitrate);
                            //current_bitrate = new_bitrate;
                            //update_encoder = true;

                            bitrate_tested_frames = 0;
                            bytes_tested_frames = 0;
                        }
                    }
                }

                int frame_size = sizeof(Frame) + encoder->packet.size;
                if (frame_size > LARGEST_FRAME_SIZE) {
                    mprintf("Frame too large: %d\n", frame_size);
                }
                else {
                    FractalCursorImage image = { 0 };
                    image = GetCurrentCursor(types);
                    Frame* frame = buf;
                    frame->width = device->width;
                    frame->height = device->height;
                    frame->size = encoder->packet.size;
                    frame->cursor = image;
                    frame->is_iframe = frames_since_first_iframe % gop_size == 0;
                    memcpy(frame->compressed_frame, encoder->packet.data, encoder->packet.size);

                    //mprintf("Sent video packet %d (Size: %d) %s\n", id, encoder->packet.size, frame->is_iframe ? "(I-frame)" : "");
                    if (SendPacket(&socketContext, PACKET_VIDEO, frame, frame_size, id) < 0) {
                        mprintf("Could not send video frame ID %d\n", id);
                    }
                    frames_since_first_iframe++;
                    id++;
                    previous_frame_size = encoder->packet.size;
                    float server_frame_time = GetTimer(server_frame_timer);
                    //mprintf("Server Frame Time for ID %d: %f\n", id, server_frame_time);
                }
            }

            ReleaseScreen(device);
        }
    }

    HCURSOR new_cursor = LoadCursor(NULL, IDC_ARROW);

    SetSystemCursor(new_cursor, last_cursor);

    DestroyCaptureDevice(device);
    device = NULL;

    return 0;
}

static int32_t SendAudio(void* opaque) {
    while (!desktopContext.ready) {
        Sleep(500);
        mprintf("Audio looping...\n");
    }

    if (setCurrentInputDesktop(desktopContext.desktop_handle) == 0) {
        mprintf("Audio thread set\n");
    }

    struct SocketContext context = *(struct SocketContext*) opaque;
    int id = 1;

    wasapi_device* audio_device = (wasapi_device*)malloc(sizeof(struct wasapi_device));
    audio_device = CreateAudioDevice(audio_device);
    StartAudioDevice(audio_device);

    FractalServerMessage fmsg;
    fmsg.type = MESSAGE_AUDIO_FREQUENCY;
    fmsg.frequency = audio_device->pwfx->nSamplesPerSec;
    SendPacket(&PacketSendContext, PACKET_MESSAGE, &fmsg, sizeof(fmsg), 1);

    mprintf("Audio Frequency: %d\n", audio_device->pwfx->nSamplesPerSec);

    HRESULT hr = CoInitialize(NULL);
    DWORD dwWaitResult;
    UINT32 nNumFramesToRead, nNextPacketSize, nBlockAlign = audio_device->pwfx->nBlockAlign;
    DWORD dwFlags;

    while (connected) {
        for (hr = audio_device->pAudioCaptureClient->lpVtbl->GetNextPacketSize(audio_device->pAudioCaptureClient, &nNextPacketSize);
            SUCCEEDED(hr) && nNextPacketSize > 0;
            hr = audio_device->pAudioCaptureClient->lpVtbl->GetNextPacketSize(audio_device->pAudioCaptureClient, &nNextPacketSize)
            ) {
            audio_device->pAudioCaptureClient->lpVtbl->GetBuffer(
                audio_device->pAudioCaptureClient,
                &audio_device->pData, &nNumFramesToRead,
                &dwFlags, NULL, NULL);

            audio_device->audioBufSize = nNumFramesToRead * nBlockAlign;

            if (audio_device->audioBufSize > 10000) {
                mprintf("Audio buffer size too large!\n");
            }
            else if (audio_device->audioBufSize > 0) {
                if (SendPacket(&context, PACKET_AUDIO, audio_device->pData, audio_device->audioBufSize, id) < 0) {
                    mprintf("Could not send audio frame\n");
                }
                id++;
            }

            audio_device->pAudioCaptureClient->lpVtbl->ReleaseBuffer(
                audio_device->pAudioCaptureClient,
                nNumFramesToRead);
        }
        dwWaitResult = WaitForSingleObject(audio_device->hWakeUp, INFINITE);
    }

    DestroyAudioDevice(audio_device);
    return 0;
}


int main(int argc, char* argv[])
{
    initMultiThreadedPrintf(true);

    // initialize the windows socket library if this is a windows client
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        mprintf("Failed to initialize Winsock with error code: %d.\n", WSAGetLastError());
        return -1;
    }

    while (true) {
        struct SocketContext PacketReceiveContext = { 0 };
        if (CreateUDPContext(&PacketReceiveContext, "S", "0.0.0.0", PORT_CLIENT_TO_SERVER, 1, -1) < 0) {
            mprintf("Failed to start connection\n");
            SDL_Delay(500);
            continue;
        }

        SDL_Delay(250);

        if (CreateUDPContext(&PacketSendContext, "S", "0.0.0.0", PORT_SERVER_TO_CLIENT, 1, 500) < 0) {
            mprintf("Failed to finish connection.\n");
            closesocket(PacketReceiveContext.s);
            SDL_Delay(500);
            continue;
        }

        SDL_Delay(250);

        clock startup_time;
        StartTimer(&startup_time);

        connected = true;
        max_mbps = START_MAX_MBPS;

        packet_mutex = SDL_CreateMutex();

        SDL_Thread* send_video = SDL_CreateThread(SendVideo, "SendVideo", &PacketSendContext);
        SDL_Thread* send_audio = SDL_CreateThread(SendAudio, "SendAudio", &PacketSendContext);
        mprintf("Sending video and audio...\n");

        struct FractalClientMessage fmsgs[6];
        struct FractalClientMessage fmsg;
        int i = 0, j = 0, active = 0;
        FractalStatus status;

        clock last_ping;
        StartTimer(&last_ping);

        clock totaltime;
        StartTimer(&totaltime);

        mprintf("Receiving packets...\n");
        while (connected) {
            if (GetTimer(last_ping) > 3.0) {
                mprintf("Client connection dropped.\n");
                connected = false;
            }

            memset(&fmsg, 0, sizeof(fmsg));
            // 1ms timeout
            if (recvp(&PacketReceiveContext, &fmsg, sizeof(fmsg)) > 0) {
                if (fmsg.type == MESSAGE_KEYBOARD) {
                    if (j >= 6) {
                        mprintf("Too long of a keyboard combination!\n");
                        active = 0;
                        j = 0;
                    }

                    if (active) {
                        fmsgs[j] = fmsg;
                        if (fmsg.keyboard.pressed) {
                            if (fmsg.keyboard.code != fmsgs[j - 1].keyboard.code) {
                                j++;
                            }
                        }
                        else {
                            status = ReplayUserInput(fmsgs, j + 1);
                            active = 0;
                            j = 0;
                        }
                    }
                    else {
                        fmsgs[0] = fmsg;
                        if (fmsg.keyboard.pressed && (fmsg.keyboard.code >= 224 && fmsg.keyboard.code <= 231)) {
                            active = 1;
                            j++;
                        }
                        else {
                            status = ReplayUserInput(fmsgs, 1);
                        }
                    }
                }
                else if (fmsg.type == MESSAGE_MOUSE_BUTTON || fmsg.type == MESSAGE_MOUSE_WHEEL || fmsg.type == MESSAGE_MOUSE_MOTION) {
                    status = ReplayUserInput(&fmsg, 1);
                }
                else if (fmsg.type == MESSAGE_MBPS) {
                    max_mbps = fmsg.mbps;
                }
                else if (fmsg.type == MESSAGE_PING) {
                    mprintf("Ping Received - ID %d\n", fmsg.ping_id); 

                    FractalServerMessage fmsg_response = { 0 };
                    fmsg_response.type = MESSAGE_PONG;
                    fmsg_response.ping_id = fmsg.ping_id;
                    StartTimer(&last_ping);
                    if (SendPacket(&PacketSendContext, PACKET_MESSAGE, &fmsg_response, sizeof(fmsg_response), 1) < 0) {
                        mprintf("Could not send Pong\n");
                    }
                }
                else if (fmsg.type == MESSAGE_DIMENSIONS) {
                    mprintf("Request to use dimensions %dx%d received\n", fmsg.dimensions.width, fmsg.dimensions.height);
                    //TODO: Check if dimensions are valid
                    server_width = fmsg.dimensions.width;
                    server_height = fmsg.dimensions.height;
                    update_device = true;
                }
                else if (fmsg.type == MESSAGE_QUIT) {
                    mprintf("Client Quit\n");
                    connected = false;
                }
                else if (fmsg.type == MESSAGE_AUDIO_NACK) {
                    //mprintf("Audio NACK requested for: ID %d Index %d\n", fmsg.nack_data.id, fmsg.nack_data.index);
                    struct RTPPacket *audio_packet = &audio_buffer[fmsg.nack_data.id % AUDIO_BUFFER_SIZE][fmsg.nack_data.index];
                    int len = audio_buffer_packet_len[fmsg.nack_data.id % AUDIO_BUFFER_SIZE][fmsg.nack_data.index];
                    if (audio_packet->id == fmsg.nack_data.id) {
                        mprintf("NACKed audio packet %d found of length %d. Relaying!\n", fmsg.nack_data.id, len);
                        ReplayPacket(&PacketSendContext, audio_packet, len);
                    }
                    // If we were asked for an invalid index, just ignore it
                    else if (fmsg.nack_data.index < audio_packet->num_indices) {
                        mprintf("NACKed audio packet %d %d not found, ID %d %d was located instead.\n", fmsg.nack_data.id, fmsg.nack_data.index, audio_packet->id, audio_packet->index);
                    }
                }
                else if (fmsg.type == MESSAGE_VIDEO_NACK) {
                    //mprintf("Video NACK requested for: ID %d Index %d\n", fmsg.nack_data.id, fmsg.nack_data.index);
                    struct RTPPacket* video_packet = &video_buffer[fmsg.nack_data.id % VIDEO_BUFFER_SIZE][fmsg.nack_data.index];
                    int len = video_buffer_packet_len[fmsg.nack_data.id % VIDEO_BUFFER_SIZE][fmsg.nack_data.index];
                    if (video_packet->id == fmsg.nack_data.id) {
                        mprintf("NACKed video packet %d found of length %d. Relaying!\n", fmsg.nack_data.id, len);
                        ReplayPacket(&PacketSendContext, video_packet, len);
                    }

                    // If we were asked for an invalid index, just ignore it
                    else if (fmsg.nack_data.index < video_packet->num_indices) {
                        mprintf("NACKed video packet %d %d not found, ID %d %d was located instead.\n", fmsg.nack_data.id, fmsg.nack_data.index, video_packet->id, video_packet->index);
                    }
                }
            }
        }
        mprintf("Disconnected\n");

        SDL_WaitThread(send_video, NULL);
        SDL_WaitThread(send_audio, NULL);

        SDL_DestroyMutex(packet_mutex);

        closesocket(PacketReceiveContext.s);
        closesocket(PacketSendContext.s);
    }

    WSACleanup();

    destroyMultiThreadedPrintf();

    return 0;
}