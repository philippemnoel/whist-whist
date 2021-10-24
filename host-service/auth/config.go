package auth

import (
	"github.com/fractal/fractal/host-service/metadata"
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
	Aud: "https://api.whist.com",
	Iss: "https://fractal-dev.us.auth0.com/",
}

var authConfigStaging = authConfig{
	Aud: "https://api.whist.com",
	Iss: "https://fractal-staging.us.auth0.com/",
}

var authConfigProd = authConfig{
	Aud: "https://api.whist.com",
	Iss: "https://auth.whist.com/",
}

func getAuthConfig() authConfig {
	switch metadata.GetAppEnvironment() {
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
