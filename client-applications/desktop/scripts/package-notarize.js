// Package the app for testing, but also notarize it. Note that we require a
// version to test for notarization testing so that we can also test the
// version-handling logic.

const helpers = require("./build-package-helpers")
const yargs = require('yargs')

// We require the version argument for notarization-level testing so that at
// least some of our argument handling is covered by CI as well.
const argv = yargs(process.argv.slice(2))
  .version(false) // necessary to prevent mis-parsing of the `--version` arg we pass in
  .option('version', {
    description: "Set the version number of the client app for notarization testing.",
    type: "string", 
    requiresArg: true,
    demandOption: true,
  })
  .help()
  .argv


helpers.buildAndCopyProtocol()
helpers.configureCodeSigning(true)
helpers.reinitializeYarn()

// For testing, we just hardcode the environment to dev
helpers.setPackageEnv("dev")

helpers.buildTailwind()

helpers.snowpackBuild(argv.version)
helpers.electronBuild()
