# Dual-display follow-up

Status: source audit and proposed investigation, not implemented in alpha 1.
Device diagnosis still requires an Ayn Thor capture. Complete the shader alpha
comparison before changing presentation policy.

## What the pinned source does

- `GPU::VBlankCallback` in `src/video_core/gpu.cpp` signals both PDC0 and PDC1
  and calls the renderer once per emulated display event.
- `RendererVulkan::SwapBuffers` prepares the top-left, top-right and bottom
  screen textures together, then calls `RenderToWindow` for the main and
  secondary windows in sequence.
- Each `PresentWindow` has its own frame pool, presentation queue, worker
  thread and swapchain. A `Frame` contains synchronization objects but no
  emulated presentation ID.
- `CopyToSwapchain` shares the scheduler's submission mutex, but each
  `Swapchain::Present` submits a separate one-swapchain presentation request.
- Each swapchain chooses a present mode and image count from its surface's
  capabilities. There is no explicit coordinator that keeps the two queues
  at the same emulated presentation event.

This makes independent host queue latency a plausible source of visible
screen mismatch. It does not establish the cause on Thor. Different game-side
update rates, Android composition and physical panel timing must be separated.

## First instrument, then coordinate

Add one monotonic ID per renderer presentation event and copy it into both
host frames. Record each display's enqueue, dequeue, copy submission and
presentation-request timestamps, queue depth, present mode, refresh information
and surface-generation ID. Use bounded telemetry and summarize outside the hot
path. GPU completion fences and presentation-request timestamps are not proof
of the instant pixels appeared on a panel.

Overlay the event ID on both displays for a high-speed camera recording. An
ordinary emulator screenshot can hide an offset introduced after rendering.
Start with Kid Icarus, then test a title with a mostly static bottom screen.
Preserve legitimate game behavior: pair the framebuffer snapshot for the same
emulated event, rather than waiting for both framebuffer contents to change.

If the trace shows independent queue backlog, prototype a paired coordinator:
reserve both frames, prepare both outputs, then release them under one bounded
presentation policy. Evaluate a common presentation request where supported.
Handle surface loss, app suspension, display detachment and layout changes
without leaving one display waiting indefinitely for a missing partner.
Measure input latency and frame pacing as well as the frame-ID difference.

## Limits of the guarantee

The Vulkan specification assigns exact presentation timing to the native
platform and presentation engine; queueing a presentation does not encompass
the engine's actual processing of the image. Therefore matching software IDs
or grouping requests cannot by itself prove simultaneous refresh of two
physical panels. The initial goal is to eliminate avoidable software backlog
and measure the remaining display offset.

Reference: [Khronos, vkQueuePresentKHR](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html),
retrieved 2026-09-24. The source findings above refer to the pinned Azahar
2126.1.2 tree and are separate from claims about Nintendo hardware.
