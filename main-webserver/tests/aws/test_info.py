"""Tests for the /container/protocol_info endpoint."""

from http import HTTPStatus

from app.helpers.blueprint_helpers.aws.aws_container_post import protocol_info


def not_found(*args, **kwargs):
    from http import HTTPStatus

    return None, HTTPStatus.NOT_FOUND


def success(*args, **kwargs):
    from http import HTTPStatus

    return {}, HTTPStatus.OK


def test_not_found(client, monkeypatch):
    monkeypatch.setattr(protocol_info, "__code__", not_found.__code__)

    response = client.post(
        "/container/protocol_info", json=dict(identifier=0, private_key="aes_secret_key")
    )

    assert response.status_code == HTTPStatus.NOT_FOUND


def test_no_port(client):
    response = client.post("/container/protocol_info", json=dict(private_key="aes_secret_key"))

    assert response.status_code == HTTPStatus.BAD_REQUEST


def test_no_key(client):
    response = client.post("/container/protocol_info", json=dict(identifier=0))

    assert response.status_code == HTTPStatus.BAD_REQUEST


def test_successful(client, monkeypatch):
    monkeypatch.setattr(protocol_info, "__code__", success.__code__)

    response = client.post(
        "/container/protocol_info", json=dict(identifier=0, private_key="aes_secret_key")
    )

    assert response.status_code == HTTPStatus.OK


def test_no_container():
    response, status = protocol_info("x.x.x.x", 0, 0)

    assert not response
    assert status == HTTPStatus.NOT_FOUND


def test_protocol_info(container):
    with container() as c:
        response, status = protocol_info(c.ip, c.port_32262, c.secret_key)

        assert status == HTTPStatus.OK
        assert response.pop("allow_autoupdate") == c.allow_autoupdate
        assert response.pop("branch") == c.branch
        assert response.pop("secret_key") == c.secret_key
        assert response.pop("using_stun") == c.using_stun
        assert response.pop("container_id") == c.container_id
        assert response.pop("user_id") == c.user_id
        assert response.pop("state") == c.state
        assert not response.pop("is_assigned")
        assert response.pop("dpi") is None
        assert not response  # The dictionary should be empty now.
