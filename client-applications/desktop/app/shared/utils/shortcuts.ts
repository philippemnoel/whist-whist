import { OperatingSystem, FractalWindowsDirectory } from "shared/types/client"
import { FractalApp } from "shared/types/ui"
import { FractalNodeEnvironment } from "shared/types/config"
import { debugLog } from "shared/utils/logging"

// Import with require() packages that require Node 10 experimental
const fs = require("fs")
const os = require("os")
const toIco = require("to-ico")

// How big should the PNG's be
const PNG_WIDTH = 64
const PNG_HEIGHT = 64

export const createDirectorySync = (
    filePath: string,
    directoryName: string
): boolean => {
    /*
        Description:
            Creates a directory on the user's computer. 
        
        Usage:
            To create a directory called "Folder" in "C:\\User\\Fake_User", call createDirectorySync("C:\\User\\Fake_User", "Folder")

        Arguments:
            filePath (string): Folder that the new directory should be placed in
            directoryName (string): Name of new directory
        
        Returns:
            success: True/False if the directory was created successfully
    */

    // If the base file path does not exist, return false
    if (!fs.existsSync(filePath) && fs.lstatSync(filePath).isDirectory()) {
        return false
    }

    // If the directory already exists, return true
    if (
        fs.existsSync(`${filePath}${directoryName}`) &&
        fs.lstatSync(`${filePath}${directoryName}`).isDirectory()
    ) {
        return true
    }

    // If the directory doesn't exist yet, create it
    fs.mkdirSync(`${filePath}${directoryName}`)
    return true
}

/* eslint-disable @typescript-eslint/no-this-alias */
export class SVGConverter {
    /*
        Description:
            Converts an SVG to various image formats. Currently supports
            converting to Base64 PNG and .ico.

        Usage: 
            const svgInput = "https://my-svg-url.svg"
            new SVGConverter().convertToPngBase64(svgInput, (pngBase64) => {
                console.log(pngBase64)
            })

        Methods:
            base64PngToBuffer(input: string) :
                Converts a base64 encoded PNG to an ArrayBuffer and returns the ArrayBuffer
            convertToPngBase64(input: string, callback: (base64: string) => void) :
                Converts an svg (requires .svg) to base64 and pass the base64 string into callback
            convertToIco(input: string, callback: (buffer: ArrayBuffer) => void) : 
                Converts an svg (requires .svg) to .ico and returns the .ico in an ArrayBuffer
    */
    canvas: HTMLCanvasElement = document.createElement("canvas")

    imgPreview: HTMLImageElement = document.createElement("img")

    canvasCtx: CanvasRenderingContext2D | null = this.canvas.getContext("2d")

    base64Png: string | null = null

    bufferPng: ArrayBuffer | null = null

    constructor() {
        this.init = this.init.bind(this)
        this.cleanUp = this.cleanUp.bind(this)
        this.convertToPngBase64 = this.convertToPngBase64.bind(this)
        this.base64PngToBuffer = this.base64PngToBuffer.bind(this)
        this.convertToIco = this.convertToIco.bind(this)
    }

    init() {
        document.body.appendChild(this.imgPreview)
    }

    cleanUp() {
        document.body.removeChild(this.imgPreview)
    }

    base64PngToBuffer(base64: string) {
        base64 = base64.replace("data:image/png;base64,", "")
        const buffer = Buffer.from(base64, "base64")
        this.bufferPng = buffer
        return buffer
    }

    convertToPngBase64(input: string, callback: (base64: string) => void) {
        this.init()
        const thisRef = this
        this.imgPreview.onload = () => {
            const img = new Image()
            thisRef.canvas.width = PNG_WIDTH
            thisRef.canvas.height = PNG_HEIGHT
            img.crossOrigin = "anonymous"
            img.src = thisRef.imgPreview.src
            // Draw SVG onto canvas with resampling (to resize to 64 x 64)
            img.onload = () => {
                if (thisRef.canvasCtx) {
                    thisRef.canvasCtx.drawImage(
                        img,
                        0,
                        0,
                        img.width,
                        img.height,
                        0,
                        0,
                        thisRef.canvas.width,
                        thisRef.canvas.height
                    )
                    // Encode PNG as a base64 string
                    const base64 = thisRef.canvas.toDataURL("image/png")
                    this.base64Png = base64
                    // Fire callback on success with base64 string
                    callback(base64)
                } else {
                    callback("")
                }
                thisRef.cleanUp()
            }
        }
        // Load the SVG into HTML image element
        this.imgPreview.src = input
    }

    convertToIco(input: string, callback: (buffer: ArrayBuffer) => void) {
        this.convertToPngBase64(input, async (base64: string) => {
            if (base64) {
                const convertedBuffer = this.base64PngToBuffer(base64)

                const bufferRef = await toIco([convertedBuffer]).then(
                    (bufferRef: ArrayBuffer) => {
                        return bufferRef
                    }
                )

                callback(bufferRef)
            } else {
                callback(new ArrayBuffer(0))
            }
        })
    }
}

export const createShortcutName = (appName: string): string => {
    /*
        Description:
            Generates a shortcut name for consistency

        Arguments:
            appName (string): Name of original app, i.e. "Figma" or "Google Chrome"
        
        Returns:
            name (string): Name of shortcut
    */
    return `${appName} Fractalized`
}

export const createShortcut = (
    app: FractalApp,
    outputPath: string,
    callback: (shortcutCreated: boolean) => void
): void => {
    /*
        Description:
            Creates a shortcut to the Fractal streamed app and stores the shortcut in a specified directory

        Arguments:
            app (FractalApp): App to create shortcut to
            outputPath (string): Folder that the shortcut should be placed in. If not specified, defaults to Desktop.
        
        Returns:
            success (boolean): True/False if shortcut was created successfully
    */

    const createDesktopShortcut = require("create-desktop-shortcuts")

    const platform: OperatingSystem = os.platform()
    // appURL is the protocol that the shortcut should run the open Fractal
    const appURL = `fractal://${app.app_id.toLowerCase().replace(/\s+/g, "-")}`

    if (platform === OperatingSystem.MAC) {
        debugLog("Mac shortcuts not yet implemented")
        callback(false)
    } else if (platform === OperatingSystem.WINDOWS) {
        // Points to the folder where windows.vbs is located (shortcut creation code)
        const vbsPath = `${require("electron")
            .remote.app.getAppPath()
            .replace(
                "app.asar",
                "app.asar.unpacked"
            )}\\node_modules\\create-desktop-shortcuts\\src\\windows.vbs`

        // Convert SVG into a .ico ArrayBuffer
        new SVGConverter().convertToIco(app.logo_url, (buffer: ArrayBuffer) => {
            // Create directory called /icons to store the .ico if it doesn't already exist
            createDirectorySync(FractalWindowsDirectory.ROOT_DIRECTORY, "icons")
            const icoPath = `${FractalWindowsDirectory.ROOT_DIRECTORY}\\icons\\${app.app_id}.ico`
            // Write .ico into directory
            fs.writeFileSync(icoPath, buffer)
            // Save shortcut in outputPath
            const success = createDesktopShortcut({
                windows: {
                    outputPath: outputPath,
                    filePath: appURL,
                    name: createShortcutName(app.app_id),
                    vbsPath:
                        process.env.NODE_ENV ===
                        FractalNodeEnvironment.DEVELOPMENT
                            ? null
                            : vbsPath,
                    icon: icoPath,
                },
            })
            // Fire callback with shortcut creation success True/False
            callback(success)
        })
    } else {
        debugLog(`no suitable os found, instead got ${platform}`)
        callback(false)
    }
}

export const checkIfShortcutExists = (shortcut: string): boolean => {
    /*
        Description:
            Checks to see if the user has the shortcut installed in their Desktop or Programs/Application folder.

        Arguments:
            shortcut (string): Name of shortcut (generated by createShortcutName)
        
        Returns:
            exists (boolean): True/False if the shortcut exists
    */
    const platform = os.platform()
    try {
        if (platform === OperatingSystem.MAC) {
            debugLog("mac shortcuts not yet implemented")
            return false
        }
        if (platform === OperatingSystem.WINDOWS) {
            // Check the desktop folder and Start Menu Programs folder
            const windowsDesktopPath = `${FractalWindowsDirectory.DESKTOP}${shortcut}.lnk`
            const windowsStartMenuPath = `${FractalWindowsDirectory.START_MENU}Fractal\\${shortcut}.lnk`

            const exists =
                fs.existsSync(windowsDesktopPath) ||
                fs.existsSync(windowsStartMenuPath)
            return exists
        }
        debugLog(`no suitable os found, instead got ${platform}`)
        return false
    } catch (err) {
        debugLog(err)
        return false
    }
}
