//
// Created by mathieu on 28/03/2020.
//
#include "x11capture.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/shm.h>
#include <X11/extensions/Xdamage.h>

#define USING_SHM false

int CreateCaptureDevice( struct CaptureDevice* device, UINT width, UINT height )
{
    device->display = XOpenDisplay( NULL );
    if (!device->display) {
       mprintf("ERROR: CreateCaptureDevice display did not open\n");
       return -1;
    }
    device->root = DefaultRootWindow( device->display );
    XWindowAttributes window_attributes;
    if( !XGetWindowAttributes( device->display, device->root, &window_attributes ) )
    {
        fprintf( stderr, "Error while getting window attributes" );
        return -1;
    }
    device->width = window_attributes.width;
    device->height = window_attributes.height;

    int damage_event, damage_error;
    XDamageQueryExtension( device->display, &damage_event, &damage_error );
    device->damage = XDamageCreate( device->display, device->root, XDamageReportRawRectangles );
    device->event = damage_event;

#if USING_SHM
    Screen* screen = window_attributes.screen;
    device->image = XShmCreateImage( device->display,
                                     DefaultVisualOfScreen( screen ), //DefaultVisual(device->display, 0), // Use a correct visual. Omitted for brevity
                                     DefaultDepthOfScreen( screen ), //24,   // Determine correct depth from the visual. Omitted for brevity
                                     ZPixmap, NULL, &device->segment, device->width, device->height );

    device->segment.shmid = shmget( IPC_PRIVATE,
                                    device->image->bytes_per_line * device->image->height,
                                    IPC_CREAT | 0777 );


    device->segment.shmaddr = device->image->data = shmat( device->segment.shmid, 0, 0 );
    device->segment.readOnly = False;


    if( !XShmAttach( device->display, &device->segment ) )
    {
        fprintf( stderr, "Error while attaching display" );
        return -1;
    }
    device->frame_data = device->image->data;
#else
    CaptureScreen(device);
#endif
    
    return 0;
}

int CaptureScreen( struct CaptureDevice* device )
{
    static bool first = true;

    XLockDisplay(device->display);

    int update = 0;
    while( XPending( device->display ) )
    {

        // XDamageNotifyEvent* dev; unused, remove or is this needed and should be used?
        XEvent ev;
        XNextEvent( device->display, &ev );
        if( ev.type == device->event + XDamageNotify )
        {
            update = 1;
        }
    }

    if( update || first )
    {
	first = false;

        XDamageSubtract( device->display, device->damage, None, None );

#if USING_SHM
        if( !XShmGetImage( device->display,
                           device->root,
                           device->image,
                           0,
                           0,
                           AllPlanes ) )
        {
            fprintf( stderr, "Error while capturing the screen" );
            update = -1;
        }
#else
	device->image = XGetImage(device->display, device->root, 0, 0, device->width, device->height, AllPlanes, ZPixmap);
        device->frame_data = device->image->data;
	if (!device->image) {
            fprintf( stderr, "Error while capturing the screen" );
            update = -1;
	}
#endif
    }
    XUnlockDisplay(device->display);
    return update;
}

void ReleaseScreen( struct CaptureDevice* device )
{

}

void DestroyCaptureDevice( struct CaptureDevice* device )
{
    XFree( device->image );
    XCloseDisplay( device->display );
}
