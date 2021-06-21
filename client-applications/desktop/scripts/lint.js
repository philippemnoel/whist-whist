/* scripts/lint.js
 *
 * This script triggers lint checking and/or fixing for all of
 * our source files, using ESLint.
 *
 * This should be invoked via:
 *   $ yarn lint:check
 * or:
 *   $ yarn lint:fix
 * from your command line.
 *
 */

const { execute } = require("../node_modules/eslint/lib/cli")

// The first two arguments here are simply placeholders, as ESLint CLI expects to be called in this way

export default function lint () {
  execute([
    "yarn",
    "eslint",
    ...process.argv.slice(2),
    "--max-warnings=0",
    "./src/**/*.{js,jsx,ts,tsx}",
    "./scripts/**/*.js",
    "./config/**/*.{js,ts}",
    "./*.js",
  ]).then((ret) => process.exit(ret))
}

if (require.main === module) {
  lint()
}
