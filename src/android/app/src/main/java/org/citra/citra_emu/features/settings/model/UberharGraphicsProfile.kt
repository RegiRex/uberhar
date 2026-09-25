// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.features.settings.model.view.SettingsItem

// AstraEH: Read-only presentation of common/uberhar_test_profile.h. The original
// setting objects retain the user's custom values and remain the objects saved to INI.
object UberharGraphicsProfile {
    private val booleans get() = mapOf(
        BooleanSetting.SPIRV_SHADER_GEN to true,
        BooleanSetting.DISABLE_SPIRV_OPTIMIZER to true,
        BooleanSetting.ASYNC_SHADERS to false,
        BooleanSetting.UBERHAR_HYBRID_TEV to true,
        BooleanSetting.UBERHAR_FORCE_TEV to true,
        BooleanSetting.UBERHAR_CPU_VERTEX_BRIDGE to false,
        BooleanSetting.LINEAR_FILTERING to true,
        BooleanSetting.SHADERS_ACCURATE_MUL to true,
        BooleanSetting.DISK_SHADER_CACHE to true,
        BooleanSetting.DISABLE_RIGHT_EYE_RENDER to false,
        BooleanSetting.SWAP_EYES_3D to false,
        BooleanSetting.DUMP_TEXTURES to false,
        BooleanSetting.CUSTOM_TEXTURES to false,
        BooleanSetting.ASYNC_CUSTOM_LOADING to true,
        BooleanSetting.USE_SKIP_DUPLICATE_FRAMES to false
    )
    private val integers get() = mapOf(
        IntSetting.GRAPHICS_API to 2,
        IntSetting.TEXTURE_FILTER to 0,
        IntSetting.TEXTURE_SAMPLING to 0,
        IntSetting.DELAY_RENDER_THREAD_US to 0,
        IntSetting.RENDER_3D_WHICH_DISPLAY to 0,
        IntSetting.STEREOSCOPIC_3D_MODE to 0,
        IntSetting.STEREOSCOPIC_3D_DEPTH to 0
    )

    fun applyTo(item: SettingsItem) {
        when (val original = item.setting) {
            is BooleanSetting -> booleans[original]?.let { effective ->
                item.setting = object : AbstractBooleanSetting by original {
                    override var boolean: Boolean
                        get() = effective
                        set(value) { /* AstraEH: Locked display; never overwrite custom values. */ }
                    override val isRuntimeEditable = false
                }
            }
            is IntSetting -> integers[original]?.let { effective ->
                item.setting = object : AbstractIntSetting by original {
                    override var int: Int
                        get() = effective
                        set(value) { /* AstraEH: Locked display; never overwrite custom values. */ }
                    override val isRuntimeEditable = false
                }
            }
            else -> Unit
        }
    }
}
