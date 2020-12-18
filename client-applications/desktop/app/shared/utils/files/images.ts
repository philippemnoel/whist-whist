// Import with require() packages that require Node 10 experimental
const png2icons = require("png2icons")
const temp = require("temp")
const fs = require("fs")
const path = require("path")

export class SVGConverter {
    /*
        Description:
            Converts an SVG to various image formats. Currently supports
            converting to Base64 PNG, .ico, and .icns.

        Usage: 
            const svgInput = "https://fractal-supported-app-images.s3.amazonaws.com/chrome-256.svg"
            const base64 = await SVGConverter.convertToPngBase64(svgInput)

        Methods:
            base64PngToBuffer(input: string) :
                Converts a base64 encoded PNG to a Buffer and returns the Buffer
            convertToPngBase64(input: string) :
                Converts an svg (requires .svg) to base64 and returns a Promise with the base64 string
            convertToIco(input: string) : 
                Converts an svg (requires .svg) to .ico and returns a Promise with the Buffer
            convertToIcns(input: string) : 
                Converts an svg (requires .svg) to .ico and returns a Promise with the Buffer
            convertToPngTemp(input: string, pngName: string):
                Converts an svg (requires .svg) to a PNG and saves it as ${pngName}.png in a temp
                folder on your local computer that auto-deletes when Electron closes (cross-platform). 
                Returns PNG file path.
    */
    private static canvas: HTMLCanvasElement = document.createElement("canvas")

    private static imgPreview: HTMLImageElement = document.createElement("img")

    private static canvasCtx: CanvasRenderingContext2D | null = SVGConverter.canvas.getContext(
        "2d"
    )

    private static init() {
        document.body.appendChild(this.imgPreview)
    }

    private static cleanUp() {
        document.body.removeChild(this.imgPreview)
    }

    private static waitForImageToLoad(img: HTMLImageElement) {
        return new Promise((resolve, reject) => {
            img.onload = () => resolve(img)
            img.onerror = reject
        })
    }

    static base64PngToBuffer(base64: string) {
        base64 = base64.replace("data:image/png;base64,", "")
        const buffer = Buffer.from(base64, "base64")
        return buffer
    }

    static async convertToPngBase64(
        input: string,
        width = 64,
        height = 64
    ): Promise<string> {
        // String where base64 output will be written to
        let base64 = ""

        // Load the SVG into HTML image element
        this.init()

        this.imgPreview.style.position = "absolute"
        this.imgPreview.style.top = "-9999px"
        this.imgPreview.src = input

        // Wait for SVG to load into HTML image preview
        await this.waitForImageToLoad(this.imgPreview)

        // Create final image
        const img = new Image()
        this.canvas.width = width
        this.canvas.height = height
        img.crossOrigin = "anonymous"
        img.src = this.imgPreview.src

        // Draw SVG onto canvas with resampling (to resize to 64 x 64)
        await this.waitForImageToLoad(img)

        if (this.canvasCtx) {
            this.canvasCtx.drawImage(
                img,
                0,
                0,
                img.width,
                img.height,
                0,
                0,
                this.canvas.width,
                this.canvas.height
            )
            // Encode PNG as a base64 string
            base64 = this.canvas.toDataURL("image/png")
        }
        this.cleanUp()
        return base64
    }

    static async convertToIco(
        input: string,
        width = 64,
        height = 64
    ): Promise<Buffer | null> {
        // SVG to base64 PNG
        const base64 = await this.convertToPngBase64(input, width, height)
        // base64 PNG to Buffer
        const convertedBuffer = this.base64PngToBuffer(base64)
        // Buffer to ICO
        const buffer = png2icons.createICO(
            convertedBuffer,
            png2icons.BICUBIC,
            0,
            true
        )

        return buffer
    }

    static async convertToIcns(
        input: string,
        width = 256,
        height = 256
    ): Promise<Buffer | null> {
        // SVG to base64 PNG
        const base64 = await this.convertToPngBase64(input, width, height)
        // base64 PNG to Buffer
        const convertedBuffer = this.base64PngToBuffer(base64)
        // Buffer to ICNS
        const buffer = png2icons.createICNS(
            convertedBuffer,
            png2icons.BICUBIC2,
            0
        )

        return buffer
    }

    static async saveAsPngTemp(
        input: string,
        pngName: string
    ): Promise<string> {
        // SVG to base64 PNG
        const base64 = await this.convertToPngBase64(input, 64, 64)
        // base64 PNG to Buffer
        const convertedBuffer = this.base64PngToBuffer(base64)
        // Create temp folder
        const dirPath = temp.mkdirSync("fractal")
        // Save PNG in temp folder
        const pngPath = path.join(dirPath, `${pngName}.png`)
        fs.writeFileSync(pngPath, convertedBuffer)
        // Return PNG path
        return pngPath
    }
}

export default SVGConverter
