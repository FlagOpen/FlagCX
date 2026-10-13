#!/usr/bin/env python3
# Copyright 2026 FlagOS Contributors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Generate an enabled CI platform matrix, filtering only platform-specific PRs.

Push, schedule and workflow_dispatch run all enabled platforms. Pull requests
use paths_watch globs from platform configs; shared or unclassified files and
unavailable diff information conservatively run the full enabled matrix.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import pathlib
import subprocess
import sys

import yaml


CORE_TRIGGERS: tuple[str, ...] = (
    ".github/configs/",
    ".github/workflows/",
    ".github/scripts/ci/",
    "setup.py",
    "setup.cfg",
    "pyproject.toml",
    "CMakeLists.txt",
    "requirements",
)


def _load_platforms(configs_dir: pathlib.Path) -> list[str]:
    """Reuse the unit-test loader's registry selection and config validation."""
    loader = pathlib.Path(__file__).with_name("load_platform_matrix.rb")
    result = subprocess.run(
        ["ruby", str(loader), str(configs_dir), "all"],
        capture_output=True,
        text=True,
        check=True,
        env={key: value for key, value in os.environ.items() if key != "GITHUB_OUTPUT"},
    )
    return sorted(entry["platform"] for entry in json.loads(result.stdout)["include"])


def _load_watch_patterns(configs_dir: pathlib.Path, platforms: list[str]) -> dict[str, list[str]]:
    watches = {}
    for platform in platforms:
        config_path = configs_dir / f"{platform}.yml"
        cfg = yaml.safe_load(config_path.read_text())
        if not isinstance(cfg, dict):
            raise ValueError(f"{config_path}: configuration must be a map")
        watch = cfg.get("paths_watch", [])
        if not isinstance(watch, list) or any(
            not isinstance(pattern, str)
            or not pattern.strip()
            or pattern != pattern.strip()
            or pattern.startswith("/")
            or ".." in pathlib.PurePosixPath(pattern).parts
            or any(char in pattern for char in "\\\r\n\0")
            for pattern in watch
        ):
            raise ValueError(f"{config_path}: paths_watch must be a list of repository-relative globs")
        watches[platform] = watch
    return watches


def _changed_files(base_ref: str) -> list[str]:
    """Return PR changes including both sides of renames so neither is missed."""
    result = subprocess.run(
        ["git", "diff", "--no-renames", "--name-only", "-z", f"origin/{base_ref}...HEAD"],
        capture_output=True,
        text=True,
        check=True,
    )
    return [path for path in result.stdout.split("\0") if path]


def _select_platforms(
    platforms: list[str], changed: list[str], watches: dict[str, list[str]]
) -> list[str]:
    # Missing watches or diff information cannot justify dropping a platform.
    if not changed or any(not watches[platform] for platform in platforms):
        return platforms
    selected = set()
    for path in changed:
        if any(path.startswith(trigger) for trigger in CORE_TRIGGERS):
            return platforms
        affected = {
            platform
            for platform in platforms
            if any(fnmatch.fnmatchcase(path, pattern) for pattern in watches[platform])
        }
        # One unknown/shared file requires every enabled platform, even when
        # another file in this diff matched a platform-specific pattern.
        if not affected:
            return platforms
        selected.update(affected)
    return [platform for platform in platforms if platform in selected]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--event-name", required=True, help="GitHub event name")
    parser.add_argument("--base-ref", default="main", help="PR base branch for git diff")
    parser.add_argument("--configs-dir", default=".github/configs")
    parser.add_argument("--changed-files", default=None, help="Newline-separated override for git diff")
    args = parser.parse_args()

    configs_dir = pathlib.Path(args.configs_dir)
    try:
        platforms = _load_platforms(configs_dir)
        watches = _load_watch_patterns(configs_dir, platforms)
    except (OSError, ValueError, yaml.YAMLError, subprocess.CalledProcessError) as exc:
        detail = exc.stderr if isinstance(exc, subprocess.CalledProcessError) else str(exc)
        parser.error(detail)

    if args.event_name != "pull_request":
        print(f"platforms={json.dumps(platforms)}")
        return

    if args.changed_files is not None:
        changed = [path for path in args.changed_files.splitlines() if path]
    else:
        try:
            changed = _changed_files(args.base_ref)
        except (OSError, subprocess.CalledProcessError) as exc:
            print(f"::warning::git diff failed ({exc}); using all enabled platforms", file=sys.stderr)
            changed = []

    print(f"platforms={json.dumps(_select_platforms(platforms, changed, watches))}")


if __name__ == "__main__":
    main()
