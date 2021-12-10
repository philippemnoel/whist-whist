/*
Package queries is almost entirely autogenerated code to interact with the
database. The code is derived from `schema.sql`.
*/
package queries // import "github.com/whisthq/whist/host-service/dbdriver/queries"

import (
	// These blank imports are necessary so `go mod tidy` doesn't remove these
	// dependencies if all the autogenerated `.sql.go` files are removed before
	// running `make build`.
	_ "github.com/jackc/pgconn"
	_ "github.com/jackc/pgtype"
	_ "github.com/jackc/pgx/v4"
)

// This is a placeholder file to ensure that the host service Makefile still
// works without any autogenerated code in this directory. Besides this file
// (queries.go), all other go files in this directory are autogenerated, so
// don't edit them! Note that they _are_ in fact supposed to be
// version-controlled as well according to the Go docs.
