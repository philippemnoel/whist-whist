// Package the app for local testing using snowpack and electron-builder

const yargs = require("yargs")
const helpers = require("./build-package-helpers")

const packageLocal = (env) => {
  helpers.buildAndCopyProtocol()
  helpers.buildTailwind()
  helpers.configureCodeSigning(false)

  // For testing, we just hardcode the environment to dev
  helpers.setPackagedEnv("local")

  // For package-local, we don't want to increment the version so we use existing version
  helpers.snowpackBuild({
    ...env,
    VERSION: helpers.getCurrentClientAppVersion(),
  })
  helpers.electronBuild()

  helpers.removeEnvOverridesFile()
}

module.exports = packageLocal

if (require.main === module) {
  // We require the version argument for notarization-level testing so that at
  // least some of our argument handling is covered by CI as well.
  yargs(process.argv.slice(2))
    .version(false) // necessary to prevent mis-parsing of the `--version` arg we pass in
    .help().argv
  packageLocal({})
}
