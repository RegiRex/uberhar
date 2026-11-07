#!/usr/bin/env python3
"""AstraEH: Check the final decoded APK manifest for known install/conflict risks."""

from pathlib import Path
import argparse  # CodexAstraLocal: Explicit new-build profiling contract; old manifests remain readable.
import xml.etree.ElementTree as ET

ANDROID = "{http://schemas.android.com/apk/res/android}"
PACKAGE = "org.uberhar.uberhar_emu"
# CodexAstraUlt-2: Select a reviewed identity explicitly; historical main APK
# validation keeps its original default while the comparison installs separately.
PRECOMPILED_PACKAGE = "org.uberhar.uberhar_emu.precompiled"
PACKAGES = {PACKAGE, PRECOMPILED_PACKAGE}
# AstraEH: Preserve the permissions already present in the pinned app and AndroidX dependencies.
# This gate catches accidental additions; runtime permission grants still belong to the user.
PLATFORM_PERMISSIONS = {
    "android.permission.ACCESS_NETWORK_STATE", "android.permission.INTERNET",
    "android.permission.CAMERA", "android.permission.RECORD_AUDIO",
    "android.permission.POST_NOTIFICATIONS", "android.permission.MANAGE_EXTERNAL_STORAGE",
    "android.permission.WRITE_EXTERNAL_STORAGE", "android.permission.READ_EXTERNAL_STORAGE",
    "android.permission.WAKE_LOCK", "android.permission.RECEIVE_BOOT_COMPLETED",
    "android.permission.FOREGROUND_SERVICE",
}


def validate(root, version, *, require_shell_profiling=False, expected_package=PACKAGE):
    def require(condition, message):
        if not condition:
            raise ValueError(message)

    def enabled(element, name, default=False):
        return element.get(ANDROID + name, str(default).lower()) not in {"false", "0"}

    # CodexAstraUlt-2: Longest known namespace wins because the comparison ID
    # extends main's ID. A main APK must not claim the comparison's authorities.
    require(expected_package in PACKAGES, "Unreviewed package identity")
    package_roots = sorted(PACKAGES, key=len, reverse=True)

    def owned_name(name):
        owner = next((p for p in package_roots if name.startswith(p + ".")), None)
        return owner == expected_package

    permissions = PLATFORM_PERMISSIONS | {
        expected_package + ".DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION"
    }

    # AstraEH: A standalone, separately named package avoids split-install and shared-UID conflicts.
    require(root.get("package") == expected_package, "Unexpected package identity")
    require(root.get(ANDROID + "versionName") == version, "Version does not match UBERHAR_VERSION")
    require(not root.get(ANDROID + "sharedUserId"), "Shared Android user IDs are not allowed")
    require(not root.get("split"), "Expected a standalone APK, not a split APK")
    require(not root.get(ANDROID + "requiredSplitTypes"), "APK requires missing split packages")
    sdk = root.find("uses-sdk")
    require(sdk is not None, "Missing SDK compatibility metadata")
    require(sdk.get(ANDROID + "minSdkVersion") == "29", "Unexpected minimum Android API")
    require(sdk.get(ANDROID + "targetSdkVersion") == "37", "Unexpected target Android API")
    require(sdk.get(ANDROID + "maxSdkVersion") is None, "Maximum API would exclude newer Android")
    app = root.find("application")
    require(app is not None, "Missing application")
    require(not enabled(app, "testOnly"), "testOnly blocks normal Android installation")
    require(not enabled(app, "debuggable"), "Published APK must use the non-debuggable release build")
    # CodexAstraLocal: New CPU experiments need actual optimized-release sampling.
    # Require the merged flavor flag only when requested by current packaging;
    # historical APK inspection retains the existing installation contract.
    if require_shell_profiling:
        profiles = app.findall("profileable")
        # CodexAstraLocal: A resolved true value proves shell access; unknown or
        # resource-valued booleans must not silently satisfy the packaging gate.
        require(len(profiles) == 1 and
                profiles[0].get(ANDROID + "shell") in {"true", "1"} and
                profiles[0].get(ANDROID + "enabled", "true") in {"true", "1"},
                "Missing enabled shell profiling")
    require(not enabled(app, "isSplitRequired"), "APK requires unavailable splits")
    require(enabled(app, "extractNativeLibs", True), "Native library extraction is required for driver loading")
    require(app.get(ANDROID + "name") == "org.citra.citra_emu.CitraApplication", "Wrong application entry point")
    main = next((a for a in app.findall("activity") if a.get(ANDROID + "name") ==
                 "org.citra.citra_emu.ui.main.MainActivity"), None)
    require(main is not None and enabled(main, "exported"), "Missing exported launcher activity")

    # AstraEH: Provider authorities and custom permissions must not collide with Azahar/other apps.
    for provider in app.findall("provider"):
        authorities = provider.get(ANDROID + "authorities", "").split(";")
        require(all(owned_name(a) for a in authorities), "Foreign provider authority")
    # AstraEH: The export provider must stay private and grant access only through selected URIs.
    export = next((p for p in app.findall("provider") if p.get(ANDROID + "name") ==
                   "org.citra.citra_emu.utils.LogFileProvider"), None)
    require(export is not None, "Missing log export provider")
    require(export.get(ANDROID + "authorities") == expected_package + ".logexports", "Wrong export authority")
    require(export.get(ANDROID + "exported") == "false", "Log export provider is publicly exported")
    require(enabled(export, "grantUriPermissions"), "Log export provider cannot share selected files")
    require(any(m.get(ANDROID + "name") == "android.support.FILE_PROVIDER_PATHS" and
                m.get(ANDROID + "resource") for m in export.findall("meta-data")),
            "Missing log export path metadata")
    for permission in root.findall("permission"):
        require(owned_name(permission.get(ANDROID + "name", "")), "Foreign custom permission")
    requested = {p.get(ANDROID + "name") for p in root if p.tag.startswith("uses-permission")}
    require(not (requested - permissions), f"Unreviewed permissions: {requested - permissions}")
    for library in app.findall("uses-library"):
        require(not enabled(library, "required", True), "APK depends on a required external Java library")
    return sorted(requested)


if __name__ == "__main__":
    # CodexAstraLocal: A named opt-in keeps older two-argument validation callers
    # compatible while CI binds profiling readiness to the actual merged APK.
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("version")
    parser.add_argument("--require-shell-profiling", action="store_true")
    # CodexAstraUlt-2: CI binds every identity/authority/permission check to the
    # same intended app, instead of accepting either package opportunistically.
    parser.add_argument("--package", choices=sorted(PACKAGES), default=PACKAGE)
    args = parser.parse_args()
    try:
        permissions = validate(ET.parse(args.manifest).getroot(), args.version,
                               require_shell_profiling=args.require_shell_profiling,
                               expected_package=args.package)
    except (ValueError, ET.ParseError) as error:
        raise SystemExit(str(error))
    print("PASS: standalone non-test-only release APK; identity, SDK, entry point and authorities checked")
    print("PASS: permissions match the reviewed upstream/AndroidX set")
    if args.require_shell_profiling:
        print("PASS: optimized release enables explicit system-shell profiling")
    print("\n".join(permissions))

