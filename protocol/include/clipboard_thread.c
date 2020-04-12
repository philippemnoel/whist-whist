#include "clipboard.h"
#include "fractal.h"

// CLIPBOARD THREAD HANDLING

int UpdateClipboardThread( void* opaque );

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

bool isUpdatingClipboard()
{
	return updating_clipboard;
}

bool updateSetClipboard( ClipboardData* cb )
{
	if( updating_clipboard )
	{
		mprintf( "Tried to SetClipboard, but clipboard is updating\n" );
		return false;
	}

	updating_clipboard = true;
	updating_set_clipboard = true;
	clipboard = cb;

	SDL_SemPost( clipboard_semaphore );

	return true;
}

bool pendingUpdateClipboard()
{
	return pending_update_clipboard;
}

void initUpdateClipboard( SEND_FMSG* send_fmsg_local, char* server_ip_local )
{
	connected = true;

	server_ip = server_ip_local;
	send_fmsg = send_fmsg_local;

	updating_clipboard = false;
	pending_update_clipboard = false;
	StartTimer( (clock*)&last_clipboard_update );
	clipboard_semaphore = SDL_CreateSemaphore( 0 );

	thread = SDL_CreateThread( UpdateClipboardThread, "UpdateClipboardThread", NULL );

	updateClipboard();
	StartTrackingClipboardUpdates();
}

void destroyUpdateClipboard()
{
	connected = false;
	SDL_SemPost( clipboard_semaphore );
}

int UpdateClipboardThread( void* opaque )
{
	opaque;

	while( connected )
	{
		SDL_SemWait( clipboard_semaphore );

		if( !connected )
		{
			break;
		}

		//ClipboardData* clipboard = GetClipboard();

		if( updating_set_clipboard )
		{
			mprintf( "Trying to set clipboard!\n" );
			ClipboardData cb;
			cb.type = CLIPBOARD_TEXT;
			cb.size = 0;
			SetClipboard( &cb );
			if( clipboard->type == CLIPBOARD_FILES )
			{
				char cmd[1000] = "";
#ifndef _WIN32
				strcat( cmd, "UNISON=./.unison; " );
#endif

#ifdef _WIN32
				strcat( cmd, "unison " );
#else
				strcat( cmd, "./unison -follow \"Path *\" " );
#endif

				strcat( cmd, "-ui text -sshargs \"-l vm1 -i sshkey\" " );
				strcat( cmd, " \"ssh://" );
				strcat( cmd, (char*)server_ip );
				strcat( cmd, "/" );
				strcat( cmd, "C:\\ProgramData\\FractalCache\\get_clipboard/" );
				strcat( cmd, "\" " );
				strcat( cmd, SET_CLIPBOARD );
				strcat( cmd, " -force " );
				strcat( cmd, " \"ssh://" );
				strcat( cmd, (char*)server_ip );
				strcat( cmd, "/" );
				strcat( cmd, "C:\\ProgramData\\FractalCache\\get_clipboard/" );
				strcat( cmd, "\" " );
				strcat( cmd, " -ignorearchives -confirmbigdel=false -batch" );

				mprintf( "COMMAND: %s\n", cmd );
				runcmd( cmd );
			}
			SetClipboard( clipboard );
		} else
		{
			clock clipboard_time;
			StartTimer( &clipboard_time );

			if( clipboard->type == CLIPBOARD_FILES )
			{
				char cmd[1000] = "";
#ifndef _WIN32
				strcat( cmd, "UNISON=./.unison; " );
#endif

#ifdef _WIN32
				strcat( cmd, "unison " );
#else
				strcat( cmd, "./unison -follow \"Path *\" " );
#endif

				strcat( cmd, "-ui text -sshargs \"-l vm1 -i sshkey\" " );
				strcat( cmd, GET_CLIPBOARD );
				strcat( cmd, " \"ssh://" );
				strcat( cmd, (char*)server_ip );
				strcat( cmd, "/" );
				strcat( cmd, "C:\\ProgramData\\FractalCache\\set_clipboard" );
				strcat( cmd, "/\" -force " );
				strcat( cmd, GET_CLIPBOARD );
				strcat( cmd, " -ignorearchives -confirmbigdel=false -batch" );

				mprintf( "COMMAND: %s\n", cmd );
				runcmd( cmd );
			}

			FractalClientMessage* fmsg = malloc( sizeof( FractalClientMessage ) + sizeof( ClipboardData ) + clipboard->size );
			fmsg->type = CMESSAGE_CLIPBOARD;
			memcpy( &fmsg->clipboard, clipboard, sizeof( ClipboardData ) + clipboard->size );
			send_fmsg( fmsg );
			free( fmsg );

			// If it hasn't been 500ms yet, then wait 500ms to prevent too much spam
			const int spam_time_ms = 500;
			if( GetTimer( clipboard_time ) < spam_time_ms / 1000.0 )
			{
				SDL_Delay( max( (int)(spam_time_ms - 1000*GetTimer( clipboard_time )), 1 ) );
			}
		}

		mprintf( "Updated clipboard!\n" );
		updating_clipboard = false;
	}

	return 0;
}

void updateClipboard()
{
	if( updating_clipboard )
	{
		pending_update_clipboard = true;
	} else
	{
		mprintf( "Pushing update to clipboard\n" );
		pending_update_clipboard = false;
		updating_clipboard = true;
		updating_set_clipboard = false;
		clipboard = GetClipboard();
		SDL_SemPost( clipboard_semaphore );
	}
}
