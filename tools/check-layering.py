#!/usr/bin/env python3
"""Fail if a module includes a header from a layer it may not depend on.

The rules live in docs/layers.txt. Keeping the check in the build is the point:
an architecture that is only written down stops being true the first time
somebody is in a hurry.
"""

import pathlib
import re
import sys

INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)


def load_rules(path):
    rules = {}
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        module, _, dependencies = line.partition(":")
        rules[module.strip()] = set(dependencies.split())
    return rules


def module_of(path, source_root):
    relative = path.relative_to(source_root)
    return relative.parts[0] if len(relative.parts) > 1 else None


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    source_root = root / "src"
    rules = load_rules(root / "docs" / "layers.txt")

    failures = []
    for path in sorted(source_root.rglob("*.[ch]")):
        module = module_of(path, source_root)
        if module is None or module not in rules:
            continue
        allowed = rules[module] | {module}
        for included in INCLUDE.findall(path.read_text()):
            target = pathlib.PurePosixPath(included).parts[0]
            if target.endswith(".h"):
                continue
            if target not in rules:
                continue
            if target not in allowed:
                failures.append(
                    f"{path.relative_to(root)}: {module} may not include {target}"
                )

    for failure in failures:
        print(failure, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
