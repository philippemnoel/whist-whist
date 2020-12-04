const homeDir = require("os").homedir()

export enum OperatingSystem {
    WINDOWS = "win32",
    MAC = "darwin",
    LINUX = "linux",
}

export enum FractalAppName {
    CHROME = "Google Chrome",
}

export class FractalWindowsDirectory {
    static START_MENU = `${homeDir}\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\`

    static DESKTOP = `${homeDir}\\Desktop\\`

    static VBS_PATH = `${require("electron")
        .remote.app.getAppPath()
        .replace(
            "app.asar",
            "app.asar.unpacked"
        )}\\node_modules\\create-desktop-shortcuts\\src\\`
}
