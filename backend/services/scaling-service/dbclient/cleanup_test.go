// Copyright (c) 2022 Whist Technologies, Inc.

package dbclient

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"reflect"
	"testing"
	"time"

	"github.com/hasura/go-graphql-client"
	"github.com/whisthq/whist/backend/services/subscriptions"
)

var pkg DBClient

// mockResponse generates a mock GraphQL mutation response.
func mockResponse(field string, ids []string) ([]byte, error) {
	// The response contains a list of instance IDs.
	type host struct {
		ID string `json:"id"`
	}

	count := len(ids)
	hosts := make([]host, 0, count)

	for _, id := range ids {
		hosts = append(hosts, host{id})
	}

	data := struct {
		Count int    `json:"affected_rows"`
		Hosts []host `json:"returning"`
	}{count, hosts}

	// Dynamically construct an auxiliary type to serialize the mock response
	// data.
	t := reflect.StructOf([]reflect.StructField{
		{
			Name: "Response",
			Type: reflect.TypeOf(data),
			Tag:  reflect.StructTag(fmt.Sprintf(`json:"%s"`, field)),
		},
	})
	v := reflect.New(t)

	v.Elem().FieldByName("Response").Set(reflect.ValueOf(data))

	return json.Marshal(v.Interface())
}

// testClient provides mock responses to GraphQL mutations.
type testClient struct {
	// ids contains mock instance IDs of unresponsive instances.
	ids []string
}

// Initialize is part of the subscriptions.WhistGraphQLClient interface.
func (*testClient) Initialize(bool) error {
	return nil
}

// Query is part of the subscriptions.WhistGraphQLClient interface.
func (*testClient) Query(context.Context, subscriptions.GraphQLQuery, map[string]interface{}) error {
	return nil
}

// Mutate is part of the subscriptions.WhistGraphQLClient interface. This
// implementation populates the mutation struct with mock host data.
func (c *testClient) Mutate(_ context.Context, q subscriptions.GraphQLQuery, v map[string]interface{}) error {
	// Depending what kind of mock response we are providing, we select a list of
	// instance IDs to return.
	var ids []string
	var field string

	switch q.(type) {
	case *subscriptions.LockBrokenInstances:
		field = "update_whist_instances"

		// We are providing a mock response to markForTermination, so we use the
		// list of instance IDs stored in the "database" (i.e. struct)
		ids = c.ids
	case *subscriptions.TerminateLockedInstances:
		field = "delete_whist_instances"

		// We are providing a mock response to finalizeTermination, so we use the
		// value of the instance IDs input variable.
		tmp1, ok := v["ids"]

		if !ok {
			return errors.New("missing instance IDs variable")
		}

		tmp2, ok := tmp1.([]graphql.String)

		if !ok {
			return errors.New("instance IDs input variable should be a slice of " +
				"graphql String types.")
		}

		ids = make([]string, 0, len(tmp2))

		for _, id := range tmp2 {
			ids = append(ids, string(id))
		}
	default:
		t := reflect.TypeOf(q)
		return fmt.Errorf("unrecognized mutation '%s'", t)
	}

	data, err := mockResponse(field, ids)

	if err != nil {
		return err
	}

	// Deserialize the mock data into the response struct.
	if err := graphql.UnmarshalGraphQL(data, q); err != nil {
		return err
	}

	return nil
}

// TestLockBrokenInstances tests that LockBrokenInstances converts the GraphQL
// mutation response from Hasura to a slice of instance IDs.
func TestLockBrokenInstances(t *testing.T) {
	var tt time.Time
	ids := []string{"instance-0", "instance-1", "instance-2"}
	client := &testClient{ids: ids}
	res, err := pkg.LockBrokenInstances(context.TODO(), client, "us-east-1", tt)

	if err != nil {
		t.Error("markForTermination:", err)
	} else if !reflect.DeepEqual(res, ids) {
		t.Error(fmt.Printf("Expected %v, got %v", ids, res))
	}
}

// TestTerminateLockedInstances tests that TerminateLockedInstances converts
// the GraphQL mutation response from Hasura to a slice of instance IDs.
func TestTerminateLockedInstances(t *testing.T) {
	client := &testClient{}
	ids := []string{"instance-0", "instance-1", "instance-2"}
	res, err := pkg.TerminateLockedInstances(context.TODO(), client, "us-east-1", ids)

	if err != nil {
		t.Error("finalizeTermination:", err)
	} else if !reflect.DeepEqual(res, ids) {
		t.Error(fmt.Printf("Expected %v, got %v", ids, res))
	}
}
