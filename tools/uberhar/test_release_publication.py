#!/usr/bin/env python3
"""CodexAstraLocal: Run the workflow's publication shell against a recording fake gh.

No GitHub request or publication occurs. This exercises alpha/beta classification,
draft ordering and the existing version/tag guards; upstream job gates are reviewed
separately and are not simulated by this fixture.
"""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import textwrap


def publication_script(workflow: str) -> str:
    # CodexAstraLocal: Extract production shell rather than restating its decisions.
    # Accept the historical step name too so --source demonstrates its beta failure.
    step = re.search(r"(?m)^      - name: Publish GitHub (?:pre-)?release\s*$", workflow)
    if not step:
        raise AssertionError("GitHub publication step not found")
    tail = workflow[step.end():]
    body = re.search(r"(?m)^        run: \|\n((?:          [^\n]*\n|\n)+)", tail)
    if not body:
        raise AssertionError("Publication shell block not found")
    return textwrap.dedent(body.group(1))


# CodexAstraLocal: Every gh invocation is intercepted locally, and unknown commands
# fail. No installed CLI, credentials, sockets or connected device are used.
FAKE_GH = r'''#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys

args = sys.argv[1:]
with Path(os.environ["UBERHAR_FAKE_CALLS"]).open("a") as output:
    output.write(json.dumps(args) + "\n")
scenario = json.loads(os.environ["UBERHAR_FAKE_SCENARIO"])
if args[:2] == ["release", "view"]:
    sys.exit(0 if scenario.get("existing") else 1)
if args[:1] == ["api"]:
    if scenario.get("tag_sha"):
        print(json.dumps({"object": {"sha": scenario["tag_sha"]}}))
        sys.exit(0)
    sys.exit(1)
if args[:2] == ["release", "create"]:
    sys.exit(1 if scenario.get("create_fails") else 0)
if args[:2] == ["release", "edit"]:
    sys.exit(0)
sys.exit("Unexpected fake gh command")
'''


def flag(arguments: list[str], name: str, default=None):
    # CodexAstraLocal: Inspect the CLI contract, accepting equivalent boolean forms.
    values = [True if item == name else item.split("=", 1)[1]
              for item in arguments if item == name or item.startswith(name + "=")]
    if not values:
        return default
    if len(values) != 1:
        raise AssertionError(f"Duplicate {name}: {arguments}")
    if values[0] in (True, "true"):
        return True
    if values[0] == "false":
        return False
    raise AssertionError(f"Unexpected boolean {name}: {arguments}")


# CodexAstraLocal: Run the extracted publication shell against a fake gh
# command, checking call order, exact target and failed-draft containment.
def run_case(script: str, binary: Path, version: str, prerelease: bool,
             scenario: dict, expected_success: bool) -> None:
    with tempfile.TemporaryDirectory(prefix="uberhar-release-case-") as temporary:
        root = Path(temporary)
        calls_path = root / "calls.jsonl"
        expected_sha = "a" * 40
        environment = {
            **os.environ,
            "PATH": str(binary) + os.pathsep + os.environ.get("PATH", ""),
            "GH_TOKEN": "unused-offline-fixture",
            "GITHUB_REPOSITORY": "RegiRex/uberhar",
            "GITHUB_SHA": expected_sha,
            "UBERHAR_VERSION": version,
            "UBERHAR_FAKE_CALLS": str(calls_path),
            "UBERHAR_FAKE_SCENARIO": json.dumps(scenario),
        }
        result = subprocess.run(
            ["bash", "--noprofile", "--norc", "-e", "-o", "pipefail", "-c", script],
            cwd=root, env=environment, text=True, capture_output=True, timeout=10,
        )
        assert (result.returncode == 0) == expected_success, (version, scenario, result.stderr)
        calls = [json.loads(line) for line in calls_path.read_text().splitlines()]
        assert calls[0] == ["release", "view", version, "--repo", "RegiRex/uberhar"], calls
        if scenario.get("existing"):
            assert len(calls) == 1, calls
            return
        assert calls[1] == ["api", f"repos/RegiRex/uberhar/git/ref/tags/{version}"], calls
        if scenario.get("tag_sha") not in (None, expected_sha):
            assert len(calls) == 2, calls
            return
        assert len(calls) == (3 if scenario.get("create_fails") else 4), calls
        create = calls[2]
        assert create[:3] == ["release", "create", version], create
        assert flag(create, "--draft") is True, create
        assert create[create.index("--target") + 1] == expected_sha, create
        assert create[create.index("--notes-file") + 1] == "artifacts/release-notes.md", create
        for asset in (
            f"artifacts/uberhar-{version}-arm64.apk", "artifacts/apk-sha256.txt",
            f"artifacts/uberhar-{version}-validation.zip",
            f"artifacts/uberhar-{version}-native-symbols.zip",
        ):
            assert asset in create, (asset, create)
        # CodexAstraLocal: A failed draft/asset creation must never reach publication.
        for operation in calls[2:]:
            assert operation[operation.index("--repo") + 1] == "RegiRex/uberhar", operation
            assert flag(operation, "--prerelease", False) is prerelease, (version, operation)
            assert flag(operation, "--latest") is False, operation
        if not scenario.get("create_fails"):
            assert calls[3][:3] == ["release", "edit", version], calls
            assert flag(calls[3], "--draft") is False, calls[3]


def main() -> None:
    # CodexAstraLocal: The optional source argument permits a failing old-workflow
    # control without mutating the working tree or accessing remote refs.
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path,
                        default=Path(".github/workflows/uberhar-alpha.yml"))
    args = parser.parse_args()
    script = publication_script(args.source.read_text())
    with tempfile.TemporaryDirectory(prefix="uberhar-fake-gh-") as temporary:
        binary = Path(temporary)
        executable = binary / "gh"
        executable.write_text(FAKE_GH)
        executable.chmod(0o700)
        cases = 0
        for version, prerelease in (("0.1.24", True), ("0.2.0", False)):
            for scenario, success in (
                ({}, True), ({"tag_sha": "a" * 40}, True),
                ({"existing": True}, False), ({"tag_sha": "b" * 40}, False),
                ({"create_fails": True}, False),
            ):
                run_case(script, binary, version, prerelease, scenario, success)
                cases += 1
        run_case(script, binary, "1.0.0", False, {}, True)
        cases += 1
    print(f"PASS: {cases} production publication-shell cases preserve classification, "
          "draft ordering, artifacts and version/tag guards (offline fake gh)")


if __name__ == "__main__":
    main()
