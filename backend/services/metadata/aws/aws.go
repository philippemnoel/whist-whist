/*
Package aws abstracts away the collection and caching of important runtime
metadata about the AWS instance the host service is running on.
*/
package aws

import (
	"context"
	"io"
	"net"
	"net/http"
	"time"

	"github.com/aws/aws-sdk-go-v2/config"
	"github.com/aws/aws-sdk-go-v2/service/ec2"
	ec2Types "github.com/aws/aws-sdk-go-v2/service/ec2/types"
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
	imageID, err := metadataRetriever("ami-id")
	if err != nil {
		return nil, utils.MakeError("failed to get aws metadata field ami-id: %s", err)
	}

	instanceID, err := metadataRetriever("instance-id")
	if err != nil {
		return nil, utils.MakeError("failed to get aws metadata field instance-id: %s", err)
	}

	instanceType, err := metadataRetriever("instance-type")
	if err != nil {
		return nil, utils.MakeError("failed to get aws metadata field instance-type: %s", err)
	}

	region, err := metadataRetriever("placement/region")
	if err != nil {
		return nil, utils.MakeError("failed to get aws metadata field placement-region: %s", err)
	}

	ip, err := metadataRetriever("public-ipv4")
	if err != nil {
		return nil, utils.MakeError("failed to get aws metadata field public-ipv4: %s", err)
	}

	instanceName, err := getInstanceName(instanceID, region)
	if err != nil {
		return nil, utils.MakeError("failed to get aws instance name: %s", err)
	}

	metadata := make(map[string]string)
	metadata["aws.image_id"] = imageID
	metadata["aws.instance_id"] = instanceID
	metadata["aws.instance_name"] = instanceName
	metadata["aws.instance_type"] = instanceType
	metadata["aws.region"] = region
	metadata["aws.ip"] = ip

	am.imageID = types.ImageID(imageID)
	am.instanceID = types.InstanceID(instanceID)
	am.instanceName = types.InstanceName(instanceName)
	am.instanceType = types.InstanceType(instanceType)
	am.region = types.PlacementRegion(region)
	am.ip = net.ParseIP(ip).To4()

	return metadata, nil
}

func getInstanceName(instanceID string, region string) (string, error) {
	// Initialize general AWS config and EC2 client
	cfg, err := config.LoadDefaultConfig(context.Background(), config.WithRegion(string(region)))
	if err != nil {
		return "", utils.MakeError("unable to load AWS SDK config: %s", err)
	}
	ec2Client := ec2.NewFromConfig(cfg)

	var (
		resourceTypeStr string = "resource-type"
		resourceIDStr   string = "resource-id"
	)

	out, err := ec2Client.DescribeTags(context.TODO(), &ec2.DescribeTagsInput{
		Filters: []ec2Types.Filter{
			{
				Name:   &resourceTypeStr,
				Values: []string{"instance"},
			},
			{
				Name:   &resourceIDStr,
				Values: []string{string(instanceID)},
			},
		},
	})

	if err != nil {
		return "", utils.MakeError("error describing tags: %s", err)
	}

	for _, t := range out.Tags {
		if *(t.Key) == "Name" {
			return *(t.Value), nil
		}
	}

	// "Name" tag wasn't found, so we print out all the ones we did find.
	printableTags := make(map[string]string)
	for _, t := range out.Tags {
		printableTags[*(t.Key)] = *(t.Value)
	}

	return "", utils.MakeError(`did not find a "Name" tag for this instance! Found these tags instead: %v`, printableTags)
}

func metadataRetriever(path string) (string, error) {
	httpClient := http.Client{
		Timeout: 1 * time.Second,
	}

	url := EndpointBase + path
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
