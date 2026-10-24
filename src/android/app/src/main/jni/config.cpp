// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <iomanip>
#include <memory>
#include <ranges>
#include <sstream>
#include <unordered_map>
#include <INIReader.h>
#include <boost/hana/string.hpp>
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "common/param_package.h"
#include "common/setting_keys.h"
#include "common/settings.h"
#include "common/uberhar_test_profile.h" // AstraEH: Temporary renderer experiment overrides.
#include "core/core.h"
#include "core/hle/service/cfg/cfg.h"
#include "core/hle/service/service.h"
#include "input_common/main.h"
#include "input_common/udp/client.h"
#include "jni/camera/ndk_camera.h"
#include "jni/config.h"
#include "jni/default_ini.h"
#include "jni/input_manager.h"

Config::Config(u64 program_id) : title_id{program_id},
                                 runtime_reload{Core::System::GetInstance().IsPoweredOn()} {
    // TODO: Don't hardcode the path; let the frontend decide where to put the config files.
    android_config_loc = FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "config.ini";
    std::string ini_buffer;
    FileUtil::ReadFileToString(true, android_config_loc, ini_buffer);
    if (!ini_buffer.empty()) {
        android_config = std::make_unique<INIReader>(ini_buffer.c_str(), ini_buffer.size());
    }

    // CodexAstraLocal: Missing title settings inherit globals; malformed overrides fail closed.
    if (title_id != 0) {
        const auto path = FileUtil::GetUserPath(FileUtil::UserPath::UserDir) +
                          fmt::format("GameSettings/{:016X}.ini", title_id);
        std::string title_buffer;
        if (FileUtil::ReadFileToString(true, path, title_buffer) && !title_buffer.empty()) {
            auto parsed = std::make_unique<INIReader>(title_buffer.c_str(), title_buffer.size());
            if (parsed->ParseError() == 0) {
                title_config = std::move(parsed);
                LOG_INFO(Config, "Loaded per-game settings for {:016X}", title_id);
            } else {
                LOG_ERROR(Config, "Ignoring malformed per-game settings for {:016X}", title_id);
            }
        }
    }
    Reload();
}

Config::~Config() = default;

bool Config::LoadINI(const std::string& default_contents, bool retry) {
    const std::string& location = this->android_config_loc;
    if (android_config == nullptr || android_config->ParseError() < 0) {
        if (retry) {
            LOG_WARNING(Config, "Failed to load {}. Creating file from defaults...", location);
            FileUtil::CreateFullPath(location);
            FileUtil::WriteStringToFile(true, location, default_contents);
            std::string ini_buffer;
            FileUtil::ReadFileToString(true, location, ini_buffer);
            android_config =
                std::make_unique<INIReader>(ini_buffer.c_str(), ini_buffer.size()); // Reopen file

            return LoadINI(default_contents, false);
        }
        LOG_ERROR(Config, "Failed.");
        return false;
    }
    LOG_INFO(Config, "Successfully loaded {}", location);
    return true;
}

static const std::array<int, Settings::NativeButton::NumButtons> default_buttons = {
    InputManager::N3DS_BUTTON_A,     InputManager::N3DS_BUTTON_B,
    InputManager::N3DS_BUTTON_X,     InputManager::N3DS_BUTTON_Y,
    InputManager::N3DS_DPAD_UP,      InputManager::N3DS_DPAD_DOWN,
    InputManager::N3DS_DPAD_LEFT,    InputManager::N3DS_DPAD_RIGHT,
    InputManager::N3DS_TRIGGER_L,    InputManager::N3DS_TRIGGER_R,
    InputManager::N3DS_BUTTON_START, InputManager::N3DS_BUTTON_SELECT,
    InputManager::N3DS_BUTTON_DEBUG, InputManager::N3DS_BUTTON_GPIO14,
    InputManager::N3DS_BUTTON_ZL,    InputManager::N3DS_BUTTON_ZR,
    InputManager::N3DS_BUTTON_HOME,
};

static const std::array<int, Settings::NativeAnalog::NumAnalogs> default_analogs{{
    InputManager::N3DS_CIRCLEPAD,
    InputManager::N3DS_STICK_C,
}};

// CodexAstraLocal: Typed overlay parsing uses inherited values as fallbacks for malformed entries.
namespace {
// CodexAstraLocal: Keep an active testing launch stable when an editor changes next-launch preferences.
bool testing_overlay_active{};
long testing_overlay_preset{};

template <typename Type>
Type ReadConfigValue(const INIReader& reader, const std::string& group,
                     const std::string& key, const Type& fallback) {
    if constexpr (std::is_same_v<Type, std::string>) {
        return reader.GetString(group, key, fallback);
    } else if constexpr (std::is_same_v<Type, bool>) {
        return reader.GetBoolean(group, key, fallback);
    } else if constexpr (std::is_floating_point_v<Type>) {
        return static_cast<Type>(reader.GetReal(group, key, fallback));
    } else {
        return static_cast<Type>(reader.GetInteger(group, key, static_cast<long>(fallback)));
    }
}
} // namespace

bool Config::HasTitleValue(const std::string& group, const std::string& key) const {
    // CodexAstraLocal: Device identity, storage, networking and debug configuration stay global.
    const bool allowed = group == "Core" || group == "Renderer" || group == "Audio" ||
                         group == "Layout" || group == "Utility";
    return allowed && key != Settings::Keys::turbo_limit && title_config &&
           title_config->HasValue(group, key);
}

bool Config::KeepRunningValue(const std::string& key) const {
    // CodexAstraLocal: Owners of these resources are created once; settings edits apply next launch.
    static constexpr std::array keys{
        Settings::Keys::graphics_api,
        Settings::Keys::uberhar_test_mode,
        Settings::Keys::uberhar_cpu_vertex_only,
        Settings::Keys::uberhar_native_full_fragments,
        Settings::Keys::uberhar_cpu_prefix_overlap,
        Settings::Keys::uberhar_parallel_texture_decode,
        Settings::Keys::uberhar_cpu_execution_census,
        Settings::Keys::turnip_pass_end_flush,
        Settings::Keys::uberhar_optimize_generic_shaders,
        Settings::Keys::uberhar_hybrid_tev,
        Settings::Keys::uberhar_force_tev,
        Settings::Keys::uberhar_cpu_vertex_bridge,
        Settings::Keys::async_shader_compilation,
        Settings::Keys::async_presentation,
        Settings::Keys::spirv_shader_gen,
        Settings::Keys::disable_spirv_optimizer,
        Settings::Keys::use_hw_shader,
        Settings::Keys::use_shader_jit,
        Settings::Keys::use_cpu_jit,
        Settings::Keys::shaders_accurate_mul,
        Settings::Keys::use_vsync,
        Settings::Keys::software_renderer_workers,
    };
    // CodexAstraLocal: Preserve the live resolution until a valid Testing overlay replaces it.
    return runtime_reload && (std::ranges::find(keys, key) != keys.end() ||
                             (key == Settings::Keys::resolution_factor && testing_overlay_active));
}

template <typename Type, bool ranged>
void Config::ReadSetting(const std::string& group, Settings::Setting<Type, ranged>& setting) {
    if (KeepRunningValue(setting.GetLabel())) return;
    Type value = ReadConfigValue(*android_config, group, setting.GetLabel(), setting.GetDefault());
    if (HasTitleValue(group, setting.GetLabel())) {
        value = ReadConfigValue(*title_config, group, setting.GetLabel(), value);
    }
    setting = value;
}

template <typename Type, bool ranged>
void Config::ReadSetting(const std::string& group, Settings::SwitchableSetting<Type, ranged>& setting) {
    ReadSetting(group, setting, setting.GetDefault());
}

template <typename Type, bool ranged>
void Config::ReadSetting(const std::string& group, Settings::SwitchableSetting<Type, ranged>& setting,
                         const Type& android_default) {
    if (KeepRunningValue(setting.GetLabel())) return;
    setting.SetGlobal(true);
    setting = ReadConfigValue(*android_config, group, setting.GetLabel(), android_default);
    if (HasTitleValue(group, setting.GetLabel())) {
        const auto inherited = setting.GetValue();
        setting.SetGlobal(false);
        setting = ReadConfigValue(*title_config, group, setting.GetLabel(), inherited);
    }
}

void Config::ApplyTestingOverlay() {
    // CodexAstraLocal: Renderer/master choices are fixed for a launch. Resolution
    // can reload from the same title's Testing overlay through normal ApplySettings.
    if (!runtime_reload) testing_overlay_active = false;
    if (title_id == 0 || (runtime_reload && !testing_overlay_active)) return;
    std::string buffer;
    const auto path = FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "testing.ini";
    if (!FileUtil::ReadFileToString(true, path, buffer) || buffer.empty()) return;
    const INIReader testing{buffer.c_str(), buffer.size()};
    if (testing.ParseError() != 0 ||
        (!runtime_reload && !testing.GetBoolean("Testing", "enabled", false))) return;
    std::ostringstream section_name;
    section_name << "Testing." << std::uppercase << std::hex << std::setw(16)
                 << std::setfill('0') << title_id;
    const auto section = section_name.str();
    // CodexAstraLocal: Match Kotlin's validated fallback chain for malformed title/default entries.
    const auto valid_preset = [](long value) { return value >= 0 && value <= 2; };
    const auto valid_resolution = [](long value) { return value == 2 || value == 4; };
    auto global_preset = testing.GetInteger("Testing", "preset", 0);
    auto global_resolution = testing.GetInteger("Testing", "resolution", 2);
    if (!valid_preset(global_preset)) global_preset = 0;
    if (!valid_resolution(global_resolution)) global_resolution = 2;
    auto preset = testing.GetInteger(section, "preset", global_preset);
    auto resolution = testing.GetInteger(section, "resolution", global_resolution);
    if (!valid_preset(preset)) preset = global_preset;
    if (!valid_resolution(resolution)) resolution = global_resolution;
    if (!runtime_reload) {
        testing_overlay_active = true;
        testing_overlay_preset = preset;
        Settings::values.uberhar_test_mode = preset == 0 ? Settings::UberharTestMode::Native :
                                                          Settings::UberharTestMode::Automatic;
        Settings::values.uberhar_cpu_vertex_only = preset == 1;
    }
    Settings::values.resolution_factor = static_cast<u16>(resolution);
    LOG_INFO(Config, "Testing overlay: title={:016X} preset={} resolution={}x live={}", title_id,
             testing_overlay_preset, resolution, runtime_reload);
}

void Config::ReadValues() {
    // Controls
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        std::string default_param = InputManager::GenerateButtonParamPackage(default_buttons[i]);
        Settings::values.current_input_profile.buttons[i] = android_config->GetString(
            "Controls", Settings::NativeButton::mapping[i], default_param);
        if (Settings::values.current_input_profile.buttons[i].empty())
            Settings::values.current_input_profile.buttons[i] = default_param;
    }

    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        std::string default_param = InputManager::GenerateAnalogParamPackage(default_analogs[i]);
        Settings::values.current_input_profile.analogs[i] = android_config->GetString(
            "Controls", Settings::NativeAnalog::mapping[i], default_param);
        if (Settings::values.current_input_profile.analogs[i].empty())
            Settings::values.current_input_profile.analogs[i] = default_param;
    }

    Settings::values.current_input_profile.motion_device = android_config->GetString(
        "Controls", "motion_device",
        "engine:motion_emu,update_period:100,sensitivity:0.01,tilt_clamp:90.0");
    Settings::values.current_input_profile.touch_device =
        android_config->GetString("Controls", "touch_device", "engine:emu_window");
    Settings::values.current_input_profile.udp_input_address = android_config->GetString(
        "Controls", "udp_input_address", InputCommon::CemuhookUDP::DEFAULT_ADDR);
    Settings::values.current_input_profile.udp_input_port =
        static_cast<u16>(android_config->GetInteger("Controls", "udp_input_port",
                                                    InputCommon::CemuhookUDP::DEFAULT_PORT));

    ReadSetting("Controls", Settings::values.use_artic_base_controller);

    // Core
    ReadSetting("Core", Settings::values.use_cpu_jit);
    ReadSetting("Core", Settings::values.cpu_clock_percentage);

    // Renderer
    Settings::values.use_gles = android_config->GetBoolean("Renderer", "use_gles", true);
    ReadSetting("Renderer", Settings::values.shaders_accurate_mul, false);
    ReadSetting("Renderer", Settings::values.graphics_api);
    // CodexAstraLocal: Retired Android Software selection migrates without changing saves or resolution.
    if (Settings::values.graphics_api.GetValue() == Settings::GraphicsAPI::Software) {
        Settings::values.graphics_api = Settings::GraphicsAPI::Vulkan;
        LOG_WARNING(Config, "Retired Software graphics API migrated to Vulkan");
    }
    // CodexAstraLocal: Reload the persisted restart-only CPU raster budget.
    ReadSetting("Renderer", Settings::values.software_renderer_workers);
    ReadSetting("Renderer", Settings::values.async_presentation);
    ReadSetting("Renderer", Settings::values.async_shader_compilation);
    // AstraEH: Load the same opt-in flags exposed in Android's Graphics settings.
    ReadSetting("Renderer", Settings::values.uberhar_hybrid_tev);
    ReadSetting("Renderer", Settings::values.uberhar_force_tev);
    // AstraEH: Share the bridge switch between global and per-game settings.
    ReadSetting("Renderer", Settings::values.uberhar_cpu_vertex_bridge);
    // CodexAstraLocal: Apply saved placement before constructing the PICA owner.
    ReadSetting("Renderer", Settings::values.uberhar_cpu_vertex_only);
    // CodexAstraLocal: Load comparison controls before constructing renderer owners.
    ReadSetting("Renderer", Settings::values.uberhar_native_full_fragments);
    // CodexAstraLocal: Resolve title/global driver policy before renderer construction.
    ReadSetting("Renderer", Settings::values.turnip_pass_end_flush);
    ReadSetting("Renderer", Settings::values.uberhar_optimize_generic_shaders);
    ReadSetting("Renderer", Settings::values.uberhar_cpu_prefix_overlap);
    ReadSetting("Renderer", Settings::values.uberhar_parallel_texture_decode);
    // CodexAstraLocal: Freeze optional census before any worker owner is constructed.
    ReadSetting("Renderer", Settings::values.uberhar_cpu_execution_census);

    // AstraEH: Read the profile before applying its temporary in-memory overrides.
    ReadSetting("Renderer", Settings::values.uberhar_test_mode);
    // CodexAstraLocal: Preserve old saved IDs as explicit migration inputs.
    const auto saved_mode = Settings::values.uberhar_test_mode.GetValue();
    const auto migrated_mode = Settings::MigrateUberharTestMode(saved_mode);
    if (saved_mode != migrated_mode) {
        LOG_WARNING(Config, "Retired renderer mode {} migrated to {}", static_cast<u32>(saved_mode),
                    static_cast<u32>(migrated_mode));
        Settings::values.uberhar_test_mode = migrated_mode;
    }
    ReadSetting("Renderer", Settings::values.spirv_shader_gen);
    ReadSetting("Renderer", Settings::values.disable_spirv_optimizer);
    ReadSetting("Renderer", Settings::values.use_hw_shader);
    ReadSetting("Renderer", Settings::values.use_shader_jit);
    ReadSetting("Renderer", Settings::values.resolution_factor);
    ReadSetting("Renderer", Settings::values.use_disk_shader_cache);
    ReadSetting("Renderer", Settings::values.use_vsync);
    ReadSetting("Renderer", Settings::values.use_skip_duplicate_frames);
    ReadSetting("Renderer", Settings::values.texture_filter);
    ReadSetting("Renderer", Settings::values.texture_sampling);
    ReadSetting("Renderer", Settings::values.turbo_limit);
    // Workaround to map Android setting for enabling the frame limiter to the format Citra expects
    // CodexAstraLocal: The legacy limiter switch follows the same title inheritance as its cap.
    const bool global_limiter = android_config->GetBoolean("Renderer", "use_frame_limit", true);
    const bool use_limiter =
        HasTitleValue("Renderer", "use_frame_limit")
            ? title_config->GetBoolean("Renderer", "use_frame_limit", global_limiter)
            : global_limiter;
    ReadSetting("Renderer", Settings::values.frame_limit);
    if (!use_limiter) {
        Settings::values.frame_limit = 0;
    }

    ReadSetting("Renderer", Settings::values.render_3d);
    ReadSetting("Renderer", Settings::values.factor_3d);
    std::string default_shader = "None (builtin)";
    if (Settings::values.render_3d.GetValue() == Settings::StereoRenderOption::Anaglyph)
        default_shader = "Dubois (builtin)";
    else if (Settings::values.render_3d.GetValue() == Settings::StereoRenderOption::Interlaced)
        default_shader = "Horizontal (builtin)";
    Settings::values.pp_shader_name =
        android_config->GetString("Renderer", "pp_shader_name", default_shader);
    ReadSetting("Renderer", Settings::values.filter_mode);
    ReadSetting("Renderer", Settings::values.use_integer_scaling);

    ReadSetting("Renderer", Settings::values.bg_red);
    ReadSetting("Renderer", Settings::values.bg_green);
    ReadSetting("Renderer", Settings::values.bg_blue);
    ReadSetting("Renderer", Settings::values.custom_second_layer_opacity);
    ReadSetting("Renderer", Settings::values.delay_game_render_thread_us);
    ReadSetting("Renderer", Settings::values.simulate_3ds_gpu_timings);
    ReadSetting("Renderer", Settings::values.disable_right_eye_render);
    ReadSetting("Renderer", Settings::values.swap_eyes_3d);
    ReadSetting("Renderer", Settings::values.render_3d_which_display);
    // Layout
    // Somewhat inelegant solution to ensure layout value is between 0 and 5 on read
    // since older config files may have other values
    // CodexAstraLocal: Use the same typed overlay path for layout enum and scalar preferences.
    ReadSetting("Layout", Settings::values.layout_option, Settings::LayoutOption::LargeScreen);
    if (static_cast<int>(Settings::values.layout_option.GetValue()) < 0 ||
        static_cast<int>(Settings::values.layout_option.GetValue()) > 5) {
        Settings::values.layout_option = Settings::LayoutOption::LargeScreen;
    }
    ReadSetting("Layout", Settings::values.large_screen_proportion, 2.25f);
    ReadSetting("Layout", Settings::values.small_screen_position,
                Settings::SmallScreenPosition::TopRight);
    ReadSetting("Layout", Settings::values.screen_gap);
    ReadSetting("Layout", Settings::values.custom_top_x);
    ReadSetting("Layout", Settings::values.custom_top_y);
    ReadSetting("Layout", Settings::values.custom_top_width);
    ReadSetting("Layout", Settings::values.custom_top_height);
    ReadSetting("Layout", Settings::values.custom_bottom_x);
    ReadSetting("Layout", Settings::values.custom_bottom_y);
    ReadSetting("Layout", Settings::values.custom_bottom_width);
    ReadSetting("Layout", Settings::values.aspect_ratio);
    ReadSetting("Layout", Settings::values.custom_bottom_height);
    ReadSetting("Layout", Settings::values.cardboard_screen_size);
    ReadSetting("Layout", Settings::values.cardboard_x_shift);
    ReadSetting("Layout", Settings::values.cardboard_y_shift);
    ReadSetting("Layout", Settings::values.upright_screen);

    ReadSetting("Layout", Settings::values.portrait_layout_option);
    ReadSetting("Layout", Settings::values.secondary_display_layout,
                Settings::SecondaryDisplayLayout::None);
    ReadSetting("Layout", Settings::values.custom_portrait_top_x);
    ReadSetting("Layout", Settings::values.custom_portrait_top_y);
    ReadSetting("Layout", Settings::values.custom_portrait_top_width);
    ReadSetting("Layout", Settings::values.custom_portrait_top_height);
    ReadSetting("Layout", Settings::values.custom_portrait_bottom_x);
    ReadSetting("Layout", Settings::values.custom_portrait_bottom_y);
    ReadSetting("Layout", Settings::values.custom_portrait_bottom_width);
    ReadSetting("Layout", Settings::values.custom_portrait_bottom_height);

    // Storage
    ReadSetting("Storage", Settings::values.compress_cia_installs);
    ReadSetting("Storage", Settings::values.async_fs_operations);

    // Utility
    ReadSetting("Utility", Settings::values.dump_textures);
    ReadSetting("Utility", Settings::values.custom_textures);
    ReadSetting("Utility", Settings::values.preload_textures);
    ReadSetting("Utility", Settings::values.async_custom_loading);

    // Audio
    ReadSetting("Audio", Settings::values.audio_emulation);
    ReadSetting("Audio", Settings::values.enable_audio_stretching);
    ReadSetting("Audio", Settings::values.enable_realtime_audio);
    ReadSetting("Audio", Settings::values.simulate_headphones_plugged);
    ReadSetting("Audio", Settings::values.volume);
    ReadSetting("Audio", Settings::values.output_type);
    ReadSetting("Audio", Settings::values.output_device);
    ReadSetting("Audio", Settings::values.input_type);
    ReadSetting("Audio", Settings::values.input_device);

    // Data Storage
    ReadSetting("Data Storage", Settings::values.use_virtual_sd);

    // System
    ReadSetting("System", Settings::values.is_new_3ds);
    ReadSetting("System", Settings::values.lle_applets);
    ReadSetting("System", Settings::values.enable_required_online_lle_modules);
    ReadSetting("System", Settings::values.region_value);
    ReadSetting("System", Settings::values.init_clock);
    {
        std::string time =
            android_config->GetString("System", Settings::HKeys::init_time.c_str(), "946681277");
        try {
            Settings::values.init_time = std::stoll(time);
        } catch (...) {
        }
    }
    ReadSetting("System", Settings::values.init_ticks_type);
    ReadSetting("System", Settings::values.init_ticks_override);
    ReadSetting("System", Settings::values.plugin_loader_enabled);
    ReadSetting("System", Settings::values.allow_plugin_loader);
    ReadSetting("System", Settings::values.steps_per_hour);
    ReadSetting("System", Settings::values.apply_region_free_patch);

    // Camera
    using namespace Service::CAM;
    Settings::values.camera_name[OuterRightCamera] = android_config->GetString(
        "Camera", Settings::HKeys::camera_outer_right_name.c_str(), "ndk");
    Settings::values.camera_config[OuterRightCamera] =
        android_config->GetString("Camera", Settings::HKeys::camera_outer_right_config.c_str(),
                                  std::string{Camera::NDK::BackCameraPlaceholder});
    Settings::values.camera_flip[OuterRightCamera] =
        android_config->GetInteger("Camera", Settings::HKeys::camera_outer_right_flip.c_str(), 0);
    Settings::values.camera_name[InnerCamera] =
        android_config->GetString("Camera", Settings::HKeys::camera_inner_name.c_str(), "ndk");
    Settings::values.camera_config[InnerCamera] =
        android_config->GetString("Camera", Settings::HKeys::camera_inner_config.c_str(),
                                  std::string{Camera::NDK::FrontCameraPlaceholder});
    Settings::values.camera_flip[InnerCamera] =
        android_config->GetInteger("Camera", Settings::HKeys::camera_inner_flip.c_str(), 0);
    Settings::values.camera_name[OuterLeftCamera] =
        android_config->GetString("Camera", Settings::HKeys::camera_outer_left_name.c_str(), "ndk");
    Settings::values.camera_config[OuterLeftCamera] =
        android_config->GetString("Camera", Settings::HKeys::camera_outer_left_config.c_str(),
                                  std::string{Camera::NDK::BackCameraPlaceholder});
    Settings::values.camera_flip[OuterLeftCamera] =
        android_config->GetInteger("Camera", Settings::HKeys::camera_outer_left_flip.c_str(), 0);

    // Miscellaneous
    ReadSetting("Miscellaneous", Settings::values.log_filter);
    ReadSetting("Miscellaneous", Settings::values.log_regex_filter);

    // Apply the log_filter setting as the logger has already been initialized
    // and doesn't pick up the filter on its own.
    Common::Log::Filter filter;
    filter.ParseFilterString(Settings::values.log_filter.GetValue());
    Common::Log::SetGlobalFilter(filter);
    Common::Log::SetRegexFilter(Settings::values.log_regex_filter.GetValue());

    // Debugging
    Settings::values.record_frame_times =
        android_config->GetBoolean("Debugging", Settings::HKeys::record_frame_times.c_str(), false);
    ReadSetting("Debugging", Settings::values.renderer_debug);
    ReadSetting("Debugging", Settings::values.pica_debugging);
    ReadSetting("Debugging", Settings::values.use_gdbstub);
    ReadSetting("Debugging", Settings::values.gdbstub_port);
    ReadSetting("Debugging", Settings::values.instant_debug_log);
    // AstraEH: Read once for startup recovery; this does not enable a resident monitor.
    ReadSetting("Debugging", Settings::values.android_crash_reports);
    ReadSetting("Debugging", Settings::values.enable_rpc_server);
    ReadSetting("Debugging", Settings::values.toggle_unique_data_console_type);
    ReadSetting("Debugging", Settings::values.enable_exception_handler);

    for (const auto& service_module : Service::service_module_map) {
        bool use_lle =
            android_config->GetBoolean("Debugging", "LLE\\" + service_module.name, false);
        Settings::values.lle_modules.emplace(service_module.name, use_lle);
    }

    // Web Service
    ReadSetting("WebService", Settings::values.web_api_url);
    ReadSetting("WebService", Settings::values.network_token);

    // AstraEH: Apply last, after reloading every saved setting; never write these overrides to INI.
    ApplyTestingOverlay();
    Settings::ApplyUberharTestProfile();
}

void Config::Reload() {
    for (auto key = Settings::Keys::keys_array.begin(); key != Settings::Keys::keys_array.end();
         ++key) {
        const auto key_declaration_string = std::string(*key) + " =";
        if ((std::ranges::find(DefaultINI::android_config_omitted_keys, std::string_view(*key)) ==
             std::end(DefaultINI::android_config_omitted_keys)) &&
            (std::string_view(DefaultINI::android_config_default_file_content)
                 .find(key_declaration_string) == std::string_view::npos)) {
            ASSERT_MSG(false,
                       "Validation of default config content (jni/default_ini.h) failed: Missing "
                       "declaration for key '{}'",
                       *key);
        }
    }
    LoadINI(DefaultINI::android_config_default_file_content);
    ReadValues();
}
