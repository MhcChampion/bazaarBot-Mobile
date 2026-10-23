// ImGui overlay rendered at eglSwapBuffers time.
#pragma once
#include "bpp_common.h"
#include <EGL/egl.h>

namespace ui {

void InstallEglHook();
bool Hooked();
int SwapCount();      // number of eglSwapBuffers-family calls observed

// Called from the main (Unity) thread once per frame.
void Tick();

// Latest touch snapshot (main thread -> render thread).
struct TouchState {
    bool down = false;
    float x = 0, y = 0;
    bool justToggled = false;
    int fingers = 0;
};
TouchState Snapshot();

bool IsOpen();
void SetOpen(bool open);

// User-facing UI scale multiplier (settings slider).
void SetUserScale(float scale);
float UserScale();

// main thread -> render thread
void UpdateTouch(float x, float y, bool down, int fingers);
void RequestToggle();

}  // namespace ui
