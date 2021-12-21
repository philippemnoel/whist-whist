package auth

import (
	"github.com/whisthq/whist/backend/core-go/metadata"
)

type authConfig struct {
	// JWT audience. Identifies the serivce that accepts the token.
	Aud string
	// JWT issuer. The issuing server.
	Iss string
}

func (a authConfig) getJwksURL() string {
	return a.Iss + ".well-known/jwks.json"
}

var authConfigDev = authConfig{
	Aud: "https://api.fractal.co",
	Iss: "https://fractal-dev.us.auth0.com/",
}

var authConfigStaging = authConfig{
	Aud: "https://api.fractal.co",
	Iss: "https://fractal-staging.us.auth0.com/",
}

var authConfigProd = authConfig{
	Aud: "https://api.fractal.co",
	Iss: "https://auth.whist.com/",
}

func getAuthConfig(appEnv metadata.AppEnvironment) authConfig {
	switch appEnv {
	case metadata.EnvDev:
		return authConfigDev
	case metadata.EnvStaging:
		return authConfigStaging
	case metadata.EnvProd:
		return authConfigProd
	default:
		return authConfigDev
	}
}
