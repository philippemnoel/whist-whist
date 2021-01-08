import fs from 'fs'
import chalk from 'chalk'
import { execSync } from 'child_process'
import { dependencies } from '../../package.json'

if (dependencies) {
    const dependenciesKeys = Object.keys(dependencies)
    const nativeDeps = fs
        .readdirSync('node_modules')
        .filter((folder) => fs.existsSync(`node_modules/${folder}/binding.gyp`))
    try {
        // Find the reason for why the dependency is installed. If it is installed
        // because of a devDependency then that is okay. Warn when it is installed
        // because of a dependency
        const { dependencies: dependenciesObject } = JSON.parse(
            execSync(`npm ls ${nativeDeps.join(' ')} --json`).toString()
        )
        const rootDependencies = Object.keys(dependenciesObject)
        const filteredRootDependencies = rootDependencies.filter(
            (rootDependency) => dependenciesKeys.includes(rootDependency)
        )
        if (filteredRootDependencies.length > 0) {
            const plural = filteredRootDependencies.length > 1
            console.log(`
 ${chalk.whiteBright.bgYellow.bold(
     'Webpack does not work with native dependencies.'
 )}
${chalk.bold(filteredRootDependencies.join(', '))} ${
                plural ? 'are native dependencies' : 'is a native dependency'
            } and should be installed inside of the "./app" folder.
 First uninstall the packages from "./package.json":
${chalk.whiteBright.bgGreen.bold('yarn remove your-package')}
 ${chalk.bold(
     'Then, instead of installing the package to the root "./package.json":'
 )}
${chalk.whiteBright.bgRed.bold('yarn add your-package')}
 ${chalk.bold('Install the package to "./app/package.json"')}
${chalk.whiteBright.bgGreen.bold('cd ./app && yarn add your-package')}
 Read more about native dependencies at:
${chalk.bold(
    'https://github.com/electron-react-boilerplate/electron-react-boilerplate/wiki/Module-Structure----Two-package.json-Structure'
)}
 `)
        }
    } catch (e) {
        console.log('Native dependencies could not be checked')
    }
}
