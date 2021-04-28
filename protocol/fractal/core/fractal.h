#ifndef FRACTAL_H
#define FRACTAL_H

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

/**
 * Copyright Fractal Computers, Inc. 2020
 * @file fractal.h
 * @brief This file contains the core of Fractal custom structs and definitions
 *        used throughout.

============================
Usage
============================
*/

/*
============================
Includes
============================
*/

#include <stdbool.h>
#include <stdint.h>

#if defined(_WIN32)
#define _WINSOCKAPI_
#include <Audioclient.h>
#include <avrt.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <process.h>
#include <synchapi.h>
#include <windows.h>
#include <winuser.h>

#include "shellscalingapi.h"
#else
#include <dirent.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_qsv.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include <fractal/utils/threads.h>
#include "../clipboard/clipboard_synchronizer.h"
#include "../utils/color.h"
#include "../cursor/cursor.h"
#include "../network/network.h"
#include "../utils/clock.h"
#include "../utils/logging.h"

#ifdef _WIN32
#pragma warning(disable : 4200)
#endif

/*
============================
Defines
============================
*/

#define UNUSED(var) (void)(var)

#define NUM_KEYCODES 265
#define CAPTURE_SPECIAL_WINDOWS_KEYS false

#define MAX_NUM_CLIENTS 10
#define PORT_DISCOVERY 32262
#define BASE_UDP_PORT 32263
#define BASE_TCP_PORT (BASE_UDP_PORT + MAX_NUM_CLIENTS)

#define USING_AUDIO_ENCODE_DECODE true
#define USING_FFMPEG_IFRAME_FLAG false

#define WINAPI_INPUT_DRIVER 1
#define XTEST_INPUT_DRIVER 2
#define UINPUT_INPUT_DRIVER 3

#ifdef _WIN32

// possible on windows, so let's do it
#define USING_SERVERSIDE_SCALE true
#define INPUT_DRIVER WINAPI_INPUT_DRIVER

#else

// not possible yet on linux
#define USING_SERVERSIDE_SCALE false
#define INPUT_DRIVER UINPUT_INPUT_DRIVER
#define USING_GPU_CAPTURE true
#define USING_SHM true

#endif

#ifdef __APPLE__

#define CAN_UPDATE_WINDOW_TITLEBAR_COLOR true

#else

#define CAN_UPDATE_WINDOW_TITLEBAR_COLOR false

#endif  // __APPLE__

// Milliseconds between sending resize events from client to server
// Used to throttle resize event spam.
#define WINDOW_RESIZE_MESSAGE_INTERVAL 200

#define MAXIMUM_BITRATE 30000000
#define MINIMUM_BITRATE 2000000
#define ACK_REFRESH_MS 50

#define LARGEST_FRAME_SIZE 1000000
#define STARTING_BITRATE 10400000
#define STARTING_BURST_BITRATE 31800000

#define AUDIO_BITRATE 128000
#define FPS 45
#define MIN_FPS 10
#define OUTPUT_WIDTH 1280
#define OUTPUT_HEIGHT 720

#define DEFAULT_BINARY_PRIVATE_KEY \
    "\xED\x5E\xF3\x3C\xD7\x28\xD1\x7D\xB8\x06\x45\x81\x42\x8D\x19\xEF"
#define DEFAULT_HEX_PRIVATE_KEY "ED5EF33CD728D17DB8064581428D19EF"

#define MOUSE_SCALING_FACTOR 100000

#define WRITE_MPRINTF_TO_LOG true

// MAXLENs are the max length of the string they represent, _without_ the null character.
// Therefore, whenever arrays are created or length of the string is compared, we should be
// comparing to *MAXLEN + 1
#define FRACTAL_IDENTIFIER_MAXLEN 31
#define WEBSERVER_URL_MAXLEN 63
// this maxlen is the determined Fractal environment max length (the upper bound on all flags passed
// into the protocol)
#define FRACTAL_ARGS_MAXLEN 255

/*
============================
Constants
============================
*/

#define MS_IN_SECOND 1000
#define BYTES_IN_KILOBYTE 1024
#ifdef _WIN32
#define DEFAULT_DPI 96.0
#else
#define DEFAULT_DPI 72.0
#endif

// noreturn silences warnings if used for a function that should never return
#ifdef _WIN32
#define NORETURN __declspec(noreturn)
#else
#define NORETURN __attribute__((noreturn))
#endif

/*
============================
Custom Types
============================
*/

typedef enum EncodeType { SOFTWARE_ENCODE = 0, NVENC_ENCODE = 1, QSV_ENCODE = 2 } EncodeType;

typedef enum CodecType {
    CODEC_TYPE_UNKNOWN = 0,
    CODEC_TYPE_H264 = 264,
    CODEC_TYPE_H265 = 265,
    CODEC_TYPE_MAKE_32 = 0x7FFFFFFF
} CodecType;

typedef enum FractalKeycode {
    FK_A = 4,              ///< 4
    FK_B = 5,              ///< 5
    FK_C = 6,              ///< 6
    FK_D = 7,              ///< 7
    FK_E = 8,              ///< 8
    FK_F = 9,              ///< 9
    FK_G = 10,             ///< 10
    FK_H = 11,             ///< 11
    FK_I = 12,             ///< 12
    FK_J = 13,             ///< 13
    FK_K = 14,             ///< 14
    FK_L = 15,             ///< 15
    FK_M = 16,             ///< 16
    FK_N = 17,             ///< 17
    FK_O = 18,             ///< 18
    FK_P = 19,             ///< 19
    FK_Q = 20,             ///< 20
    FK_R = 21,             ///< 21
    FK_S = 22,             ///< 22
    FK_T = 23,             ///< 23
    FK_U = 24,             ///< 24
    FK_V = 25,             ///< 25
    FK_W = 26,             ///< 26
    FK_X = 27,             ///< 27
    FK_Y = 28,             ///< 28
    FK_Z = 29,             ///< 29
    FK_1 = 30,             ///< 30
    FK_2 = 31,             ///< 31
    FK_3 = 32,             ///< 32
    FK_4 = 33,             ///< 33
    FK_5 = 34,             ///< 34
    FK_6 = 35,             ///< 35
    FK_7 = 36,             ///< 36
    FK_8 = 37,             ///< 37
    FK_9 = 38,             ///< 38
    FK_0 = 39,             ///< 39
    FK_ENTER = 40,         ///< 40
    FK_ESCAPE = 41,        ///< 41
    FK_BACKSPACE = 42,     ///< 42
    FK_TAB = 43,           ///< 43
    FK_SPACE = 44,         ///< 44
    FK_MINUS = 45,         ///< 45
    FK_EQUALS = 46,        ///< 46
    FK_LBRACKET = 47,      ///< 47
    FK_RBRACKET = 48,      ///< 48
    FK_BACKSLASH = 49,     ///< 49
    FK_SEMICOLON = 51,     ///< 51
    FK_APOSTROPHE = 52,    ///< 52
    FK_BACKTICK = 53,      ///< 53
    FK_COMMA = 54,         ///< 54
    FK_PERIOD = 55,        ///< 55
    FK_SLASH = 56,         ///< 56
    FK_CAPSLOCK = 57,      ///< 57
    FK_F1 = 58,            ///< 58
    FK_F2 = 59,            ///< 59
    FK_F3 = 60,            ///< 60
    FK_F4 = 61,            ///< 61
    FK_F5 = 62,            ///< 62
    FK_F6 = 63,            ///< 63
    FK_F7 = 64,            ///< 64
    FK_F8 = 65,            ///< 65
    FK_F9 = 66,            ///< 66
    FK_F10 = 67,           ///< 67
    FK_F11 = 68,           ///< 68
    FK_F12 = 69,           ///< 69
    FK_PRINTSCREEN = 70,   ///< 70
    FK_SCROLLLOCK = 71,    ///< 71
    FK_PAUSE = 72,         ///< 72
    FK_INSERT = 73,        ///< 73
    FK_HOME = 74,          ///< 74
    FK_PAGEUP = 75,        ///< 75
    FK_DELETE = 76,        ///< 76
    FK_END = 77,           ///< 77
    FK_PAGEDOWN = 78,      ///< 78
    FK_RIGHT = 79,         ///< 79
    FK_LEFT = 80,          ///< 80
    FK_DOWN = 81,          ///< 81
    FK_UP = 82,            ///< 82
    FK_NUMLOCK = 83,       ///< 83
    FK_KP_DIVIDE = 84,     ///< 84
    FK_KP_MULTIPLY = 85,   ///< 85
    FK_KP_MINUS = 86,      ///< 86
    FK_KP_PLUS = 87,       ///< 87
    FK_KP_ENTER = 88,      ///< 88
    FK_KP_1 = 89,          ///< 89
    FK_KP_2 = 90,          ///< 90
    FK_KP_3 = 91,          ///< 91
    FK_KP_4 = 92,          ///< 92
    FK_KP_5 = 93,          ///< 93
    FK_KP_6 = 94,          ///< 94
    FK_KP_7 = 95,          ///< 95
    FK_KP_8 = 96,          ///< 96
    FK_KP_9 = 97,          ///< 97
    FK_KP_0 = 98,          ///< 98
    FK_KP_PERIOD = 99,     ///< 99
    FK_APPLICATION = 101,  ///< 101
    FK_F13 = 104,          ///< 104
    FK_F14 = 105,          ///< 105
    FK_F15 = 106,          ///< 106
    FK_F16 = 107,          ///< 107
    FK_F17 = 108,          ///< 108
    FK_F18 = 109,          ///< 109
    FK_F19 = 110,          ///< 110
    FK_MENU = 118,         ///< 118
    FK_MUTE = 127,         ///< 127
    FK_VOLUMEUP = 128,     ///< 128
    FK_VOLUMEDOWN = 129,   ///< 129
    FK_LCTRL = 224,        ///< 224
    FK_LSHIFT = 225,       ///< 225
    FK_LALT = 226,         ///< 226
    FK_LGUI = 227,         ///< 227
    FK_RCTRL = 228,        ///< 228
    FK_RSHIFT = 229,       ///< 229
    FK_RALT = 230,         ///< 230
    FK_RGUI = 231,         ///< 231
    FK_AUDIONEXT = 258,    ///< 258
    FK_AUDIOPREV = 259,    ///< 259
    FK_AUDIOSTOP = 260,    ///< 260
    FK_AUDIOPLAY = 261,    ///< 261
    FK_AUDIOMUTE = 262,    ///< 262
    FK_MEDIASELECT = 263,  ///< 263
} FractalKeycode;

/// @brief Modifier keys applied to keyboard input.
/// @details Codes for when keyboard input is modified. These values may be
/// bitwise OR'd together.
typedef enum FractalKeymod {
    MOD_NONE = 0x0000,    ///< No modifier key active.
    MOD_LSHIFT = 0x0001,  ///< `LEFT SHIFT` is currently active.
    MOD_RSHIFT = 0x0002,  ///< `RIGHT SHIFT` is currently active.
    MOD_LCTRL = 0x0040,   ///< `LEFT CONTROL` is currently active.
    MOD_RCTRL = 0x0080,   ///< `RIGHT CONTROL` is currently active.
    MOD_LALT = 0x0100,    ///< `LEFT ALT` is currently active.
    MOD_RALT = 0x0200,    ///< `RIGHT ALT` is currently active.
    MOD_NUM = 0x1000,     ///< `NUMLOCK` is currently active.
    MOD_CAPS = 0x2000,    ///< `CAPSLOCK` is currently active.
} FractalKeymod;

/// @brief Mouse button.
/// @details Codes for encoding mouse actions.
typedef enum FractalMouseButton {
    MOUSE_L = 1,       ///< Left mouse button.
    MOUSE_MIDDLE = 2,  ///< Middle mouse button.
    MOUSE_R = 3,       ///< Right mouse button.
    MOUSE_X1 = 4,      ///< Extra mouse button 1.
    MOUSE_X2 = 5,      ///< Extra mouse button 2.
    MOUSE_MAKE_32 = 0x7FFFFFFF,
} FractalMouseButton;

/// @brief Cursor properties.
/// @details Track important information on cursor.
typedef struct FractalCursor {
    uint32_t size;       ///< Size in bytes of the cursor image buffer.
    uint32_t positionX;  ///< When leaving relative mode, the horizontal position
                         ///< in screen coordinates where the cursor reappears.
    uint32_t positionY;  ///< When leaving relative mode, the vertical position
                         ///< in screen coordinates where the cursor reappears.
    uint16_t width;      ///< Width of the cursor image in pixels.
    uint16_t height;     ///< Height of the cursor position in pixels.
    uint16_t hotX;       ///< Horizontal pixel position of the cursor hotspot within
                         ///< the image.
    uint16_t hotY;       ///< Vertical pixel position of the cursor hotspot within
                         ///< the image.
    bool modeUpdate;     ///< `true` if the cursor mode should be updated. The
                         ///< `relative`, `positionX`, and `positionY` members are
                         ///< valid.
    bool imageUpdate;    ///< `true` if the cursor image should be updated. The
                         ///< `width`, `height`, `hotX`, `hotY`, and `size`
                         ///< members are valid.
    bool relative;       ///< `true` if in relative mode, meaning the client should
                         ///< submit mouse motion in relative distances rather than
                         ///< absolute screen coordinates.
    uint8_t __pad[1];
} FractalCursor;

/// @brief Keyboard message.
/// @details Messages related to keyboard usage.
typedef struct FractalKeyboardMessage {
    FractalKeycode code;  ///< Keyboard input.
    FractalKeymod mod;    ///< Stateful modifier keys applied to keyboard input.
    bool pressed;         ///< `true` if pressed, `false` if released.
    uint8_t __pad[3];
} FractalKeyboardMessage;

/// @brief Mouse button message.
/// @details Message from mouse button.
typedef struct FractalMouseButtonMessage {
    FractalMouseButton button;  ///< Mouse button.
    bool pressed;               ///< `true` if clicked, `false` if released.
    uint8_t __pad[3];
} FractalMouseButtonMessage;

/// @brief Mouse wheel message.
/// @details Message from mouse wheel.
typedef struct FractalMouseWheelMessage {
    int32_t x;        ///< Horizontal delta of mouse wheel rotation. Negative values
                      ///< scroll left. Only used for Windows server.
    int32_t y;        ///< Vertical delta of mouse wheel rotation. Negative values
                      ///< scroll up. Only used for Windows server.
    float precise_x;  ///< Horizontal floating delta of mouse wheel/trackpad scrolling.
    float precise_y;  ///< Vertical floating delta of mouse wheel/trackpad scrolling.
} FractalMouseWheelMessage;

/// @brief Mouse motion message.
/// @details Member of FractalMessage. Mouse motion can be sent in either
/// relative or absolute mode via the `relative` member. Absolute mode treats
/// the `x` and `y` values as the exact destination for where the cursor will
/// appear. These values are sent from the client in device screen coordinates
/// and are translated in accordance with the values set via
/// FractalClientSetDimensions. Relative mode `x` and `y` values are not
/// affected by FractalClientSetDimensions and move the cursor with a signed
/// delta value from its previous location.
typedef struct FractalMouseMotionMessage {
    int32_t x;      ///< The absolute horizontal screen coordinate of the cursor  if
                    ///< `relative` is `false`, or the delta (can be negative) if
                    ///< `relative` is `true`.
    int32_t y;      ///< The absolute vertical screen coordinate of the cursor if
                    ///< `relative` is `false`, or the delta (can be negative) if
                    ///< `relative` is `true`.
    bool relative;  ///< `true` for relative mode, `false` for absolute mode.
                    ///< See details.
    int x_nonrel;
    int y_nonrel;
    uint8_t __pad[3];
} FractalMouseMotionMessage;

typedef struct FractalDiscoveryRequestMessage {
    int user_id;
    FractalTimeData time_data;
    char user_email[FRACTAL_ARGS_MAXLEN + 1];
} FractalDiscoveryRequestMessage;

typedef struct FractalMultigestureMessage {
    float d_theta;         ///< The amount the fingers rotated.
    float d_dist;          ///< The amount the fingers pinched.
    float x;               ///< Normalized gesture x-axis center.
    float y;               ///< Normalized gesture y-axis center.
    uint16_t num_fingers;  ///< Number of fingers used in the gesture.
} FractalMultigestureMessage;

typedef enum InteractionMode { CONTROL = 1, SPECTATE = 2, EXCLUSIVE_CONTROL = 3 } InteractionMode;

typedef enum FractalClientMessageType {
    CMESSAGE_NONE = 0,     ///< No Message
    MESSAGE_KEYBOARD = 1,  ///< `keyboard` FractalKeyboardMessage is valid in
                           ///< FractClientMessage.
    MESSAGE_KEYBOARD_STATE = 2,
    MESSAGE_MOUSE_BUTTON = 3,  ///< `mouseButton` FractalMouseButtonMessage is
                               ///< valid in FractClientMessage.
    MESSAGE_MOUSE_WHEEL = 4,   ///< `mouseWheel` FractalMouseWheelMessage is
                               ///< valid in FractClientMessage.
    MESSAGE_MOUSE_MOTION = 5,  ///< `mouseMotion` FractalMouseMotionMessage is

    MESSAGE_MOUSE_INACTIVE = 6,
    MESSAGE_MULTIGESTURE = 7,  ///< Gesture Event
    MESSAGE_RELEASE = 8,       ///< Message instructing the host to release all input
                               ///< that is currently pressed.
    MESSAGE_MBPS = 107,        ///< `mbps` double is valid in FractClientMessage.
    MESSAGE_PING = 108,
    MESSAGE_DIMENSIONS = 109,  ///< `dimensions.width` int and `dimensions.height`
                               ///< int is valid in FractClientMessage
    MESSAGE_VIDEO_NACK = 110,
    MESSAGE_AUDIO_NACK = 111,
    CMESSAGE_CLIPBOARD = 112,
    MESSAGE_IFRAME_REQUEST = 113,
    CMESSAGE_INTERACTION_MODE = 115,
    MESSAGE_DISCOVERY_REQUEST = 116,

    CMESSAGE_QUIT = 999,
} FractalClientMessageType;

typedef struct FractalClientMessage {
    FractalClientMessageType type;  ///< Input message type.
    unsigned int id;
    union {
        FractalKeyboardMessage keyboard;                  ///< Keyboard message.
        FractalMouseButtonMessage mouseButton;            ///< Mouse button message.
        FractalMouseWheelMessage mouseWheel;              ///< Mouse wheel message.
        FractalMouseMotionMessage mouseMotion;            ///< Mouse motion message.
        FractalDiscoveryRequestMessage discoveryRequest;  ///< Discovery request message.

        // MESSAGE_MULTIGESTURE
        FractalMultigestureMessage multigesture;  ///< Multigesture message.

        // CMESSAGE_INTERACTION_MODE
        InteractionMode interaction_mode;

        // MESSAGE_MBPS
        double mbps;

        // MESSAGE_PING
        int ping_id;

        // MESSAGE_DIMENSIONS
        struct dimensions {
            int width;
            int height;
            int dpi;
            CodecType codec_type;
        } dimensions;

        // MESSAGE_VIDEO_NACK or MESSAGE_AUDIO_NACK
        struct nack_data {
            int id;
            int index;
        } nack_data;

        // MESSAGE_KEYBOARD_STATE
        struct {
            short num_keycodes;
            bool caps_lock;
            bool num_lock;
            char keyboard_state[NUM_KEYCODES];
        };

        // MESSAGE_IFRAME_REQUEST
        bool reinitialize_encoder;
    };

    // CMESSAGE_CLIPBOARD
    ClipboardData clipboard;
} FractalClientMessage;

typedef enum FractalServerMessageType {
    SMESSAGE_NONE = 0,  ///< No Message
    MESSAGE_PONG = 1,
    MESSAGE_AUDIO_FREQUENCY = 2,
    SMESSAGE_CLIPBOARD = 3,
    SMESSAGE_WINDOW_TITLE = 4,
    MESSAGE_DISCOVERY_REPLY = 5,
    SMESSAGE_QUIT = 100,
} FractalServerMessageType;

typedef struct FractalDiscoveryReplyMessage {
    int client_id;
    int UDP_port;
    int TCP_port;
    int connection_id;
    int audio_sample_rate;
    char filename[300];
    char username[50];
} FractalDiscoveryReplyMessage;

typedef struct PeerUpdateMessage {
    int peer_id;
    int x;
    int y;
    bool is_controlling;
    FractalRGBColor color;
} PeerUpdateMessage;

typedef struct FractalServerMessage {
    FractalServerMessageType type;  ///< Input message type.
    union {
        int ping_id;
        int frequency;
    };
    union {
        ClipboardData clipboard;
        char window_title[0];
        char discovery_reply[0];
        char init_msg[0];
    };
} FractalServerMessage;

typedef struct FractalDestination {
    int host;
    int port;
} FractalDestination;

typedef struct Frame {
    FractalCursorImage cursor;
    int width;
    int height;
    CodecType codec_type;
    int size;
    bool is_iframe;
    int num_peer_update_msgs;
    unsigned char compressed_frame[];
} Frame;

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Print the memory trace of a process
 */
void print_memory_info();

/**
 * @brief                          Print the system info of the computer
 */
void print_system_info();

/**
 * @brief                          Run a system command via command prompt or
 *                                 terminal
 *
 * @param cmdline                  String of the system command to run
 * @param response                 Terminal output from the cmdline
 *
 * @returns                        0 or value of pipe if success, else -1
 */
int runcmd(const char* cmdline, char** response);

/**
 * @brief                          Retrieves the public IPv4 of the computer it
 *                                 is run on
 *
 * @returns                        The string of the public IPv4 address of the
 *                                 computer
 */
char* get_ip();

/**
 * @brief                          Reads a 16-byte hexidecimal string and copies
 * it into private_key
 *
 * @param hex_string               The hexidecimal string to copy
 * @param binary_private_key       The 16-byte buffer to copy the bytes into
 * @param hex_private_key          The 33-byte buffer to fill with a hex string
 *                                 representation of the private key.
 *
 * @returns                        True if hex_string was a 16-byte hexadecimal
 * value, otherwise false
 */
bool read_hexadecimal_private_key(char* hex_string, char* binary_private_key,
                                  char* hex_private_key);

/**
 * @brief                          Calculate the size of a FractalClientMessage
 *                                 struct
 *
 * @param fmsg                     The Fractal Client Message to find the size
 *
 * @returns                        The size of the Fractal Client Message struct
 */
int get_fmsg_size(FractalClientMessage* fmsg);

/**
 * @brief                          Terminates the protocol
 */
NORETURN void terminate_protocol();

/**
 * @brief                          Wrapper around malloc that will correctly exit the
 *                                 protocol when malloc fails
 */
void* safe_malloc(size_t size);

/**
 * @brief                          Wrapper around realloc that will correctly exit the
 *                                 protocol when realloc fails
 */
void* safe_realloc(void* buffer, size_t new_size);

/**
 * @brief                          Safely copy a string from source to destination.
 *
 * @details                        Copies at most `num` bytes from `source` to `destination`. Bytes
 *                                 after the first null character of `source` are not copied.
 *                                 If no null character is encountered within the first `num` bytes
 *                                 of `source`, `destination[num - 1]` will be manually set to zero,
 *                                 so `destination` is guaranteed to be null terminated, unless
 *                                 `num` is zero, in which case the `destination` is left unchanged.
 *
 * @param destination              Address to copy to. Should have at least `num` bytes available.
 *
 * @param source                   Address to copy from.
 *
 * @param num                      Number of bytes to copy.
 *
 * @returns                        True if all bytes of source were copied (i.e. strlen(source) <=
 *                                 num - 1)
 */
bool safe_strncpy(char* destination, const char* source, size_t num);

typedef struct DynamicBuffer {
    size_t size;
    size_t capacity;
    bool use_memory_regions;
    char* buf;
} DynamicBuffer;

/**
 * @brief                          Initializes a new dynamically sizing buffer.
 *                                 Note that accessing a dynamic buffer's buf outside
 *                                 of db->size is undefined behavior
 *
 * @param use_memory_regions       If true, will use OS-level memory regions [See allocate_region]
 *                                 If false, will use malloc for db->buf
 *                                 The DynamicBuffer struct itself will always use malloc
 *
 * @returns                        The dynamic buffer (With initial size 0)
 */
DynamicBuffer* init_dynamic_buffer(bool use_memory_regions);

/**
 * @brief                          This will resize the given DynamicBuffer to the given size.
 *                                 This function may reallocate db->buf
 *
 * @param db                       The DynamicBuffer to resize
 *
 * @param new_size                 The new size to resize db to
 */
void resize_dynamic_buffer(DynamicBuffer* db, size_t new_size);

/**
 * @brief                          This will free the DynamicBuffer and its contents
 *
 * @param db                       The DynamicBuffer to free
 */
void free_dynamic_buffer(DynamicBuffer* db);

// Dummy typedef for block allocator since only pointers are used anyway
// See fractal.c for the real BlockAllocator struct
typedef char BlockAllocator;

/**
 * @brief                          Creates a block allocator that will create and free blocks of the
 *                                 given block_size. The block allocator will _not_ interfere with
 *                                 the malloc heap.
 *
 * @param block_size               The size of blocks that this block allocator will allocate/free
 *
 * @returns                        The block allocator
 */
BlockAllocator* create_block_allocator(size_t block_size);

/**
 * @brief                          Allocates a block using the given block allocator
 *
 * @param block_allocator          The block allocator to use for allocating the block
 *
 * @returns                        The new block
 */
void* allocate_block(BlockAllocator* block_allocator);

/**
 * @brief                          Frees a block allocated by allocate_block
 *
 * @param block_allocator          The block allocator that the block was allocated from
 *
 * @param block                    The block to free
 */
void free_block(BlockAllocator* block_allocator, void* block);

/*
==================
Region Allocator
==================

Note: All of these functions are safe, and will FATAL_ERROR on failure

This is a thin wrapper around mmap/VirtualAlloc, and will give us fine-grained
control over regions of memory and whether or not the OS will allocate actual pages of RAM for us.
(Pages are usually size 4096, but are in-general OS-defined)

This gives us fine-grained control over the "reported memory usage", which represents pages of
RAM that we demand must be allocated, and must not be written-to/read-from by any other processes.
This is what controls when the OS is "out of RAM", and what is reported by the OS's "Task Manager".

The OS by-default uses lazy allocation. So we won't actually use RAM until we read or write
to a page allocated by allocate_region. All fresh pages we read from will be 0-ed out first

Once we read/write to a page, that page becomes allocated in RAM. If you're done using it,
and you want to tell the OS that the data in the region isn't important to you anymore, call the
`mark_unused_region` function. That will decrease our reported memory usage by the size of the
region. However, the OS won't actually take those pages from us until it's low on RAM.

If we want to tell the OS that the region is important to us again, call
`mark_used_region`. Just like allocate_region, this won't actually increase reported memory usage,
until we read/write to those pages. However, our pages will usually be left exactly as they were
prior to `mark_unused_region`. They'll only be zero-ed out if the OS needed those pages in the
meantime. This is much more efficient than having it be zero-ed out every time, it'll
only be zero-ed out if it was actually necessary to zero it out.

If we want to completely give the region back to the OS, call `deallocate_region`,
and the RAM + address space will be freed.

*/

/**
 * @brief                          Allocates a region of memory, of the requested size.
 *                                 This region will be allocated independently of
 *                                 the malloc heap or any block allocator.
 *                                 Will be zero-initialized.
 *
 *                                 This will only increase reported memory usage,
 *                                 once the pages are actually read or written to
 *
 * @param region_size              The size of the region to create
 *
 * @returns                        The new region of memory
 */
void* allocate_region(size_t region_size);

/**
 * @brief                          Marks the region as unused (for now).
 *                                 This will let other processes use it if they desire.
 *
 *                                 This will decrease the reported memory usage,
 *                                 by the size of the region that was used
 *
 * @param region                   The region to mark as unused
 */
void mark_unused_region(void* region);

/**
 * @brief                          Marks the region as used again.
 *                                 This will grab new memory regions from the OS,
 *                                 only give other processes have taken the
 *                                 memory while it was unused
 *
 *                                 This will only increase the reported memory usage,
 *                                 once the pages are used
 *
 * @param region                   The region to mark as used now
 */
void mark_used_region(void* region);

/**
 * @brief                          Resize the memory region.
 *
 *                                 This will try to simply grow the region,
 *                                 but if it can't, it will allocate a new region
 *                                 and copy everything over
 *
 * @param region                   The region to resize
 *
 * @param new_region_size          The new size of the region
 *
 * @returns                        The new pointer to the region
 *                                 (Will be different from the given region,
 *                                 if an allocation + copy was necessary)
 */
void* realloc_region(void* region, size_t new_region_size);

/**
 * @brief                          Give the region back to the OS
 *
 * @param region                   The region to deallocate
 */
void deallocate_region(void* region);

/**
 * @brief                          Returns a short string representing the current git commit
 *                                 of fractal at the time of compilation
 *
 * @returns                        The git commit
 */
char* fractal_git_revision();

#endif  // FRACTAL_H
