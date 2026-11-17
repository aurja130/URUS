# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Aurora Jahan
# Licensed under the GNU GPL v3 or later, WITHOUT ANY WARRANTY; see LICENSE.
"""Local CI: the gates run by ``python3 -m tools.ci.local_ci`` and the helpers they share."""

from __future__ import annotations

import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


def repository_files(root: Path) -> list[str]:
    """Sorted paths of the existing tracked and untracked (not ignored) files under root."""
    listing = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=root,
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    return sorted(name for name in listing.split("\0") if name and (root / name).is_file())
