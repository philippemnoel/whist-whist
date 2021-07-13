from random import randint
from sys import maxsize

import requests

from flask import current_app
from app.models import db, RegionToAmi, InstanceInfo
import app.helpers.blueprint_helpers.aws.aws_instance_post as aws_funcs

from app.constants.instance_state_values import InstanceState

from tests.helpers.utils import get_random_regions


def test_scale_up_single(app, hijack_ec2_calls, mock_get_num_new_instances, hijack_db, region_name):
    """
    Tests that we successfully scale up a single instance when required.
    Mocks every side-effecting function.
    """
    call_list = hijack_ec2_calls
    mock_get_num_new_instances(1)
    random_region_image_obj = RegionToAmi.query.filter_by(
        region_name=region_name, ami_active=True
    ).one_or_none()
    aws_funcs.do_scale_up_if_necessary(region_name, random_region_image_obj.ami_id, flask_app=app)
    assert len(call_list) == 1
    assert call_list[0]["kwargs"]["image_id"] == random_region_image_obj.ami_id


def test_scale_up_multiple(
    app, hijack_ec2_calls, mock_get_num_new_instances, hijack_db, region_name
):
    """
    Tests that we successfully scale up multiple instances when required.
    Mocks every side-effecting function.
    """
    desired_num = randint(1, 10)
    call_list = hijack_ec2_calls
    mock_get_num_new_instances(desired_num)
    us_east_1_image_obj = RegionToAmi.query.filter_by(
        region_name=region_name, ami_active=True
    ).one_or_none()
    aws_funcs.do_scale_up_if_necessary(region_name, us_east_1_image_obj.ami_id, flask_app=app)
    assert len(call_list) == desired_num
    assert all(elem["kwargs"]["image_id"] == us_east_1_image_obj.ami_id for elem in call_list)


def test_scale_down_single_available(
    monkeypatch, hijack_ec2_calls, mock_get_num_new_instances, bulk_instance, region_name
):
    """
    Tests that we scale down an instance when desired
    tests the correct requests, db, and ec2 calls are made.
    """
    post_list = []

    def _helper(*args, **kwargs):
        nonlocal post_list
        post_list.append({"args": args, "kwargs": kwargs})
        raise requests.exceptions.RequestException()

    monkeypatch.setattr(requests, "post", _helper)
    instance = bulk_instance(
        instance_name="test_instance", aws_ami_id="test-AMI", location=region_name
    )
    assert instance.status != InstanceState.DRAINING.value
    mock_get_num_new_instances(-1)
    aws_funcs.try_scale_down_if_necessary(region_name, "test-AMI")
    assert post_list[0]["args"][0] == f"https://{current_app.config['AUTH0_DOMAIN']}/oauth/token"
    db.session.refresh(instance)
    assert instance.status == InstanceState.HOST_SERVICE_UNRESPONSIVE.value


def test_scale_down_single_unavailable(
    hijack_ec2_calls, mock_get_num_new_instances, bulk_instance, region_name
):
    """
    Tests that we don't scale down an instance with running mandelboxes
    """
    instance = bulk_instance(
        instance_name="test_instance",
        associated_mandelboxes=1,
        aws_ami_id="test-AMI",
        location=region_name,
    )
    mock_get_num_new_instances(-1)
    aws_funcs.try_scale_down_if_necessary(region_name, "test-AMI")
    db.session.refresh(instance)
    assert instance.status == "ACTIVE"


def test_scale_down_single_wrong_region(
    hijack_ec2_calls, mock_get_num_new_instances, bulk_instance, region_names
):
    """
    Tests that we don't scale down an instance in a different region
    """
    region_name_1, region_name_2 = region_names
    instance = bulk_instance(
        instance_name="test_instance",
        associated_mandelboxes=1,
        aws_ami_id="test-AMI",
        location=region_name_1,
    )
    mock_get_num_new_instances(-1)
    aws_funcs.try_scale_down_if_necessary(region_name_2, "test-AMI")
    db.session.refresh(instance)
    assert instance.status == "ACTIVE"


def test_scale_down_single_wrong_ami(
    hijack_ec2_calls, mock_get_num_new_instances, bulk_instance, region_name
):
    """
    Tests that we don't scale down an instance with a different AMI
    """
    instance = bulk_instance(
        instance_name="test_instance",
        associated_mandelboxes=1,
        aws_ami_id="test-AMI",
        location=region_name,
    )
    mock_get_num_new_instances(-1)
    aws_funcs.try_scale_down_if_necessary(region_name, "wrong-AMI")
    db.session.refresh(instance)
    assert instance.status == "ACTIVE"


def test_scale_down_multiple_available(
    hijack_ec2_calls, mock_get_num_new_instances, bulk_instance, region_name
):
    """
    Tests that we scale down multiple instances when desired
    """
    desired_num = randint(1, 10)
    instance_list = []
    for instance in range(desired_num):
        bulk_instance(
            instance_name=f"test_instance_{instance}", aws_ami_id="test-AMI", location=region_name
        )
        instance_list.append(f"test_instance_{instance}")
    mock_get_num_new_instances(-desired_num)
    aws_funcs.try_scale_down_if_necessary(region_name, "test-AMI")
    for instance in instance_list:
        instance_info = InstanceInfo.query.get(instance)
        assert instance_info.status == InstanceState.HOST_SERVICE_UNRESPONSIVE.value


def test_scale_down_multiple_partial_available(
    hijack_ec2_calls, mock_get_num_new_instances, bulk_instance, region_name
):
    """
    Tests that we only scale down inactive instances
    """
    desired_num = randint(2, 10)
    num_inactive = randint(1, desired_num - 1)
    num_active = desired_num - num_inactive
    instance_list = []
    active_list = []
    for instance in range(num_inactive):
        bulk_instance(
            instance_name=f"test_instance_{instance}", aws_ami_id="test-AMI", location=region_name
        )
        instance_list.append(f"test_instance_{instance}")
    for instance in range(num_active):
        bulk_instance(
            instance_name=f"test_active_instance_{instance}",
            aws_ami_id="test-AMI",
            associated_mandelboxes=1,
            location=region_name,
        )
        active_list.append(f"test_active_instance_{instance}")
    mock_get_num_new_instances(-desired_num)
    aws_funcs.try_scale_down_if_necessary(region_name, "test-AMI")
    for instance in instance_list:
        instance_info = InstanceInfo.query.get(instance)
        assert instance_info.status == InstanceState.HOST_SERVICE_UNRESPONSIVE.value
    for instance in active_list:
        instance_info = InstanceInfo.query.get(instance)
        assert instance_info.status == InstanceState.ACTIVE.value


def test_buffer_wrong_region():
    """
    checks that we return -sys.maxsize when we ask about a nonexistent region
    """
    assert aws_funcs._get_num_new_instances("fake_region", "fake-AMI") == -maxsize


def test_buffer_wrong_ami():
    """
    checks that we return -sys.maxsize when we ask about a nonexistent ami
    """
    assert aws_funcs._get_num_new_instances("us-east-1", "fake-AMI") == -maxsize


def test_buffer_empty(region_ami_pair):
    """
    Tests that we ask for a new instance when the buffer is empty
    """
    region_name, ami_id = region_ami_pair
    assert aws_funcs._get_num_new_instances(region_name, ami_id) == 1


def test_buffer_part_full(bulk_instance, region_ami_pair):
    """
    Tests that we ask for a new instance when there's only a full instance running
    """
    region_name, ami_id = region_ami_pair
    bulk_instance(
        aws_ami_id=ami_id, associated_mandelboxes=10, mandelbox_capacity=10, location=region_name
    )
    assert aws_funcs._get_num_new_instances(region_name, ami_id) == 1


def test_buffer_good(bulk_instance, region_ami_pair):
    """
    Tests that we don't ask for a new instance when there's an empty instance running
    """
    region_name, ami_id = region_ami_pair
    bulk_instance(aws_ami_id=ami_id, mandelbox_capacity=10, location=region_name)
    assert aws_funcs._get_num_new_instances(region_name, ami_id) == 0


def test_buffer_with_multiple(bulk_instance, region_ami_pair):
    """
    Tests that we don't ask for a new instance when we have enough space in multiple instances
    """
    region_name, ami_id = region_ami_pair
    bulk_instance(
        aws_ami_id=ami_id, associated_mandelboxes=5, mandelbox_capacity=10, location=region_name
    )
    bulk_instance(
        aws_ami_id=ami_id, associated_mandelboxes=5, mandelbox_capacity=10, location=region_name
    )
    assert aws_funcs._get_num_new_instances(region_name, ami_id) == 0


def test_buffer_with_multiple_draining(bulk_instance, region_ami_pair):
    """
    Tests that we don't ask for a new instance when we have enough space in multiple instances
    and also that draining instances are ignored
    """
    region_name, ami_id = region_ami_pair
    bulk_instance(
        aws_ami_id=ami_id, associated_mandelboxes=5, mandelbox_capacity=10, location=region_name
    )
    bulk_instance(
        aws_ami_id=ami_id, associated_mandelboxes=5, mandelbox_capacity=10, location=region_name
    )
    bulk_instance(
        aws_ami_id=ami_id,
        associated_mandelboxes=0,
        mandelbox_capacity=10,
        status="DRAINING",
        location=region_name,
    )
    bulk_instance(
        aws_ami_id=ami_id,
        associated_mandelboxes=0,
        mandelbox_capacity=10,
        status="DRAINING",
        location=region_name,
    )
    assert aws_funcs._get_num_new_instances(region_name, ami_id) == 0


def test_buffer_overfull(bulk_instance, region_ami_pair):
    """
    Tests that we ask to scale down an instance when we have too much free space
    """
    region_name, ami_id = region_ami_pair
    bulk_instance(
        aws_ami_id=ami_id, associated_mandelboxes=0, mandelbox_capacity=10, location=region_name
    )
    bulk_instance(
        aws_ami_id=ami_id, associated_mandelboxes=0, mandelbox_capacity=10, location=region_name
    )
    assert aws_funcs._get_num_new_instances(region_name, ami_id) == -1


def test_buffer_not_too_full(bulk_instance, region_ami_pair):
    """
    Tests that we don't ask to scale down an instance when we have some free space
    """
    region_name, ami_id = region_ami_pair
    bulk_instance(
        aws_ami_id=ami_id, associated_mandelboxes=5, mandelbox_capacity=10, location=region_name
    )
    bulk_instance(
        aws_ami_id=ami_id, associated_mandelboxes=0, mandelbox_capacity=10, location=region_name
    )
    assert aws_funcs._get_num_new_instances(region_name, ami_id) == 0


def test_buffer_overfull_split(bulk_instance, region_ami_pair):
    """
    Tests that we ask to scale down an instance when we have too much free space
    over several separate instances
    """
    region_name, ami_id = region_ami_pair
    bulk_instance(
        aws_ami_id=ami_id, associated_mandelboxes=9, mandelbox_capacity=10, location=region_name
    )
    bulk_instance(
        aws_ami_id=ami_id, associated_mandelboxes=1, mandelbox_capacity=10, location=region_name
    )
    bulk_instance(
        aws_ami_id=ami_id, associated_mandelboxes=0, mandelbox_capacity=10, location=region_name
    )
    assert aws_funcs._get_num_new_instances(region_name, ami_id) == -1


def test_buffer_not_too_full_split(bulk_instance, region_ami_pair):
    """
    Tests that we don't ask to scale down an instance when we have some free space
    over several separate instances
    """
    region_name, ami_id = region_ami_pair
    bulk_instance(
        aws_ami_id=ami_id, associated_mandelboxes=9, mandelbox_capacity=10, location=region_name
    )
    bulk_instance(
        aws_ami_id=ami_id, associated_mandelboxes=4, mandelbox_capacity=10, location=region_name
    )
    bulk_instance(
        aws_ami_id=ami_id, associated_mandelboxes=0, mandelbox_capacity=10, location=region_name
    )
    assert aws_funcs._get_num_new_instances(region_name, ami_id) == 0


def test_buffer_region_sensitive(region_to_ami_map, bulk_instance):
    """
    Tests that our buffer is based on region
    """
    randomly_picked_ami_objs = get_random_regions(2)
    assert len(randomly_picked_ami_objs) == 2
    region_ami_pairs = [
        (ami_obj.region_name, ami_obj.ami_id) for ami_obj in randomly_picked_ami_objs
    ]
    region_ami_with_buffer, region_ami_without_buffer = region_ami_pairs
    bulk_instance(
        aws_ami_id=region_ami_with_buffer[1],
        associated_mandelboxes=9,
        mandelbox_capacity=10,
        location=region_ami_with_buffer[0],
    )
    bulk_instance(
        aws_ami_id=region_ami_with_buffer[1],
        associated_mandelboxes=1,
        mandelbox_capacity=10,
        location=region_ami_with_buffer[0],
    )
    bulk_instance(
        aws_ami_id=region_ami_with_buffer[1],
        associated_mandelboxes=0,
        mandelbox_capacity=10,
        location=region_ami_with_buffer[0],
    )
    assert (
        aws_funcs._get_num_new_instances(region_ami_with_buffer[0], region_ami_with_buffer[1]) == -1
    )
    assert (
        aws_funcs._get_num_new_instances(region_ami_without_buffer[0], region_ami_without_buffer[1])
        == 1
    )


def test_scale_down_harness(monkeypatch, bulk_instance):
    """
    tests that try_scale_down_if_necessary_all_regions actually runs on every region/AMI pair in our db
    """
    call_list = []

    def _helper(*args, **kwargs):
        nonlocal call_list
        call_list.append({"args": args, "kwargs": kwargs})

    monkeypatch.setattr(aws_funcs, "try_scale_down_if_necessary", _helper)
    region_ami_pairs_length = 4
    randomly_picked_ami_objs = get_random_regions(region_ami_pairs_length)
    region_ami_pairs = [
        (ami_obj.region_name, ami_obj.ami_id) for ami_obj in randomly_picked_ami_objs
    ]
    for region, ami_id in region_ami_pairs:
        num_instances = randint(1, 10)
        for _ in range(num_instances):
            bulk_instance(location=region, aws_ami_id=ami_id)

    aws_funcs.try_scale_down_if_necessary_all_regions()
    assert len(call_list) == region_ami_pairs_length
    args = [called["args"] for called in call_list]
    assert set(args) == set(region_ami_pairs)


def test_get_num_mandelboxes():
    # Ensures our get_num_mandelboxes utility works as expected
    assert aws_funcs.get_base_free_mandelboxes("g4dn.xlarge") == 1
    assert aws_funcs.get_base_free_mandelboxes("g4dn.2xlarge") == 1
    assert aws_funcs.get_base_free_mandelboxes("g4dn.4xlarge") == 1
    assert aws_funcs.get_base_free_mandelboxes("g4dn.8xlarge") == 1
    assert aws_funcs.get_base_free_mandelboxes("g4dn.16xlarge") == 1
    assert aws_funcs.get_base_free_mandelboxes("g4dn.12xlarge") == 4
