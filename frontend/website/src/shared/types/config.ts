export interface WhistConfig {
  sentry_env: string
  client_download_urls: {
    macOS_x64: string
    macOS_arm64: string
    Windows: string
  }
}

export interface WhistEnvironment {
  dev: WhistConfig
  staging: WhistConfig
  prod: WhistConfig
}
