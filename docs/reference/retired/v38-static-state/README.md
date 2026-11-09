# Retired V38 static state experiment

<!-- CodexAstraLocal: Owner-approved removal; preserve reconstructable source outside runtime and package inputs. -->

Origin: `bdcde19490581fdcc28e7ae221107c8beb16c9d9`, inherited from .57. License: GPLv2 or later, with original notices in affected files. `removal.patch` is the isolated removal diff; apply to the origin tree to remove the option, or reverse-apply to that resulting tree to recover the exact option, JNI keys, Android controls, config plumbing and Turnip capability override. Do not apply blindly to later source.

Retired because earlier Xenoblade v27 standalone/pair tests broadly regressed; no selected optimal profile retained it. This was an optional override of extended dynamic state, not a necessary driver workaround. All original ARM/Qualcomm exclusions, border-color policy and ordinary Vulkan fallback remain.

Old INI keys are ignored, not repurposed for another feature. Restoring requires all native/config/Android generated-key plumbing, test fixture entries, a clean restart before Vulkan instance negotiation and renewed per-driver qualification. No game data or unrelated preferences are removed.
