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

execute([...process.argv.slice(2), "--max-warnings 0", "./src/**/*.{js,jsx,ts,tsx}"])
