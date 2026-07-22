#!/usr/bin/env python3
"""Fetch or verify StopWatch source dependencies."""

import argparse
import pathlib
import sys

from dependency_bootstrap import (
    DependencyError,
    load_manifest,
    materialize_all,
    verify_all,
)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--check",
        action="store_true",
        help="verify local dependencies without downloading or modifying them",
    )
    mode.add_argument(
        "--force",
        action="store_true",
        help="replace dependency trees that differ from the locked manifest",
    )
    return parser.parse_args(argv)


def main(argv=None):
    root = pathlib.Path(__file__).resolve().parent
    args = parse_args(argv)
    try:
        specs = load_manifest(root)
        if args.check:
            issues = verify_all(specs, root)
            for issue in issues:
                try:
                    display_path = issue.path.relative_to(root)
                except ValueError:
                    display_path = issue.path
                print(
                    "{}: {}: {}".format(
                        issue.category, display_path, issue.message
                    ),
                    file=sys.stderr,
                )
            return 1 if issues else 0
        materialize_all(specs, root, force=args.force)
        return 0
    except DependencyError as error:
        print("dependency error: {}".format(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
