var navigator = window.navigator
// This element is created in the content script and should
//     exist by the time this resource script is run.
const metaLanguage = document.documentElement.querySelector(
    `meta[name="browser-language"]`
) as HTMLMetaElement

// Override navigator.language
Object.defineProperty(navigator, "language", {
    get: () => {
        if (metaLanguage) {
            const languageInfo = JSON.parse(metaLanguage.content)
            return languageInfo.language
        }

        // Default to "en-US" if could not get actual language
        return "en-US"
    }
})

// Override navigator.languages
Object.defineProperty(navigator, "languages", {
    get: () => {
        if (metaLanguage) {
            const languageInfo = JSON.parse(metaLanguage.content)
            return languageInfo.languages
        }

        // Default to ["en-US"] if could not get actual language
        return ["en-US"]
    }
})

// Override window.languagechange event
const languageChangeEvent = new Event('languagechange')
const metaLanguageObserver = new MutationObserver((mutationList, observer) => {
    window.dispatchEvent(languageChangeEvent)
})
metaLanguageObserver.observe(metaLanguage, {
    attributes: true,
    attributeFilter: [ "content" ]
})

