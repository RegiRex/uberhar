#!/usr/bin/env python3
"""CodexAstraLocal: Run the workflow's publication shell against a recording fake gh.

No GitHub request or publication occurs. This exercises alpha/beta classification,
draft ordering and the existing version/tag guards. CodexAstraUlt-2 additionally
executes the production job expressions against trusted and untrusted events;
this is an offline routing fixture, not GitHub-hosted execution or signing.
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
        # CodexAstraUlt-2: The next alpha retires the historical .56 repair alias.
        release_tag = version
        assert calls[0] == ["release", "view", release_tag, "--repo", "RegiRex/uberhar"], calls
        if scenario.get("existing"):
            assert len(calls) == 1, calls
            return
        assert calls[1] == ["api", f"repos/RegiRex/uberhar/git/ref/tags/{release_tag}"], calls
        if scenario.get("tag_sha") not in (None, expected_sha):
            assert len(calls) == 2, calls
            return
        assert len(calls) == (3 if scenario.get("create_fails") else 4), calls
        create = calls[2]
        assert create[:3] == ["release", "create", release_tag], create
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
            assert calls[3][:3] == ["release", "edit", release_tag], calls
            assert flag(calls[3], "--draft") is False, calls[3]



# CodexAstraUlt-2: Evaluate the actual workflow conditions with a deliberately
# small Actions-expression grammar. Unknown syntax fails closed; event decisions
# are never copied into a second handwritten allowlist implementation.
def actions_expression(source: str, context: dict):
    tokens = []
    at = 0
    pattern = re.compile(r"\s*('(?:[^']|'')*'|==|!=|&&|\|\||[()]|[A-Za-z_][A-Za-z_0-9.]*)")
    while at < len(source.rstrip()):
        token = pattern.match(source, at)
        if not token:
            raise AssertionError(f"Unsupported Actions expression: {source[at:]}")
        tokens.append(token[1])
        at = token.end()
    position = 0

    def accept(token):
        nonlocal position
        if position < len(tokens) and tokens[position] == token:
            position += 1
            return True
        return False

    def primary():
        nonlocal position
        if accept("("):
            value = disjunction()
            assert accept(")"), "Unclosed Actions expression"
            return value
        assert position < len(tokens), "Incomplete Actions expression"
        token = tokens[position]
        position += 1
        if token.startswith("'"):
            return token[1:-1].replace("''", "'")
        if token in ("true", "false", "null"):
            return {"true": True, "false": False, "null": None}[token]
        assert re.fullmatch(r"[A-Za-z_][A-Za-z_0-9.]*", token), token
        return context.get(token, "")

    def comparison():
        value = primary()
        if position < len(tokens) and tokens[position] in ("==", "!="):
            unequal = accept("!=")
            if not unequal:
                assert accept("==")
            other = primary()
            # CodexAstraUlt-2: Actions string comparisons ignore case. This
            # fixture intentionally supports only the string/bool guards in use.
            if isinstance(value, str) and isinstance(other, str):
                equal = value.casefold() == other.casefold()
            else:
                equal = value == other
            value = not equal if unequal else equal
        return value

    def conjunction():
        value = comparison()
        while accept("&&"):
            other = comparison()
            value = other if value else value
        return value

    def disjunction():
        value = conjunction()
        while accept("||"):
            other = conjunction()
            value = value if value else other
        return value

    value = disjunction()
    assert position == len(tokens), f"Unconsumed Actions tokens: {tokens[position:]}"
    return value


def job_source(workflow: str, name: str) -> str:
    # CodexAstraUlt-2: Extract by YAML indentation, independent of comments and
    # folded condition lines. A renamed/missing job is a fixture failure.
    marker = re.search(r"(?m)^  " + re.escape(name) + r":\s*$", workflow)
    assert marker, f"Missing workflow job: {name}"
    tail = workflow[marker.end():]
    following = re.search(r"(?m)^  [A-Za-z_][A-Za-z_0-9-]*:\s*$", tail)
    return tail[:following.start()] if following else tail


def job_condition(workflow: str, name: str) -> str:
    body = job_source(workflow, name)
    condition = re.search(r"(?m)^    if: ([^\n]+)", body)
    assert condition, f"Missing job condition: {name}"
    if condition[1] == ">-":
        folded = re.match(r"\n((?:      [^\n]+\n)+)", body[condition.end():])
        assert folded, f"Missing folded condition: {name}"
        return " ".join(line.strip() for line in folded[1].splitlines())
    return condition[1]


def expand_actions(source: str, context: dict) -> str:
    return re.sub(r"\$\{\{(.*?)\}\}", lambda match: str(actions_expression(match[1], context)),
                  source, flags=re.S)


def workflow_guards(workflow: str) -> int:
    # CodexAstraUlt-2: Exercise production guards with real event-shaped data,
    # including hostile fork/repository/base/name variants and retained lanes.
    assert not re.search(r"(?m)^  pull_request_target:", workflow), "Privileged PR trigger added"
    assert re.search(r"(?m)^    needs: \[android, shaders\]$", job_source(workflow, "publish")), \
        "Publication no longer requires both build gates"
    jobs = ("shaders", "comparison_shaders", "android", "publish")
    conditions = {job: job_condition(workflow, job) for job in jobs}
    owner, hybrid = "RegiRex/uberhar", "uberhar/hybrid-shaders"
    candidate = "uberhar/precompiled-first"
    diagonal, cpu = "uberhar/codexastra-diag-comparison", "uberhar/cpu-only-048-comparison"

    def event(kind, head, *, repository=owner, source=owner, base=hybrid):
        pull = kind in ("pull_request", "pull_request_target")
        result = {
            "github.event_name": kind, "github.repository": repository,
            "github.ref": "refs/pull/57/merge" if pull else "refs/heads/" + head,
            "github.head_ref": head if pull else "", "github.sha": "a" * 40,
            "steps.version.outputs.value": "0.1.57",
        }
        if pull:
            result.update({
                "github.event.pull_request.base.ref": base,
                "github.event.pull_request.head.ref": head,
                "github.event.pull_request.head.repo.full_name": source,
                "github.event.pull_request.head.sha": "b" * 40,
            })
        return result

    ordinary = {"shaders", "android"}
    release = ordinary | {"publish"}
    historical = {"comparison_shaders", "android"}
    cases = [
        ("candidate PR", event("pull_request", candidate), ordinary),
        ("fork PR", event("pull_request", candidate, source="outsider/uberhar"), set()),
        ("other repository", event("pull_request", candidate, repository="outsider/uberhar",
                                   source="outsider/uberhar"), set()),
        ("other base", event("pull_request", candidate, base="main"), set()),
        ("near-match branch", event("pull_request", candidate + "-extra"), set()),
        ("unlisted branch", event("pull_request", "uberhar/unlisted"), set()),
        ("empty branch", event("pull_request", ""), set()),
        ("privileged PR event", event("pull_request_target", candidate), set()),
        ("candidate push", event("push", candidate), set()),
        ("candidate manual", event("workflow_dispatch", candidate), set()),
        ("main push", event("push", hybrid), release),
        ("main manual", event("workflow_dispatch", hybrid), release),
        ("diagnostic PR", event("pull_request", diagonal), ordinary),
        ("diagnostic manual", event("workflow_dispatch", diagonal), ordinary),
        ("CPU comparison PR", event("pull_request", cpu), historical),
        ("CPU comparison manual", event("workflow_dispatch", cpu), historical),
        ("unlisted manual", event("workflow_dispatch", "uberhar/unlisted"), set()),
        ("unlisted push", event("push", "uberhar/unlisted"), set()),
    ]
    artifact = re.search(r"(?m)^          name: (uberhar-\$\{\{ steps.version.outputs.value.*)$",
                         job_source(workflow, "android"))
    assert artifact, "Missing versioned Android artifact name"
    for label, context, expected in cases:
        actual = {job for job in jobs if actions_expression(conditions[job], context)}
        assert actual == expected, f"{label}: unexpected jobs {actual}; expected {expected}"
        if "android" not in actual:
            continue
        expected_sha = context.get("github.event.pull_request.head.sha", context["github.sha"])
        for job in actual - {"publish"}:
            key = "ref" if job == "android" else "checkout_ref"
            checkout = re.search(r"(?m)^\s+" + key + r": (\$\{\{[^\n]+\}\})$",
                                 job_source(workflow, job))
            assert checkout, f"Missing immutable checkout: {job}"
            assert expand_actions(checkout[1], context) == expected_sha, \
                f"{label}: {job} checks out the wrong source"
        head = context.get("github.event.pull_request.head.ref", context["github.ref"].removeprefix("refs/heads/"))
        suffix = ("-precompiled-first-comparison" if head == candidate else
                  "-cpu-only-comparison" if head == cpu else "")
        assert expand_actions(artifact[1], context) == "uberhar-0.1.57" + suffix + "-arm64", \
            f"{label}: ambiguous Android artifact name"
    return len(cases)


def check_workflow_guards(workflow: str) -> tuple[int, int]:
    cases = workflow_guards(workflow)
    # CodexAstraUlt-2: Each defect changes production workflow text, then must
    # fail the behavioral checks. Missing anchors are failures, never successes.
    mutants = (
        ("any comparison head", "github.event.pull_request.head.ref == 'uberhar/precompiled-first'", "true",
         "near-match branch: unexpected jobs"),
        ("fork signing", "github.event.pull_request.head.repo.full_name == github.repository", "true",
         "fork PR: unexpected jobs"),
        ("other repository", "github.repository == 'RegiRex/uberhar'", "true",
         "other repository: unexpected jobs"),
        ("publish comparison", "\n    if: github.ref == 'refs/heads/uberhar/hybrid-shaders'", "\n    if: true",
         "candidate PR: unexpected jobs"),
        ("merged checkout", "github.event.pull_request.head.sha || github.sha", "github.sha",
         "checks out the wrong source"),
        ("ambiguous artifact", "'-precompiled-first-comparison'", "''",
         "candidate PR: ambiguous Android artifact name"),
        ("missing shader gate", "needs: [android, shaders]", "needs: [android]",
         "Publication no longer requires both build gates"),
        ("privileged trigger", "\n  pull_request:\n", "\n  pull_request_target:\n",
         "Privileged PR trigger added"),
    )
    for label, before, after, expected in mutants:
        assert before in workflow, f"Missing negative-control anchor: {label}"
        try:
            workflow_guards(workflow.replace(before, after))
        except AssertionError as error:
            assert expected in str(error), f"{label}: unintended control failure: {error}"
            continue
        raise AssertionError(f"Workflow guard failed to reject: {label}")
    return cases, len(mutants)


def main() -> None:
    # CodexAstraLocal: The optional source argument permits a failing old-workflow
    # control without mutating the working tree or accessing remote refs.
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path,
                        default=Path(".github/workflows/uberhar-alpha.yml"))
    args = parser.parse_args()
    workflow = args.source.read_text()
    script = publication_script(workflow)
    # CodexAstraUlt-2: Check actual workflow routing before the existing fake-gh cases.
    guard_cases, guard_mutants = check_workflow_guards(workflow)
    with tempfile.TemporaryDirectory(prefix="uberhar-fake-gh-") as temporary:
        binary = Path(temporary)
        executable = binary / "gh"
        executable.write_text(FAKE_GH)
        executable.chmod(0o700)
        cases = 0
        for version, prerelease in (("0.1.24", True), ("0.1.56", True), ("0.1.57", True), ("0.2.0", False)):
            for scenario, success in (
                ({}, True), ({"tag_sha": "a" * 40}, True),
                ({"existing": True}, False), ({"tag_sha": "b" * 40}, False),
                ({"create_fails": True}, False),
            ):
                run_case(script, binary, version, prerelease, scenario, success)
                cases += 1
        run_case(script, binary, "1.0.0", False, {}, True)
        cases += 1
    print(f"PASS: {guard_cases} production workflow event cases and {guard_mutants} "
          "guard defect controls preserve isolated artifacts and publication")
    print(f"PASS: {cases} production publication-shell cases preserve classification, "
          "draft ordering, artifacts and version/tag guards (offline fake gh)")


if __name__ == "__main__":
    main()
