/*
Package aws abstracts away the collection and caching of important runtime
metadata about the AWS instance the host service is running on.
*/
package aws

import (
	"io"
	"net"
	"net/http"
	"time"

	"github.com/whisthq/whist/backend/services/types"
	"github.com/whisthq/whist/backend/services/utils"
)

const EndpointBase = "http://169.254.169.254/latest/meta-data/"

type Metadata struct {
	instanceID   types.InstanceID
	instanceName types.InstanceName
	imageID      types.ImageID
	instanceType types.InstanceType
	region       types.PlacementRegion
	ip           net.IP
}

func (am *Metadata) GetImageID() types.ImageID {
	return am.imageID
}

func (am *Metadata) GetInstanceID() types.InstanceID {
	return am.instanceID
}

func (am *Metadata) GetInstanceType() types.InstanceType {
	return am.instanceType
}

func (am *Metadata) GetInstanceName() types.InstanceName {
	return am.instanceName
}

func (am *Metadata) GetPlacementRegion() types.PlacementRegion {
	return am.region
}

func (am *Metadata) GetPublicIpv4() net.IP {
	return am.ip
}

func (gc *Metadata) GetUserID() types.UserID {
	return types.UserID("")
}

func (am *Metadata) GetMetadata() (map[string]string, error) {
	return map[string]string{}, nil
}

func generateGCPMetadataRetriever(path string) func() (string, error) {
	httpClient := http.Client{
		Timeout: 1 * time.Second,
	}

	url := EndpointBase + path
	return func() (string, error) {
		resp, err := httpClient.Get(url)
		if err != nil {
			return "", utils.MakeError("error retrieving data from URL %s: %v. Is the service running on a GCP VM instance?", url, err)
		}
		defer resp.Body.Close()

		body, err := io.ReadAll(resp.Body)
		if err != nil {
			return string(body), utils.MakeError("error reading response body from URL %s: %v", url, err)
		}
		if resp.StatusCode != 200 {
			return string(body), utils.MakeError("got non-200 response from URL %s: %s", url, resp.Status)
		}
		return string(body), nil
	}
}

// /*
// Package aws abstracts away the collection and caching of important runtime
// metadata about the AWS instance the host service is running on.
// */
// package aws // import "github.com/whisthq/whist/backend/services/metadata/aws"

// import (
// 	"context"
// 	"io"
// 	"net"
// 	"net/http"
// 	"time"

// 	"github.com/aws/aws-sdk-go-v2/config"
// 	"github.com/aws/aws-sdk-go-v2/service/ec2"
// 	"github.com/aws/aws-sdk-go-v2/service/ec2/types"

// 	"github.com/whisthq/whist/backend/services/utils"
// )

// // This file is laid out as a series of types corresponding to various
// // AWS-specific metadata. Each type is accompanied by the functions needed to
// // provide a "Get" method for the type. The bottom of the file contains any
// // necessary helper functions.

// // As soon as Go has generics this file will look very different. ;)

// //===============

// // An AmiID represents the AWS AMI ID for this host.
// type AmiID string

// var getAmiIDStr = utils.MemoizeStringWithError(generateAWSMetadataRetriever("ami-id"))

// // GetAmiID returns the AMI ID of the current EC2 instance.
// func GetAmiID() (AmiID, error) {
// 	str, err := getAmiIDStr()
// 	return AmiID(str), err
// }

// //===============

// // An InstanceID represents an AWS Instance ID.
// type InstanceID string

// var getInstanceIDStr = utils.MemoizeStringWithError(generateAWSMetadataRetriever("instance-id"))

// // GetInstanceID returns the Instance ID of the. current EC2 instance
// func GetInstanceID() (InstanceID, error) {
// 	str, err := getInstanceIDStr()
// 	return InstanceID(str), err
// }

// //===============

// // An InstanceType represents an EC2 instance type (e.g. `g4dn.xlarge`).
// type InstanceType string

// var getInstanceTypeStr = utils.MemoizeStringWithError(generateAWSMetadataRetriever("instance-type"))

// // GetInstanceType returns the type of the current EC2 instance (e.g. g4dn.xlarge).
// func GetInstanceType() (InstanceType, error) {
// 	str, err := getInstanceTypeStr()
// 	return InstanceType(str), err
// }

// //===============

// // An PlacementRegion represents the placement region of an EC2 instance (e.g. `us-east-1`).
// type PlacementRegion string

// var getPlacementRegionStr = utils.MemoizeStringWithError(generateAWSMetadataRetriever("placement/region"))

// // GetPlacementRegion returns the placement region of the current EC2 instance (e.g. `us-east-1`).
// func GetPlacementRegion() (PlacementRegion, error) {
// 	str, err := getPlacementRegionStr()
// 	return PlacementRegion(str), err
// }

// //===============

// var getPublicIpv4 = utils.MemoizeStringWithError(generateAWSMetadataRetriever("public-ipv4"))

// // GetPublicIpv4 returns the public IPv4 address of the current EC2 instance.
// func GetPublicIpv4() (net.IP, error) {
// 	str, err := getPublicIpv4()
// 	if err != nil {
// 		return net.IP{}, err
// 	}
// 	ip := net.ParseIP(str).To4()
// 	if ip == nil {
// 		return ip, utils.MakeError("unable to parse response from IMDS endpoint as an IPv4 address: %v", str)
// 	}
// 	return ip, nil
// }

// //===============

// // An InstanceName represents the "Name" tag assigned to an instance on
// // creation (i.e. what shows up in the EC2 console as the name of the
// // instance).
// type InstanceName string

// var getInstanceName = utils.MemoizeStringWithError(func() (string, error) {
// 	region, err := GetPlacementRegion()
// 	if err != nil {
// 		return "", utils.MakeError("unable to find region to create AWS SDK config!")
// 	}

// 	// Initialize general AWS config and EC2 client
// 	cfg, err := config.LoadDefaultConfig(context.Background(), config.WithRegion(string(region)))
// 	if err != nil {
// 		return "", utils.MakeError("unable to load AWS SDK config: %s", err)
// 	}
// 	ec2Client := ec2.NewFromConfig(cfg)

// 	instanceID, err := GetInstanceID()
// 	if err != nil {
// 		return "", utils.MakeError("unable to compute instance name because unable to get AWS instance ID: %s", err)
// 	}

// 	var resourceTypeStr string = "resource-type"
// 	var resourceIDStr string = "resource-id"

// 	out, err := ec2Client.DescribeTags(context.TODO(), &ec2.DescribeTagsInput{
// 		Filters: []types.Filter{
// 			{
// 				Name:   &resourceTypeStr,
// 				Values: []string{"instance"},
// 			},
// 			{
// 				Name:   &resourceIDStr,
// 				Values: []string{string(instanceID)},
// 			},
// 		},
// 	})

// 	if err != nil {
// 		return "", utils.MakeError("error describing tags: %s", err)
// 	}

// 	for _, t := range out.Tags {
// 		if *(t.Key) == "Name" {
// 			return *(t.Value), nil
// 		}
// 	}

// 	// "Name" tag wasn't found, so we print out all the ones we did find.
// 	printableTags := make(map[string]string)
// 	for _, t := range out.Tags {
// 		printableTags[*(t.Key)] = *(t.Value)
// 	}

// 	return "", utils.MakeError(`did not find a "Name" tag for this instance! Found these tags instead: %v`, printableTags)
// })

// // GetInstanceName returns the "Name" tag of the current EC2 instance.
// func GetInstanceName() (InstanceName, error) {
// 	str, err := getInstanceName()
// 	return InstanceName(str), err
// }

// // GetAWSMetadata will get a map of all the AWS metadata
// // we can collect or infer from the instance.
// func GetAWSMetadata() (map[string]string, error) {
// 	metadata := make(map[string]string)

// 	ami, err := GetAmiID()
// 	if err != nil {
// 		return nil, utils.MakeError("failed to get ami id: %s", err)
// 	}
// 	metadata["aws.ami-id"] = string(ami)

// 	id, err := GetInstanceID()
// 	if err != nil {
// 		return nil, utils.MakeError("failed to get instance id: %s", err)
// 	}
// 	metadata["aws.instance-id"] = string(id)

// 	instanceType, err := GetInstanceType()
// 	if err != nil {
// 		return nil, utils.MakeError("failed to get instance type: %s", err)
// 	}
// 	metadata["aws.instance-type"] = string(instanceType)

// 	region, err := GetPlacementRegion()
// 	if err != nil {
// 		return nil, utils.MakeError("failed to get instance region: %s", err)
// 	}
// 	metadata["aws.placement-region"] = string(region)

// 	ip, err := GetPublicIpv4()
// 	if err != nil {
// 		return nil, utils.MakeError("failed to get ip address: %s", err)
// 	}
// 	metadata["aws.public-ipv4"] = ip.String()

// 	name, err := GetInstanceName()
// 	if err != nil {
// 		return nil, utils.MakeError("failed to get instance name: %s", err)
// 	}
// 	metadata["aws.instance-name"] = string(name)

// 	return metadata, nil
// }

// //===============

// // This helper function generates functions that retrieve metadata from the
// // internal AWS endpoint. We use this together with
// // utils.MemoizeStringWithError() to expose memoized functions that query
// // information about the host from AWS.  We use this setup so that we only have
// // to query the AWS endpoint successfully once to get information for our
// // startup, shutdown, and heartbeat messages. This architecture is compelling
// // because of reports online that the AWS endpoint to query this information
// // has transient failures. We want to minimize the impact of these transient
// // failures on our application --- with this approach, host startup will be
// // affected by these failures, but transient failures should not affect already
// // running hosts.
// func generateAWSMetadataRetriever(path string) func() (string, error) {
// 	// Note that we could use the ec2imds package of the AWS SDK, but that's
// 	// proven unnecessary over months of usage of the (simple) below code in
// 	// production so far. In the future, we could make the migration at the price
// 	// of a small increase in code complexity.

// 	const AWSendpointBase = "http://169.254.169.254/latest/meta-data/"
// 	httpClient := http.Client{
// 		Timeout: 1 * time.Second,
// 	}

// 	url := AWSendpointBase + path
// 	return func() (string, error) {
// 		resp, err := httpClient.Get(url)
// 		if err != nil {
// 			return "", utils.MakeError("error retrieving data from URL %s: %v. Is the service running on an AWS EC2 instance?", url, err)
// 		}
// 		defer resp.Body.Close()

// 		body, err := io.ReadAll(resp.Body)
// 		if err != nil {
// 			return string(body), utils.MakeError("error reading response body from URL %s: %v", url, err)
// 		}
// 		if resp.StatusCode != 200 {
// 			return string(body), utils.MakeError("got non-200 response from URL %s: %s", url, resp.Status)
// 		}
// 		return string(body), nil
// 	}
// }
