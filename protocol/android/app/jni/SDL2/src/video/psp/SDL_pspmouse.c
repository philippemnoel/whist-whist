/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2020 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_PSP

#include <stdio.h>

#include "SDL_error.h"
#include "SDL_mouse.h"
#include "../../events/SDL_events_c.h"

#include "SDL_pspmouse_c.h"


/* The implementation dependent data for the window manager cursor */
struct WMcursor {
    int unused;
};

#endif /* SDL_VIDEO_DRIVER_PSP */

/* vi: set ts=4 sw=4 expandtab: */
