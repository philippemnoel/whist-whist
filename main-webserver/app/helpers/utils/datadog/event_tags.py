"""Datadog takes in string tags that let us categorize different events into categories depending
on what they correspond to (which makes analysis way easier). Here are some basic tags that are commonly
used by our datadog loggers. Please use these constants to keep our code clean in a growing codebase.
"""

"""Event type tags."""
CONTAINER_CREATION = "container-creation"
CONTAINER_ASSIGNMENT = "container-assignment"
CONTAINER_DELETION = "container-deletion"

CLUSTER_CREATION = "cluster-creation"
CLUSTER_DELETION = "cluster-delection"

LOGON = "logon"
LOGOFF = "logoff"

CONTAINER_LIFECYCLE = "container-lifecycle"
CLUSTER_LIFECYCLE = "cluster-lifecycle"
USER_LIFECYCLE = "user-lifecycle"


_type_tags = {
    CONTAINER_CREATION,
    CONTAINER_DELETION,
    CLUSTER_CREATION,
    CLUSTER_DELETION,
    CONTAINER_LIFECYCLE,
    CLUSTER_LIFECYCLE,
    LOGON,
    LOGOFF,
}

"""Status tags"""
SUCCESS = "success"
FAILURE = "failure"

_status_tags = {SUCCESS, FAILURE}

# these need to be formatted
# do something like CONTAINER_NAME.format(container_name="my_container")
# to get "container-name:my_container"
CONTAINER_NAME = "container-name:{container_name}"
CLUSTER_NAME = "cluster-name:{cluster_name}"
CONTAINER_USER = "container-user:{container_user}"
USER_NAME = "user-name:{user_name}"


def validTags(tags):
    """Check whether the given tags are valid in that they contain the minimum requirements to be
    logged to datadog. There should not be more than one of these.

    Args:
        tags (List[str]): some tags that someone wants to put in some datadog event to be logged.

    Returns:
        boolean: [description]
    """
    has_status, has_type = 0, 0
    for tag in tags:
        if tag in _status_tags:
            has_status += 1
        elif tag in _type_tags:
            has_type += 1

    return has_status == 1 and has_type == 1
