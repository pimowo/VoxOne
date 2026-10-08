"""Pass the checked-out revision to firmware without generating source files."""

Import("env")

import subprocess
from pathlib import Path


project_dir = Path(env.subst("$PROJECT_DIR"))
revision = "unknown"
try:
    repo_root = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        cwd=project_dir, capture_output=True, text=True, check=True,
    ).stdout.strip()
    if Path(repo_root).resolve() == project_dir.resolve():
        short_sha = subprocess.run(
            ["git", "rev-parse", "--short=7", "HEAD"],
            cwd=project_dir, capture_output=True, text=True, check=True,
        ).stdout.strip()
        if len(short_sha) == 7 and all(c in "0123456789abcdef" for c in short_sha):
            changed = subprocess.run(
                ["git", "status", "--porcelain=v1", "--untracked-files=normal"],
                cwd=project_dir, capture_output=True, text=True, check=True,
            ).stdout
            revision = short_sha + ("-dirty" if changed else "")
except (OSError, subprocess.CalledProcessError):
    pass

env.Append(CPPDEFINES=[("VOXONE_BUILD_SHA", f'\\"{revision}\\"')])
