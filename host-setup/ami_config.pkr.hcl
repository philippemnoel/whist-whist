/*
 * This Packer HCL configuration file creates an AWS EBS-backed AMI. For full documentation,
 * pleaser refer to this link: https://www.packer.io/docs/builders/amazon/ebs
**/

/* 
 * Variables passed to Packer via the -var-file flag, as a JSON file.
**/

/* AWS variables */

variable "access_key" {
  type      = string
  default   = ""
  sensitive = true
}

variable "secret_key" {
  type      = string
  default   = ""
  sensitive = true
}

variable "instance_types" {
  type    = list(string)
  default = []
}

variable "ami_name" {
  type    = string
  default = ""
}

variable "source_ami" {
  type    = string
  default = ""
}

variable "source_region" {
  type    = string
  default = ""
}

variable "destination_regions" {
  type    = list(string)
  default = []
}

variable "vpc_id" {
  type    = string
  default = ""
}

/* GitHub variables */

variable "git_branch" {
  type    = string
  default = ""
}

variable "git_hash" {
  type    = string
  default = ""
}

variable "pr_number" {
  type    = string
  default = ""
}

variable "github_pat" {
  type      = string
  default   = ""
  sensitive = true
}

variable "github_username" {
  type      = string
  default   = ""
  sensitive = true
}

/* Miscellaneous variables */

variable "mandelbox_logz_shipping_token" {
  type    = string
  default = ""
}

/* 
 * Packer Builder configuration, using the variables from the `variable` configurations defined above.
 * Note that we don't specify availability_zone so that Packer tries all availabilities zones it is configured
 * for (i.e. zones with a subnet with tag `Purpose: packer`) in the `region`. 
**/

source "amazon-ebs" "Whist_AWS_AMI_Builder" {

  /* AMI configuration */

  ami_description = "Whist-optimized Ubuntu 20.04 AWS Machine Image"
  ami_name        = "${var.ami_name}"
  ami_regions     = "${var.destination_regions}" # All AWS regions the AMI will get copied to
  
  # Options are paravirtual (default) or hvm. AWS recommends usings HVM,
  # as per: https://cloudacademy.com/blog/aws-ami-hvm-vs-pv-paravirtual-amazon/
  ami_virtualization_type = "hvm" 
  
  # TODO: add enhanced networking support to our instances in another PR, and uncomment this!
  # Enable enhanced networking for the AMI. These two flags enabled Elastic Network Adapter support and SriovNetsupport
  # on HVM-compatible AMIs. If set, you need ec2:ModifyInstanceAttribute on your AWS IAM policy and must make sure that
  # enhanced networking is enabled on the AWS EC2 instances.
  #ena_support   = true 
  #sriov_support = true 

  force_deregister      = true # Force Packer to first deregister an existing AMI if one with the same name already exists
  force_delete_snapshot = true # Force Packer to delete snapshots associated with AMIs, which have been deregistered by force_deregister

  /* Access configuration */

  access_key  = "${var.access_key}"
  secret_key  = "${var.secret_key}"
  region      = "${var.source_region}" # The source AWS region where the Packer Builder will run
  max_retries = 5 # Retry up to 5 times using exponential backoff if Packer fails to connect to AWS, in case of throttling/transient failures

  /* Run configuration */

  source_ami                  = "${var.source_ami}" # The AWS AMI to use as a base for the new AMI
  associate_public_ip_address = true # Make new instances with this AMI get assigned a public IP address
  ebs_optimized               = true # Optimize for EBS volumes

  # We do not specifiy the availability_zone parameter, since leaving it empty allows Amazon to auto-assign. 
  
  # spot_instance_types is a list of acceptable instance types to run your build on. We will request a spot
  # instance using the max price of spot_price and the allocation strategy of "lowest price". Your instance
  # will be launched on an instance type of the lowest available price that you have in your list. This is 
  # used in place of instance_type. You may only set either spot_instance_types or instance_type, not both. 
  # This feature exists to help prevent situations where a Packer build fails because a particular availability
  # zone does not have capacity for the specific instance_type requested in instance_type.
  spot_instance_types = "${var.instance_types}"

  # We do not set spot_price (string), so that it defaults to a maximum price equal to the on demand price 
  # of the instance. In the situation where the current Amazon-set spot price exceeds the value set in this
  # field, Packer will not launch an instance and the build will error. In the situation where the Amazon-set
  # spot price is less than the value set in this field, Packer will launch and you will pay the Amazon-set 
  # spot price, not this maximum value. For more information, see the Amazon docs on spot pricing.

  iam_instance_profile = "PackerAMIBuilder" # This is the IAM role we configured for Packer in AWS
  shutdown_behavior    = "terminate" # Automatically terminate instances on shutdown in case Packer exits ungracefully. Possible values are stop and terminate. Defaults to stop.

  vpc_id = "${var.vpc_id}" # The VPC where the Packer Builer will run. This VPC needs to have subnet(s) configured as per the `subnet_filter` below

  # This filter ensures Packer will pick a subnet which was configured for Packer by looking for the tag
  # Purpose: packer. If no subnet with this tag is found in `region`, Packer will fail.
  subnet_filter {
    filters = {
      "tag:Purpose": "packer"
    }
    most_free = true # The Subnet with the most free IPv4 addresses will be used if multiple Subnets matches the filter.
    random    = true # A random Subnet will be used if multiple Subnets matches the filter. most_free have precendence over this.
  }

  /* Metadata configuration */

  http_endpoint = "enabled"  # A string to enable or disble the IMDS endpoint for an instance. Defaults to enabled. Accepts either "enabled" or "disabled"
  http_tokens   = "required" # A string to either set the use of IMDSv2 for the instance to optional or required. Defaults to "optional". Accepts either "optional" or "required"

  /* Block Device configuration */

  launch_block_device_mappings {
    device_name           = "/dev/sda1"
    volume_size           = 64
    volume_type           = "gp3" # Options are gp2 and gp3. gp3 is the newer, more performant and cheaper AWS block volume
    delete_on_termination = true  # This ensures that the EBS volume of the EC2 instance(s) using the AMI Packer creates get deleted when the instance gets deleted
  }

  /* Comunicator configuration */

  communicator = "ssh"
  ssh_username = "ubuntu"

  /* Tags configuration */

  run_tag {
    key = "Instance Region"
    value = "${var.source_region}"
  }

  run_tag {
    key = "Git Commit SHA"
    value = "${var.git_hash}"
  }

  run_tag {
    key = "PR Number"
    value = "${var.pr_number}"
  }
}


/* 
 * Packer build commands, run on the Packer Builder created via the `source` configuration defined above. 
 */

build {
  sources = ["source.amazon-ebs.Whist_AWS_AMI_Builder"]

  provisioner "shell" {
    inline = ["while [ ! -f /var/lib/cloud/instance/boot-finished ]; do echo 'Waiting for cloud-init...'; sleep 1; done"]
  }

  provisioner "shell" {
    inline = ["sudo systemctl disable --now unattended-upgrades.service"]
  }

  provisioner "file" {
    destination  = "/home/ubuntu"
    pause_before = "10s"
    source       = "../host-setup"
  }

  provisioner "shell" {
    inline = ["sudo mkdir -p /usr/share/whist", "sudo mv /home/ubuntu/host-setup/app_env.env /usr/share/whist/app_env.env"]
  }

  provisioner "file" {
    destination = "/home/ubuntu/host-service"
    source      = "../host-service/build/host-service"
  }

  provisioner "shell" {
    inline       = ["cd /home/ubuntu/host-setup", "./setup_host.sh --deployment ${var.github_username} ${var.github_pat} ${var.git_branch} ${var.git_hash} ${var.mandelbox_logz_shipping_token}", "cd ..", "rm -rf host-setup"]
    pause_before = "10s"
  }

  # This file is used to verify that Packer succeeded and pass Packer data to the next deploy stages
  post-processor "manifest" {
    output     = "manifest.json"
    strip_path = true
  }
}
