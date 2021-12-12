package subscriptions // import "github.com/fractal/whist/host-service/subscriptions"

import (
	"github.com/fractal/whist/core-go/metadata"
	"github.com/fractal/whist/core-go/metadata/heroku"
	"github.com/fractal/whist/core-go/utils"
)

// Whist database connection strings

const localHasuraURL = "http://localhost:8080/v1/graphql"

// getWhistHasuraParams obtains and returns the heroku parameters
// from the metadata package that are necessary to initialize the client.
func getWhistHasuraParams() (HasuraParams, error) {
	if metadata.IsLocalEnv() {
		return HasuraParams{
			URL:       localHasuraURL,
			AccessKey: "hasura",
		}, nil
	}

	url, err := heroku.GetHasuraURL()
	if err != nil {
		return HasuraParams{}, utils.MakeError("Couldn't get Hasura connection URL: %s", err)
	}
	config, err := heroku.GetHasuraConfig()
	if err != nil {
		return HasuraParams{}, utils.MakeError("Couldn't get Hasura config: %s", err)
	}
	result, ok := config["HASURA_GRAPHQL_ACCESS_KEY"]
	if !ok {
		return HasuraParams{}, utils.MakeError("Couldn't get Hasura connection URL: couldn't find HASURA_GRAPHQL_ACCESS_KEY in Heroku environment.")
	}

	params := HasuraParams{
		URL:       url,
		AccessKey: result,
	}

	return params, nil
}
