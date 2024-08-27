variable "org_id" {
  type        = string
  description = "MongoDB Organization ID"
}
variable "project_id" {
  type        = string
  description = "The MongoDB Atlas Project ID"
}
variable "cluster_name" {
  type        = string
  description = "The MongoDB Atlas Cluster Name"
}
variable "cloud_provider" {
  type        = string
  description = "The cloud provider to use, must be AWS, GCP or AZURE"
}
variable "region" {
  type        = string
  description = "MongoDB Atlas Cluster Region, must be a region for the provider given"
}
variable "mongodbversion" {
  type        = string
  description = "The Major MongoDB Version"
}
