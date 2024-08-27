resource "mongodbatlas_advanced_cluster" "dashboard_cluster" {
  project_id            = var.project_id
  name                  = var.cluster_name
  cluster_type          = "REPLICASET"
  mongodb_major_version = var.mongodbversion
  replication_specs {
    electable_specs {
      instance_size = "M0"
      node_count    = 3
    }
    analytics_specs {
      instance_size = "M0"
      node_count    = 1
    }
    provider_name = var.cloud_provider
    priority      = 1
    region_name   = var.region
  }
}

resource "mongodbatlas_project_ip_access_list" "ip" {
  project_id = var.project.id

  # Note: Since the Netlify site changes ip address constantly,
  # we allow access for all ip addresses. Other methods should
  # be used to authorize access to the cluster.
  ip_address = "0.0.0.0/0"
  comment    = "Allow access to all ip addresses."
}

resource "mongodbatlas_database_user" "dashboard_user" {
  # TODO: Credentials should be filled in by CI.
  username           = ""
  password           = ""
  project_id         = var.project_id
  auth_database_name = "admin"

  roles {
    role_name     = "readWrite"
    database_name = var.database_name # The database name and collection name need not exist in the cluster before creating the user.
  }

  scopes {
    name = var.cluster_name
    type = "CLUSTER"
  }

  labels {
    key   = "Name"
    value = "Dashboard User"
  }
}
