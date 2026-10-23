#pragma once
#include "bpp_common.h"

namespace features {

void Boot();          // called from boot thread once il2cpp is ready
void InstallHooks();  // called on the unity main thread on the first tick
void Tick();          // unity main thread, every frame
void DrawOverlay(bool overlay_open);
bool WantsOverlay();  // true when the overlay must stay visible (e.g. modal)

}  // namespace features
