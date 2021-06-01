import pytest
from app.helpers.blueprint_helpers.aws.aws_instance_post import find_instance, bundled_region


def test_empty_instances():
    """
    Confirms that we don't find any instances on a fresh db
    """
    assert find_instance("us-east-1") is None


def test_find_initial_instance(bulk_instance):
    """
    Confirms that we find an empty instance
    """
    instance = bulk_instance(location="us-east-1")
    assert find_instance("us-east-1") == instance.instance_id


def test_find_part_full_instance(bulk_instance):
    """
    Confirms that we find an in-use instance
    """
    instance = bulk_instance(location="us-east-1", associated_containers=3)
    assert find_instance("us-east-1") == instance.instance_id


def test_find_part_full_instance_order(bulk_instance):
    """
    Confirms that we find an in-use instance
    """
    instance = bulk_instance(location="us-east-1", associated_containers=3)
    _ = bulk_instance(location="us-east-1", associated_containers=2)
    assert find_instance("us-east-1") == instance.instance_id


def test_no_find_full_instance(bulk_instance):
    """
    Confirms that we don't find a full instance
    """
    _ = bulk_instance(location="us-east-1", associated_containers=10)
    assert find_instance("us-east-1") is None


def test_no_find_full_small_instance(bulk_instance):
    """
    Confirms that we don't find a full instance with <10 max
    """
    _ = bulk_instance(location="us-east-1", max_containers=5, associated_containers=5)
    assert find_instance("us-east-1") is None


@pytest.fixture
def test_assignment_replacement_code(bulk_instance):
    """
    generates a function which, given a region, returns a multistage test
    of the instance finding code
    """

    def _test_find_unassigned(location):
        """
        tests 4 properties of our find unassigned algorithm:
        1) we don't return an overfull instance in the requested region
        2) we don't return an overfull instance in a replacement region
        3) we return an available instance in a replacement region
        4) if available, we use the requested region rather than a replacement one
        these properties are tested in order
        """
        replacement_region = bundled_region[location][0]
        bulk_instance(associated_containers=10, location=location)
        assert (
            find_instance(location) is None
        ), f"we assigned an already full instance in the main region, {location}"
        bulk_instance(associated_containers=10, location=replacement_region)
        assert (
            find_instance(location) is None
        ), f"we assigned an already full instance in the secondary region, {replacement_region}"
        bulk_instance(location=replacement_region, instance_name="replacement-container")
        assert find_instance(location) is not None, (
            f"we failed to find the available instance "
            f"in the replacement region {replacement_region}"
        )
        bulk_instance(location=location, instance_name="main-container")
        instance = find_instance(location)
        assert (
            instance is not None and instance == "main-container"
        ), f"we failed to find the available instance in the main region {location}"

    return _test_find_unassigned


@pytest.mark.parametrize(
    "location",
    bundled_region.keys(),
)
def test_assignment_logic(test_assignment_replacement_code, location):
    # ensures that replacement will work for all regions
    test_assignment_replacement_code(location)
