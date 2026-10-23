#pragma once
#include <string>
#include <vector>

// Adds a WebView on top of the game's own window (same process, same uid, no
// SYSTEM_ALERT_WINDOW needed). Android view operations are posted to its UI thread.
bool ShowWebPanel(const std::string& html_path);
bool WebPanelCreated();
bool WebPanelReady();
bool WebPanelVisible();
void SetWebPanelVisible(bool visible);
void SetWebPanelMode(const std::string& mode);
bool WebMiniVisible();
void SetWebMini(bool visible, float width=300, float height=72, float x=-1, float y=-1);
void MoveWebMini(float x, float y);
void SetWebMiniDragging(bool dragging);
struct WebMarker { float x=0, y=0; std::string guid; float width=0,height=0; };
void UpdateWebMarkers(const std::vector<WebMarker>& markers, const std::string& summary="");
void CaptureGameScreen(const std::string& path);
void HideWebKeyboard();
void PushJs(const std::string& js);
struct WebCommand { std::string action; std::string value; };
std::vector<WebCommand> DrainWebCommands();
