#!/usr/bin/env python3
"""Secure local credential loading for authorized VoxOne HIL tests."""

from __future__ import annotations

import json
import os
from pathlib import Path
import stat
from dataclasses import dataclass, field


DEFAULT_CREDENTIALS_PATH = (
    Path.home() / ".config" / "voxone-hil" / "test-credentials.json"
)
SUPPORTED_PROFILES = frozenset({"W0", "M0", "L0"})
MAX_CREDENTIALS_BYTES = 64 * 1024


class CredentialsError(RuntimeError):
    """A credential store is malformed or not safely accessible."""


class AuthRequired(CredentialsError):
    """The approved test requires a credential that is not available."""


@dataclass(frozen=True)
class MqttCredentials:
    host: str = field(repr=False)
    port: int = field(repr=False)
    username: str = field(repr=False)
    password: str = field(repr=False)


def _validate_directory_metadata(metadata: os.stat_result) -> None:
    if not stat.S_ISDIR(metadata.st_mode):
        raise CredentialsError("local credential directory must not be a symlink")
    if metadata.st_uid != os.geteuid():
        raise CredentialsError("local credential directory has the wrong owner")
    if stat.S_IMODE(metadata.st_mode) != 0o700:
        raise CredentialsError("local credential directory must have mode 0700")


def _require_directory(path: Path) -> None:
    try:
        metadata = path.lstat()
    except OSError as error:
        raise AuthRequired(
            "AUTH_REQUIRED: local credential directory is unavailable"
        ) from error
    _validate_directory_metadata(metadata)


def _read_store(path: Path = DEFAULT_CREDENTIALS_PATH) -> dict[str, object]:
    _require_directory(path.parent)
    common_flags = (
        os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) |
        getattr(os, "O_NOFOLLOW", 0)
    )
    try:
        directory_descriptor = os.open(
            path.parent,
            common_flags | getattr(os, "O_DIRECTORY", 0),
        )
    except FileNotFoundError as error:
        raise AuthRequired(
            "AUTH_REQUIRED: local credential directory is unavailable"
        ) from error
    except OSError as error:
        raise CredentialsError(
            "local credential directory cannot be opened safely"
        ) from error
    try:
        _validate_directory_metadata(os.fstat(directory_descriptor))
    except Exception:
        os.close(directory_descriptor)
        raise

    try:
        descriptor = os.open(path.name, common_flags, dir_fd=directory_descriptor)
    except FileNotFoundError as error:
        raise AuthRequired(
            "AUTH_REQUIRED: local credential file is unavailable"
        ) from error
    except OSError as error:
        raise CredentialsError(
            "local credential file cannot be opened safely"
        ) from error
    finally:
        os.close(directory_descriptor)

    try:
        metadata = os.fstat(descriptor)
        if not stat.S_ISREG(metadata.st_mode):
            raise CredentialsError("local credential file is not a regular file")
        if metadata.st_uid != os.geteuid():
            raise CredentialsError("local credential file has the wrong owner")
        if stat.S_IMODE(metadata.st_mode) != 0o600:
            raise CredentialsError("local credential file must have mode 0600")
        with os.fdopen(descriptor, "rb") as stream:
            descriptor = -1
            serialized = stream.read(MAX_CREDENTIALS_BYTES + 1)
    finally:
        if descriptor >= 0:
            os.close(descriptor)

    if len(serialized) > MAX_CREDENTIALS_BYTES:
        raise CredentialsError("local credential file is too large")
    try:
        document = json.loads(serialized)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CredentialsError("local credential file is not valid JSON") from error
    if not isinstance(document, dict) or document.get("version") != 1:
        raise CredentialsError("local credential file has an unsupported schema")
    return document


def _mapping(value: object, label: str) -> dict[str, object]:
    if not isinstance(value, dict):
        raise CredentialsError(
            f"local credential file has an invalid {label} section"
        )
    return value


def www_admin_password(
    profile: str,
    path: Path = DEFAULT_CREDENTIALS_PATH,
) -> str:
    normalized = profile.upper()
    if normalized not in SUPPORTED_PROFILES:
        raise CredentialsError("unsupported VoxOne credential profile")
    document = _read_store(path)
    voxone = _mapping(document.get("voxone"), "voxone")
    selected = _mapping(voxone.get(normalized), f"voxone.{normalized}")
    password = selected.get("www_admin_password")
    if not isinstance(password, str):
        raise CredentialsError(
            "local credential file has an invalid WWW password field"
        )
    if not password:
        raise AuthRequired(
            f"AUTH_REQUIRED: WWW administrator password for {normalized} is empty"
        )
    return password


def mqtt_credentials(path: Path = DEFAULT_CREDENTIALS_PATH) -> MqttCredentials:
    document = _read_store(path)
    mqtt = _mapping(document.get("mqtt"), "mqtt")
    host = mqtt.get("host")
    port = mqtt.get("port")
    username = mqtt.get("username")
    password = mqtt.get("password")
    if not all(isinstance(value, str) for value in (host, username, password)):
        raise CredentialsError("local credential file has invalid MQTT text fields")
    if (
        not isinstance(port, int) or isinstance(port, bool) or
        not 1 <= port <= 65535
    ):
        raise CredentialsError("local credential file has an invalid MQTT port")
    if not host or not username or not password:
        raise AuthRequired("AUTH_REQUIRED: MQTT test credentials are incomplete")
    return MqttCredentials(host, port, username, password)
