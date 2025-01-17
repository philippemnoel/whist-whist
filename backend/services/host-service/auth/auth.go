/*
Package auth provides functions for validating JWTs sent by the user application (i.e. Whist browser).

Currently, it has been tested on JWTs generated with our Auth0 configuration. It should work with other
JWTs too, provided that they are signed with the RS256 algorithm.
*/
package auth // import "github.com/whisthq/whist/backend/services/host-service/auth"

import (
	"encoding/json"
	"strings"
	"time"

	"github.com/MicahParks/keyfunc"
	"github.com/golang-jwt/jwt/v4"

	"github.com/whisthq/whist/backend/services/metadata"
	"github.com/whisthq/whist/backend/services/utils"
	logger "github.com/whisthq/whist/backend/services/whistlogger"
)

// Audience is an alias for []string with some custom deserialization behavior.
// It is used to store the value of an access token's "aud" claim.
type Audience []string

// Scopes is an alias for []string with some custom deserialization behavior.
// It is used to store the value of an access token's "scope" claim.
type Scopes []string

// WhistClaims is a struct type that models the claims that must be present
// in an Auth0-issued Whist access token.
type WhistClaims struct {
	jwt.StandardClaims

	// Audience stores the value of the access token's "aud" claim. Inside the
	// token's payload, the value of the "aud" claim can be either a JSON
	// string or list of strings. We implement custom deserialization on the
	// Audience type to handle both of these cases.
	Audience Audience `json:"aud"`

	// Scopes stores the value of the access token's "scope" claim. The value
	// of the "scope" claim is a string of one or more space-separated words.
	// The *Scopes type implements the encoding.TextUnmarshaler interface such
	// that the string of space-separated words is deserialized into a string
	// slice.
	Scopes Scopes `json:"scope"`

	// Stripe customer data. We use these values to verify payment status,
	// they are added to the access token via Auth0 rules.
	CustomerID         string `json:"https://api.fractal.co/stripe_customer_id"`
	SubscriptionStatus string `json:"https://api.fractal.co/subscription_status"`
}

var config authConfig = getAuthConfig(metadata.GetAppEnvironment())
var jwks *keyfunc.JWKS

func init() {
	refreshInterval := time.Hour * 1
	refreshUnknown := true
	var err error // don't want to shadow jwks accidentally

	jwks, err = keyfunc.Get(config.getJwksURL(), keyfunc.Options{
		RefreshInterval: refreshInterval,
		RefreshErrorHandler: func(err error) {
			logger.Errorf("error refreshing JWKs: %s", err)
		},
		RefreshUnknownKID: refreshUnknown,
	})
	if err != nil {
		// Can do a "real" panic since we're in an init function
		logger.Panicf(nil, "Error getting JWKs on startup: %s", err)
	}
	logger.Infof("Successfully got JWKs from %s on startup.", config.getJwksURL())
}

// UnmarshalJSON unmarshals a JSON string or list of strings into an *Audience
// type. It overwrites the contents of *audience with the unmarshalled data.
func (audience *Audience) UnmarshalJSON(data []byte) error {
	var aud string

	// Try to unmarshal the input data into a string slice.
	err := json.Unmarshal(data, (*[]string)(audience))

	switch v := err.(type) {
	case *json.UnmarshalTypeError:
		// Ignore *json.UnmarshalTypeErrors, which are returned when the input
		// data cannot be unmarshalled into a string slice. Instead, we will
		// try to unmarshal the data into a string type below.
	default:
		// Return an error if err was non-nil or nil otherwise.
		return v
	}

	// Try to unmarshal the input data into a string.
	if err := json.Unmarshal(data, &aud); err != nil {
		return err
	}

	// Overwrite any pre-existing data in *audience. Avoid creating a new
	// allocation if possible by truncating and reusing the existing slice.
	*audience = append((*audience)[0:0], aud)

	return nil
}

// UnmarshalJSON unmarshals a space-separate string of words into a *Scopes
// type. It overwrites the contents of *scopes with the unmarshalled data.
func (scopes *Scopes) UnmarshalJSON(data []byte) error {
	var s string

	if err := json.Unmarshal(data, &s); err != nil {
		return err
	}

	// The following line is borrowed from
	// https://cs.opensource.google/go/go/+/refs/tags/go1.16.6:src/encoding/json/stream.go;l=271.
	// @djsavvy and @owenniles's best guess is that the (*scopes)[0:0] syntax
	// decreases the likelihood that new memory must be allocated for the
	// data that are written to the slice.
	*scopes = append((*scopes)[0:0], strings.Fields(s)...)

	return nil
}

// ParseToken will parses a raw access token string verifies the token's signature, and
// ensures that it is valid at the current moment in time.
// It returns a pointer to a WhistClaims type with the values it claims if all checks are successful.
func ParseToken(tokenString string) (*WhistClaims, error) {
	claims := new(WhistClaims)
	_, err := jwt.ParseWithClaims(tokenString, claims, jwks.Keyfunc)

	if err != nil {
		return nil, err
	}

	return claims, nil
}

// Verify checks that the claim was issued by the proper issuer for the proper audience.
// It returns an error if any of the checks are fail.
func Verify(claims *WhistClaims) error {
	if claims == nil {
		return utils.MakeError("expected claims but received nil")
	}

	if !claims.VerifyAudience(config.Aud, true) {
		return jwt.NewValidationError(
			utils.Sprintf("bad audience %s", claims.Audience),
			jwt.ValidationErrorAudience,
		)
	}

	if !claims.VerifyIssuer(config.Iss, true) {
		return jwt.NewValidationError(
			utils.Sprintf("bad issuer %s", claims.Issuer),
			jwt.ValidationErrorIssuer,
		)
	}

	return nil
}

// VerifyAudience compares the "aud" claim against cmp. If req is false, this
// method will return true if the value of the "aud" claim matches cmp or is
// unset.
func (claims *WhistClaims) VerifyAudience(cmp string, req bool) bool {
	c := &jwt.MapClaims{"aud": []string(claims.Audience)}
	return c.VerifyAudience(cmp, req)
}

// VerifyScope returns true if claims.Scopes contains the requested scope and
// false otherwise. This function's name and type signature is inspired by
// those of the Verify* methods defined on jwt.StandardClaims.
func (claims *WhistClaims) VerifyScope(scope string) bool {
	for _, s := range claims.Scopes {
		if s == scope {
			return true
		}
	}

	return false
}
