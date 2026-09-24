#!/usr/bin/env python3
"""AstraEH: Catch missing default-INI keys, which Config::Reload treats as fatal."""

from pathlib import Path
import re

registry = Path("CMakeModules/GenerateSettingKeys.cmake").read_text()
defaults = Path("src/android/app/src/main/jni/default_ini.h").read_text()


def first_key_block(source):
    # AstraEH: Read the shared/Android lists using the registry's existing foreach format.
    block = re.search(r"foreach\(KEY IN ITEMS(.*?)\n\s*\)", source, re.S)
    assert block, "Setting registry format changed"
    return set(re.findall(r'^\s*"(\w+)"', block[1], re.M))


# AstraEH: Config::Reload accepts either an INI declaration or an explicit omitted-key entry.
keys = first_key_block(registry) | first_key_block(registry.split("# Android exclusive setting keys")[1])
declared = set(re.findall(r"DECLARE_KEY\((\w+)\)", defaults))
omitted = set(re.findall(r"Settings::Keys::(\w+)", defaults))
missing = keys - declared - omitted
if missing:
    raise SystemExit(f"Android would abort at startup; missing default-INI keys: {sorted(missing)}")
print(f"Android default configuration covers all {len(keys)} registered keys")
