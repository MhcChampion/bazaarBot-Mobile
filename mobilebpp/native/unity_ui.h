// Unity-UI fallback renderer.
//
// The ImGui overlay needs an EGL/GLES swap; if the game renders with Vulkan that
// path never runs. This module instead clones an existing TextMeshPro label that
// the game already renders, reparents it to the root canvas and drives its text.
// It only uses the engine's own UI stack, so it works under GLES and Vulkan.
//
// Opt-in: create files/bpp_unityui. Every step is logged so a failure is visible.
#pragma once
#include "bpp_common.h"

namespace unityui {

bool Init();                    // resolve classes (main thread)
bool Spawn(const std::string& initial_text);  // returns false if anything is missing
void SetText(const std::string& text);
void Destroy();
bool Active();

}  // namespace unityui
