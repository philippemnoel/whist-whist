// Copyright (c) 2022 Whist Technologies, Inc.

package main

import (
	"context"
	"sync"
	"time"

	"github.com/whisthq/whist/backend/services/scaling-service/dbclient"
	"github.com/whisthq/whist/backend/services/scaling-service/hosts"
	"github.com/whisthq/whist/backend/services/subscriptions"
	logger "github.com/whisthq/whist/backend/services/whistlogger"
)

var db dbclient.DBClient

// CleanRegion starts the unresponsive instance cleanup thread for a particular
// region and returns a function that can be used to stop it. You should call
// CleanRegion once per region, but it doesn't really make sense to call it
// more than once per region.
//
// The stop function blocks until all in-progress cleaning operations have
// completed. Consider calling this method in its own goroutine like so:
//
//	var cleaner *Cleaner
//	var wg sync.WaitGroup
//
//	wg.Add(1)
//
//	go func() {
//	    defer wg.Done()
//	    cleaner.Stop()
//	}()
//
//	wg.Wait()
func CleanRegion(client subscriptions.WhistGraphQLClient, h hosts.HostHandler, d time.Duration) func() {
	stop := make(chan struct{})
	ticker := time.NewTicker(d)
	var wg sync.WaitGroup

	// Don't bother adding this goroutine to the cleaner's wait group. It will
	// finish as soon as the stop channel is closed.
	go func() {
		for {
			select {
			case <-ticker.C:
				wg.Add(1)

				go func() {
					defer wg.Done()

					// TODO: Make the deadline more configurable.
					deadline := time.Now().Add(5 * time.Minute)
					ctx, cancel := context.WithDeadline(context.Background(), deadline)
					defer cancel()

					do(ctx, client, h)
				}()
			case <-stop:
				return
			}
		}
	}()

	return func() {
		ticker.Stop()
		close(stop)
		wg.Wait()
	}
}

// do marks all unresponsive instances as TERMINATING in the database before
// subsequently terminating them and finally removing them from the database
// altogether.
func do(ctx context.Context, client subscriptions.WhistGraphQLClient, h hosts.HostHandler) {
	region := h.GetRegion()
	maxAge := time.Now().Add(-150 * time.Second)
	ids, err := db.LockBrokenInstances(ctx, client, region, maxAge)

	if err != nil {
		logger.Errorf("failed to mark unresponsive instances as TERMINATING in "+
			"region %s: %s", region, err)
		return
	} else if len(ids) < 1 {
		logger.Debugf("Didn't find any unresponsive instances in region %s",
			region)
		return
	}

	if err := h.SpinDownInstances(ctx, ids); err != nil {
		logger.Errorf("failed to terminate unresponsive instances in region %s:"+
			"%s", region, err)
		logger.Errorf("please verify that the instances in %s with the following "+
			"instance IDs have been terminated and then remove them from the "+
			"database: %s", region, ids)
		return
	}

	deleted, err := db.TerminateLockedInstances(ctx, client, region, ids)

	if err != nil {
		logger.Errorf("failed to remove unresponsive instances in %s from the "+
			"database: %s", region, err)
		return
	}

	if !equal(deleted, ids) {
		logger.Errorf("some %s instance rows were not deleted: requested "+
			"%v, got %v", region, ids, deleted)
	} else {
		logger.Info("Successfully removed the following unresponsive instance "+
			"rows from the database for %s:", region, deleted)
	}
}

func equal(u, v []string) bool {
	if len(u) != len(v) {
		return false
	}

	same := true
	set := make(map[string]struct{}, len(u))

	for _, s := range u {
		set[s] = struct{}{}
	}

	for _, s := range v {
		_, ok := set[s]
		same = same && ok
	}
}
