#ifndef WHIST_CLIENT_FRONTEND_H
#define WHIST_CLIENT_FRONTEND_H

#include <whist/core/whist.h>
#include <whist/core/error_codes.h>
#include "frontend_structs.h"
#include "api.h"

typedef struct WhistFrontend WhistFrontend;

typedef enum FrontendEventType {
    FRONTEND_EVENT_UNHANDLED = 0,
    FRONTEND_EVENT_RESIZE,
    FRONTEND_EVENT_MOVE,
    FRONTEND_EVENT_CLOSE,
    FRONTEND_EVENT_MINIMIZE,
    FRONTEND_EVENT_RESTORE,
    FRONTEND_EVENT_VISIBILITY,
    FRONTEND_EVENT_AUDIO_UPDATE,
    FRONTEND_EVENT_KEYPRESS,
    FRONTEND_EVENT_MOUSE_MOTION,
    FRONTEND_EVENT_MOUSE_BUTTON,
    FRONTEND_EVENT_MOUSE_WHEEL,
    FRONTEND_EVENT_MOUSE_LEAVE,
    FRONTEND_EVENT_GESTURE,
    FRONTEND_EVENT_FILE_DRAG,
    FRONTEND_EVENT_FILE_DROP,
    FRONTEND_EVENT_QUIT,
} FrontendEventType;

typedef struct FrontendKeypressEvent {
    WhistKeycode code;
    WhistKeymod mod;
    bool pressed;
} FrontendKeypressEvent;

typedef struct FrontendResizeEvent {
    int id;
    int width;
    int height;
} FrontendResizeEvent;

typedef struct FrontendCloseEvent {
    int id;
} FrontendCloseEvent;

typedef struct FrontendMoveEvent {
    int id;
    int x;
    int y;
} FrontendMoveEvent;

typedef struct FrontendMinimizeEvent {
    int id;
} FrontendMinimizeEvent;

typedef struct FrontendRestoreEvent {
    int id;
} FrontendRestoreEvent;

typedef struct FrontendVisibilityEvent {
    int id;
    bool visible;
} FrontendVisibilityEvent;

typedef struct FrontendMouseMotionEvent {
    int id;
    struct {
        int x;
        int y;
    } absolute;
    struct {
        int x;
        int y;
    } relative;
    bool relative_mode;
} FrontendMouseMotionEvent;

typedef struct FrontendMouseButtonEvent {
    WhistMouseButton button;
    bool pressed;
} FrontendMouseButtonEvent;

typedef struct FrontendMouseWheelEvent {
    WhistMouseWheelMomentumType momentum_phase;
    struct {
        int x;
        int y;
    } delta;
    struct {
        float x;
        float y;
    } precise_delta;
} FrontendMouseWheelEvent;

typedef struct FrontendGestureEvent {
    struct {
        float theta;
        float dist;
    } delta;
    struct {
        float x;
        float y;
    } center;
    unsigned int num_fingers;
    WhistMultigestureType type;
} FrontendGestureEvent;

typedef struct FrontendFileDropEvent {
    struct {
        int x;
        int y;
    } position;
    char* filename;  // must be freed by handler, if not NULL
    bool end_drop;   // true when ending a series of a drop events part of the same multi-file drop
} FrontendFileDropEvent;

typedef struct FrontendFileDragEvent {
    struct {
        int x;
        int y;
    } position;
    int group_id;
    bool end_drag;
    char* filename;  // File being dragged (multiple files should
                     //     be sent in multiple messages)
} FrontendFileDragEvent;

typedef struct FrontendQuitEvent {
    bool quit_application;
} FrontendQuitEvent;

typedef struct WhistFrontendEvent {
    FrontendEventType type;
    union {
        FrontendKeypressEvent keypress;
        FrontendMouseMotionEvent mouse_motion;
        FrontendMouseButtonEvent mouse_button;
        FrontendMouseWheelEvent mouse_wheel;
        FrontendGestureEvent gesture;
        FrontendFileDropEvent file_drop;
        FrontendFileDragEvent file_drag;
        FrontendQuitEvent quit;
        FrontendResizeEvent resize;
        FrontendVisibilityEvent visibility;
        FrontendCloseEvent close;
        FrontendMoveEvent move;
        FrontendMinimizeEvent minimize;
        FrontendRestoreEvent restore;
    };
} WhistFrontendEvent;

// TODO: Opaquify this
#define FRONTEND_FUNCTION_TABLE_DECLARATION(return_type, name, ...) \
    return_type (*name)(__VA_ARGS__);
typedef struct WhistFrontendFunctionTable {
    FRONTEND_API(FRONTEND_FUNCTION_TABLE_DECLARATION)
} WhistFrontendFunctionTable;
#undef FRONTEND_FUNCTION_TABLE_DECLARATION

struct WhistFrontend {
    void* context;
    unsigned int id;
    const WhistFrontendFunctionTable* call;
    const char* type;
};

#define WHIST_FRONTEND_SDL "sdl"
#define WHIST_FRONTEND_EXTERNAL "external"

#define FRONTEND_HEADER_DECLARATION(return_type, name, ...) \
    return_type whist_frontend_##name(__VA_ARGS__);
FRONTEND_API(FRONTEND_HEADER_DECLARATION)
#undef FRONTEND_HEADER_DECLARATION

WhistFrontend* whist_frontend_create(const char* type);
unsigned int whist_frontend_get_id(WhistFrontend* frontend);
const char* whist_frontend_event_type_string(FrontendEventType type);

#endif  // WHIST_CLIENT_FRONTEND_H
