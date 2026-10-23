// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>
#include "common/settings.h"

class INIReader;

class Config {
private:
    std::unique_ptr<INIReader> android_config;
    // CodexAstraLocal: Sparse title values are read-only and never merged into the global file.
    std::unique_ptr<INIReader> title_config;
    u64 title_id{};
    bool runtime_reload{};
    bool HasTitleValue(const std::string& group, const std::string& key) const;
    bool KeepRunningValue(const std::string& key) const;
    void ApplyTestingOverlay();
    std::string android_config_loc;

    bool LoadINI(const std::string& default_contents = "", bool retry = true);
    void ReadValues();

public:
    explicit Config(u64 program_id = 0);
    ~Config();

    void Reload();

private:
    /**
     * Applies a value read from the android_config to a Setting.
     *
     * @param group The name of the INI group
     * @param setting The yuzu setting to modify
     */
    template <typename Type, bool ranged>
    void ReadSetting(const std::string& group, Settings::Setting<Type, ranged>& setting);

    template <typename Type, bool ranged>
    void ReadSetting(const std::string& group, Settings::SwitchableSetting<Type, ranged>& setting);
    template <typename Type, bool ranged>
    void ReadSetting(const std::string& group, Settings::SwitchableSetting<Type, ranged>& setting,
                     const Type& android_default);
};
