#include "sdl_utils.h"

#if defined(_WIN32)
HHOOK g_hKeyboardHook;
LRESULT CALLBACK LowLevelKeyboardProc(INT nCode, WPARAM wParam, LPARAM lParam);
#endif

// Send a key to SDL event queue, presumably one that is captured and wouldn't
// naturally make it to the event queue by itself

void SendCapturedKey(FractalKeycode key, int type, int time) {
  SDL_Event e = {0};
  e.type = type;
  e.key.keysym.scancode = (SDL_Scancode)key;
  e.key.timestamp = time;
  SDL_PushEvent(&e);
}

// Handle SDL resize events
int resizingEventWatcher(void* data, SDL_Event* event) {
  if (event->type == SDL_WINDOWEVENT &&
      event->window.event == SDL_WINDOWEVENT_RESIZED) {
    // If the resize event if for the current window
    SDL_Window* win = SDL_GetWindowFromID(event->window.windowID);
    if (win == (SDL_Window*)data) {
      // Notify video.c about the active resizing
      set_video_active_resizing(true);
    }
  }
  return 0;
}

SDL_Window* initSDL(int output_width, int output_height) {
#if defined(_WIN32)
  // set Windows DPI
  SetProcessDpiAwareness(PROCESS_SYSTEM_DPI_AWARE);
#endif

#if defined(_WIN32)
  // Hook onto windows keyboard to intercept windows special key combinations
  g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                     GetModuleHandle(NULL), 0);
#endif

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
    LOG_ERROR("Could not initialize SDL - %s", SDL_GetError());
    return NULL;
  }

  // TODO: make this a commandline argument based on client app settings!
  int full_width = get_virtual_screen_width();
  int full_height = get_virtual_screen_height();

  // Default output dimensions will be full screen
  if (output_width < 0) {
    output_width = full_width;
  }

  if (output_height < 0) {
    output_height = full_height;
  }

  bool is_fullscreen =
      output_width == full_width && output_height == full_height;

  SDL_Window* window;

#if defined(_WIN32)
  int fullscreen_flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP;
#else
  int fullscreen_flags =
      SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALWAYS_ON_TOP;
#endif

  // Simulate fullscreen with borderless always on top, so that it can still be
  // used with multiple monitors
  window = SDL_CreateWindow(
      "Fractal", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, output_width,
      output_height,
      SDL_WINDOW_ALLOW_HIGHDPI | (is_fullscreen ? fullscreen_flags : 0));

  if (!is_fullscreen) {
    // Resize event handling
    SDL_AddEventWatch(resizingEventWatcher, (SDL_Window*)window);
    if (!window) {
      LOG_ERROR("SDL: could not create window - exiting: %s", SDL_GetError());
      return NULL;
    }
    SDL_SetWindowResizable((SDL_Window*)window, true);
  }

  return window;
}

void destroySDL(SDL_Window* window) {
  LOG_INFO("Destroying SDL");
#if defined(_WIN32)
  UnhookWindowsHookEx(g_hKeyboardHook);
#endif
  if (window) {
    SDL_DestroyWindow((SDL_Window*)window);
    window = NULL;
  }
  SDL_Quit();
}

#if defined(_WIN32)
// Function to capture keyboard strokes and block them if they encode special
// key combinations, with intent to redirect them to SendCapturedKey so that the
// keys can still be streamed over to the host

HHOOK mule;
LRESULT CALLBACK LowLevelKeyboardProc(INT nCode, WPARAM wParam, LPARAM lParam) {
  // By returning a non-zero value from the hook procedure, the
  // message does not get passed to the target window
  KBDLLHOOKSTRUCT* pkbhs = (KBDLLHOOKSTRUCT*)lParam;

  switch (nCode) {
    case HC_ACTION: {
      // Check to see if the CTRL key is pressed
      BOOL bControlKeyDown =
          GetAsyncKeyState(VK_CONTROL) >> ((sizeof(SHORT) * 8) - 1);
      BOOL bAltKeyDown = pkbhs->flags & LLKHF_ALTDOWN;

      int type = (pkbhs->flags & LLKHF_UP) ? SDL_KEYUP : SDL_KEYDOWN;
      int time = pkbhs->time;

      // Disable LWIN
      if (pkbhs->vkCode == VK_LWIN) {
        SendCapturedKey(FK_LGUI, type, time);
        return 1;
      }

      // Disable RWIN
      if (pkbhs->vkCode == VK_RWIN) {
        SendCapturedKey(FK_RGUI, type, time);
        return 1;
      }

      // Disable CTRL+ESC
      if (pkbhs->vkCode == VK_ESCAPE && bControlKeyDown) {
        SendCapturedKey(FK_ESCAPE, type, time);
        return 1;
      }

      // Disable ALT+ESC
      if (pkbhs->vkCode == VK_ESCAPE && bAltKeyDown) {
        SendCapturedKey(FK_ESCAPE, type, time);
        return 1;
      }

      // Disable ALT+TAB
      if (pkbhs->vkCode == VK_TAB && bAltKeyDown) {
        SendCapturedKey(FK_TAB, type, time);
        return 1;
      }

      // Disable ALT+F4
      if (pkbhs->vkCode == VK_F4 && bAltKeyDown) {
        SendCapturedKey(FK_F4, type, time);
        return 1;
      }

      break;
    }
    default:
      break;
  }
  return CallNextHookEx(mule, nCode, wParam, lParam);
}
#endif
