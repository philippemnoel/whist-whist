/*
 * User input processing on Linux Ubuntu.
 *
 * Copyright Fractal Computers, Inc. 2020
 **/

#include "input_driver.h"

#if !USING_XTEST_INPUT_DRIVER

#include <fcntl.h>
#include <linux/uinput.h>
#include <dirent.h>
#include <sys/sysmacros.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "../utils/string_utils.h"

#define _FRACTAL_IOCTL_TRY(FD, PARAMS...)                                          \
    if (ioctl(FD, PARAMS) == -1) {                                                 \
        char buf[1024];                                                            \
        /* strerror_r should not fail here since ioctl returned -1 */              \
        strerror_r(errno, buf, 1024);                                              \
        mprintf("Failure at setting " #PARAMS " on fd " #FD ". Error: %s\n", buf); \
        goto failure;                                                              \
    }

// we control this to specify the normalization to uinput during device creation; we run into
// annoying overflow issues if this is on the order of magnitude 0xffff
#define UINPUT_MOUSE_COORDINATE_RANGE 0xfff

// @brief Linux keycodes for replaying SDL user inputs on server
// @details index is SDL keycode, value is Linux keycode.
// To debug specific keycodes, use 'sudo showkey --keycodes'.
const int linux_keycodes[NUM_KEYCODES] = {
    0,               // SDL keycodes start at index 4
    0,               // SDL keycodes start at index 4
    0,               // SDL keycodes start at index 4
    0,               // SDL keycodes start at index 4
    KEY_A,           // 4 -> A
    KEY_B,           // 5 -> B
    KEY_C,           // 6 -> C
    KEY_D,           // 7 -> D
    KEY_E,           // 8 -> E
    KEY_F,           // 9 -> F
    KEY_G,           // 10 -> G
    KEY_H,           // 11 -> H
    KEY_I,           // 12 -> I
    KEY_J,           // 13 -> J
    KEY_K,           // 14 -> K
    KEY_L,           // 15 -> L
    KEY_M,           // 16 -> M
    KEY_N,           // 17 -> N
    KEY_O,           // 18 -> O
    KEY_P,           // 19 -> P
    KEY_Q,           // 20 -> Q
    KEY_R,           // 21 -> R
    KEY_S,           // 22 -> S
    KEY_T,           // 23 -> T
    KEY_U,           // 24 -> U
    KEY_V,           // 25 -> V
    KEY_W,           // 26 -> W
    KEY_X,           // 27 -> X
    KEY_Y,           // 28 -> Y
    KEY_Z,           // 29 -> Z
    KEY_1,           // 30 -> 1
    KEY_2,           // 31 -> 2
    KEY_3,           // 32 -> 3
    KEY_4,           // 33 -> 4
    KEY_5,           // 34 -> 5
    KEY_6,           // 35 -> 6
    KEY_7,           // 36 -> 7
    KEY_8,           // 37 -> 8
    KEY_9,           // 38 -> 9
    KEY_0,           // 39 -> 0
    KEY_ENTER,       // 40 -> Enter
    KEY_ESC,         // 41 -> Escape
    KEY_BACKSPACE,   // 42 -> Backspace
    KEY_TAB,         // 43 -> Tab
    KEY_SPACE,       // 44 -> Space
    KEY_MINUS,       // 45 -> Minus
    KEY_EQUAL,       // 46 -> Equal
    KEY_LEFTBRACE,   // 47 -> Left Bracket
    KEY_RIGHTBRACE,  // 48 -> Right Bracket
    KEY_BACKSLASH,   // 49 -> Backslash
    0,               // 50 -> no SDL keycode at index 50
    KEY_SEMICOLON,   // 51 -> Semicolon
    KEY_APOSTROPHE,  // 52 -> Apostrophe
    KEY_GRAVE,       // 53 -> Backtick
    KEY_COMMA,       // 54 -> Comma
    KEY_DOT,         // 55 -> Period
    KEY_SLASH,       // 56 -> Forward Slash
    KEY_CAPSLOCK,    // 57 -> Capslock
    KEY_F1,          // 58 -> F1
    KEY_F2,          // 59 -> F2
    KEY_F3,          // 60 -> F3
    KEY_F4,          // 61 -> F4
    KEY_F5,          // 62 -> F5
    KEY_F6,          // 63 -> F6
    KEY_F7,          // 64 -> F7
    KEY_F8,          // 65 -> F8
    KEY_F9,          // 66 -> F9
    KEY_F10,         // 67 -> F10
    KEY_F11,         // 68 -> F11
    KEY_F12,         // 69 -> F12
    KEY_SYSRQ,       // 70 -> Print Screen
    KEY_SCROLLLOCK,  // 71 -> Scroll Lock
    KEY_PAUSE,       // 72 -> Pause
    KEY_INSERT,      // 73 -> Insert
    KEY_HOME,        // 74 -> Home
    KEY_PAGEUP,      // 75 -> Pageup
    KEY_DELETE,      // 76 -> Delete
    KEY_END,         // 77 -> End
    KEY_PAGEDOWN,    // 78 -> Pagedown
    KEY_RIGHT,       // 79 -> Right
    KEY_LEFT,        // 80 -> Left
    KEY_DOWN,        // 81 -> Down
    KEY_UP,          // 82 -> Up
    KEY_NUMLOCK,     // 83 -> Numlock
    KEY_KPSLASH,     // 84 -> Numeric Keypad Divide
    KEY_KPASTERISK,  // 85 -> Numeric Keypad Multiply
    KEY_KPMINUS,     // 86 -> Numeric Keypad Minus
    KEY_KPPLUS,      // 87 -> Numeric Keypad Plus
    KEY_KPENTER,     // 88 -> Numeric Keypad Enter
    KEY_KP1,         // 89 -> Numeric Keypad 1
    KEY_KP2,         // 90 -> Numeric Keypad 2
    KEY_KP3,         // 91 -> Numeric Keypad 3
    KEY_KP4,         // 92 -> Numeric Keypad 4
    KEY_KP5,         // 93 -> Numeric Keypad 5
    KEY_KP6,         // 94 -> Numeric Keypad 6
    KEY_KP7,         // 95 -> Numeric Keypad 7
    KEY_KP8,         // 96 -> Numeric Keypad 8
    KEY_KP9,         // 97 -> Numeric Keypad 9
    KEY_KP0,         // 98 -> Numeric Keypad 0
    KEY_KPDOT,       // 99 -> Numeric Keypad Period
    0,               // 100 -> no SDL keycode at index 100
    KEY_COMPOSE,     // 101 -> Application
    0,               // 102 -> no SDL keycode at index 102
    0,               // 103 -> no SDL keycode at index 103
    KEY_F13,         // 104 -> F13
    KEY_F14,         // 105 -> F14
    KEY_F15,         // 106 -> F15
    KEY_F16,         // 107 -> F16
    KEY_F17,         // 108 -> F17
    KEY_F18,         // 109 -> F18
    KEY_F19,         // 110 -> F19
    KEY_F20,         // 111 -> F20
    KEY_F21,         // 112 -> F21
    KEY_F22,         // 113 -> F22
    KEY_F23,         // 114 -> F23
    KEY_F24,         // 115 -> F24
    0,               // 116 -> Execute (can't find what this is supposed to be)
    KEY_HELP,        // 117 -> Help
    KEY_MENU,        // 118 -> Menu
    KEY_SELECT,      // 119 -> Select
    0,               // 120 -> no SDL keycode at index 120
    0,               // 121 -> no SDL keycode at index 121
    0,               // 122 -> no SDL keycode at index 122
    0,               // 123 -> no SDL keycode at index 123
    0,               // 124 -> no SDL keycode at index 124
    0,               // 125 -> no SDL keycode at index 125
    0,               // 126 -> no SDL keycode at index 126
    KEY_MUTE,        // 127 -> Mute
    KEY_VOLUMEUP,    // 128 -> Volume Up
    KEY_VOLUMEDOWN,  // 129 -> Volume Down
    0,               // 130 -> no SDL keycode at index 130
    0,               // 131 -> no SDL keycode at index 131
    0,               // 132 -> no SDL keycode at index 132
    0,               // 133 -> no SDL keycode at index 133
    0,               // 134 -> no SDL keycode at index 134
    0,               // 135 -> no SDL keycode at index 135
    0,               // 136 -> no SDL keycode at index 136
    0,               // 137 -> no SDL keycode at index 137
    0,               // 138 -> no SDL keycode at index 138
    0,               // 139 -> no SDL keycode at index 139
    0,               // 140 -> no SDL keycode at index 140
    0,               // 141 -> no SDL keycode at index 141
    0,               // 142 -> no SDL keycode at index 142
    0,               // 143 -> no SDL keycode at index 143
    0,               // 144 -> no SDL keycode at index 144
    0,               // 145 -> no SDL keycode at index 145
    0,               // 146 -> no SDL keycode at index 146
    0,               // 147 -> no SDL keycode at index 147
    0,               // 148 -> no SDL keycode at index 148
    0,               // 149 -> no SDL keycode at index 149
    0,               // 150 -> no SDL keycode at index 150
    0,               // 151 -> no SDL keycode at index 151
    0,               // 152 -> no SDL keycode at index 152
    0,               // 153 -> no SDL keycode at index 153
    0,               // 154 -> no SDL keycode at index 154
    0,               // 155 -> no SDL keycode at index 155
    0,               // 156 -> no SDL keycode at index 156
    0,               // 157 -> no SDL keycode at index 157
    0,               // 158 -> no SDL keycode at index 158
    0,               // 159 -> no SDL keycode at index 159
    0,               // 160 -> no SDL keycode at index 160
    0,               // 161 -> no SDL keycode at index 161
    0,               // 162 -> no SDL keycode at index 162
    0,               // 163 -> no SDL keycode at index 163
    0,               // 164 -> no SDL keycode at index 164
    0,               // 165 -> no SDL keycode at index 165
    0,               // 166 -> no SDL keycode at index 166
    0,               // 167 -> no SDL keycode at index 167
    0,               // 168 -> no SDL keycode at index 168
    0,               // 169 -> no SDL keycode at index 169
    0,               // 170 -> no SDL keycode at index 170
    0,               // 171 -> no SDL keycode at index 171
    0,               // 172 -> no SDL keycode at index 172
    0,               // 173 -> no SDL keycode at index 173
    0,               // 174 -> no SDL keycode at index 174
    0,               // 175 -> no SDL keycode at index 175
    0,               // 176 -> no SDL keycode at index 176
    0,               // 177 -> no SDL keycode at index 177
    0,               // 178 -> no SDL keycode at index 178
    0,               // 179 -> no SDL keycode at index 179
    0,               // 180 -> no SDL keycode at index 180
    0,               // 181 -> no SDL keycode at index 181
    0,               // 182 -> no SDL keycode at index 182
    0,               // 183 -> no SDL keycode at index 183
    0,               // 184 -> no SDL keycode at index 184
    0,               // 185 -> no SDL keycode at index 185
    0,               // 186 -> no SDL keycode at index 186
    0,               // 187 -> no SDL keycode at index 187
    0,               // 188 -> no SDL keycode at index 188
    0,               // 189 -> no SDL keycode at index 189
    0,               // 190 -> no SDL keycode at index 190
    0,               // 191 -> no SDL keycode at index 191
    0,               // 192 -> no SDL keycode at index 192
    0,               // 193 -> no SDL keycode at index 193
    0,               // 194 -> no SDL keycode at index 194
    0,               // 195 -> no SDL keycode at index 195
    0,               // 196 -> no SDL keycode at index 196
    0,               // 197 -> no SDL keycode at index 197
    0,               // 198 -> no SDL keycode at index 198
    0,               // 199 -> no SDL keycode at index 199
    0,               // 200 -> no SDL keycode at index 200
    0,               // 201 -> no SDL keycode at index 201
    0,               // 202 -> no SDL keycode at index 202
    0,               // 203 -> no SDL keycode at index 203
    0,               // 204 -> no SDL keycode at index 204
    0,               // 205 -> no SDL keycode at index 205
    0,               // 206 -> no SDL keycode at index 206
    0,               // 207 -> no SDL keycode at index 207
    0,               // 208 -> no SDL keycode at index 208
    0,               // 209 -> no SDL keycode at index 209
    0,               // 210 -> no SDL keycode at index 210
    0,               // 211 -> no SDL keycode at index 212
    0,               // 213 -> no SDL keycode at index 213
    0,               // 214 -> no SDL keycode at index 214
    0,               // 215 -> no SDL keycode at index 215
    0,               // 216 -> no SDL keycode at index 216
    0,               // 217 -> no SDL keycode at index 217
    0,               // 218 -> no SDL keycode at index 218
    0,               // 219 -> no SDL keycode at index 219
    0,               // 220 -> no SDL keycode at index 220
    0,               // 221 -> no SDL keycode at index 221
    0,               // 222 -> no SDL keycode at index 222
    0,               // 223 -> no SDL keycode at index 223
    0,
    KEY_LEFTCTRL,      // 224 -> Left Ctrl
    KEY_LEFTSHIFT,     // 225 -> Left Shift
    KEY_LEFTALT,       // 226 -> Left Alt
    KEY_LEFTMETA,      // 227 -> Left GUI (Windows Key)
    KEY_RIGHTCTRL,     // 228 -> Right Ctrl
    KEY_RIGHTSHIFT,    // 229 -> Right Shift
    KEY_RIGHTALT,      // 230 -> Right Alt
    KEY_RIGHTMETA,     // 231 -> Right GUI (Windows Key)
    0,                 // 232 -> no SDL keycode at index 232
    0,                 // 233 -> no SDL keycode at index 233
    0,                 // 234 -> no SDL keycode at index 234
    0,                 // 235 -> no SDL keycode at index 235
    0,                 // 236 -> no SDL keycode at index 236
    0,                 // 237 -> no SDL keycode at index 237
    0,                 // 238 -> no SDL keycode at index 238
    0,                 // 239 -> no SDL keycode at index 239
    0,                 // 240 -> no SDL keycode at index 240
    0,                 // 241 -> no SDL keycode at index 241
    0,                 // 242 -> no SDL keycode at index 242
    0,                 // 243 -> no SDL keycode at index 243
    0,                 // 244 -> no SDL keycode at index 244
    0,                 // 245 -> no SDL keycode at index 245
    0,                 // 246 -> no SDL keycode at index 246
    0,                 // 247 -> no SDL keycode at index 247
    0,                 // 248 -> no SDL keycode at index 248
    0,                 // 249 -> no SDL keycode at index 249
    0,                 // 250 -> no SDL keycode at index 250
    0,                 // 251 -> no SDL keycode at index 251
    0,                 // 252 -> no SDL keycode at index 252
    0,                 // 253 -> no SDL keycode at index 253
    0,                 // 254 -> no SDL keycode at index 254
    0,                 // 255 -> no SDL keycode at index 255
    0,                 // 256 -> no SDL keycode at index 256
    KEY_MODE,          // 257 -> ModeSwitch
    KEY_NEXTSONG,      // 258 -> Audio/Media Next
    KEY_PREVIOUSSONG,  // 259 -> Audio/Media Prev
    KEY_STOPCD,        // 260 -> Audio/Media Stop
    KEY_PLAYPAUSE,     // 261 -> Audio/Media Play
    KEY_MUTE,          // 262 -> Audio/Media Mute
    KEY_SELECT         // 263 -> Media Select
};

const int linux_mouse_buttons[6] = {
    0,           // 0 -> no FractalMouseButton
    BTN_LEFT,    // 1 -> Left Button
    BTN_MIDDLE,  // 2 -> Middle Button
    BTN_RIGHT,   // 3 -> Right Button
    BTN_3,       // 4 -> Extra Mouse Button 1
    BTN_4        // 5 -> Extra Mouse Button 2
};

#define GetLinuxKeyCode(sdl_keycode) linux_keycodes[sdl_keycode]
#define GetLinuxMouseButton(sdl_button) linux_mouse_buttons[sdl_button]

int unix_socket;

InputDevice* create_input_device() {
    struct sockaddr_un addr;
    unix_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (unix_socket == -1) {
        LOG_ERROR("uinput: open socket failed");
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    safe_strncpy(addr.sun_path, "/tmp/uinput.socket", sizeof(addr.sun_path));
    if (connect(unix_socket, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        LOG_ERROR("uinput: connect failed");
    }

    InputDevice* input_device = safe_malloc(sizeof(InputDevice));
    memset(input_device, 0, sizeof(InputDevice));

    /* // create event writing FDs */
    /* input_device->fd_absmouse = open("/dev/uinput", O_WRONLY | O_NONBLOCK); */
    /* input_device->fd_relmouse = open("/dev/uinput", O_WRONLY | O_NONBLOCK); */
    /* input_device->fd_keyboard = open("/dev/uinput", O_WRONLY | O_NONBLOCK); */

    /* if (input_device->fd_absmouse < 0 || input_device->fd_relmouse < 0 || */
    /*     input_device->fd_keyboard < 0) { */
    /*     char buf[1024]; */
    /*     strerror_r(errno, buf, 1024); */
    /*     LOG_ERROR("CreateInputDevice: Error opening '/dev/uinput' for writing: %s", buf); */
    /*     goto failure; */
    /* } */

    /* // register keyboard events */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_keyboard, UI_SET_EVBIT, EV_KEY); */
    /* int kcode; */
    /* for (int i = 0; i < NUM_KEYCODES; ++i) { */
    /*     if ((kcode = GetLinuxKeyCode(i))) { */
    /*         _FRACTAL_IOCTL_TRY(input_device->fd_keyboard, UI_SET_KEYBIT, kcode); */
    /*     } */
    /* } */

    /* // register relative mouse events */

    /* _FRACTAL_IOCTL_TRY(input_device->fd_relmouse, UI_SET_EVBIT, EV_KEY); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_relmouse, UI_SET_KEYBIT, BTN_LEFT); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_relmouse, UI_SET_KEYBIT, BTN_RIGHT); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_relmouse, UI_SET_KEYBIT, BTN_MIDDLE); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_relmouse, UI_SET_KEYBIT, BTN_3); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_relmouse, UI_SET_KEYBIT, BTN_4); */

    /* _FRACTAL_IOCTL_TRY(input_device->fd_relmouse, UI_SET_EVBIT, EV_REL); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_relmouse, UI_SET_RELBIT, REL_X); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_relmouse, UI_SET_RELBIT, REL_Y); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_relmouse, UI_SET_RELBIT, REL_WHEEL); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_relmouse, UI_SET_RELBIT, REL_HWHEEL); */

    /* // register absolute mouse events */

    /* _FRACTAL_IOCTL_TRY(input_device->fd_absmouse, UI_SET_EVBIT, EV_KEY); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_absmouse, UI_SET_KEYBIT, BTN_TOUCH); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_absmouse, UI_SET_KEYBIT, BTN_TOOL_PEN); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_absmouse, UI_SET_KEYBIT, BTN_STYLUS); */

    /* _FRACTAL_IOCTL_TRY(input_device->fd_absmouse, UI_SET_EVBIT, EV_ABS); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_absmouse, UI_SET_ABSBIT, ABS_X); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_absmouse, UI_SET_ABSBIT, ABS_Y); */

    /* _FRACTAL_IOCTL_TRY(input_device->fd_absmouse, UI_SET_PROPBIT, INPUT_PROP_POINTER); */

    /* // config devices */

    /* struct uinput_setup usetup = {0}; */
    /* usetup.id.bustype = BUS_USB; */
    /* usetup.id.vendor = 0xf4c1; */
    /* usetup.id.version = 1; */

    /* // keyboard config */

    /* usetup.id.product = 0x1122; */
    /* strcpy(usetup.name, "Fractal Virtual Keyboard"); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_keyboard, UI_DEV_SETUP, &usetup); */

    /* // relative mouse config */

    /* usetup.id.product = 0x1123; */
    /* strcpy(usetup.name, "Fractal Virtual Relative Input"); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_relmouse, UI_DEV_SETUP, &usetup); */

    /* // absolute mouse config */

    /* usetup.id.product = 0x1124; */
    /* strcpy(usetup.name, "Fractal Virtual Absolute Input"); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_absmouse, UI_DEV_SETUP, &usetup); */

    /* struct uinput_abs_setup axis_setup = {0}; */
    /* axis_setup.absinfo.resolution = 1; */

    /* axis_setup.code = ABS_X; */
    /* axis_setup.absinfo.maximum = UINPUT_MOUSE_COORDINATE_RANGE; */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_absmouse, UI_ABS_SETUP, &axis_setup); */

    /* axis_setup.code = ABS_Y; */
    /* axis_setup.absinfo.maximum = UINPUT_MOUSE_COORDINATE_RANGE; */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_absmouse, UI_ABS_SETUP, &axis_setup); */

    /* // create devices */

    /* _FRACTAL_IOCTL_TRY(input_device->fd_absmouse, UI_DEV_CREATE); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_relmouse, UI_DEV_CREATE); */
    /* _FRACTAL_IOCTL_TRY(input_device->fd_keyboard, UI_DEV_CREATE); */
    /* LOG_INFO("Created input devices!"); */

    return input_device;
failure:
    destroy_input_device(input_device);
    return NULL;
}

void destroy_input_device(InputDevice* input_device) {
    if (!input_device) {
        LOG_INFO("destroy_input_device: Nothing to do, device is null!");
        return;
    }
    /* ioctl(input_device->fd_absmouse, UI_DEV_DESTROY); */
    /* ioctl(input_device->fd_relmouse, UI_DEV_DESTROY); */
    /* ioctl(input_device->fd_keyboard, UI_DEV_DESTROY); */
    /* close(input_device->fd_absmouse); */
    /* close(input_device->fd_relmouse); */
    /* close(input_device->fd_keyboard); */
    free(input_device);
}

void emit_input_event(int fd, int type, int code, int val) {
    struct input_event ie;
    ie.type = type;
    ie.code = code;
    ie.value = val;

    ie.time.tv_sec = 0;
    ie.time.tv_usec = 0;

    LOG_INFO("emitting input event");
    // write(fd, &ie, sizeof(ie));
    write(unix_socket, &ie, sizeof(ie));
}

int get_keyboard_modifier_state(InputDevice* input_device, FractalKeycode sdl_keycode) {
    switch (sdl_keycode) {
        case FK_CAPSLOCK:
            return input_device->caps_lock;
        case FK_NUMLOCK:
            return input_device->num_lock;
        default:
            LOG_WARNING("Not a modifier!");
            return -1;
    }
}

int get_keyboard_key_state(InputDevice* input_device, FractalKeycode sdl_keycode) {
    return input_device->keyboard_state[sdl_keycode];
    return -1;
}

int emit_key_event(InputDevice* input_device, FractalKeycode sdl_keycode, int pressed) {
    emit_input_event(input_device->fd_keyboard, EV_KEY, GetLinuxKeyCode(sdl_keycode), pressed);
    emit_input_event(input_device->fd_keyboard, EV_SYN, SYN_REPORT, 0);
    input_device->keyboard_state[sdl_keycode] = pressed;

    if (sdl_keycode == FK_CAPSLOCK && pressed) {
        input_device->caps_lock = !input_device->caps_lock;
    }
    if (sdl_keycode == FK_NUMLOCK && pressed) {
        input_device->num_lock = !input_device->num_lock;
    }

    return 0;
}

int emit_mouse_motion_event(InputDevice* input_device, int32_t x, int32_t y, int relative) {
    if (relative) {
        emit_input_event(input_device->fd_relmouse, EV_REL, REL_X, x);
        emit_input_event(input_device->fd_relmouse, EV_REL, REL_Y, y);
        emit_input_event(input_device->fd_relmouse, EV_SYN, SYN_REPORT, 0);
    } else {
        emit_input_event(
            input_device->fd_absmouse, EV_ABS, ABS_X,
            (int)(x * (int32_t)UINPUT_MOUSE_COORDINATE_RANGE / (int32_t)MOUSE_SCALING_FACTOR));
        emit_input_event(
            input_device->fd_absmouse, EV_ABS, ABS_Y,
            (int)(y * (int32_t)UINPUT_MOUSE_COORDINATE_RANGE / (int32_t)MOUSE_SCALING_FACTOR));
        emit_input_event(input_device->fd_absmouse, EV_KEY, BTN_TOOL_PEN, 1);
        emit_input_event(input_device->fd_absmouse, EV_SYN, SYN_REPORT, 0);
    }
    return 0;
}

int emit_mouse_button_event(InputDevice* input_device, FractalMouseButton button, int pressed) {
    emit_input_event(input_device->fd_relmouse, EV_KEY, GetLinuxMouseButton(button), pressed);
    emit_input_event(input_device->fd_relmouse, EV_SYN, SYN_REPORT, 0);
    return 0;
}

int emit_mouse_wheel_event(InputDevice* input_device, int32_t x, int32_t y) {
    emit_input_event(input_device->fd_relmouse, EV_REL, REL_HWHEEL, x);
    emit_input_event(input_device->fd_relmouse, EV_REL, REL_WHEEL, y);
    emit_input_event(input_device->fd_relmouse, EV_SYN, SYN_REPORT, 0);
    return 0;
}

#endif  // !USING_XTEST_INPUT_DRIVER
