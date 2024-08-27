resource "mongodbatlas_project" "dashboard" {
  name   = var.project_name
  org_id = var.org_id

  # TODO: not sure if these options are enabled by default or
  # are addons that incur in costs.
  is_collect_database_specifics_statistics_enabled = true
  is_data_explorer_enabled                         = true
  is_performance_advisor_enabled                   = true
  is_realtime_performance_panel_enabled            = true
  is_schema_advisor_enabled                        = true
}

output "project_id" {
  value = mongodbatlas_project.dashboard.id
}
