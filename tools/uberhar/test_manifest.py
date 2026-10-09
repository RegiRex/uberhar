#!/usr/bin/env python3
"""AstraEH: Regression cases for installation flags and cross-app manifest conflicts."""

import unittest
import xml.etree.ElementTree as ET

from validate_manifest import ANDROID, PACKAGE, validate


class ManifestValidationTest(unittest.TestCase):
    def setUp(self):
        self.root = ET.fromstring(f"""
            <manifest xmlns:android="http://schemas.android.com/apk/res/android"
                package="{PACKAGE}" android:versionName="0.0.2">
                <uses-sdk android:minSdkVersion="29" android:targetSdkVersion="37"/>
                <application android:name="org.citra.citra_emu.CitraApplication"
                    android:extractNativeLibs="true">
                    <activity android:name="org.citra.citra_emu.ui.main.MainActivity"
                        android:exported="true"/>
                    <provider android:name="org.citra.citra_emu.utils.LogFileProvider"
                        android:authorities="{PACKAGE}.logexports"
                        android:exported="false" android:grantUriPermissions="true">
                        <meta-data android:name="android.support.FILE_PROVIDER_PATHS"
                            android:resource="@xml/log_export_paths"/>
                    </provider>
                </application>
            </manifest>
        """)
        self.app = self.root.find("application")

    def test_standalone_release_is_accepted(self):
        self.assertEqual(validate(self.root, "0.0.2"), [])

    # CodexAstraLocal: Current packaging requires profiling without allowing a
    # debuggable build; historical manifests still use the original contract.
    def test_requested_shell_profiling_requires_enabled_merged_flag(self):
        with self.assertRaisesRegex(ValueError, "shell profiling"):
            validate(self.root, "0.0.2", require_shell_profiling=True)
        profile = ET.SubElement(self.app, "profileable", {ANDROID + "shell": "true"})
        self.assertEqual(validate(self.root, "0.0.2", require_shell_profiling=True), [])
        for field in ["shell", "enabled"]:
            # CodexAstraLocal: Reject unresolved/resource values as well as
            # explicit false; exercise both known-true decoded spellings.
            for value in ["false", "0", "", "unknown", "@bool/profile_enabled"]:
                with self.subTest(field=field, value=value):
                    profile.set(ANDROID + field, value)
                    with self.assertRaisesRegex(ValueError, "shell profiling"):
                        validate(self.root, "0.0.2", require_shell_profiling=True)
            for value in ["true", "1"]:
                profile.set(ANDROID + field, value)
                self.assertEqual(validate(self.root, "0.0.2", require_shell_profiling=True), [])
        # CodexAstraLocal: Conflicting duplicate declarations must not pass by
        # selecting the first flag, even when both individual flags are true.
        duplicate = ET.SubElement(self.app, "profileable", {ANDROID + "shell": "true"})
        with self.assertRaisesRegex(ValueError, "shell profiling"):
            validate(self.root, "0.0.2", require_shell_profiling=True)
        self.app.remove(duplicate)
        self.app.set(ANDROID + "debuggable", "true")
        with self.assertRaisesRegex(ValueError, "non-debuggable"):
            validate(self.root, "0.0.2", require_shell_profiling=True)

    def test_test_only_and_debug_and_split_flags_are_rejected(self):
        for flag in ["testOnly", "debuggable", "isSplitRequired"]:
            with self.subTest(flag=flag):
                self.app.set(ANDROID + flag, "true")
                with self.assertRaises(ValueError):
                    validate(self.root, "0.0.2")
                self.app.attrib.pop(ANDROID + flag)

    def test_wrong_version_and_shared_identity_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "Version"):
            validate(self.root, "0.0.3")
        self.root.set(ANDROID + "sharedUserId", "org.azahar_emu.azahar")
        with self.assertRaisesRegex(ValueError, "Shared"):
            validate(self.root, "0.0.2")

    def test_provider_collision_is_rejected(self):
        ET.SubElement(self.app, "provider", {ANDROID + "authorities": "org.azahar_emu.azahar.files"})
        with self.assertRaisesRegex(ValueError, "authority"):
            validate(self.root, "0.0.2")

    def test_new_permission_is_rejected(self):
        ET.SubElement(self.root, "uses-permission", {ANDROID + "name": "android.permission.READ_CONTACTS"})
        with self.assertRaisesRegex(ValueError, "permissions"):
            validate(self.root, "0.0.2")

    # AstraEH: A broken export provider must fail before an APK can be published.
    def test_log_provider_must_be_private_and_grant_selected_uris(self):
        provider = self.app.find("provider")
        for field, value in [("exported", "true"), ("grantUriPermissions", "false")]:
            with self.subTest(field=field):
                previous = provider.get(ANDROID + field)
                provider.set(ANDROID + field, value)
                with self.assertRaises(ValueError):
                    validate(self.root, "0.0.2")
                provider.set(ANDROID + field, previous)
        self.app.remove(provider)
        with self.assertRaisesRegex(ValueError, "Missing log"):
            validate(self.root, "0.0.2")

    def test_export_paths_are_limited_to_log_snapshots(self):
        from pathlib import Path
        root = Path(__file__).resolve().parents[2]
        paths = ET.parse(root / "src/android/app/src/main/res/xml/log_export_paths.xml").getroot()
        self.assertEqual(len(paths), 1)
        self.assertEqual(paths[0].tag, "cache-path")
        self.assertEqual(paths[0].get("path"), "log_exports/")

    def test_required_external_library_is_rejected(self):
        library = ET.SubElement(self.app, "uses-library", {ANDROID + "name": "third.party.library"})
        with self.assertRaisesRegex(ValueError, "external Java library"):
            validate(self.root, "0.0.2")
        library.set(ANDROID + "required", "false")
        validate(self.root, "0.0.2")


if __name__ == "__main__":
    unittest.main()
