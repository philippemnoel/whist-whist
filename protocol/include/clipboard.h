#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#if defined(_WIN32)
#pragma warning(disable: 4200)
#endif

#include <stdbool.h>

typedef enum ClipboardType
{
	CLIPBOARD_NONE,
	CLIPBOARD_TEXT,
	CLIPBOARD_IMAGE,
	CLIPBOARD_FILES
} ClipboardType;

typedef struct ClipboardData
{
	int size;
	ClipboardType type;
	char data[];
} ClipboardData;

typedef struct ClipboardFiles
{
	int size;
	char* files[];
} ClipboardFiles;

ClipboardData* GetClipboard();

void SetClipboard( ClipboardData* cb );

void StartTrackingClipboardUpdates();

bool hasClipboardUpdated();

#if defined(_WIN32)
#pragma warning(default: 4200)
#endif

#endif // CLIPBOARD_H