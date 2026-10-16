"""Inject the local Recovery AP credential without exposing it in flags."""

Import("env")

import os
import stat
from pathlib import Path


project_dir = Path(env.subst("$PROJECT_DIR"))
credential_path = project_dir / ".voxone-recovery-ap-password"
secure_dir = project_dir / ".pio" / "secure"
header_path = secure_dir / "recovery_ap_credential.h"


def write_private_file(path, content):
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    try:
        path.parent.chmod(0o700)
    except OSError:
        pass
    current = path.read_bytes() if path.is_file() else None
    if current != content:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(content)
    try:
        path.chmod(0o600)
    except OSError:
        pass


definition = b"#pragma once\n"
if credential_path.exists():
    if not credential_path.is_file():
        raise RuntimeError("Recovery AP credential path is not a regular file")
    if os.name == "posix":
        mode = stat.S_IMODE(credential_path.stat().st_mode)
        if mode & 0o077:
            raise RuntimeError(
                "Recovery AP credential file must not be accessible by group or others"
            )
    raw = credential_path.read_bytes()
    if raw.endswith(b"\r\n"):
        raw = raw[:-2]
    elif raw.endswith(b"\n"):
        raw = raw[:-1]
    if b"\r" in raw or b"\n" in raw:
        raise RuntimeError("Recovery AP credential must occupy exactly one line")
    if not 12 <= len(raw) <= 63:
        raise RuntimeError("Recovery AP credential must contain 12 to 63 bytes")
    if any(byte < 0x20 or byte > 0x7E for byte in raw):
        raise RuntimeError("Recovery AP credential must use printable ASCII")
    escaped = raw.replace(b"\\", b"\\\\").replace(b'"', b'\\"')
    definition += (
        b'#define VOXONE_SECURITY_RECOVERY_AP_PASSWORD "'
        + escaped
        + b'"\n'
    )

write_private_file(header_path, definition)
env.Append(CCFLAGS=["-include", str(header_path)])
