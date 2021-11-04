#!/usr/bin/env python3

import sys
import os
import psycopg2


# add the current directory to the path no matter where this is called from
sys.path.append(os.path.join(os.getcwd(), os.path.dirname(__file__), "."))


def execute_db_query(database_url, path, query):
    """
    Executes a given query based on the database url and path

    Args:
        database_url (str): current database url
        path (str): the search path (schema)
        query (str): query to be executed

    Returns:
        arr: result of executing query
    """
    conn = psycopg2.connect(database_url, sslmode="require")

    cur = conn.cursor()

    cur.execute("SET search_path TO %s;" % path)
    cur.execute("%s;" % query)

    return cur.fetchall()


def get_instance_ids(database_url, region):
    """
    Gets all aws instance ids using the database url and region

    Args:
        database_url (str): current database url
        region (str): current region

    Returns:
        arr: array of instance ids
    """
    query = "SELECT cloud_provider_id FROM instance_info WHERE location='%s';" % region
    ids = execute_db_query(database_url, "cloud", query)

    return [id[0] for id in ids]


def get_host_service_unresponsive_instances(database_url, region):
    """
    Gets all aws instance ids and names using the database url and region which have the status HOST_SERVICE_UNRESPONSIVE

    Args:
        database_url (str): current database url
        region (str): current region

    Returns:
        arr: array of tuples (name, id)
    """

    query = (
        "SELECT instance_name, cloud_provider_id FROM instance_info WHERE status='HOST_SERVICE_UNRESPONSIVE' AND location = '%s';"
        % region
    )
    instances = execute_db_query(database_url, "cloud", query)

    return instances


def get_lingering_instances(database_url, region):
    """
    Gets all lingering instances name and id using the database url and region

    Args:
        database_url (str): current database url
        region (str): current region

    Returns:
        arr: array of tuples (name, id)
    """

    query = (
        "SELECT instance_name, cloud_provider_id FROM lingering_instances WHERE instance_name IN (SELECT instance_name FROM instance_info WHERE location='%s');"
        % region
    )
    instances = execute_db_query(database_url, "cloud", query)

    return instances

def get_instance_id(database_url, region, omit_instance_names):
    """
    Gets all instance id that do not have a matching instance name from omit_instance_names the using the database url and region

    Args:
        database_url (str): current database url
        region (str): current region
        omit_instance_names (arr): list of instances name to omit

    Returns:
        arr: array of instance cloud provider id
    """

    query = (
        "SELECT cloud_provider_id FROM instance_info WHERE instance_name NOT IN %s AND location = '%s';"
        % (omit_instance_names, region)
    )
    instances = execute_db_query(database_url, "cloud", query)

    return instances

def delete_instances_from_instance_info(database_url, region, instance_ids):
    """
    Delete all instances with a matching instance id to instance_ids 

    Args:
        database_url (str): current database url
        region (str): current region
        instance_ids (arr): list of instances ids to delete
    """

    query = (
        "DELETE FROM instance_info WHERE instance_id IN %s AND location = '%s';"
        % (instance_ids, region)
    )

    instances = execute_db_query(database_url, "cloud", query)

    return
