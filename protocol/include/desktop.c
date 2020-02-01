/*
 * This file contains the implementations of simple functions to set the
 * Windows desktop of a specific thread or process using WinAPI.
 * 
 * Fractal Protocol version: 1.0
 * 
 * Last modified: 01/26/2020
 * 
 * By: Philippe Noël, Ming Ying
 *
 * Copyright Fractal Computers, Inc. 2019
**/

#include "desktop.h"

void logToFile(char *msg, char* filename) {
  FILE *fp;
  fp = fopen(filename, "a+");
  fprintf(fp, msg);
  printf(msg);
  fclose(fp); 
}

// @brief Attaches the current thread to the current input desktop.
// @details Uses OpenInputDesktop and SetThreadDesktop from WinAPI.
int setCurrentInputDesktop(HDESK currentInputDesktop) {
    // Set current thread to the current user input desktop
    if (!SetThreadDesktop(currentInputDesktop)) {
        printf("SetThreadDesktop failed w/ error code: %d.\n", GetLastError());
        return -2;
    }
    return 0;
}

DesktopContext OpenNewDesktop(char* desktop_name, bool get_name) {
  DesktopContext context = {0};
  HDESK new_desktop;

  if(desktop_name == NULL) {
    new_desktop = OpenInputDesktop(0, FALSE, GENERIC_ALL);
  } else {
    new_desktop = OpenDesktop("default", 0, FALSE, GENERIC_ALL);
  }
  
  setCurrentInputDesktop(new_desktop);

  if(get_name) {
    TCHAR szName[1000];
    DWORD dwLen;
    GetUserObjectInformation(new_desktop, UOI_TYPE, szName, sizeof(szName), &dwLen);
    memcpy(context.desktop_name, szName, strlen(szName));
  }

  context.desktop_handle = new_desktop;
  CloseDesktop(new_desktop);
  return context;
}

void OpenWindow() {
  HWINSTA hwinsta = OpenWindowStation("winsta0", FALSE, GENERIC_ALL);
  SetProcessWindowStation(hwinsta);
}

void InitDesktop() {
  DesktopContext lock_screen;
  FILE *fp;

  OpenWindow();
  lock_screen = OpenNewDesktop(NULL, true);

  logToFile("Found initial desktop\n", "/log1.txt");
  printf("Desktop name is %s\n", lock_screen.desktop_name);

  if(strcmp("Winlogon", lock_screen.desktop_name) == 0 || 
     strcmp("Desktop" , lock_screen.desktop_name) == 0) 
  {
    logToFile("Found winlogon screen\n", "/log1.txt");
    enum FractalKeycode keycodes[100] = {
      KEY_SPACE, KEY_BACKSPACE, KEY_BACKSPACE
    };

    EnterWinString(keycodes, 3);

    Sleep(500);

    enum FractalKeycode keycodes2[100] = {
      KEY_P, KEY_A, KEY_S, KEY_S, KEY_W, KEY_O, KEY_R, KEY_D, KEY_1, 
      KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_PERIOD, KEY_ENTER, 
      KEY_ENTER
    };

    EnterWinString(keycodes2, 18);
  }
}
