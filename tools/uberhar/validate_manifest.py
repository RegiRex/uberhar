#!/usr/bin/env python3
"""AstraEH: Check the final decoded APK manifest for known install/conflict risks."""

from pathlib import Path
import sys
import xml.etree.ElementTree as ET

ANDROID = "{http://schemas.android.com/apk/res/android}"
PACKAGE = "org.uberhar.uberhar_emu"
# AstraEH: Preserve the permissions already present in the pinned app and AndroidX dependencies.
# This gate catches accidental additions; runtime permission grants still belong to the user.
PERMISSIONS = {
    "android.permission.ACCESS_NETWORK_STATE", "android.permission.INTERNET",
    "android.permission.CAMERA", "android.permission.RECORD_AUDIO",
    "android.permission.POST_NOTIFICATIONS", "android.permission.MANAGE_EXTERNAL_STORAGE",
    "android.permission.WRITE_EXTERNAL_STORAGE", "android.permission.READ_EXTERNAL_STORAGE",
    "android.permission.WAKE_LOCK", "android.permission.RECEIVE_BOOT_COMPLETED",
    "android.permission.FOREGROUND_SERVICE",
    PACKAGE + ".DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION",
}


def validate(root, version):
    def require(condition, message):
        if not condition:
            raise ValueError(message)

    def enabled(element, name, default=False):
        return element.get(ANDROID + name, str(default).lower()) not in {"false", "0"}

    # AstraEH: A standalone, separately named package avoids split-install and shared-UID conflicts.
    require(root.get("package") == PACKAGE, "Unexpected package identity")
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
    require(not enabled(app, "isSplitRequired"), "APK requires unavailable splits")
    require(enabled(app, "extractNativeLibs", True), "Native library extraction is required for driver loading")
    require(app.get(ANDROID + "name") == "org.citra.citra_emu.CitraApplication", "Wrong application entry point")
    main = next((a for a in app.findall("activity") if a.get(ANDROID + "name") ==
                 "org.citra.citra_emu.ui.main.MainActivity"), None)
    require(main is not None and enabled(main, "exported"), "Missing exported launcher activity")

    # AstraEH: Provider authorities and custom permissions must not collide with Azahar/other apps.
    for provider in app.findall("provider"):
        authorities = provider.get(ANDROID + "authorities", "").split(";")
        require(all(a.startswith(PACKAGE + ".") for a in authorities), "Foreign provider authority")
    # AstraEH: The export provider must stay private and grant access only through selected URIs.
    export = next((p for p in app.findall("provider") if p.get(ANDROID + "name") ==
                   "org.citra.citra_emu.utils.LogFileProvider"), None)
    require(export is not None, "Missing log export provider")
    require(export.get(ANDROID + "authorities") == PACKAGE + ".logexports", "Wrong export authority")
    require(export.get(ANDROID + "exported") == "false", "Log export provider is publicly exported")
    require(enabled(export, "grantUriPermissions"), "Log export provider cannot share selected files")
    require(any(m.get(ANDROID + "name") == "android.support.FILE_PROVIDER_PATHS" and
                m.get(ANDROID + "resource") for m in export.findall("meta-data")),
            "Missing log export path metadata")
    for permission in root.findall("permission"):
        require(permission.get(ANDROID + "name", "").startswith(PACKAGE + "."), "Foreign custom permission")
    requested = {p.get(ANDROID + "name") for p in root if p.tag.startswith("uses-permission")}
    require(not (requested - PERMISSIONS), f"Unreviewed permissions: {requested - PERMISSIONS}")
    for library in app.findall("uses-library"):
        require(not enabled(library, "required", True), "APK depends on a required external Java library")
    return sorted(requested)


if __name__ == "__main__":
    try:
        permissions = validate(ET.parse(Path(sys.argv[1])).getroot(), sys.argv[2])
    except (ValueError, ET.ParseError) as error:
        raise SystemExit(str(error))
    print("PASS: standalone non-test-only release APK; identity, SDK, entry point and authorities checked")
    print("PASS: permissions match the reviewed upstream/AndroidX set")
    print("\n".join(permissions))
