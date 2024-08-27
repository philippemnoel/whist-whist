terraform {
  # Use an AWS S3 bucket to store state
  # AWS credentials will be filled in by CI.
  backend "s3" {
    bucket = "whist-terraform-state"
    key    = "dev/terraform.tfstate"
    region = "us-east-1"
  }

  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 4.30.0"
    }
  }

  required_version = ">= 0.14.9"
}

# Global modules, use default provider

module "iam" {
  source = "../modules/aws_iam"
  env    = var.env
}

module "s3" {
  source = "../modules/aws_s3"
  env    = var.env
}

# Dashboard database provider. Credentials are filled in by CI and
# passed as environment variables to terraform.
provider "mongodbatlas" {}


# Region-specific modules, these are enabled only on certain regions

# Enable all AWS regions on Terraform. Doing this will create
# all multi-region resources on each region declared below. See 
# `providers.tf` for the provider abbreviations used below.

# ------------------------------ North America modules ------------------------------ #

module "us-east-1" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
}

module "us-east-2" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.use2
  }
}

module "us-west-1" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.usw1
  }
}

module "us-west-2" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.usw2
  }
}

module "ca-central-1" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.cac1
  }
}

# ------------------------------ South America modules ------------------------------ #

module "sa-east-1" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.sae1
  }
}

# ------------------------------ modules ------------------------------ #

module "eu-central-1" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.euc1
  }
}

module "eu-west-1" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.euw1
  }
}

module "eu-west-2" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.euw2
  }
}

module "eu-west-3" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.euw3
  }
}

module "eu-south-1" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.eus1
  }
}

module "eu-north-1" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.eun1
  }
}

# ------------------------------ Africa modules ------------------------------ #

module "af-south-1" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.afs1
  }
}

# ------------------------------ Middle East modules ------------------------------ #

module "me-south-1" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.mes1
  }
}

# ------------------------------ Asia modules ------------------------------ #

module "ap-east-1" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.ape1
  }
}

module "ap-south-1" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.aps1
  }
}

module "ap-southeast-1" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.apse1
  }
}

module "ap-southeast-2" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.apse2
  }
}

module "ap-southeast-3" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.apse3
  }
}

module "ap-northeast-1" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.apne1
  }
}

module "ap-northeast-2" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.apne2
  }
}

module "ap-northeast-3" {
  source     = "../modules/aws_multi_region"
  env        = var.env
  cidr_block = var.cidr_block
  providers = {
    aws = aws.apne3
  }
}
