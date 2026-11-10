// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - platform abstraction. A host (game engine, SDL window, test harness) implements Platform
// so the framework never talks to an OS directly.
#pragma once
#include "Core.h"
#include <chrono>
#include <map>
#include <cstdint>
#include <string>

namespace prism {

// Read-only view of the rendered UI frame. Pixels are 32-bit ARGB (0xAARRGGBB), row-major.
struct PixelView {
  const uint32_t* data = nullptr;
  int width = 0, height = 0;
  int stride = 0; // in pixels
  explicit operator bool() const { return data != nullptr; }
};

class Platform {
public:
  virtual ~Platform() = default;
  virtual uint64_t nowMs() const = 0;
  virtual std::string clipboard() const = 0;
  virtual void setClipboard(const std::string& text) = 0;
  virtual void setCursor(Cursor c) = 0;
  // Called when a text-entry widget gains/loses focus (show/hide OS keyboard, enable IME, ...).
  virtual void setTextInputActive(bool active) = 0;
  virtual float dpiScale() const { return 1.0f; }
  virtual void requestQuit() {}
  // Native multi-window (Application::openWindow). Return false if the platform cannot create OS windows; headless
  // hosts keep the window purely logical and drive it with Application::window* calls.
  virtual bool createNativeWindow(int id, const struct OsWindowDesc& desc) { (void)id; (void)desc; return false; }
  virtual void destroyNativeWindow(int id) { (void)id; }
  // OS handle of the main window (HWND on Windows) for bridges that attach to it, e.g. UI Automation. Engines that
  // host PRISM in their own window return it here.
  virtual void* nativeWindowHandle() const { return nullptr; }
  // PRISM window chrome (borderless windows with PRISM's own title bar). `id` 0 is the main window. The title bar
  // reports its drag strip so the OS can move the window from it (SDL / Win32 hit test); the buttons call these.
  virtual void setWindowCaption(int id, Rect dragArea) { (void)id; (void)dragArea; }
  virtual void minimizeWindow(int id) { (void)id; }
  virtual void toggleMaximizeWindow(int id) { (void)id; }
  virtual bool windowMaximized(int id) const { (void)id; return false; }
};

// Platform with no OS access: monotonic clock, in-memory clipboard, records the cursor and text state.
// Used for headless hosting and tests; engines usually derive from it and override what they support.
class HeadlessPlatform : public Platform {
public:
  uint64_t nowMs() const override {
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
  }
  std::string clipboard() const override { return m_clip; }
  void setClipboard(const std::string& t) override { m_clip = t; }
  void setCursor(Cursor c) override { cursor = c; }
  void setTextInputActive(bool a) override { textInput = a; }
  float dpiScale() const override { return dpi; }
  void setDpiScale(float d) { dpi = d > 0.0f ? d : 1.0f; }
  void requestQuit() override { quitRequested = true; }
  // chrome commands are recorded so tests and headless hosts can observe them
  void setWindowCaption(int id, Rect r) override { captions[id] = r; }
  void minimizeWindow(int id) override { minimized[id] = true; }
  void toggleMaximizeWindow(int id) override { maximized[id] = !maximized[id]; }
  bool windowMaximized(int id) const override { auto it = maximized.find(id); return it != maximized.end() && it->second; }
  std::map<int, Rect> captions;
  std::map<int, bool> minimized, maximized;
  Cursor cursor = Cursor::Arrow;
  bool textInput = false, quitRequested = false;
  float dpi = 1.0f;
private:
  std::string m_clip;
};

} // namespace prism
