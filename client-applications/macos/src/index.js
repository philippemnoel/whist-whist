const { app, BrowserWindow } = require('electron');
const path = require('path');
const { ipcMain } = require('electron')

// Handle creating/removing shortcuts on Windows when installing/uninstalling.
if (require('electron-squirrel-startup')) { // eslint-disable-line global-require
  app.quit();
}

// Keep a global reference of the window object, if you don't, the window will
// be closed automatically when the JavaScript object is garbage collected.
let mainWindow;
var splashScreen;

const createWindow = () => {
  // Create the browser window.
    mainWindow = new BrowserWindow({
        width: 1000,
        height: 600,
        resizable: false,
        maximizable: false,
        center: true,
        icon: process.cwd() + "\\resources\\app\\src\\assets\\logo.ico",
        webPreferences: {
            nodeIntegration: true
        },
        titleBarStyle: 'hidden'
    });

  // and load the index.html of the app.
  mainWindow.loadFile(path.join(__dirname, 'index.html'));
  // Open the DevTools.
  // mainWindow.webContents.openDevTools();

  // Emitted when the window is closed.
  mainWindow.on('closed', () => {
    // Dereference the window object, usually you would store windows
    // in an array if your app supports multi windows, this is the time
    // when you should delete the corresponding element.
    mainWindow = null;
  });
};


function sleep (time) {
  return new Promise((resolve) => setTimeout(resolve, time));
}

// This method will be called when Electron has finished
// initialization and is ready to create browser windows.
// Some APIs can only be used after this event occurs.
app.on('ready', () => {
  createWindow();
splashScreen = new BrowserWindow({
    width: 1000,
    height: 600,
    center:true,
    maximizable: false,
    backgroundColor: '#e0eff8',
    webPreferences: {
      nodeIntegration: true,
      webSecurity: false
                            },
    frame: false,
    resizable: false,
    alwaysOnTop: true,
});
  splashScreen.loadFile(path.join(__dirname, 'loading.html'));
});

// Quit when all windows are closed.
app.on('window-all-closed', () => {
  // On OS X it is common for applications and their menu bar
  // to stay active until the user quits explicitly with Cmd + Q
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('activate', () => {
  // On OS X it's common to re-create a window in the app when the
  // dock icon is clicked and there are no other windows open.
  if (mainWindow === null) {
    createWindow();
  }
});

ipcMain.on('showMainWindow',()=>{
    splashScreen.destroy();
    mainWindow.show();
});

// In this file you can include the rest of your app's specific main process
// code. You can also put them in separate files and import them here.
