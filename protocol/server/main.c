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
#include "../include/audiocapture.h"
#include "../include/videoencode.h"
#include "../include/audioencode.h"

#define SDL_MAIN_HANDLED
#include "../include/SDL2/SDL.h"
#include "../include/SDL2/SDL_thread.h"

#pragma comment (lib, "ws2_32.lib")

#define BUFLEN 1000
#define RECV_BUFFER_LEN 38 // exact user input packet line to prevent clumping
#define FRAME_BUFFER_SIZE (1024 * 1024)

bool repeat = true; // global flag to keep streaming until client disconnects

typedef enum {
  videotype = 0xFA010000,
  audiotype = 0xFB010000
} Fractalframe_type_t;

typedef struct {
  Fractalframe_type_t type;
  int size;
  char data[0];
} Fractalframe_t;


static int fragmented_sendto(struct context *context, char *buf, int len, int max_size) {
  int sent_size, slen = sizeof(context->addr), i = 0;

  while(len > 0) {
    char payload[32];
    sprintf(payload, "%d", i);
    strcat(payload, buf);
    if((sent_size = sendto(context->s, payload, max_size, 0, (struct sockaddr*)(&context->addr), slen)) < 0) {
      return -1;
    } else {
      if(strlen(buf) > max_size) {
        buf += max_size;
      }
      len -= max_size;
      i++;
    }
  }
  return 0;
}

static int32_t ReceiveUserInput(void *opaque) {
    struct context context = *(struct context *) opaque;
    int i, recv_size, slen = sizeof(context.addr);
    char recv_buf[BUFLEN];

    while(1) {
        if ((recv_size = recvfrom(context.s, &recv_buf, sizeof(recv_buf), 0, (struct sockaddr*)(&context.addr), &slen)) < 0) {
            printf("Packet not received \n");
        }
    }
}

static int32_t SendVideo(void *opaque) {
    // struct context context = *(struct context *) opaque;
    // int i, slen = sizeof(context.addr);
    // char *message = "VideoVideoVideoVideoVideoVideoVideoVideoVideoVideoVideoVideoVideoVideoVideoVideoVideo";


    // while(1) {
    //   // send packet
    //   if (fragmented_sendto(&context, message, strlen(message), 10) < 0)
    //       printf("Could not send video frame\n");
    // }
    struct context context = *(struct context *) opaque;
    int i, slen = sizeof(context.addr);
    char *message = "Video";

    while(1) {
        if (sendto(context.s, message, strlen(message), 0, (struct sockaddr*)(&context.addr), slen) < 0)
            printf("Could not send packet\n");
    }
    return 0;
    // get window
  //   HWND window = NULL;
  //   window = GetDesktopWindow();
  //   frame_area frame = {0, 0, 0, 0}; // init  frame area

  //   // init screen capture device
  //   video_capture_device *device;
  //   device = create_video_capture_device(window, frame);

  //   // set encoder parameters
  //   int width = device->width; // in and out from the capture device
  //   int height = device->height; // in and out from the capture device
  //   int bitrate = width * 8000; // estimate bit rate based on output size

  //   // init encoder
  //   encoder_t *encoder;
  //   encoder = create_video_encoder(width, height, CAPTURE_WIDTH, CAPTURE_HEIGHT, bitrate);

  //   // video variables
  //   void *capturedframe; // var to hold captured frame, as a void* to RGB pixels
  //   int sent_size; // var to keep track of size of packets sent

  //   // init encoded frame parameters
  //   Fractalframe_t *encodedframe = (Fractalframe_t *) malloc(FRAME_BUFFER_SIZE);
  //   memset(encodedframe, 0, FRAME_BUFFER_SIZE); // set memory to null
  //   encodedframe->type = videotype; // specify that this is a video frame
  //   size_t encoded_size; // init encoded buffer size

  //   // while stream is on
  //   while (repeat) {
  //     // capture a frame
  //     capturedframe = capture_screen(device);
  //     // reset encoded frame to 0 and reset buffer  before encoding
  //     encodedframe->size = 0;
  //     encoded_size = FRAME_BUFFER_SIZE - sizeof(Fractalframe_t);

  //     // encode captured frame into encodedframe->data
  //     video_encoder_encode(encoder, capturedframe, encodedframe->data, &encoded_size);


  //     // if (encoded_size != 0) {
  //     //   // send packet
  //     //   if (fragmented_sendto(&context, encodedframe->data, encoded_size, 1000) < 0)
  //     //       printf("Could not send video frame\n");
  //     // }

  //     // packet sent, let's reset the encoded frame memory for the next one
  //     memset(encodedframe, 0, FRAME_BUFFER_SIZE);
  // }

  // // exited while loop, stream done let's close everything
  // destroy_video_encoder(encoder); // destroy encoder
  // destroy_video_capture_device(device); // destroy capture device
  // _endthreadex(0); // end thread and return
  // return 0;
}

static int32_t SendAudio(void *opaque) {
    struct context context = *(struct context *) opaque;
    int i, slen = sizeof(context.addr);
    char *message = "Audio";

    while(1) {
        if (sendto(context.s, message, strlen(message), 0, (struct sockaddr*)(&context.addr), slen) < 0)
            printf("Could not send packet\n");
    }

    return 0;
}



int main(int argc, char* argv[])
{
    // initialize the windows socket library if this is a windows client
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Failed to initialize Winsock with error code: %d.\n", WSAGetLastError());
        return -1;
    }

    struct sockaddr_in receive_address;
    int recv_size, slen=sizeof(receive_address);
    char recv_buf[BUFLEN];

    struct context InputReceiveContext = {0};
    if(CreateUDPContext(&InputReceiveContext, "S", "", -1) < 0) {
        exit(1);
    }

    struct context VideoContext = {0};
    if(CreateUDPContext(&VideoContext, "S", "", -1) < 0) {
        exit(1);
    }

    struct context AudioContext = {0};
    if(CreateUDPContext(&AudioContext, "S", "", -1) < 0) {
        exit(1);
    }

    SDL_Thread *send_input_ack = SDL_CreateThread(ReceiveUserInput, "ReceiveUserInput", &InputReceiveContext);
    SDL_Thread *send_video = SDL_CreateThread(SendVideo, "SendVideo", &VideoContext);
    SDL_Thread *send_audio = SDL_CreateThread(SendAudio, "SendAudio", &AudioContext);

    while (1)
    {
        if (SendAck(&InputReceiveContext) < 0)
            printf("Could not send packet\n");
        if ((recv_size = recvfrom(VideoContext.s, &recv_buf, sizeof(recv_buf), 0, (struct sockaddr*)(&VideoContext.addr), &slen)) < 0) 
            printf("Packet not received \n");
        if ((recv_size = recvfrom(AudioContext.s, &recv_buf, sizeof(recv_buf), 0, (struct sockaddr*)(&AudioContext.addr), &slen)) < 0) 
            printf("Packet not received \n");
        Sleep(2000);
    }
 
    // Actually, we never reach this point...

    closesocket(InputReceiveContext.s);
    closesocket(VideoContext.s);
    closesocket(AudioContext.s);

    WSACleanup();

    return 0;
}