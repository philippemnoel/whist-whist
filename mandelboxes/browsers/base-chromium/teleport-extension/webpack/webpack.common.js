const path = require("path")
const CopyPlugin = require("copy-webpack-plugin")
const HtmlWebpackPlugin = require("html-webpack-plugin")
const { ProvidePlugin } = require("webpack")

const srcDir = path.join(__dirname, "..", "src")
const outDir = path.join(__dirname, "..", "build", "js")

module.exports = {
  entry: {
    worker: path.join(srcDir, "worker", "index.ts"),
    content: path.join(srcDir, "content", "index.ts"),
    userAgent: path.join(srcDir, "resources", "userAgent.ts"),
    geolocation: path.join(srcDir, "resources", "geolocation.ts"),
    language: path.join(srcDir, "resources", "language.ts"),
  },
  output: {
    path: outDir,
    filename: "[name].js",
  },
  module: {
    rules: [
      {
        test: /\.tsx?$/,
        use: "ts-loader",
        exclude: /node_modules/,
      },
      {
        test: /\.(jpe?g|png|gif|svg)$/i,
        loader: "file-loader",
      },
    ],
  },
  resolve: {
    extensions: [".ts", ".tsx", ".js"],
    fallback: {
      path: require.resolve("path-browserify"),
    },
    alias: {
      "@app": srcDir,
    },
  },
  performance: {
    maxAssetSize: 512000,
  },
  plugins: [
    new CopyPlugin({
      patterns: [{ from: ".", to: "../", context: "public" }],
    }),
    new ProvidePlugin({
      process: "process/browser",
    }),
    new HtmlWebpackPlugin({
      template: path.join(srcDir, "..", "public", "templates", "page.html"),
      filename: "page.html",
      chunks: ["page"],
      cache: false,
    }),
  ],
  externals: {
    modules: [path.join(srcDir, "..", "node_modules")],
  },
}
