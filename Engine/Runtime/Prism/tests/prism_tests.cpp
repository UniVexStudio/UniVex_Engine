// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include <cstdlib>
#include <prism/Application.h>
#include <prism/Commands.h>
#include <prism/Containers.h>
#include <prism/Controls.h>
#include <prism/EngineView.h>
#include <prism/Tabs.h>
#include <prism/RenderBackend.h>
#include <prism/GpuBatch.h>
#include <prism/FrameArena.h>
#include <prism/RenderThreadBridge.h>
#include <prism/Brush.h>
#include <prism/TableView.h>
#include <prism/Accessibility.h>
#include <prism/AccessibilityBridge.h>
#include <prism/IconAtlas.h>
#include <prism/TextLayout.h>
#include <prism/Gizmo.h>
#include <prism/GpuDevice.h>
#include <sstream>
#include <fstream>
#include <prism/BlueprintVM.h>
#include <prism/SvgPath.h>
#include <prism/Attribute.h>
#include <prism/Declarative.h>
#include <prism/EventRouting.h>
#include <prism/Animation.h>
#include <prism/DragDrop.h>
#include <prism/Layouts.h>
#include <prism/Transaction.h>
#include <prism/Reflection.h>
#include <prism/AssetBrowser.h>
#include <prism/CurveEditor.h>
#include <prism/CodeEditor.h>
#include <prism/Localization.h>
#include <prism/ColorPicker.h>
#include <prism/StyleSheet.h>
#include <prism/RuntimeProfile.h>
#include <prism/BlueprintGraph.h>
#include <vector>
#include <cmath>
#include <cstdio>
#include "prism/Profiler.h"
#include "prism/Docking.h"
#include "prism/Views.h"
#include <filesystem>
#include <string>
#include <thread>
#include <atomic>

using namespace prism;

static std::atomic<bool> g_trackAlloc{false};
static std::atomic<size_t> g_allocCount{0};

// Allocation counting (zero-allocation steady state). Sanitizers install their own allocator, so the hook is
// compiled out there and that one check is skipped; every other check still runs under ASan / UBSan.
#if defined(__SANITIZE_ADDRESS__)
#define PRISM_COUNT_ALLOCS 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PRISM_COUNT_ALLOCS 0
#endif
#endif
#ifndef PRISM_COUNT_ALLOCS
#define PRISM_COUNT_ALLOCS 1
#endif
#if PRISM_COUNT_ALLOCS
void* operator new(size_t size) {
  if (g_trackAlloc.load()) {
    g_allocCount++;
  }
  void* p = std::malloc(size);
  if (!p) throw std::bad_alloc();
  return p;
}

void operator delete(void* p) noexcept {
  std::free(p);
}

void operator delete(void* p, size_t) noexcept {
  std::free(p);
}
// The array forms must be replaced too, or new[] (library) / delete[] (here) mix two allocators.
void* operator new[](size_t size) { return operator new(size); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
#endif


static int g_fail = 0, g_run = 0;
#define CHECK(cond) do { g_run++; if (!(cond)) { g_fail++; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define SECTION(name) std::printf("%s\n", name)
static bool near(float a, float b, float eps = 0.51f) { return std::fabs(a - b) <= eps; }

class TestPlatform : public HeadlessPlatform {
public:
  uint64_t nowMs() const override { return ms; }
  uint64_t ms = 0;
};

static void testChords() {
  SECTION("chords");
  Chord c;
  CHECK(Chord::parse("Ctrl+Shift+Z", c) && c.key == Key::Z && c.mods == (ModCtrl | ModShift));
  CHECK(c.str() == "Ctrl+Shift+Z");
  CHECK(Chord::parse("ctrl + s", c) && c.str() == "Ctrl+S");
  CHECK(Chord::parse("f11", c) && c.key == Key::F11 && c.mods == 0);
  CHECK(Chord::parse("Alt+Delete", c) && c.str() == "Alt+Delete");
  CHECK(Chord::parse("", c) && !c.valid());
  CHECK(!Chord::parse("Ctrl+", c));
  CHECK(!Chord::parse("Hyper+Q", c));
  CHECK(!Chord::parse("Ctrl+Banana", c));
  for (int i = int(Key::Backspace); i <= int(Key::F12); i++) {      // every key round-trips through text
    Chord a{Key(i), ModCtrl | ModAlt}, b;
    CHECK(Chord::parse(a.str(), b) && b == a);
  }
}

static void testCommands() {
  SECTION("commands");
  CommandList L;
  int undo = 0, redo = 0, del = 0;
  bool canUndo = false;
  L.add("edit.undo", "Undo", "Ctrl+Z", [&] { undo++; }).enabled = [&] { return canUndo; };
  L.add("edit.redo", "Redo", "Ctrl+Y", [&] { redo++; });
  L.add("edit.delete", "Delete", "Delete", [&] { del++; });
  L.add("noop", "Unbound", "", [] {});

  CHECK(!L.dispatch(Key::Z, ModCtrl) && undo == 0);                    // disabled command does not run
  canUndo = true;
  CHECK(L.dispatch(Key::Z, ModCtrl) && undo == 1);
  CHECK(!L.dispatch(Key::Z, 0) && !L.dispatch(Key::Z, ModCtrl | ModShift));   // exact modifier match
  CHECK(L.dispatch(Key::Delete, 0) && del == 1);
  CHECK(L.execute("edit.redo") && redo == 1 && !L.execute("missing"));

  int changed = 0;
  L.onChanged = [&] { changed++; };
  std::string other;
  CHECK(!L.rebind("edit.redo", {Key::Z, ModCtrl}, &other) && other == "edit.undo");   // conflict refused
  CHECK(L.chordText("edit.redo") == "Ctrl+Y" && changed == 0);
  CHECK(L.rebind("edit.redo", {Key::K, ModCtrl}));
  CHECK(!L.dispatch(Key::Y, ModCtrl) && L.dispatch(Key::K, ModCtrl) && redo == 2 && changed == 1);
  CHECK(L.rebind("edit.redo", {}) && L.chordText("edit.redo").empty());               // unbind
  CHECK(L.rebind("edit.redo", {Key::K, ModCtrl}));

  std::string saved = L.serialize();
  CHECK(saved.find("edit.redo=Ctrl+K") != std::string::npos && saved.find("edit.undo") == std::string::npos);
  L.resetAll();
  CHECK(L.chordText("edit.redo") == "Ctrl+Y");
  CHECK(L.deserialize(saved) == 1 && L.chordText("edit.redo") == "Ctrl+K");
  CHECK(L.deserialize("nonsense\nmissing.id=Q\nedit.delete=Banana\n") == 0);             // junk is ignored
  L.resetAll();
  CHECK(L.deserialize("edit.undo=Ctrl+Y\nedit.redo=Ctrl+Z\n") == 2);                    // a swap is not a conflict
  CHECK(L.chordText("edit.undo") == "Ctrl+Y" && L.chordText("edit.redo") == "Ctrl+Z");
  L.resetAll();
  CHECK(L.deserialize("edit.undo=Ctrl+Y\n") == 0 && L.chordText("edit.undo") == "Ctrl+Z"); // real conflict -> default kept

  MenuItem m = L.menuItem("edit.undo");
  CHECK(m.label == "Undo" && m.shortcut == "Ctrl+Z" && m.enabled);
  canUndo = false;
  CHECK(!L.menuItem("edit.undo").enabled);
  CHECK(L.tip("edit.undo", "Undo") == "Undo (Ctrl+Z)" && L.tip("noop", "Thing") == "Thing");
}

static void testTheme() {
  SECTION("theme");
  Theme t = Theme::dark();
  std::string err;
  CHECK(t.applyJson(R"({"base":"light","accent":"#ff8800","radius":6,"rowHover":"#ffffff20"})", &err) && err.empty());
  CHECK(t.accent == Color(255, 136, 0) && near(t.radius, 6) && t.rowHover.a == 0x20);
  CHECK(t.panel == Theme::light().panel);                                // base applied first
  CHECK(t.applyJson(R"({"wat":"#000000","text":"#101010"})", &err) && err.find("wat") != std::string::npos);
  CHECK(t.text == Color(16, 16, 16));
  Theme before = t;
  CHECK(!t.applyJson(R"({"accent":"red"})", &err) && err.find("accent") != std::string::npos);
  CHECK(t.accent == before.accent);                                      // failed apply changes nothing
  CHECK(!t.applyJson(R"({"accent": "#fff")", &err));
  CHECK(!t.applyJson("[]", &err));
  CHECK(!t.applyJson(R"({"radius":"big"})", &err));
  CHECK(t.applyJson("{}", &err));
  CHECK(Theme::colorTokens().size() >= 30);
  ThemeColor tc(&Theme::accent);
  Color old = themeMutable().accent;
  themeMutable().accent = Color(1, 2, 3);
  CHECK(tc.get() == Color(1, 2, 3));                                     // resolves at use time
  themeMutable().accent = old;
}

static void testLayout(Application& app) {
  SECTION("layout");
  app.hostResize(700, 120);
  auto a = make<Spacer>(), b = make<Spacer>(), c = make<Spacer>();
  a->setFixedSize(100, 40);
  auto row = hbox(0);
  row->add(a);
  row->add(b, {.fill = 1});
  row->add(c, {.fill = 3});
  app.setRoot(row);
  app.tick();
  CHECK(near(a->rect().w, 100));
  CHECK(near(b->rect().w, 150) && near(c->rect().w, 450));              // 600 left, split 1:3
  CHECK(near(a->rect().x, 0) && near(b->rect().x, 100) && near(c->rect().x, 250));

  auto g = make<Spacer>();
  g->setFixedSize(50, 30);
  auto col = vbox(10);
  auto g2 = make<Spacer>();
  g2->setFixedSize(50, 30);
  col->add(g);
  col->add(g2);
  auto box = make<Border>();
  box->setPadding(Margin(8, 4));
  box->setChild(col);
  app.setRoot(box);
  app.tick();
  CHECK(near(g->rect().x, 8) && near(g->rect().y, 4));                  // padding applied
  CHECK(near(g2->rect().y, 4 + 30 + 10));                               // spacing applied
  CHECK(near(box->desired().x, 66) && near(box->desired().y, 78));      // 50+16 , 30+30+10+8
}

static void testHitAndRouting(Application& app) {
  SECTION("hit testing and host routing");
  app.hostResize(400, 300);
  auto view = make<EngineView>();
  int downs = 0;
  view->onInput = [&](const EngineInput& in) { if (in.kind == EngineInput::Kind::MouseDown) downs++; };
  auto btn = make<Button>("Go");
  int clicks = 0;
  btn->onClick = [&] { clicks++; };
  auto row = hbox(0);
  row->add(view, {.fill = 1});
  row->add(btn);
  app.setRoot(row);
  app.tick();
  Vec2 pb = btn->rect().center(), pv = view->rect().center();
  CHECK(app.wantsPointerAt(pb) && !app.wantsPointerAt(pv));
  app.hostMouseMove(pb);
  app.hostMouseButton(pb, MouseButton::Left, true);
  app.hostMouseButton(pb, MouseButton::Left, false);
  CHECK(clicks == 1 && downs == 0);
  app.hostMouseMove(pv);
  app.hostMouseButton(pv, MouseButton::Left, true);
  app.hostMouseButton(pv, MouseButton::Left, false);
  CHECK(downs == 1 && clicks == 1);
  btn->setEnabled(false);
  app.hostMouseMove(pb);
  app.hostMouseButton(pb, MouseButton::Left, true);
  app.hostMouseButton(pb, MouseButton::Left, false);
  CHECK(clicks == 1);                                                    // disabled widgets ignore clicks
}

static void testToastsAndTimers(Application& app, TestPlatform& plat) {
  SECTION("toasts and timers");
  app.setRoot(make<Spacer>());
  app.tick();
  plat.ms = 1000;
  app.notify("hello", ToastLevel::Success, 500);
  app.tick();
  CHECK(app.needsRender());
  app.render();
  plat.ms += 200; app.tick(); app.render();
  plat.ms += 400; app.tick();                                            // past the 500 ms lifetime
  CHECK(app.needsRender());                                              // the last frame must clear the toast
  app.render();
  plat.ms += 100; app.tick();
  CHECK(!app.needsRender());                                             // timer stopped, UI idle again

  int fired = 0;
  int id = app.addTimer(50, [&] { fired++; return true; });
  plat.ms += 120; app.tick();
  CHECK(fired == 1);
  plat.ms += 60; app.tick();
  CHECK(fired == 2);
  app.cancelTimer(id);
  plat.ms += 500; app.tick();
  CHECK(fired == 2);
}

static void dragMouse(Application& app, Vec2 a, Vec2 b) {
  app.hostMouseMove(a);
  app.hostMouseButton(a, MouseButton::Left, true);
  for (int i = 1; i <= 8; i++) app.hostMouseMove(a + (b - a) * (float(i) / 8.f));
  app.hostMouseButton(b, MouseButton::Left, false);
}

static void testDocking(Application& app) {
  SECTION("docking");
  app.hostResize(800, 400);
  {
    // A floating window leaves the rest of the window working; a menu does not.
    app.closeAllPopups();
    CHECK(!app.hasBlockingPopup());
    FloatingWindowOptions fo;
    fo.title = "Floating";
    Ref<Widget> floating = showFloatingWindow(make<Spacer>(), fo);
    CHECK(app.hasPopup() && !app.hasBlockingPopup());
    showMenu({MenuItem::Item("Item", [] {})}, Rect{10, 10, 40, 20}, PopupPlacement::Below);
    CHECK(app.hasBlockingPopup());
    app.closeAllPopups();
    CHECK(!app.hasPopup());
  }
  auto A = make<TabView>(), B = make<TabView>(), C = make<TabView>();
  for (auto* tv : {A.get(), B.get()}) tv->dockGroup = "main";
  C->dockGroup = "other";
  A->addTab("One", Icon::None, make<Spacer>());
  A->addTab("Two", Icon::None, make<Spacer>());
  A->addTab("Three", Icon::None, make<Spacer>());
  B->addTab("Beta", Icon::None, make<Spacer>());
  C->addTab("Gamma", Icon::None, make<Spacer>());
  int changesA = 0, changesB = 0;
  A->onTabsChanged = [&] { changesA++; };
  B->onTabsChanged = [&] { changesB++; };
  auto row = hbox(0);
  row->add(A, {.fill = 1});
  row->add(B, {.fill = 1});
  row->add(C, {.fill = 1});
  app.setRoot(row);
  app.tick();

  // plain click selects, does not move anything
  Vec2 two = A->tabRect(1).center();
  app.hostMouseMove(two); app.hostMouseButton(two, MouseButton::Left, true); app.hostMouseButton(two, MouseButton::Left, false);
  CHECK(A->current() == 1 && A->count() == 3 && changesA == 0);

  // a tiny wobble is a click, not a drag
  Vec2 three = A->tabRect(2).center();
  dragMouse(app, three, three + Vec2(3, 0));
  CHECK(A->count() == 3 && A->titleAt(2) == "Three" && changesA == 0);

  // reorder inside one view: drop "Three" before "One"
  dragMouse(app, A->tabRect(2).center(), Vec2(A->tabRect(0).x + 2, A->tabRect(0).y + 10));
  CHECK(A->titleAt(0) == "Three" && A->titleAt(1) == "One" && A->titleAt(2) == "Two" && changesA == 1 && changesB == 0);
  CHECK(A->current() == 0);                                              // the moved tab becomes current

  // drop onto the same place is a no-op
  dragMouse(app, A->tabRect(1).center(), Vec2(A->tabRect(1).x + 2, A->tabRect(1).y + 10));
  CHECK(A->titleAt(1) == "One" && changesA == 1);

  // move across views (drop on the body of B appends)
  Vec2 bBody{B->rect().center().x, B->rect().center().y + 40};
  dragMouse(app, A->tabRect(1).center(), bBody);
  CHECK(A->count() == 2 && B->count() == 2 && B->titleAt(1) == "One" && B->current() == 1);
  CHECK(changesA == 2 && changesB == 1);
  CHECK(A->titleAt(0) == "Three" && A->titleAt(1) == "Two");

  // drop into B's strip between its tabs
  dragMouse(app, A->tabRect(0).center(), Vec2(B->tabRect(1).x + 1, B->tabRect(1).y + 10));
  CHECK(B->count() == 3 && B->titleAt(1) == "Three" && B->titleAt(2) == "One");

  // a different dock group refuses the drop; the tab stays where it was
  dragMouse(app, A->tabRect(0).center(), C->rect().center());
  CHECK(A->count() == 1 && C->count() == 1 && A->titleAt(0) == "Two");

  // dropping outside any view cancels
  dragMouse(app, B->tabRect(0).center(), Vec2(850, 200));   // beyond the 800px window
  CHECK(B->count() == 3 && B->titleAt(0) == "Beta");

  // emptying a view leaves it usable, and it can take tabs again
  dragMouse(app, A->tabRect(0).center(), B->rect().center() + Vec2(0, 40));
  CHECK(A->count() == 0);
  app.tick(); app.render();
  dragMouse(app, B->tabRect(0).center(), A->rect().center() + Vec2(0, 40));
  CHECK(A->count() == 1 && A->titleAt(0) == "Beta" && B->count() == 3);

  // programmatic API
  CHECK(!B->takeTab(-1).page && !B->takeTab(99).page && B->count() == 3);   // bad indices are harmless
  TabView::Page p = B->takeTab(0);
  CHECK(p.title == "Three" && p.page && B->count() == 2);
  CHECK(B->insertTab(99, std::move(p)) == 2 && B->current() == 2);
  CHECK(B->indexOf("One") >= 0 && B->indexOf("nope") == -1);
}

static void testDrawList(Application& app) {
  SECTION("drawlist");
  app.hostResize(600, 400);
  auto col = vbox(6);
  for (const char* t : {"Alpha", "Beta", "Gamma delta"}) col->add(make<Button>(t));
  auto tabs = make<TabView>();
  tabs->addTab("One", Icon::None, make<Spacer>());
  tabs->addTab("Two", Icon::None, make<Spacer>());
  col->add(tabs, {.fill = 1});
  app.setRoot(col);
  app.tick();
  PixelView a = app.render();
  CHECK(a);
  std::vector<uint32_t> direct;
  for (int y = 0; y < a.height; y++) direct.insert(direct.end(), a.data + size_t(y) * a.stride, a.data + size_t(y) * a.stride + a.width);
  app.setRenderBackend(std::make_shared<SoftwareBackend>());
  PixelView b = app.render();
  CHECK(b && b.width == a.width && b.height == a.height);
  size_t diff = 0;
  for (int y = 0; y < b.height && b; y++)
    for (int x = 0; x < b.width; x++) diff += direct[size_t(y) * size_t(a.width) + size_t(x)] != b.data[size_t(y) * size_t(b.stride) + size_t(x)];
  CHECK(diff == 0);                                       // record + replay is bit-exact with direct painting
  const DrawList& dl = app.renderToDrawList();
  CHECK(!dl.cmds.empty() && dl.width == 600 && dl.height == 400);
  GlyphAtlas atlas;
  GpuFrame gf;
  CHECK(buildGpuFrame(dl, atlas, gf));
  std::printf("  gpu: cmds=%zu batches=%zu verts=%zu idx=%zu\n", dl.cmds.size(), gf.batches.size(), gf.vertices.size(), gf.indices.size());
  CHECK(!gf.batches.empty() && !gf.indices.empty() && gf.indices.size() % 3 == 0);
  bool inRange = true;
  for (uint32_t i : gf.indices) inRange &= i < gf.vertices.size();
  CHECK(inRange);
  GlyphAtlas tiny(16);                                    // overflow path: frame still builds after atlas resets or fails cleanly
  GpuFrame gf2;
  (void)buildGpuFrame(dl, tiny, gf2);
  CHECK(gf2.indices.size() % 3 == 0);
  app.setRenderBackend(nullptr);
}

static void testFrameArena() {
  SECTION("frame_arena");
  FrameArena arena(4096);
  CHECK(arena.capacity() >= 4096);
  CHECK(arena.allocatedBytes() == 0);

  struct Point { float x, y, z; };
  Point* p1 = arena.create<Point>(1.f, 2.f, 3.f);
  CHECK(p1 != nullptr && p1->x == 1.f && p1->y == 2.f && p1->z == 3.f);
  CHECK((reinterpret_cast<uintptr_t>(p1) % alignof(Point)) == 0);

  int* arr = arena.allocateArray<int>(100);
  CHECK(arr != nullptr);
  for (int i = 0; i < 100; i++) arr[i] = i * 2;
  CHECK(arr[99] == 198);

  size_t used = arena.allocatedBytes();
  CHECK(used > 0);

  arena.reset();
  CHECK(arena.allocatedBytes() == 0);
  CHECK(arena.capacity() >= 4096);

  Point* p2 = arena.create<Point>(4.f, 5.f, 6.f);
  CHECK(p2 != nullptr && p2->x == 4.f);
  CHECK(arena.blockCount() == 1);
}

static void testThreadingContract(Application& app) {
  SECTION("threading_contract");
  RenderThreadBridge bridge(3);

  std::atomic<int> framesRendered{0};
  std::atomic<bool> renderDone{false};

  std::thread renderThread([&] {
    GlyphAtlas atlas;
    GpuFrame gf;
    while (!bridge.isStopped()) {
      auto frame = bridge.acquireRenderFrame(true);
      if (!frame) break;
      bool ok = buildGpuFrame(*frame, atlas, gf);
      CHECK(ok);
      framesRendered++;
      bridge.recycleFrame(std::move(frame));
    }
    renderDone = true;
  });

  for (int i = 0; i < 10; i++) {
    app.tick();
    const DrawList& dl = app.renderToDrawList();
    auto buffer = bridge.acquireRecordBuffer(dl.width, dl.height);
    *buffer = dl;
    bridge.publishFrame(std::move(buffer));
  }

  while (framesRendered < 10) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  bridge.stop();
  renderThread.join();

  CHECK(framesRendered == 10);
  CHECK(renderDone);
}

static void testBrushesAndGradients() {
  SECTION("brushes_and_gradients");
  Canvas direct;
  direct.resize(100, 100);
  direct.clear(Color(0, 0, 0, 255));

  direct.gradientH(Rect(10, 10, 40, 20), Color(255, 0, 0), Color(0, 0, 255));
  direct.gradientV(Rect(60, 10, 30, 40), Color(0, 255, 0), Color(255, 255, 0));

  uint32_t sampleImg[16] = {
    0xFF111111, 0xFF222222, 0xFF222222, 0xFF333333,
    0xFF444444, 0xFF555555, 0xFF555555, 0xFF666666,
    0xFF444444, 0xFF555555, 0xFF555555, 0xFF666666,
    0xFF777777, 0xFF888888, 0xFF888888, 0xFF999999,
  };
  direct.nineSlice(sampleImg, 4, 4, Margin(1, 1, 1, 1), Rect(10, 50, 40, 30));

  Brush bSolid = Brush::solid(Color(100, 150, 200));
  Brush bRounded = Brush::rounded(Color(200, 100, 50), 4.f);
  Brush bOutline = Brush::outline(Color(255, 255, 255), 2.f, 3.f);
  Brush bGrad = Brush::gradient(Color(10, 20, 30), Color(40, 50, 60), GradientDirection::Horizontal);

  direct.drawBrush(Rect(60, 60, 15, 15), bSolid);
  direct.drawBrush(Rect(80, 60, 15, 15), bRounded);
  direct.drawBrush(Rect(60, 80, 15, 15), bOutline);
  direct.drawBrush(Rect(80, 80, 15, 15), bGrad);

  RecordingCanvas rec;
  rec.resize(100, 100);
  rec.beginFrame();
  rec.clear(Color(0, 0, 0, 255));
  rec.gradientH(Rect(10, 10, 40, 20), Color(255, 0, 0), Color(0, 0, 255));
  rec.gradientV(Rect(60, 10, 30, 40), Color(0, 255, 0), Color(255, 255, 0));
  rec.nineSlice(sampleImg, 4, 4, Margin(1, 1, 1, 1), Rect(10, 50, 40, 30));
  rec.drawBrush(Rect(60, 60, 15, 15), bSolid);
  rec.drawBrush(Rect(80, 60, 15, 15), bRounded);
  rec.drawBrush(Rect(60, 80, 15, 15), bOutline);
  rec.drawBrush(Rect(80, 80, 15, 15), bGrad);

  Canvas replayed;
  replayed.resize(100, 100);
  rec.list().replay(replayed);

  size_t mismatches = 0;
  for (int y = 0; y < 100; y++) {
    for (int x = 0; x < 100; x++) {
      if (direct.pixels()[y * 100 + x] != replayed.pixels()[y * 100 + x]) {
        mismatches++;
      }
    }
  }
  CHECK(mismatches == 0);

  GlyphAtlas atlas;
  GpuFrame gf;
  CHECK(buildGpuFrame(rec.list(), atlas, gf));
  CHECK(!gf.batches.empty());
  CHECK(gf.indices.size() % 3 == 0);
}

static void testSvgPath() {
  SECTION("svg_path");
  SvgPath path;
  path.moveTo(0, 0);
  path.lineTo(100, 0);
  path.quadTo(150, 50, 100, 100);
  path.cubicTo(80, 120, 20, 120, 0, 100);
  path.close();

  auto contours = path.flatten();
  CHECK(!contours.empty());
  CHECK(contours[0].size() > 10);

  SvgPath parsed = SvgPath::parse("M 10 20 L 50 20 H 80 V 60 C 70 80 40 80 30 60 Z");
  CHECK(!parsed.empty());
  CHECK(parsed.commands().size() >= 6);

  Canvas c;
  c.resize(200, 200);
  c.clear(Color(0, 0, 0));
  parsed.draw(c, Color(255, 255, 255), 2.0f, Color(100, 100, 100, 150), {10, 10}, 1.0f);
}

static void testHiDpiAndScale(Application& app, TestPlatform& plat) {
  SECTION("hidpi_and_scale");
  CHECK(near(plat.dpiScale(), 1.0f));
  CHECK(near(app.uiScale(), 1.0f));
  CHECK(near(app.effectiveScale(), 1.0f));

  plat.setDpiScale(1.5f);
  CHECK(near(app.dpiScale(), 1.5f));
  CHECK(near(app.effectiveScale(), 1.5f));

  app.setUiScale(1.25f);
  CHECK(near(app.uiScale(), 1.25f));
  CHECK(near(app.effectiveScale(), 1.5f * 1.25f));

  float s = app.effectiveScale();
  float rawX = 10.3f;
  float snappedX = app.snap(rawX);
  CHECK(near(snappedX, std::round(rawX * s) / s, 0.001f));

  Rect rawRect(10.3f, 20.7f, 100.4f, 50.8f);
  Rect snappedRect = app.snap(rawRect);
  CHECK(snappedRect.w >= 0 && snappedRect.h >= 0);

  plat.setDpiScale(1.0f);
  app.setUiScale(1.0f);
  CHECK(near(app.effectiveScale(), 1.0f));
}

static void testZeroAllocations(Application& app) {
  SECTION("zero_steady_state_allocations");
  GlyphAtlas atlas;
  GpuFrame gf;

  // Warm-up frames
  for (int i = 0; i < 3; i++) {
    app.tick();
    const DrawList& dl = app.renderToDrawList();
    buildGpuFrame(dl, atlas, gf);
  }

  // Steady-state frame: verify zero dynamic allocations
  g_allocCount = 0;
  g_trackAlloc = true;

  app.tick();
  const DrawList& dl = app.renderToDrawList();
  buildGpuFrame(dl, atlas, gf);

  g_trackAlloc = false;
  std::printf("  steady-state heap allocations: %zu\n", g_allocCount.load());
  if (PRISM_COUNT_ALLOCS) CHECK(g_allocCount == 0);
}

static void testAttributesAndDeclarative() {
  SECTION("attributes_and_declarative");
  Attribute<int> a = 42;
  CHECK(a.get() == 42 && !a.isBound());
  CHECK(int(a) == 42);

  int counter = 10;
  Attribute<int> b = [&] { return counter * 3; };
  CHECK(b.isBound() && b.get() == 30);
  counter = 20;
  CHECK(b.get() == 60);

  Attribute<std::string> str = "PRISM Engine UI";
  CHECK(str.get() == "PRISM Engine UI");

  Ref<Button> btn;
  PAssignNew(btn, Button, "Declarative Button");
  CHECK(btn != nullptr && btn->text() == "Declarative Button");

  auto wrapped = build(btn).padding(Margin(8)).fill(1.5f).tooltip("My Button").get();
  CHECK(wrapped->slot.fill == 1.5f && wrapped->slot.pad.l == 8);
  CHECK(wrapped->tooltip() == "My Button");
}

static void testInvalidation() {
  SECTION("invalidation");
  auto w = make<Widget>();
  CHECK(!w->isLayoutDirty() && !w->isPaintDirty() && !w->isVolatile());

  w->invalidate(InvalidateFlags::Paint);
  CHECK(w->isPaintDirty() && !w->isLayoutDirty());

  w->relayout();
  CHECK(w->isLayoutDirty() && w->isPaintDirty());

  w->clearDirtyFlags();
  CHECK(!w->isLayoutDirty() && !w->isPaintDirty());

  w->setVolatile(true);
  CHECK(w->isVolatile());
  w->clearDirtyFlags();
  CHECK(w->isVolatile()); // volatile persists across frame resets
}

class PreviewInterceptor : public Box {
public:
  using Box::Box;
  bool intercepted = false;
  Reply onPreviewMouseDown(const MouseEvent& e) override {
    if (e.button == MouseButton::Left) {
      intercepted = true;
      return Reply::handled();
    }
    return Reply::unhandled();
  }
};

static void testEventRouting(Application& app) {
  SECTION("event_routing");
  app.hostResize(400, 300);

  auto interceptor = make<PreviewInterceptor>(Orientation::Vertical);
  interceptor->setFixedSize(200, 100);

  int buttonClicks = 0;
  auto btn = make<Button>("Child Button");
  btn->onClick = [&] { buttonClicks++; };
  interceptor->add(btn);

  app.setRoot(interceptor);
  app.tick();
  app.render();

  Vec2 btnPos = btn->rect().center();
  app.hostMouseMove(btnPos);
  app.hostMouseButton(btnPos, MouseButton::Left, true);
  app.hostMouseButton(btnPos, MouseButton::Left, false);

  // The tunnel/preview intercepted the event top-down, so the child button was NOT clicked!
  CHECK(interceptor->intercepted);
  CHECK(buttonClicks == 0);
}

static void testLayoutContainers(Application& app) {
  SECTION("layout_containers");

  // 1. Grid
  Grid grid;
  grid.addRow(GridTrack::pixel(40));
  grid.addRow(GridTrack::star(1.0f));
  grid.addColumn(GridTrack::pixel(120));
  grid.addColumn(GridTrack::star(1.0f));

  auto g1 = make<Button>("Cell 0,0");
  auto g2 = make<Button>("Cell 1,1");
  grid.add(g1, 0, 0);
  grid.add(g2, 1, 1);
  grid.arrange(Rect(0, 0, 400, 300));

  CHECK(g1->rect().w == 120 && g1->rect().h == 40);
  CHECK(g2->rect().w > 200 && g2->rect().h > 200);

  // 2. UniformGrid
  UniformGrid ug(2, 10.0f);
  auto u1 = make<Button>("U1");
  auto u2 = make<Button>("U2");
  ug.add(u1);
  ug.add(u2);
  ug.arrange(Rect(0, 0, 210, 100));
  CHECK(near(u1->rect().w, 100.0f));
  CHECK(near(u2->rect().w, 100.0f));

  // 3. WrapBox
  WrapBox wb(Orientation::Horizontal, 10.0f, 10.0f);
  for (int i = 0; i < 4; i++) {
    auto b = make<Button>("WrapItem");
    b->setFixedSize(80, 30);
    wb.add(b);
  }
  wb.arrange(Rect(0, 0, 200, 300));
  // 80 + 10 + 80 = 170 <= 200 (2 items fit on row 0; 3rd wraps to row 1)
  CHECK(wb.children()[0]->rect().y == 0);
  CHECK(wb.children()[1]->rect().y == 0);
  CHECK(wb.children()[2]->rect().y >= 35);

  // 4. SizeBox
  SizeBox sb;
  auto sContent = make<Spacer>();
  sb.setContent(sContent);
  sb.setWidthOverride(140.0f);
  sb.setHeightOverride(70.0f);
  Vec2 sbSize = sb.measure({1000, 1000});
  CHECK(sbSize.x == 140.0f && sbSize.y == 70.0f);

  // 5. ScaleBox
  ScaleBox scb;
  auto scContent = make<Button>("Scaled");
  scContent->setFixedSize(100, 50);
  scb.setContent(scContent);
  scb.setStretch(StretchMode::ScaleToFit);
  scb.arrange(Rect(0, 0, 200, 200));
  CHECK(scContent->rect().w == 200 && scContent->rect().h == 100);

  // 6. StackSwitcher
  StackSwitcher ss;
  auto page0 = make<Button>("P0");
  auto page1 = make<Button>("P1");
  ss.addPage(page0);
  ss.addPage(page1);
  CHECK(ss.activeIndex() == 0);
  ss.setActiveIndex(1);
  CHECK(ss.activeIndex() == 1);

  // 7. Expander
  Expander exp("Section Details", true);
  auto expContent = make<Button>("Body Content");
  expContent->setFixedSize(120, 60);
  exp.setContent(expContent);
  Vec2 dOpen = exp.measure({500, 500});
  CHECK(dOpen.y >= 80);
  exp.setExpanded(false);
  Vec2 dClosed = exp.measure({500, 500});
  CHECK(dClosed.y == 24);

  // 8. ScrollBox
  ScrollBox sbox;
  auto scrollContent = make<Spacer>(300, 800);
  sbox.setContent(scrollContent);
  sbox.arrange(Rect(0, 0, 200, 400));
  CHECK(sbox.scrollOffset().y == 0);
}

static void testAnimationCurvesAndSprings() {
  SECTION("animation_curves_and_springs");

  // Curves
  CHECK(near(evaluateCurve(0.0f, CurveEase::Linear), 0.0f));
  CHECK(near(evaluateCurve(1.0f, CurveEase::Linear), 1.0f));
  CHECK(near(evaluateCurve(0.0f, CurveEase::QuadOut), 0.0f));
  CHECK(near(evaluateCurve(1.0f, CurveEase::QuadOut), 1.0f));
  CHECK(evaluateCurve(0.5f, CurveEase::QuadOut) > 0.5f);
  CHECK(near(evaluateCurve(1.0f, CurveEase::BounceOut), 1.0f, 0.02f));

  // Sequence
  CurveSequence seq(0.4f, CurveEase::QuadOut);
  seq.play();
  CHECK(seq.isPlaying());
  seq.tick(0.2f);
  CHECK(near(seq.progress(), 0.5f));
  seq.tick(0.3f);
  CHECK(seq.isAtEnd() && !seq.isPlaying());

  // Spring simulation
  SpringSimulation spring(250.0f, 15.0f, 1.0f);
  spring.reset(0.0f);
  spring.setTarget(100.0f);
  for (int i = 0; i < 60; i++) spring.update(0.016f);
  CHECK(near(spring.position(), 100.0f, 6.0f));
}

class TestDropReceiver : public Widget {
public:
  bool received = false;
  std::string tag;
  bool onDragOver(const DragDropEvent&) override { return true; }
  bool onDrop(const DragDropEvent& e) override {
    if (e.operation) {
      tag = e.operation->tag();
      received = true;
      return true;
    }
    return false;
  }
};

static void testDragAndDrop(Application& app) {
  SECTION("drag_and_drop");
  app.hostResize(400, 300);

  auto receiver = make<TestDropReceiver>();
  receiver->setFixedSize(200, 200);
  app.setRoot(receiver);
  app.tick();
  app.render();

  auto op = std::make_shared<DragDropOperation>("EngineMesh_Cube");
  app.startDragDrop(op);
  CHECK(app.isDragging());

  Vec2 center = receiver->rect().center();
  app.hostMouseMove(center);
  app.hostMouseButton(center, MouseButton::Left, false);

  CHECK(!app.isDragging());
  CHECK(receiver->received && receiver->tag == "EngineMesh_Cube");
}

static void testTransactionService() {
  SECTION("transaction_service");
  TransactionService tx(10);
  int value = 100;

  tx.push("Set to 200", [&] { value = 200; }, [&] { value = 100; });
  value = 200;
  CHECK(value == 200);
  CHECK(tx.canUndo());
  CHECK(!tx.canRedo());
  CHECK(tx.undoDescription() == "Set to 200");

  CHECK(tx.undo());
  CHECK(value == 100);
  CHECK(tx.canRedo());
  CHECK(!tx.canUndo());
  CHECK(tx.redoDescription() == "Set to 200");

  CHECK(tx.redo());
  CHECK(value == 200);

  // Scoped compound transaction
  {
    ScopedTransaction st(tx, "Batch Transform");
    tx.push("Step 1", [&] { value += 10; }, [&] { value -= 10; });
    value += 10;
    tx.push("Step 2", [&] { value *= 2; }, [&] { value /= 2; });
    value *= 2;
    st.commit();
  }
  CHECK(value == 420);
  CHECK(tx.undoDescription() == "Batch Transform");

  CHECK(tx.undo());
  CHECK(value == 200); // Both step 2 and step 1 undone atomically!

  CHECK(tx.redo());
  CHECK(value == 420);
}

struct MockEntity {
  std::string name = "Actor";
  float health = 100.0f;
  bool isStatic = false;
  Color tint{255, 255, 255};
};

static void testPropertyReflection() {
  SECTION("property_reflection");
  ClassReflection ref("MockEntity");
  ref.addString("Name", "General", &MockEntity::name)
     .addFloat("Health", "Stats", &MockEntity::health, 0.0f, 100.0f, 1.0f)
     .addBool("Static", "Transform", &MockEntity::isStatic)
     .addColor("Tint", "Rendering", &MockEntity::tint);

  MockEntity e1{"Player", 100.0f, false, Color(255, 0, 0)};
  MockEntity e2{"Enemy", 50.0f, false, Color(255, 0, 0)};
  MockEntity e3{"Boss", 100.0f, false, Color(255, 0, 0)};

  std::vector<void*> objs{&e1, &e2, &e3};

  // Test multi-object read:
  // 'Static' is false on all three -> not mixed
  auto staticVal = ref.readMulti("Static", objs);
  CHECK(!staticVal.isMixed);
  CHECK(staticVal.get<bool>() == false);

  // 'Tint' is Color(255, 0, 0) on all three -> not mixed
  auto tintVal = ref.readMulti("Tint", objs);
  CHECK(!tintVal.isMixed);
  CHECK(tintVal.get<Color>() == Color(255, 0, 0));

  // 'Health' is 100, 50, 100 -> mixed!
  auto healthVal = ref.readMulti("Health", objs);
  CHECK(healthVal.isMixed);

  // 'Name' is Player, Enemy, Boss -> mixed!
  auto nameVal = ref.readMulti("Name", objs);
  CHECK(nameVal.isMixed);

  // Test multi-object write with Undo integration:
  TransactionService tx;
  ref.writeMulti("Health", objs, 80.0f, &tx);
  CHECK(e1.health == 80.0f);
  CHECK(e2.health == 80.0f);
  CHECK(e3.health == 80.0f);

  auto newHealth = ref.readMulti("Health", objs);
  CHECK(!newHealth.isMixed);
  CHECK(newHealth.get<float>() == 80.0f);

  // Undo the multi-object write
  CHECK(tx.undo());
  CHECK(e1.health == 100.0f);
  CHECK(e2.health == 50.0f);
  CHECK(e3.health == 100.0f);

  // Redo
  CHECK(tx.redo());
  CHECK(e1.health == 80.0f);
  CHECK(e2.health == 80.0f);
  CHECK(e3.health == 80.0f);
}

static void testDockEdgeSplitting(Application& app) {
  SECTION("dock_edge_splitting");
  auto tabs = make<TabView>();
  tabs->dockGroup = "EditorPanels";
  tabs->setFixedSize(400, 300);

  auto page1 = make<Button>("Page 1");
  auto page2 = make<Button>("Page 2");
  tabs->addTab("Tab 1", Icon::File, page1);
  tabs->addTab("Tab 2", Icon::Folder, page2);

  TabView::DockZone splitZone = TabView::DockZone::None;
  std::string splitTitle;
  tabs->onDockSplit = [&](TabView*, TabView::DockZone zone, TabView::Page p) {
    splitZone = zone;
    splitTitle = p.title;
  };

  app.setRoot(tabs);
  app.tick();
  app.render();

  // Drag Tab 2 over the left edge of the TabView body
  Rect t2Rect = tabs->tabRect(1);
  app.hostMouseMove(t2Rect.center());
  app.hostMouseButton(t2Rect.center(), MouseButton::Left, true);

  // Drag to left edge zone of body (x = tabs->rect().x + 20, y = tabs->rect().y + 150)
  Vec2 leftEdgePos{tabs->rect().x + 20.0f, tabs->rect().y + 150.0f};
  app.hostMouseMove(leftEdgePos);
  app.hostMouseButton(leftEdgePos, MouseButton::Left, false);

  CHECK(splitZone == TabView::DockZone::Left);
  CHECK(splitTitle == "Tab 2");
}

static void testLocalization() {
  SECTION("localization_pipeline");
  auto& loc = LocalizationService::get();
  loc.setLocale("en-US");
  CHECK(loc.get("menu.file") == "File");
  CHECK(loc.get("tab.hierarchy") == "Hierarchy");

  // Format arguments {0}
  std::string formatted = loc.format("msg.item_count", {"42"});
  CHECK(formatted == "Found 42 items");

  // Switch locale to Japanese
  loc.setLocale("ja-JP");
  CHECK(loc.get("menu.file") == "ファイル");
  CHECK(loc.get("tab.hierarchy") == "階層");
  CHECK(loc.format("msg.item_count", {"42"}) == "42 個のアイテムが見つかりました");

  // Switch locale to German
  loc.setLocale("de-DE");
  CHECK(loc.get("menu.file") == "Datei");
  CHECK(loc.get("tab.hierarchy") == "Hierarchie");

  // Fallback test
  CHECK(loc.get("nonexistent.key", "DefaultFallback") == "DefaultFallback");

  // Reset to en-US
  loc.setLocale("en-US");
}

static void testCurveEditor() {
  SECTION("curve_editor");
  CurveEditor curve;
  CHECK(curve.keyframes().size() == 4);

  // Evaluate at bounds
  CHECK(near(curve.evaluate(0.0f), 0.0f, 0.01f));
  CHECK(near(curve.evaluate(1.0f), 1.0f, 0.01f));

  // Evaluate midpoint
  float mid = curve.evaluate(0.5f);
  CHECK(mid > 0.2f && mid < 0.8f);

  // Clear and add custom keyframes
  curve.clearKeyframes();
  CHECK(curve.keyframes().empty());
  curve.addKeyframe(0.0f, 10.0f, TangentMode::Linear);
  curve.addKeyframe(1.0f, 30.0f, TangentMode::Linear);
  CHECK(near(curve.evaluate(0.0f), 10.0f, 0.01f));
  CHECK(near(curve.evaluate(0.5f), 20.0f, 0.01f));
  CHECK(near(curve.evaluate(1.0f), 30.0f, 0.01f));
}

static void testCodeEditor() {
  SECTION("code_editor");
  CodeEditor editor;
  CHECK(editor.language() == SyntaxLanguage::Glsl);
  editor.setText("layout(location = 0) in vec3 pos;\nvoid main() {\n  gl_Position = vec4(pos, 1.0);\n}");
  CHECK(editor.text().find("gl_Position") != std::string::npos);

  // Tokenize checks
  auto tokens = editor.tokenizeLine("uniform vec4 uColor; // ambient");
  bool foundUniform = false, foundVec4 = false, foundComment = false;
  for (const auto& tok : tokens) {
    if (tok.type == TokenType::Keyword && tok.text == "uniform") foundUniform = true;
    if (tok.type == TokenType::Type && tok.text == "vec4") foundVec4 = true;
    if (tok.type == TokenType::Comment) foundComment = true;
  }
  CHECK(foundUniform);
  CHECK(foundVec4);
  CHECK(foundComment);
}

static void testAssetBrowser() {
  SECTION("asset_browser");
  AssetBrowser browser;
  browser.addAsset({"Meshes/Cube.mesh", "Cube.mesh", "Mesh", Icon::Cube, Color(80, 140, 230), 1024});
  browser.addAsset({"Materials/Gold.mat", "Gold.mat", "Material", Icon::Material, Color(240, 200, 60), 256});
  browser.addAsset({"Textures/Grid.png", "Grid.png", "Texture", Icon::Frame, Color(180, 100, 220), 4096});
  CHECK(browser.assets().size() == 3);

  // Filter by search query
  browser.setSearchQuery("Gold");
  CHECK(browser.selectedAsset() == nullptr);
  browser.setSearchQuery("");
  CHECK(browser.assets().size() == 3);
}

static void testColorPicker() {
  SECTION("color_picker");
  // HSV conversions
  HsvColor redHsv = HsvColor::fromRgb(Color(255, 0, 0));
  CHECK(near(redHsv.h, 0.0f, 1.0f));
  CHECK(near(redHsv.s, 1.0f, 0.01f));
  CHECK(near(redHsv.v, 1.0f, 0.01f));

  HsvColor greenHsv = HsvColor::fromRgb(Color(0, 255, 0));
  CHECK(near(greenHsv.h, 120.0f, 1.0f));

  HsvColor blueHsv = HsvColor::fromRgb(Color(0, 0, 255));
  CHECK(near(blueHsv.h, 240.0f, 1.0f));

  // Roundtrip conversion
  Color testCol(120, 80, 200, 255);
  HsvColor testHsv = HsvColor::fromRgb(testCol);
  Color roundtrip = testHsv.toRgb();
  CHECK(std::abs(int(roundtrip.r) - int(testCol.r)) <= 2);
  CHECK(std::abs(int(roundtrip.g) - int(testCol.g)) <= 2);
  CHECK(std::abs(int(roundtrip.b) - int(testCol.b)) <= 2);

  // ColorPicker widget
  ColorPicker picker(Color(10, 20, 30));
  CHECK(picker.color() == Color(10, 20, 30));
  bool changed = false;
  picker.onColorChanged = [&](Color c) { changed = true; };
  picker.setColor(Color(50, 100, 150));
  CHECK(picker.color() == Color(50, 100, 150));
  CHECK(changed);

  // sRGB <-> linear
  CHECK(near(srgbToLinear(0.0f), 0.0f, 1e-6f));
  CHECK(near(srgbToLinear(1.0f), 1.0f, 1e-5f));
  CHECK(near(srgbToLinear(0.5f), 0.2140f, 0.002f));
  CHECK(near(linearToSrgb(srgbToLinear(0.37f)), 0.37f, 1e-4f));

  // Channels in display units (sRGB mode)
  ColorPicker p2(Color(255, 0, 0));
  CHECK(near(p2.channel(ColorChannel::R), 255.0f, 0.5f));
  CHECK(near(p2.channel(ColorChannel::H), 0.0f, 0.5f));
  p2.setChannel(ColorChannel::G, 128.0f);
  CHECK(p2.color().g == 128);
  p2.setChannel(ColorChannel::H, 120.0f);
  CHECK(p2.color().g > p2.color().r);
  p2.setChannel(ColorChannel::A, 64.0f);
  CHECK(p2.color().a == 64);

  // HSV edit keeps hue on greys
  ColorPicker p3(Color(0, 255, 0));
  p3.setChannel(ColorChannel::S, 0.0f);
  CHECK(near(p3.channel(ColorChannel::H), 120.0f, 0.5f));
  CHECK(p3.color().r == p3.color().g && p3.color().g == p3.color().b);

  // Linear view: 0.5 linear == ~188 sRGB; intensity scales HDR output, not the 8-bit colour
  ColorPicker p4(Color(10, 10, 10));
  p4.setLinear(true);
  p4.setChannel(ColorChannel::R, 0.5f);
  CHECK(std::abs(int(p4.color().r) - 188) <= 1);
  CHECK(near(p4.channel(ColorChannel::R), 0.5f, 0.01f));
  p4.setIntensity(4.0f);
  CHECK(near(p4.hdr().r, 2.0f, 0.02f));
  CHECK(std::abs(int(p4.color().r) - 188) <= 1);
  p4.setChannel(ColorChannel::I, 2.5f);
  CHECK(near(p4.intensity(), 2.5f, 1e-4f));
  ColorHdr seen{};
  p4.onHdrChanged = [&](ColorHdr h) { seen = h; };
  p4.setChannel(ColorChannel::B, 1.0f);
  CHECK(near(seen.b, 2.5f, 0.01f));

  // Recent colours: most recent first, no duplicates
  ColorPicker::addRecent(Color(1, 2, 3));
  ColorPicker::addRecent(Color(4, 5, 6));
  ColorPicker::addRecent(Color(1, 2, 3));
  CHECK(ColorPicker::recent().size() >= 2 && ColorPicker::recent()[0] == Color(1, 2, 3) && ColorPicker::recent()[1] == Color(4, 5, 6));
}

static void testStyleSheet() {
  SECTION("stylesheet");
  StyleSheet& ss = StyleSheet::get();
  CHECK(ss.hasStyle("button.default"));
  CHECK(ss.hasStyle("button.primary"));
  CHECK(ss.hasStyle("button.danger"));
  CHECK(ss.hasStyle("button.toolbar"));
  CHECK(ss.hasStyle("hud.card"));

  // Retrieve style
  ControlStyle primary = ss.getStyle("button.primary");
  CHECK(primary.normalText == Color(255, 255, 255));

  // Custom style insertion
  ControlStyle custom;
  custom.cornerRadius = 12.0f;
  ss.setStyle("test.custom", custom);
  CHECK(ss.hasStyle("test.custom"));
  CHECK(ss.getStyle("test.custom").cornerRadius == 12.0f);
}

static void testRuntimeProfile() {
  SECTION("runtime_profile_and_game_hud");
  // Directional spatial focus navigation
  auto row = hbox();
  auto b1 = make<Button>("A");
  auto b2 = make<Button>("B");
  auto b3 = make<Button>("C");
  row->add(b1);
  row->add(b2);
  row->add(b3);

  // Set positions manually to simulate arranged layout
  b1->arrange(Rect{10, 10, 40, 20});
  b2->arrange(Rect{60, 10, 40, 20});
  b3->arrange(Rect{110, 10, 40, 20});

  Widget* nextR = FocusNavigation::findNextFocus(row.get(), b1.get(), FocusDirection::Right);
  CHECK(nextR == b2.get());
  Widget* nextR2 = FocusNavigation::findNextFocus(row.get(), b2.get(), FocusDirection::Right);
  CHECK(nextR2 == b3.get());
  Widget* nextL = FocusNavigation::findNextFocus(row.get(), b3.get(), FocusDirection::Left);
  CHECK(nextL == b2.get());

  // GameHud widget state
  GameHud hud;
  hud.setHealth(80.0f, 100.0f);
  hud.setShield(50.0f, 100.0f);
  hud.setYaw(1.57f);
  hud.setActionPrompt("E", "Open Door");
  bool exited = false;
  hud.onExitGameMode = [&] { exited = true; };

  KeyEvent escKey;
  escKey.key = Key::Escape;
  CHECK(hud.onKeyDown(escKey));
  CHECK(exited);
}

static void testBlueprintGraph() {
  SECTION("blueprint_graph");
  BlueprintGraph graph;
  CHECK(graph.preset() == BlueprintGraphPreset::WidgetBlueprint);   // default: the Get / Calc / SetVisible UI graph
  CHECK(graph.nodeCount() == 14 && graph.wireCount() == 17 && graph.commentCount() == 1);
  graph.setPreset(BlueprintGraphPreset::MaterialShader);
  CHECK(graph.preset() == BlueprintGraphPreset::MaterialShader);
  CHECK(graph.nodeCount() == 7);
  CHECK(graph.wireCount() == 6);
  CHECK(graph.commentCount() == 4);
  CHECK(graph.isPinConnected(2, 0, false)); // Albedo RGB connected
  CHECK(!graph.isPinConnected(2, 1, false)); // Albedo Alpha unconnected

  // Test GameplayScript preset
  graph.setPreset(BlueprintGraphPreset::GameplayScript);
  CHECK(graph.preset() == BlueprintGraphPreset::GameplayScript);
  CHECK(graph.nodeCount() == 7);
  CHECK(graph.wireCount() == 6);
  CHECK(graph.commentCount() == 2);
  CHECK(graph.isPinConnected(1, 0, false)); // Tick Exec connected
  CHECK(graph.isPinConnected(2, 0, false)); // Branch True connected
  CHECK(!graph.isPinConnected(2, 1, false)); // Branch False unconnected
  CHECK(graph.isPinConnected(5, 0, true));  // AddRot Exec connected
  CHECK(!graph.isPinConnected(5, 3, true)); // Sweep unconnected

  // Coordinate transforms
  graph.arrange(Rect{100, 100, 800, 600});
  Vec2 worldPt(200.0f, 150.0f);
  Vec2 screenPt = graph.worldToScreen(worldPt);
  Vec2 roundtrip = graph.screenToWorld(screenPt);
  CHECK(near(worldPt.x, roundtrip.x, 0.01f));
  CHECK(near(worldPt.y, roundtrip.y, 0.01f));

  // Custom node, comment and wire addition
  graph.clear();
  CHECK(graph.nodeCount() == 0);
  CHECK(graph.wireCount() == 0);
  CHECK(graph.commentCount() == 0);

  BlueprintNode customNode;
  customNode.id = 42;
  customNode.title = "Custom Node";
  customNode.category = BlueprintNodeCategory::PureMath;
  customNode.pos = Vec2(100, 100);
  customNode.inputs = {{"In", BlueprintPinType::Float, BlueprintPinKind::Input, "0"}};
  customNode.outputs = {{"Out", BlueprintPinType::Float, BlueprintPinKind::Output, ""}};
  graph.addNode(customNode);
  CHECK(graph.nodeCount() == 1);
  CHECK(graph.nodes()[0].id == 42);

  BlueprintComment customComment;
  customComment.id = 99;
  customComment.title = "// My Logic Group";
  customComment.bounds = Rect{80, 70, 200, 150};
  graph.addComment(customComment);
  CHECK(graph.commentCount() == 1);
  CHECK(graph.comments()[0].id == 99);
}

static void testBlueprintEditing() {
  SECTION("blueprint_editing");
  BlueprintGraph g;                                   // Widget preset
  CHECK(g.canConnect(10, 1, 30, 1));                  // Object -> Object
  std::string why;
  CHECK(!g.canConnect(20, 0, 30, 1, &why) && !why.empty());   // Bool -> Object context is refused
  CHECK(!g.canConnect(10, 0, 30, 2));                 // Exec -> Bool is refused
  CHECK(!g.canConnect(10, 0, 10, 0));                 // never to itself
  // replacing a data input keeps a single wire per input
  int before = g.wireCount();
  CHECK(g.connect(20, 0, 31, 2) && g.wireCount() == before);
  CHECK(g.isPinConnected(31, 2, true));
  // undo / redo round trip
  g.commit("start");
  CHECK(g.disconnectPin(31, 2, true));
  g.commit("cut");
  CHECK(!g.isPinConnected(31, 2, true) && g.canUndo());
  CHECK(g.undo() && g.isPinConnected(31, 2, true));
  CHECK(g.redo() && !g.isPinConnected(31, 2, true));
  g.undo();
  // data loops are rejected, exec loops are allowed
  g.setPreset(BlueprintGraphPreset::GameplayScript);
  // serialisation round trip
  std::string a = g.serialize();
  BlueprintGraph h;
  CHECK(h.deserialize(a));
  CHECK(h.nodeCount() == g.nodeCount() && h.wireCount() == g.wireCount() && h.commentCount() == g.commentCount());
  CHECK(!h.deserialize("garbage") && h.nodeCount() == g.nodeCount());     // malformed input leaves the graph intact
  CHECK(!h.deserialize("PRISMBP\t1\nW\t1\t0\t99\t0\n"));               // dangling wire
  // selection, copy, paste, delete
  g.selectAll();
  int n0 = g.nodeCount();
  g.copySelected();
  g.paste({500, 500});
  CHECK(g.nodeCount() == n0 * 2 && g.wireCount() == 12);
  g.removeSelected();
  CHECK(g.nodeCount() == n0);
  // compile + execution order
  g.setPreset(BlueprintGraphPreset::WidgetBlueprint);
  auto order = g.executionOrder();
  CHECK(!order.empty() && order.front() == 1);        // starts at the event
  auto rep = g.compile();
  CHECK(rep.errors == 0);
  g.removeNode(2);                                    // orphan the sequence's children
  CHECK(g.compile().warnings >= 4);
  // library: every spec builds, pin types resolve, reroute adopts the wired type
  for (auto& spec : blueprintNodeLibrary()) { auto n = spec.make(); CHECK(!n.inputs.empty() || !n.outputs.empty()); }
  BlueprintGraph r;
  r.clear();
  const BlueprintNodeSpec* rer = nullptr; const BlueprintNodeSpec* flt = nullptr;
  for (auto& spec : blueprintNodeLibrary()) { if (spec.name == "Reroute Node") rer = &spec; if (spec.name == "Random Float") flt = &spec; }
  int a1 = r.spawnNode(*flt, {0, 0}), b1 = r.spawnNode(*rer, {200, 0});
  CHECK(r.connect(a1, 0, b1, 0));
  CHECK(r.resolvedType(b1, 0, false) == BlueprintPinType::Float);
}

static void testBlueprintInteraction(Application& app) {
  SECTION("blueprint_interaction");
  app.closeAllPopups();
  app.hostResize(1000, 700);
  auto g = make<BlueprintGraph>();
  app.setRoot(g);
  g->clear();
  g->setView({0, 0}, 1.0f);
  const BlueprintNodeSpec *rnd = nullptr, *add = nullptr;
  for (auto& sp : blueprintNodeLibrary()) { if (sp.name == "Random Float") rnd = &sp; if (sp.name == "Add (+)") add = &sp; }
  int a = g->spawnNode(*rnd, {80, 80}), b = g->spawnNode(*add, {420, 120});
  app.tick(); app.render();
  g->commit("setup");
  Vec2 from = g->pinScreenPos(a, 0, false), to = g->pinScreenPos(b, 0, true);
  // drag a wire from output to input
  app.hostMouseMove(from);
  app.hostMouseButton(from, MouseButton::Left, true);
  app.hostMouseMove((from + to) * 0.5f);
  app.hostMouseMove(to);
  app.hostMouseButton(to, MouseButton::Left, false);
  CHECK(g->wireCount() == 1 && g->isPinConnected(a, 0, false) && g->isPinConnected(b, 0, true));
  CHECK(g->undo() && g->wireCount() == 0);
  CHECK(g->redo() && g->wireCount() == 1);
  // an incompatible drop is refused: Float -> Exec
  const BlueprintNodeSpec* br = nullptr;
  for (auto& sp : blueprintNodeLibrary()) if (sp.name == "Branch") br = &sp;
  int c = g->spawnNode(*br, {420, 320});
  app.tick(); app.render();
  Vec2 bad = g->pinScreenPos(c, 0, true);
  app.hostMouseButton(from, MouseButton::Left, true);
  app.hostMouseMove(bad);
  app.hostMouseButton(bad, MouseButton::Left, false);
  CHECK(!g->isPinConnected(c, 0, true));
  // drag a node: moves and snaps to the 8px grid
  Vec2 hdr = g->worldToScreen({80, 80}) + Vec2(30, 14);
  int n0 = g->nodeCount();
  Vec2 before = g->findNode(a)->pos;
  app.hostMouseButton(hdr, MouseButton::Left, true);
  app.hostMouseMove(hdr + Vec2(41, 23));
  app.hostMouseButton(hdr + Vec2(41, 23), MouseButton::Left, false);
  Vec2 after = g->findNode(a)->pos;
  CHECK(after.x != before.x && std::fmod(after.x, 8.f) == 0.f && std::fmod(after.y, 8.f) == 0.f);
  // right click on empty space opens the palette; typing filters; Enter spawns the node
  Vec2 empty{700, 560};
  app.hostMouseButton(empty, MouseButton::Right, true);
  app.hostMouseButton(empty, MouseButton::Right, false);
  app.tick(); app.render();
  app.hostText("sequence");
  app.hostKey(Key::Enter, 0);
  app.tick();
  CHECK(g->nodeCount() == n0 + 1);
  CHECK(g->nodes().back().title == "Sequence");
  // dropping a wire on empty space opens the palette filtered to compatible nodes, then auto-wires
  g->clearSelection();
  Vec2 out = g->pinScreenPos(b, 0, false), drop{800, 420};
  app.hostMouseButton(out, MouseButton::Left, true);
  app.hostMouseMove(drop);
  app.hostMouseButton(drop, MouseButton::Left, false);
  app.tick(); app.render();
  app.hostText("clamp");
  app.hostKey(Key::Enter, 0);
  app.tick();
  CHECK(g->nodes().back().title == "Clamp" && g->isPinConnected(b, 0, false));
  // keyboard: select all, delete, undo
  int cnt = g->nodeCount();
  app.hostKey(Key::A, ModCtrl);
  app.hostKey(Key::Delete, 0);
  CHECK(g->nodeCount() == 0);
  app.hostKey(Key::Z, ModCtrl);
  CHECK(g->nodeCount() == cnt);
  app.closeAllPopups();
}

// An embedded graph: no chrome, its own library and spawn hook, commit / selection callbacks, the
// host's connection rule, an icon override, and a settings widget inside a node that takes clicks.
static void testBlueprintEmbedded(Application& app) {
  SECTION("blueprint_embedded");
  app.closeAllPopups();
  app.hostResize(1000, 700);
  auto g = make<BlueprintGraph>();
  app.setRoot(g);
  g->clear();
  BlueprintGraph::Features f;
  f.toolbar = f.crumbs = f.minimap = f.watermark = f.shortcuts = f.history = false;
  g->setFeatures(f);
  g->setView({0, 0}, 1.0f);
  auto mkPose = [](const char* title, int inputs) {
    BlueprintNode n;
    n.title = title;
    n.category = BlueprintNodeCategory::Animation;
    for (int i = 0; i < inputs; i++) n.inputs.push_back(BlueprintPin{"In", BlueprintPinType::Pose, BlueprintPinKind::Input});
    n.outputs.push_back(BlueprintPin{"Pose", BlueprintPinType::Pose, BlueprintPinKind::Output});
    return n;
  };
  std::vector<BlueprintNodeSpec> lib{{"Animation", "Clip", "clip", [&] { return mkPose("Clip", 0); }}};
  g->setNodeLibrary(lib);
  CHECK(g->nodeLibrary().size() == 1 && g->nodeLibrary()[0].name == "Clip");
  int spawned = 0;
  g->onSpawn = [&](const BlueprintNodeSpec& spec, Vec2 world) {
    BlueprintNode n = spec.make();
    n.id = 500 + spawned++;
    n.pos = world;
    g->addNode(n);
    return n.id;
  };
  std::vector<std::string> commits;
  g->onCommit = [&](const std::string& name) { commits.push_back(name); };
  std::vector<int> picked;
  g->onSelectionChanged = [&](const std::vector<int>& ids) { picked = ids; };
  bool refuse = true;
  g->canConnectHook = [&](int, int, int, int, std::string* why) { if (refuse && why) *why = "host says no"; return !refuse; };

  BlueprintNode clip = mkPose("Clip", 0);
  clip.id = 10; clip.pos = {60, 60}; clip.icon = Icon::NodeClip;
  BlueprintNode blend = mkPose("Blend2", 2);
  blend.id = 11; blend.pos = {400, 60}; blend.bodyH = 40; blend.bodyW = 200; blend.bodySummary = "weight 0.5";
  g->addNode(clip);
  g->addNode(blend);
  bool clicked = false;
  auto button = make<Button>("Weight");
  button->onClick = [&] { clicked = true; };
  g->setNodeBody(11, button);
  app.tick(); app.render();
  CHECK(g->findNode(11)->size.y >= 40 && g->findNode(11)->size.x >= 200);   // the body is part of the node
  CHECK(button->visible() && button->rect().w > 0);
  Vec2 bc = button->rect().center();
  app.hostMouseMove(bc);
  app.hostMouseButton(bc, MouseButton::Left, true);
  app.hostMouseButton(bc, MouseButton::Left, false);
  CHECK(clicked);                                                     // the widget inside the node got the click
  CHECK(g->findNode(11)->pos.x == 400);                               // and the node did not move

  // the host's rule refuses, then allows
  CHECK(!g->canConnect(10, 0, 11, 0));
  refuse = false;
  CHECK(g->canConnect(10, 0, 11, 0));
  Vec2 from = g->pinScreenPos(10, 0, false), to = g->pinScreenPos(11, 0, true);
  app.hostMouseMove(from);
  app.hostMouseButton(from, MouseButton::Left, true);
  app.hostMouseMove((from + to) * 0.5f);
  app.hostMouseMove(to);
  app.hostMouseButton(to, MouseButton::Left, false);
  CHECK(g->wireCount() == 1);
  CHECK(!commits.empty() && commits.back() == "Connect pins");
  CHECK(!g->canUndo());                                               // the host owns undo

  // selection reaches the host
  Vec2 head = g->worldToScreen(g->findNode(10)->pos) + Vec2{30, 12};
  app.hostMouseMove(head);
  app.hostMouseButton(head, MouseButton::Left, true);
  app.hostMouseButton(head, MouseButton::Left, false);
  CHECK(picked.size() == 1 && picked[0] == 10);
  // without shortcuts the host gets Ctrl+Z
  CHECK(!g->onKeyDown(KeyEvent{Key::Z, ModCtrl, false}));

  // the palette lists the graph's own library and spawns through the host
  g->openPalette({700, 400});
  app.tick(); app.render();
  app.hostKey(Key::Enter, 0);
  app.tick();
  CHECK(spawned == 1 && g->findNode(500) != nullptr);
  app.closeAllPopups();

  // a pose graph: an output feeds one input, and a node added off a wired pin goes into that wire
  f.singleLinkOutputs = true;
  g->setFeatures(f);
  lib.push_back({"Animation", "Blend", "blend", [&] { return mkPose("Blend", 2); }});
  g->setNodeLibrary(lib);
  CHECK(g->wireCount() == 1);                                         // Clip 10 -> Blend2 11
  BlueprintPinFilter fromClip;
  fromClip.active = true;
  fromClip.type = BlueprintPinType::Pose;
  fromClip.fromKind = BlueprintPinKind::Output;
  g->openPalette({700, 300}, fromClip, 10, 0);
  app.tick(); app.render();
  app.hostKey(Key::Enter, 0);
  app.tick();
  CHECK(spawned == 2 && g->findNode(501) != nullptr);
  const auto wired = [&](int a, int ap, int b, int bp) {
    return std::any_of(g->wires().begin(), g->wires().end(), [&](const BlueprintWire& w) {
      return w.fromNodeId == a && w.fromPinIndex == ap && w.toNodeId == b && w.toPinIndex == bp;
    });
  };
  CHECK(wired(10, 0, 501, 0));                                        // the dragged wire ends on the new node
  CHECK(wired(501, 0, 11, 0));                                        // which took the old wire's place
  CHECK(!wired(10, 0, 11, 0) && g->wireCount() == 2);
  // moving a used output's wire takes it off its old input
  CHECK(g->connect(10, 0, 11, 1));
  CHECK(!wired(10, 0, 501, 0) && wired(10, 0, 11, 1));
  // picking a wire up off its input and dropping it in empty space: the new node goes into it
  Vec2 grab = g->pinScreenPos(11, 1, true), drop{820, 620};
  app.hostMouseMove(grab);
  app.hostMouseButton(grab, MouseButton::Left, true);
  app.hostMouseMove((grab + drop) * 0.5f);
  app.hostMouseMove(drop);
  app.hostMouseButton(drop, MouseButton::Left, false);
  app.tick(); app.render();
  app.hostKey(Key::Enter, 0);
  app.tick();
  CHECK(spawned == 3 && g->findNode(502) != nullptr);
  CHECK(wired(10, 0, 502, 0) && wired(502, 0, 11, 1));
  app.closeAllPopups();
}

static void testBlueprintGraphTypes(Application& app) {
  SECTION("blueprint_graph_types");
  app.hostResize(1200, 700);
  auto g = make<BlueprintGraph>();
  app.setRoot(g);
  const BlueprintGraphPreset all[] = {BlueprintGraphPreset::EventBlueprint, BlueprintGraphPreset::AnimationGraph, BlueprintGraphPreset::StateMachine,
      BlueprintGraphPreset::BehaviorTree, BlueprintGraphPreset::ParticleGraph, BlueprintGraphPreset::AudioGraph};
  for (auto p : all) {
    g->setPreset(p);
    app.tick(); app.render();
    CHECK(g->preset() == p);
    CHECK(g->nodes().size() >= 8 && g->wires().size() >= 7);
    auto rep = g->compile();
    CHECK(rep.errors == 0 && rep.warnings == 0);                 // every sample graph is clean
    std::string snap = g->serialize();
    CHECK(g->deserialize(snap) && g->serialize() == snap);       // new pin types / categories round-trip
    g->run(); app.tick(); app.render();                            // run animation works for exec and non-exec graphs
    g->debugRunAt(0.0f);
  }
  // pose / link / audio pins only connect to their own kind
  CHECK(blueprintTypesCompatible(BlueprintPinType::Pose, BlueprintPinType::Pose));
  CHECK(!blueprintTypesCompatible(BlueprintPinType::Pose, BlueprintPinType::Float));
  CHECK(!blueprintTypesCompatible(BlueprintPinType::Link, BlueprintPinType::String));
  CHECK(!blueprintTypesCompatible(BlueprintPinType::Audio, BlueprintPinType::Pose));
  // a graph without its output node reports an error
  g->setPreset(BlueprintGraphPreset::AnimationGraph);
  g->clear();
  CHECK(g->compile().errors == 1);
  CHECK(std::string(blueprintPresetName(BlueprintGraphPreset::BehaviorTree)) == "Behavior Tree");
}

static void testBlueprintAdvanced(Application& app) {
  SECTION("blueprint_advanced");
  app.closeAllPopups();
  app.hostResize(1300, 700);
  auto g = make<BlueprintGraph>();
  g->saveDir = "bp_test_graphs";
  app.setRoot(g);
  app.tick(); app.render();
  // every shipped preset opens with no overlapping nodes
  for (int pi = 0; pi <= int(BlueprintGraphPreset::AudioGraph); pi++) {
    g->setPreset(BlueprintGraphPreset(pi));
    bool ov = false;
    for (size_t i = 0; i < g->nodes().size(); i++) for (size_t j = i + 1; j < g->nodes().size(); j++) {
      auto &a = g->nodes()[i], &b = g->nodes()[j];
      if (a.pos.x < b.pos.x + b.size.x && b.pos.x < a.pos.x + a.size.x && a.pos.y < b.pos.y + b.size.y && b.pos.y < a.pos.y + a.size.y) ov = true;
    }
    CHECK(!ov);
  }
  g->setPreset(BlueprintGraphPreset::EventBlueprint);
  // auto layout: every wire flows left-to-right, nothing overlaps
  for (auto& n : std::vector<BlueprintNode>(g->nodes())) { (void)n; }
  g->autoLayout();
  bool ltr = true;
  for (auto& w : g->wires()) {
    auto *a = g->findNode(w.fromNodeId), *b = g->findNode(w.toNodeId);
    if (a && b && a != b && b->pos.x <= a->pos.x) ltr = false;
  }
  CHECK(ltr);
  bool overlap = false;
  for (size_t i = 0; i < g->nodes().size(); i++) for (size_t j = i + 1; j < g->nodes().size(); j++) {
    auto &a = g->nodes()[i], &b = g->nodes()[j];
    if (a.pos.x < b.pos.x + b.size.x && b.pos.x < a.pos.x + a.size.x && a.pos.y < b.pos.y + b.size.y && b.pos.y < a.pos.y + a.size.y) overlap = true;
  }
  CHECK(!overlap);
  CHECK(g->undo());                                              // layout is one undo step
  g->redo();
  // collapse / expand round trip preserves node + wire counts and external connections
  int n0 = g->nodeCount(), w0 = g->wireCount();
  g->clearSelection();
  int picked = 0;
  for (auto& w : g->wires()) { if (!picked) { g->select(w.fromNodeId, true); g->select(w.toNodeId, true); picked = 1; } }
  int sel = g->selectedCount();
  CHECK(sel >= 2 && g->collapseSelected());
  CHECK(g->nodeCount() == n0 - sel + 1);
  auto ids = g->selectedIds();
  CHECK(ids.size() == 1 && g->isCollapsed(ids[0]));
  CHECK(g->expandNode(ids[0]));
  CHECK(g->nodeCount() == n0 && g->wireCount() == w0);
  CHECK(!g->collapseSelected() || g->nodeCount() < n0);           // collapse again is allowed (selection restored)
  g->undo();
  // variables: unique names, Get/Set nodes, rename, persistence, removal
  g->clear();
  auto* v = g->addVariable("Health", BlueprintPinType::Float, "100");
  CHECK(v && v->name == "Health");
  CHECK(g->addVariable("Health", BlueprintPinType::Bool)->name == "Health1");
  int get = g->spawnVariableNode("Health", false, {0, 0});
  int set = g->spawnVariableNode("Health", true, {200, 0});
  CHECK(get > 0 && set > 0 && g->spawnVariableNode("Nope", false, {0, 0}) == -1);
  CHECK(g->findNode(set)->inputs.size() == 2 && g->findNode(set)->inputs[1].defaultValue == "100");
  CHECK(g->renameVariable("Health", "HP") && g->findNode(get)->title == "HP" && g->findNode(set)->title == "Set HP");
  CHECK(!g->renameVariable("HP", "Health1"));                     // name clash refused
  std::string text = g->serialize();
  auto h = make<BlueprintGraph>();
  CHECK(h->deserialize(text) && h->variables().size() == 2 && h->variables()[0].name == "HP" && h->variables()[0].value == "100");
  CHECK(g->removeVariable("HP") && g->variables().size() == 1 && g->nodeCount() == 0);
  // debugger: breakpoints toggle, survive selection, clear
  g->setPreset(BlueprintGraphPreset::EventBlueprint);
  int first = g->nodes().front().id;
  g->toggleBreakpoint(first);
  CHECK(g->hasBreakpoint(first) && g->breakpoints().size() == 1);
  g->toggleBreakpoint(first);
  CHECK(!g->hasBreakpoint(first));
  g->toggleBreakpoint(first); g->toggleBreakpoint(-77);
  CHECK(g->breakpoints().size() == 1 && !g->paused() && g->pausedNode() == -1);
  g->resume();                                                    // no-op when not paused
  g->clearBreakpoints();
  CHECK(g->breakpoints().empty());
  app.tick(); app.render();
}

static void testBlueprintTools(Application& app) {
  SECTION("blueprint_tools");
  app.closeAllPopups();
  app.hostResize(1300, 700);
  auto g = make<BlueprintGraph>();
  g->saveDir = "bp_test_graphs";
  app.setRoot(g);
  app.tick(); app.render();
  // navigation history across graph types
  g->setPreset(BlueprintGraphPreset::EventBlueprint);
  g->setPreset(BlueprintGraphPreset::AnimationGraph);
  CHECK(g->canNavBack() && !g->canNavForward());
  g->navBack();
  CHECK(g->preset() == BlueprintGraphPreset::EventBlueprint && g->canNavForward());
  g->navForward();
  CHECK(g->preset() == BlueprintGraphPreset::AnimationGraph);
  // save / load round trip + diff
  CHECK(g->saveToFile());
  CHECK(g->savedGraphs().size() == 1);
  std::string snap = g->serialize();
  CHECK(g->diffAgainst(snap).empty());
  g->setPreset(BlueprintGraphPreset::AudioGraph);
  CHECK(g->loadFromFile("bp_test_graphs/Animation_Graph.prismbp"));
  CHECK(g->preset() == BlueprintGraphPreset::AnimationGraph && g->nodes().size() == 9);
  g->clear();
  CHECK(!g->diffAgainst(snap).empty());                        // all nodes reported removed
  // hide unrelated + find popups open and close cleanly
  g->setHideUnrelated(true);
  app.tick(); app.render();
  CHECK(g->hideUnrelated());
  g->openFind({100, 100, 100, 24});
  app.tick(); app.render();
  CHECK(app.topPopup() != nullptr);
  app.closeAllPopups();
  g->openFind({100, 100, 100, 24}, true);
  app.closeAllPopups();
  g->showClassSettings(); g->showClassDefaults();
  app.tick(); app.render();
  app.closeAllPopups();
  std::error_code ec;
  std::filesystem::remove_all("bp_test_graphs", ec);
}

static void testLogView(Application& app) {
  SECTION("log_view");
  app.hostResize(700, 400);
  auto lv = make<LogView>();
  app.setRoot(lv);
  lv->add(LogLevel::Info, "[Renderer] frame ok");
  lv->add(LogLevel::Warning, "[Physics] slow step in src/Solver.cpp:88");
  lv->add(LogLevel::Error, "[Renderer] shader failed at Shaders/PBR.glsl:12");
  lv->add(LogLevel::Command, "spawn cube");
  app.tick(); app.render();
  CHECK(lv->total() == 4 && lv->visibleCount() == 4);
  CHECK(lv->categories().size() == 2);                       // Renderer, Physics (prefix is parsed out of the text)
  lv->setCategoryEnabled("Renderer", false);
  CHECK(lv->visibleCount() == 2);
  lv->setCategoryEnabled("Renderer", true);
  lv->setVerbosity(2);                                       // warnings + errors only
  CHECK(lv->visibleCount() == 2);
  lv->setVerbosity(3);
  CHECK(lv->visibleCount() == 1);
  lv->setVerbosity(0);
  lv->setTextFilter("solver");
  CHECK(lv->visibleCount() == 1 && lv->visibleText().find("[Physics]") != std::string::npos);
  lv->setTextFilter("");
  LogView::Source s;
  CHECK(LogView::findSource("fail at Shaders/PBR.glsl:12 now", s) && s.path == "Shaders/PBR.glsl" && s.line == 12);
  CHECK(!LogView::findSource("time 12:30 not a path", s));
  CHECK(lv->visibleText().find("spawn cube") != std::string::npos);
  lv->clear();
  CHECK(lv->total() == 0 && lv->categories().empty());
}

// Deterministic mutation fuzzing: every loader must reject or accept hostile input without crashing, hanging or
// corrupting state. (Run the suite under ASan / valgrind to turn silent memory errors into failures.)
struct Fuzz {
  uint32_t s;
  uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
  std::string mutate(const std::string& in) {
    std::string o = in;
    int edits = 1 + int(next() % 6);
    static const char* tok[] = {"\t", "\n", "-1", "999999999", "{", "}", "[", "]", "\"", ",", ":", "\\", "S\t1\t", "W\t", "N\t", "nan", "1e99", "\xff\xfe", ""};
    for (int e = 0; e < edits && !o.empty(); e++) {
      size_t at = next() % o.size();
      switch (next() % 6) {
        case 0: o[at] = char(next() & 0xFF); break;                                   // flip a byte
        case 1: o.erase(at, 1 + next() % 12); break;                                  // delete a run
        case 2: o.insert(at, tok[next() % (sizeof tok / sizeof *tok)]); break;        // insert a structural token
        case 3: o.insert(at, o.substr(next() % o.size(), next() % 40)); break;        // duplicate a slice
        case 4: o.resize(at); break;                                                  // truncate
        default: { size_t b = next() % o.size(); std::swap(o[at], o[b]); }
      }
    }
    return o;
  }
};

static std::string slurp(const std::string& path) { std::ifstream f(path, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

// ---------------------------------------------------------------- golden visual regression
// Each scene is rendered and reduced to a 48x27 grid of mean colours (robust to sub-pixel AA noise, sensitive to any
// real layout / colour / icon change). The grid is compared with tests/golden/<name>.sig.
//   PRISM_UPDATE_GOLDEN=1 ./prism_tests      re-records the goldens after an intentional visual change.
// On failure the frame is written next to the test binary as golden_<name>.png for inspection.
struct Sig { int w = 48, h = 27; std::vector<float> c; };
static Sig signatureOf(const uint32_t* px, int W, int H, int stride) {
  Sig s; s.c.assign(size_t(s.w * s.h * 3), 0.f);
  for (int gy = 0; gy < s.h; gy++) for (int gx = 0; gx < s.w; gx++) {
    int x0 = gx * W / s.w, x1 = (gx + 1) * W / s.w, y0 = gy * H / s.h, y1 = (gy + 1) * H / s.h;
    double r = 0, g = 0, b = 0; int n = 0;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) { uint32_t p = px[y * stride + x]; r += (p >> 16) & 255; g += (p >> 8) & 255; b += p & 255; n++; }
    float* o = &s.c[size_t((gy * s.w + gx) * 3)];
    o[0] = float(r / std::max(1, n)); o[1] = float(g / std::max(1, n)); o[2] = float(b / std::max(1, n));
  }
  return s;
}
static bool goldenCheck(const std::string& dir, const std::string& name, const uint32_t* px, int W, int H, int stride, double* meanOut = nullptr, bool report = true) {
  Sig now = signatureOf(px, W, H, stride);
  std::string path = dir + "/" + name + ".sig";
  const char* upd = std::getenv("PRISM_UPDATE_GOLDEN");
  std::ifstream in(path);
  if (!in || (upd && upd[0] == '1')) {
    std::filesystem::create_directories(dir);
    std::ofstream o(path);
    o << now.w << ' ' << now.h << '\n';
    for (size_t i = 0; i < now.c.size(); i++) o << int(std::lround(now.c[i])) << ((i + 1) % 24 ? ' ' : '\n');
    std::printf("  golden %s recorded\n", name.c_str());
    return true;
  }
  Sig ref; in >> ref.w >> ref.h;
  ref.c.resize(size_t(ref.w * ref.h * 3));
  for (auto& v : ref.c) { int k; in >> k; v = float(k); }
  if (ref.w != now.w || ref.h != now.h || !in) return false;
  double sum = 0, worst = 0;
  for (size_t i = 0; i < ref.c.size(); i++) { double d = std::fabs(ref.c[i] - now.c[i]); sum += d; worst = std::max(worst, d); }
  double mean = sum / double(ref.c.size());
  if (meanOut) *meanOut = mean;
  bool ok = mean < 1.5 && worst < 48;
  if (!ok && report) {
    Canvas::writePng("golden_" + name + ".png", px, W, H, stride);
    std::printf("  golden %s differs: mean %.2f, worst cell %.0f (see golden_%s.png)\n", name.c_str(), mean, worst, name.c_str());
  }
  return ok;
}

// A fake device that validates every call the way a real API's debug layer would.
struct FakeDevice : GpuDevice {
  std::map<uint64_t, std::pair<int, int>> tex;
  uint64_t next = 1;
  size_t creates = 0, updates = 0, destroys = 0, draws = 0, clears = 0, frames = 0, badCalls = 0, verts = 0, idx = 0;
  int fw = 0, fh = 0;
  bool inFrame = false;
  uint64_t createTexture(int w, int h, GpuTextureFormat) override { creates++; tex[next] = {w, h}; return next++; }
  void updateTexture(uint64_t t, int w, int h, GpuTextureFormat, const void* d) override {
    updates++;
    auto it = tex.find(t);
    if (it == tex.end() || it->second != std::pair{w, h} || !d) badCalls++;
  }
  void destroyTexture(uint64_t t) override { destroys++; if (!tex.erase(t)) badCalls++; }
  void beginFrame(int w, int h) override { if (inFrame) badCalls++; inFrame = true; fw = w; fh = h; frames++; }
  void uploadGeometry(const GpuVertex* v, size_t vc, const uint32_t* i, size_t ic) override {
    if (!inFrame || (vc && !v) || (ic && !i) || ic % 3) badCalls++;
    for (size_t k = 0; k < ic; k++) if (i[k] >= vc) { badCalls++; break; }
    verts = vc; idx = ic;
  }
  void clear(Color, int, int, int sw, int sh) override { clears++; if (!inFrame || sw < 0 || sh < 0) badCalls++; }
  void draw(const GpuDrawCall& c) override {
    draws++;
    if (!inFrame || c.firstIndex + c.indexCount > idx || c.indexCount % 3) badCalls++;
    if (c.texture && !tex.count(c.texture)) badCalls++;
    if (c.sx < 0 || c.sy < 0 || c.sx + c.sw > fw || c.sy + c.sh > fh) badCalls++;
  }
  void endFrame() override { if (!inFrame) badCalls++; inFrame = false; }
};

// Complex scripts. System fonts are used when present (DejaVu Sans: Arabic / Hebrew, Loma: Thai, Noto Color Emoji);
// checks that need a missing font are skipped, never failed, so the suite runs on any machine.
static void testComplexText(Application& app) {
  SECTION("complex_text");
  std::printf("  shaping: %s, unicode algorithms: %s\n", textShapingAvailable() ? "HarfBuzz" : "built-in", unicodeAlgorithmsAvailable() ? "ICU" : "built-in");
  auto exists = [](const char* p) { return std::filesystem::exists(p); };
  // grapheme clusters: emoji modifiers, ZWJ families, flags and combining marks stay whole
  CHECK(graphemeBounds(U"a\U0001F44D\U0001F3FDb").size() == 4);                                 // a | 👍🏽 | b
  CHECK(graphemeBounds(U"\U0001F468\u200D\U0001F469\u200D\U0001F467").size() == 2);          // one family
  CHECK(graphemeBounds(U"e\u0301x").size() == 3);                                               // é (combining) | x
  if (unicodeAlgorithmsAvailable()) {
    CHECK(graphemeBounds(U"\U0001F1F5\U0001F1ED\U0001F1EF\U0001F1F5").size() == 3);          // 🇵🇭 | 🇯🇵
    // dictionary line breaking: Thai has no spaces between words, ICU still finds the word boundaries
    std::u32string thai = toU32("\xE0\xB8\xA0\xE0\xB8\xB2\xE0\xB8\xA9\xE0\xB8\xB2\xE0\xB9\x84\xE0\xB8\x97\xE0\xB8\xA2\xE0\xB8\x87\xE0\xB9\x88\xE0\xB8\xB2\xE0\xB8\xA2");   // ภาษาไทยง่าย
    auto ops = lineBreakOps(thai);
    int inner = 0;
    for (size_t i = 1; i < thai.size(); i++) inner += ops[i] == BreakOp::Allowed;
    CHECK(inner >= 2);
    // explicit isolates (U+2067 RLI ... U+2069 PDI) are honoured
    auto iso = resolveBidi(U"ab \u2067\u05D0\u05D1\u2069 cd");
    CHECK(!iso.rtlParagraph && iso.levels[4] == 1 && iso.levels[1] == 0);
  }
  if (textShapingAvailable() && exists("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")) {
    Font dv("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 18);
    FontStack fs; fs.add(dv);
    // Arabic joining: the letters of a word use contextual (initial / medial / final) forms, not their isolated glyphs
    const char* word = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";                      // سلام
    ShapedLine joined = shapeText(fs, word);
    CHECK(joined.rtl && joined.glyphs.size() >= 3);
    std::u32string w32 = toU32(word);
    int different = 0;
    for (auto& g : joined.glyphs) {
      std::string one; appendUtf8(one, w32[g.cluster]);
      ShapedLine iso = shapeText(fs, one);
      if (!iso.glyphs.empty() && iso.glyphs[0].key != g.key) different++;
    }
    CHECK(different >= 2);
    // RTL visual order: the first logical letter is drawn right-most
    CHECK(joined.glyphs.back().cluster == 0);
    // mixed direction line: Latin stays left-to-right around the Arabic run
    ShapedLine mixed = shapeText(fs, std::string("Hi ") + word + " ok");
    CHECK(!mixed.rtl && mixed.glyphs.front().cluster == 0 && mixed.glyphs.back().cluster == 9);
    // combining mark is positioned over its base, not after it
    ShapedLine mark = shapeText(fs, "e\xCC\x81");
    CHECK(mark.glyphs.size() == 1 || (mark.glyphs.size() == 2 && mark.glyphs[1].advance == 0));
    Canvas cv; cv.resize(300, 40); cv.clear(Color(0, 0, 0));
    float w = drawTextLine(cv, fs, {4, 4}, word, Color(255, 255, 255));
    CHECK(std::abs(w - joined.width) < 0.01f && w > 10);
    int lit = 0;
    for (int i = 0; i < 300 * 40; i++) lit += (cv.pixels()[i] & 0xFF) > 100;
    CHECK(lit > 20);
  }
  if (exists("/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf")) {
    Font emoji("/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf", 24);
    FontStack fs; fs.add(*theme().ui).add(emoji);
    CHECK(emoji.hasColor() && emoji.bitmapScale() < 1.f);
    const Glyph& g = emoji.glyph(0x1F600);
    CHECK(!g.argb.empty() && g.w > 10 && g.w <= 32);                                             // scaled from the 136px strike
    ShapedLine fam = shapeText(fs, "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7");   // 👨‍👩‍👧
    if (textShapingAvailable()) CHECK(fam.glyphs.size() == 1 && fam.glyphs[0].font == &emoji);   // one ZWJ ligature glyph
    Canvas cv; cv.resize(120, 40); cv.clear(Color(0, 0, 0));
    drawTextLine(cv, fs, {4, 4}, "ok \xF0\x9F\x98\x80", Color(255, 255, 255));                 // ok 😀
    int colourful = 0;
    for (int i = 0; i < 120 * 40; i++) { uint32_t p = cv.pixels()[i]; int r = (p >> 16) & 255, gg = (p >> 8) & 255, b = p & 255; colourful += std::abs(r - b) > 60 || std::abs(gg - b) > 60; }
    CHECK(colourful > 30);                                                                         // real colour, not a grey silhouette
    // the same line through the recorder and GPU batching: colour glyphs become blended image quads
    RecordingCanvas rc; rc.resize(120, 40); rc.beginFrame();
    drawTextLine(rc, fs, {4, 4}, "ok \xF0\x9F\x98\x80", Color(255, 255, 255));
    GlyphAtlas atlas(512); GpuFrame fr;
    CHECK(buildGpuFrame(rc.list(), atlas, fr));
    bool img = false, glyphQuads = false;
    for (auto& b : fr.batches) { if (b.texture == GpuBatch::Texture::Image && !b.replace) img = true; if (b.texture == GpuBatch::Texture::Atlas) glyphQuads = true; }
    CHECK(img && glyphQuads && fr.images.size() == 1);
    Canvas replay; replay.resize(120, 40); replay.clear(Color(0, 0, 0));
    rc.list().replay(replay);
    CHECK(std::equal(replay.pixels(), replay.pixels() + 120 * 40, cv.pixels()));                // replay is bit-exact
  }
  (void)app;
}

static void testMultiWindow(Application& app) {
  SECTION("multi_window");
  app.closeAllPopups();
  app.hostResize(800, 500);
  // main window: a text field
  auto mainCol = vbox(4);
  auto mainEdit = make<TextInput>();
  mainCol->add(mainEdit, {.pad = Margin(10)});
  app.setRoot(mainCol);
  app.tick(); app.render();
  // a torn-off tool window: a button that opens a menu, and its own text field
  auto toolCol = vbox(4);
  auto menuBtn = make<Button>("Options");
  auto toolEdit = make<TextInput>();
  int picked = 0;
  menuBtn->onClick = [&] { showContextMenu({MenuItem::Item("Pick", [&] { picked++; })}, {40, 40}); };
  toolCol->add(menuBtn, {.pad = Margin(10)});
  toolCol->add(toolEdit, {.pad = Margin(10)});
  int closed = -1;
  app.onWindowClosed = [&](int id) { closed = id; };
  OsWindowDesc d; d.title = "Tool"; d.width = 320; d.height = 240;
  int w = app.openWindow(toolCol, d);
  CHECK(w > 0 && app.hasWindow(w) && app.activeWindow() == w && app.windowOrder().back() == w);
  CHECK(app.root() == mainCol.get() && app.windowRoot(w) == toolCol.get());
  CHECK(app.windowNeedsRender(w));
  PixelView pv = app.renderWindow(w);
  CHECK(pv.width == 320 && pv.height == 240 && !app.windowNeedsRender(w));
  const uint32_t* toolPixels = pv.data;
  PixelView mainPv = app.render();
  CHECK(mainPv.data != toolPixels && mainPv.width == 800);            // separate frame buffers, no aliasing
  CHECK(app.renderWindow(w).data == toolPixels);                     // stable buffer, swapped not copied
  CHECK(app.windowSize().x == 800);                                  // the main canvas is untouched
  // focus is per window: each text field keeps its own focus and receives only its window's typing
  Rect er = mainEdit->rect();
  app.windowMouseButton(0, er.center(), MouseButton::Left, true); app.windowMouseButton(0, er.center(), MouseButton::Left, false);
  Rect tr = toolEdit->rect();
  app.windowMouseButton(w, tr.center(), MouseButton::Left, true); app.windowMouseButton(w, tr.center(), MouseButton::Left, false);
  CHECK(app.windowFocus(w) == toolEdit.get() && app.windowFocus(0) == mainEdit.get());
  app.windowText(w, "tool");
  app.windowText(0, "main");
  CHECK(toolEdit->text() == "tool" && mainEdit->text() == "main");
  CHECK(app.activeWindow() == w);                                    // the last click activated the tool window
  app.windowKey(0, Key::End, 0);
  CHECK(app.activeWindow() == 0 && app.windowOrder().back() == 0);   // key input from the OS focuses window 0
  // popups open in the window that raised them
  Rect br = menuBtn->rect();
  app.windowMouseButton(w, br.center(), MouseButton::Left, true); app.windowMouseButton(w, br.center(), MouseButton::Left, false);
  CHECK(!app.hasPopup());                                            // main window has no popup
  bool toolHasMenu = false;
  app.windowMouseMove(w, {60, 50});
  CHECK(app.windowNeedsRender(w));                                   // the menu opened inside the tool window
  // the menu item sits at the context-menu position inside the tool window
  app.windowMouseButton(w, {60, 50}, MouseButton::Left, true); app.windowMouseButton(w, {60, 50}, MouseButton::Left, false);
  toolHasMenu = picked == 1;
  CHECK(toolHasMenu);
  // a click in another window dismisses outside-dismissable popups of this one
  app.windowMouseButton(w, br.center(), MouseButton::Left, true); app.windowMouseButton(w, br.center(), MouseButton::Left, false);
  app.windowMouseButton(0, {700, 450}, MouseButton::Left, true); app.windowMouseButton(0, {700, 450}, MouseButton::Left, false);
  app.windowMouseButton(w, {60, 50}, MouseButton::Left, true); app.windowMouseButton(w, {60, 50}, MouseButton::Left, false);
  CHECK(picked == 1);                                                // the menu was gone, nothing picked
  // z-order and activation; resize; close
  app.activateWindow(w);
  CHECK(app.windowOrder().back() == w && app.activeWindow() == w);
  app.windowResize(w, 400, 300);
  CHECK(app.windowSizeOf(w).x == 400 && app.renderWindow(w).width == 400);
  // a host drawing every window on its own GPU backend: the frame is recorded at that window's size and the main
  // window's next frame at its own; the per-window questions are answered for that window
  {
    SoftwareBackend toolBackend;
    app.renderWindowTo(w, toolBackend);
    PixelView tb = toolBackend.readback();
    CHECK(tb.width == 400 && tb.height == 300 && !app.windowNeedsRender(w));
    const DrawList& mainList = app.renderToDrawList();
    CHECK(mainList.width == 800 && mainList.height == 500);
    CHECK(!app.windowHasBlockingPopup(w) && !app.windowHasBlockingPopup(0));
    app.windowMouseButton(w, br.center(), MouseButton::Left, true); app.windowMouseButton(w, br.center(), MouseButton::Left, false);
    CHECK(app.windowHasBlockingPopup(w) && !app.windowHasBlockingPopup(0));   // the menu belongs to the tool window
    CHECK(app.windowWantsKeyboard(w) && !app.windowWantsKeyboard(0));
    app.layoutWindow(w);
    CHECK(app.windowWantsPointerAt(w, {60, 50}));
    app.windowMouseButton(0, {700, 450}, MouseButton::Left, true); app.windowMouseButton(0, {700, 450}, MouseButton::Left, false);
    CHECK(!app.windowHasBlockingPopup(w));
  }
  int w2 = app.openWindow(make<Label>("Second"), OsWindowDesc{});
  CHECK(w2 != w && app.windowOrder().size() == 3 && app.activeWindow() == w2);
  app.closeWindow(w);
  CHECK(!app.hasWindow(w) && closed == w && app.windowOrder().size() == 2 && app.renderWindow(w).data == nullptr);
  app.closeWindow(w2);
  // a click in a secondary window that opens many windows (m_windows grows and moves) and closes its
  // own window: the window state swapped back afterwards is still the right one, focus included
  {
    auto opener = make<Button>("Open");
    int self = 0;
    std::vector<int> opened;
    opener->onClick = [&] {
      for (int i = 0; i < 12; i++) opened.push_back(app.openWindow(make<Label>("More"), OsWindowDesc{}));
      app.closeWindow(self);
    };
    auto col = vbox();
    col->add(opener, {.pad = Margin(10)});
    self = app.openWindow(col, OsWindowDesc{});
    app.layoutWindow(self);
    Rect orc = opener->rect();
    app.windowMouseButton(0, er.center(), MouseButton::Left, true); app.windowMouseButton(0, er.center(), MouseButton::Left, false);
    app.windowMouseButton(self, orc.center(), MouseButton::Left, true); app.windowMouseButton(self, orc.center(), MouseButton::Left, false);
    CHECK(opened.size() == 12 && !app.hasWindow(self));               // closed once the click returned to window 0
    CHECK(app.root() == mainCol.get() && app.focus() == mainEdit.get());   // window 0's own state came back
    for (int id : opened) CHECK(app.hasWindow(id) && app.windowRoot(id) != nullptr);
    for (int id : opened) app.closeWindow(id);
  }
  // PRISM chrome: a borderless window with our own title bar (minimize / maximize-restore / close)
  {
    auto* hp = dynamic_cast<HeadlessPlatform*>(&app.platform());
    OsWindowDesc cd; cd.title = "Chromed"; cd.width = 400; cd.height = 300; cd.prismChrome = true;
    auto body = make<Label>("Body");
    int cw = app.openWindow(body, cd);
    CHECK(cw > 0 && app.windowRoot(cw) != body.get());               // wrapped in the frame
    app.renderWindow(cw);
    if (hp) {
      CHECK(hp->captions.count(cw) && hp->captions[cw].w > 100 && hp->captions[cw].y < 4);   // drag strip reported to the OS
      Vec2 closeP{375, 16}, maxP{337, 16}, minP{299, 16};
      CHECK(!hp->captions[cw].contains(closeP) && !hp->captions[cw].contains(minP));         // buttons are not draggable
      auto click = [&](Vec2 p) { app.windowMouseMove(cw, p); app.windowMouseButton(cw, p, MouseButton::Left, true); app.windowMouseButton(cw, p, MouseButton::Left, false); };
      click(minP);
      CHECK(hp->minimized[cw]);
      click(maxP);
      CHECK(hp->windowMaximized(cw));
      app.renderWindow(cw);                                          // restore glyph paints
      click(maxP);
      CHECK(!hp->windowMaximized(cw));
      click(closeP);
      CHECK(app.hasWindow(cw));                                      // closing is deferred out of the click handler
      for (int i = 0; i < 5 && app.hasWindow(cw); i++) { app.tick(); }
      CHECK(!app.hasWindow(cw));
    } else app.closeWindow(cw);
  }
  app.closeWindow(0);                                                // the main window cannot be closed this way
  CHECK(app.windowOrder().size() == 1 && app.activeWindow() == 0 && app.hasWindow(0));
  app.onWindowClosed = nullptr;
  app.render();
}

static void testDeviceBackend(Application& app) {
  SECTION("device_backend");
  auto dev = std::make_shared<FakeDevice>();
  {
    DeviceBackend be(dev);
    app.closeAllPopups();
    app.hostResize(1300, 700);
    auto g = make<BlueprintGraph>();
    g->setPreset(BlueprintGraphPreset::AnimationGraph);
    app.setRoot(g);
    app.tick(); app.render();
    RecordingCanvas rc;
    rc.resize(1300, 700);
    rc.beginFrame();
    g->paint(rc);
    Canvas img; img.resize(64, 32); img.clear(Color(200, 40, 40));
    rc.blit(img.pixels(), 64, 32, {10, 10});                   // an image batch (host viewport / thumbnail)
    be.submit(rc.list());
    auto s1 = be.lastFrame();
    CHECK(dev->frames == 1 && dev->badCalls == 0);
    CHECK(s1.draws > 0 && s1.vertices > 0 && s1.indices > 0 && s1.atlasUploads == 1 && s1.imageUploads == 1);
    CHECK(dev->draws == s1.draws && dev->idx == s1.indices);
    size_t texAfter1 = dev->tex.size();
    be.submit(rc.list());                                       // same frame again: nothing re-uploaded but the image
    CHECK(dev->frames == 2 && dev->badCalls == 0 && be.lastFrame().atlasUploads == 0 && be.totalAtlasUploads() == 1);
    CHECK(dev->tex.size() == texAfter1);                        // textures are pooled, not leaked per frame
    CHECK(!be.readback());                                      // a device without readback reports none
  }
  CHECK(dev->tex.empty() && dev->destroys == dev->creates);    // the backend releases everything it created
}

static void testGoldenImages(Application& app, const std::string& assets) {
  SECTION("golden_images");
  const std::string dir = assets + "/../tests/golden";
  app.closeAllPopups();
  app.hostResize(1300, 700);
  // blueprint editor: two graph types (toolbar, crumbs, nodes, wires, icons, watermark, minimap)
  auto g = make<BlueprintGraph>();
  app.setRoot(g);
  for (auto [preset, name] : {std::pair{BlueprintGraphPreset::EventBlueprint, "blueprint_event"}, std::pair{BlueprintGraphPreset::AnimationGraph, "blueprint_anim"}}) {
    g->setPreset(preset);
    app.tick(); PixelView v = app.render();
    CHECK(goldenCheck(dir, name, v.data, v.width, v.height, v.stride));
  }
  // gizmos (all five modes) on a neutral canvas
  Canvas cv; cv.resize(1000, 200);
  cv.clear(Color(30, 32, 36));
  int k = 0;
  for (GizmoMode m : {GizmoMode::Move, GizmoMode::Rotate, GizmoMode::Scale, GizmoMode::Universal, GizmoMode::Dots}) {
    TransformGizmo gz;
    GizmoCamera cam;
    Vec3 eye{6, 4.5f, 8};
    cam.eye = eye; cam.fwd = (Vec3{0, 0, 0} - eye).normalized(); cam.right = cam.fwd.cross({0, 1, 0}).normalized(); cam.up = cam.right.cross(cam.fwd);
    cam.focal = 300; cam.cx = 100.f + float(k) * 200.f; cam.cy = 100; cam.viewH = 200;
    gz.setCamera(cam); gz.mode = m; gz.size = 1.8f; gz.setTarget(Mat4::scaling({2, 2, 2}));
    gz.draw(cv);
    k++;
  }
  CHECK(goldenCheck(dir, "gizmos", cv.pixels(), cv.width(), cv.height(), cv.width()));
  // the whole icon set at 16 px and 32 px
  Canvas ic; ic.resize(32 * 40, 8 * 40);
  ic.clear(Color(36, 38, 42));
  for (int i = 1; i < 128; i++) {
    float x = float((i % 32) * 40), y = float((i / 32) * 40);
    drawIcon(ic, Icon(i), {x + 12, y + 12, 16, 16}, Color(220, 222, 226), 1.3f);
    drawIcon(ic, Icon(i), {x + 4, y + 164, 32, 32}, Color(220, 222, 226), 2.f);
  }
  CHECK(goldenCheck(dir, "icons", ic.pixels(), ic.width(), ic.height(), ic.width()));
  // one glyph per concept: every dedicated icon draws something, and no two icons in the set draw the same picture
  {
    const int first = int(Icon::AnimClip), last = int(Icon::BlueprintClass);
    std::vector<uint64_t> hashes;
    for (int i = 1; i <= last; i++) {
      Canvas one; one.resize(32, 32);
      one.clear(Color(0, 0, 0));
      drawIcon(one, Icon(i), {0, 0, 32, 32}, Color(255, 255, 255), 2.f);
      uint64_t h = 1469598103934665603ull;
      int lit = 0;
      for (int k = 0; k < 32 * 32; k++) { h = (h ^ one.pixels()[k]) * 1099511628211ull; lit += (one.pixels()[k] & 0xFFu) > 40u; }
      if (i >= first) CHECK(lit > 12);
      hashes.push_back(h);
    }
    for (int i = first; i <= last; i++)
      for (int j = 1; j <= last; j++)
        if (j != i) CHECK(hashes[size_t(i - 1)] != hashes[size_t(j - 1)]);
  }
  // the comparison itself: an unchanged frame matches, a visibly changed one does not
  double mean = 0;
  CHECK(goldenCheck(dir, "gizmos", cv.pixels(), cv.width(), cv.height(), cv.width(), &mean) && mean < 0.5);   // goldens are stored rounded to integers
  Canvas changed; changed.resize(cv.width(), cv.height());
  std::copy(cv.pixels(), cv.pixels() + cv.width() * cv.height(), changed.pixels());
  changed.fillRect({300, 40, 220, 120}, Color(255, 0, 255));
  if (!std::getenv("PRISM_UPDATE_GOLDEN")) CHECK(!goldenCheck(dir, "gizmos", changed.pixels(), changed.width(), changed.height(), changed.width(), nullptr, false));
  app.closeAllPopups();
}

static void testFuzzLoaders(Application& app, const std::string& assets) {
  SECTION("fuzz_loaders");
  Fuzz fz{0xC0FFEEu};
  // blueprint documents (incl. subgraph + breakpoint + variable records)
  auto g = make<BlueprintGraph>();
  g->setPreset(BlueprintGraphPreset::EventBlueprint);
  g->addVariable("Health", BlueprintPinType::Float, "100");
  g->clearSelection();
  int picked = 0;
  for (auto& w : g->wires()) if (!picked) { g->select(w.fromNodeId, true); g->select(w.toNodeId, true); picked = 1; }
  g->collapseSelected();
  g->toggleBreakpoint(g->nodes().front().id);
  const std::string doc = g->serialize();
  auto probe = make<BlueprintGraph>();
  int accepted = 0;
  for (int i = 0; i < 2500; i++) {
    std::string m = fz.mutate(doc);
    if (probe->deserialize(m)) {
      accepted++;
      std::string again = probe->serialize();                    // accepted documents are stable and executable
      auto p2 = make<BlueprintGraph>();
      if (!p2->deserialize(again)) { CHECK(false); break; }
      BlueprintVM vm(probe->program());
      vm.maxSteps = 2000;
      vm.fireAll();
      probe->compile();
    }
  }
  CHECK(accepted > 0 && accepted < 2500);                        // the fuzzer exercises both paths
  CHECK(probe->deserialize(doc) && probe->serialize() == doc);   // and the loader still round-trips a clean file
  // themes, style sheets, locale tables
  const std::string themeDoc = slurp(assets + "/themes/midnight.json"), locDoc = slurp(assets + "/locales/en-US.json");
  CHECK(!themeDoc.empty() && !locDoc.empty());
  const std::string styleDoc = "{\"Button\":{\"normal\":\"#2a2c30\",\"hover\":\"#34373c\",\"padding\":[8,4,8,4],\"radius\":3},\"Panel\":{\"normal\":\"#1e1f22\"}}";
  for (int i = 0; i < 1500; i++) {
    Theme t = Theme::dark();
    std::string err;
    t.applyJson(fz.mutate(themeDoc), &err);
    StringTable st;
    st.loadFromJson(fz.mutate(locDoc));
    StyleSheet::get().loadFromJson(fz.mutate(styleDoc));   // singleton: this test runs last so later tests see no stray styles
  }
  // small parsers
  IconAtlas at;
  for (int i = 0; i < 1500; i++) {
    Chord ch; Chord::parse(fz.mutate("Ctrl+Shift+F5"), ch);
    SvgPath::parse(fz.mutate("M12 5 V19 C1 2 3 4 5 6 Q7 8 9 10 Z H3 S1 1 2 2 T4 4"));
    at.addSvg("x", fz.mutate("<svg viewBox=\"0 0 16 16\" stroke-width=\"1.5\"><path d=\"M3 8 L7 12 L13 4\"/></svg>"));
    at.loadManifest(fz.mutate("plus | 24 | 2 | 0 | M12 5 V19 ; M5 12 H19\n"));
    LogView::Source src; LogView::findSource(fz.mutate("[Render] shader.cpp:42 failed (see Material.h:7)"), src);
    std::string junk = fz.mutate("ab \xD7\x90\xD7\x91 \xE6\x97\xA5\xE6\x9C\xAC 123 (cd)\n-x");
    auto u = toU32(junk);
    resolveBidi(u); lineBreakOps(u);
    FontStack fs; fs.add(*theme().ui).add(*theme().mono);
    wrapText(fs, junk, float(fz.next() % 200));
  }
  at.bake({1.f}, 16);
  CHECK(true);                                                   // reaching here = no crash / hang in 12k hostile inputs
  // random programs: the compiler + VM never crash on arbitrary wiring
  std::vector<BlueprintNode> lib;
  int id = 1;
  for (auto& sp : blueprintNodeLibrary()) { BlueprintNode n = sp.make(); n.id = id++; lib.push_back(n); }
  for (int i = 0; i < 400; i++) {
    std::vector<BlueprintWire> ws;
    for (int k = 0; k < 60; k++) {
      BlueprintWire w;
      w.fromNodeId = int(fz.next() % (lib.size() + 3)); w.toNodeId = int(fz.next() % (lib.size() + 3));
      w.fromPinIndex = int(fz.next() % 5) - 1; w.toPinIndex = int(fz.next() % 5) - 1;
      ws.push_back(w);
    }
    BlueprintVM vm(compileBlueprint(lib, ws));
    vm.maxSteps = 3000;
    vm.fireAll();
  }
  CHECK(true);
  (void)app;
}

static BlueprintNode libNode(const char* name, int id, float y = 0) {
  for (auto& sp : blueprintNodeLibrary()) if (sp.name == name) { BlueprintNode n = sp.make(); n.id = id; n.pos = {0, y}; return n; }
  std::printf("  FAIL library node %s missing\n", name);
  return {};
}

static void testBlueprintVM(Application& app) {
  SECTION("blueprint_vm");
  // BeginPlay -> For Loop(1..4) -> body: Set Score = Score + Index ; completed -> Branch(Score > 9) -> Print "big" / "small"
  std::vector<BlueprintNode> n;
  n.push_back(libNode("Event BeginPlay", 1));
  n.push_back(libNode("For Loop", 2));
  n.push_back(libNode("Set Score", 3));
  n.push_back(libNode("Add (+)", 4));
  n.push_back(libNode("Get Score", 5));
  n.push_back(libNode("Branch", 6));
  n.push_back(libNode("Greater (>)", 7));
  n.push_back(libNode("Print String", 8));
  n.push_back(libNode("Print String", 9));
  n[1].inputs[1].defaultValue = "1"; n[1].inputs[2].defaultValue = "4";
  n[6].inputs[1].defaultValue = "9";
  n[7].inputs[1].defaultValue = "big"; n[8].inputs[1].defaultValue = "small";
  auto W = [](int a, int ap, int b, int bp) { BlueprintWire w; w.fromNodeId = a; w.fromPinIndex = ap; w.toNodeId = b; w.toPinIndex = bp; return w; };
  std::vector<BlueprintWire> w{
    W(1, 0, 2, 0),            // BeginPlay -> For Loop
    W(2, 0, 3, 0),            // Loop Body -> Set Score
    W(5, 0, 4, 0), W(2, 1, 4, 1), W(4, 0, 3, 1),   // Score + Index -> Set
    W(2, 2, 6, 0),            // Completed -> Branch
    W(5, 0, 7, 0), W(7, 0, 6, 1),                   // Score > 9 -> Condition
    W(6, 0, 8, 0), W(6, 1, 9, 0)};                  // True -> big, False -> small
  std::vector<BlueprintVariable> vars{{"Score", BlueprintPinType::Int, "0"}};
  BpProgram prog = compileBlueprint(n, w, vars);
  CHECK(prog.ok() && prog.events.size() == 1);
  BlueprintVM vm(prog);
  CHECK(vm.fire("Event BeginPlay") && !vm.fire("Event Nope"));
  CHECK(vm.variable("Score").number() == 10);                       // 1+2+3+4: the loop really ran
  CHECK(vm.log().size() == 1 && vm.log()[0] == "big");              // the branch took the True pin
  CHECK(std::count(vm.trace().begin(), vm.trace().end(), 3) == 4);   // Set Score executed four times
  CHECK(std::find(vm.trace().begin(), vm.trace().end(), 9) == vm.trace().end() && !vm.aborted() && vm.unhandled().empty());
  // variables persist across events; reset() clears the run but keeps them
  vm.reset();
  vm.setVariable("Score", BpValue::number(-50));
  vm.fire("Event BeginPlay");
  CHECK(vm.variable("Score").number() == -40 && vm.log()[0] == "small");
  // natives: the host implements engine functions, reads pins by name, writes outputs, picks the exec pin
  std::vector<BlueprintNode> g{libNode("Event AnyDamage", 1), libNode("Apply Damage", 2), libNode("Cast To Player", 3), libNode("Print String", 4), libNode("Multiply (*)", 5)};
  g[4].inputs[1].defaultValue = "2";
  std::vector<BlueprintWire> gw{W(1, 0, 3, 0), W(1, 2, 3, 1), W(3, 0, 2, 0), W(3, 2, 2, 1), W(1, 1, 5, 0), W(5, 0, 2, 2), W(2, 0, 4, 0), W(2, 1, 4, 1)};
  BlueprintVM v2(compileBlueprint(g, gw));
  std::string target; double dealt = 0;
  v2.registerNative("Apply Damage", [&](BpCall& c) { target = c.in("Damaged Actor").text(); dealt = c.in("Base Damage").number(); c.out("Damage Applied", BpValue::number(dealt)); });
  v2.fire("Event AnyDamage", {BpValue::number(21), BpValue::string("Hero")});
  CHECK(target == "Hero" && dealt == 42);                           // damage * 2, object passed through the cast
  CHECK(v2.log().size() == 1 && v2.log()[0] == "42");               // native output feeds the next node
  v2.reset();
  v2.fire("Event AnyDamage", {BpValue::number(5), BpValue::string("")});
  CHECK(dealt == 42 && v2.log().empty());                           // empty object -> Cast Failed, chain stops
  // unregistered natives are reported, not silently ignored; runaway loops stop at the step budget
  std::vector<BlueprintNode> u{libNode("Event BeginPlay", 1), libNode("Destroy Actor", 2), libNode("Print String", 3)};
  BlueprintVM v3(compileBlueprint(u, {W(1, 0, 2, 0), W(2, 0, 3, 0)}));
  v3.fireAll();
  CHECK(v3.unhandled().count("Destroy Actor") == 1 && v3.log().size() == 1);
  std::vector<BlueprintNode> lp{libNode("Event BeginPlay", 1), libNode("Print String", 2), libNode("Sequence", 3)};
  BlueprintVM v4(compileBlueprint(lp, {W(1, 0, 2, 0), W(2, 0, 3, 0), W(3, 0, 2, 0)}));
  v4.maxSteps = 500;
  v4.fireAll();
  CHECK(v4.aborted() && v4.trace().size() <= 501);
  // compile errors: bad wires, data cycles, divide by zero is defined
  CHECK(!compileBlueprint(lp, {W(1, 0, 77, 0)}).ok() && !compileBlueprint(lp, {W(1, 5, 2, 0)}).ok());
  std::vector<BlueprintNode> cy{libNode("Add (+)", 1), libNode("Add (+)", 2)};
  CHECK(!compileBlueprint(cy, {W(1, 0, 2, 0), W(2, 0, 1, 0)}).ok());
  std::vector<BlueprintNode> dz{libNode("Event BeginPlay", 1), libNode("Print String", 2), libNode("Divide (/)", 3)};
  dz[2].inputs[1].defaultValue = "0";
  BlueprintVM v5(compileBlueprint(dz, {W(1, 0, 2, 0), W(3, 0, 2, 1)}));
  v5.fireAll();
  CHECK(v5.log().size() == 1 && v5.log()[0] == "0");
  CHECK(BpValue::parse(BlueprintPinType::Vector, "1,2,3").v[2] == 3 && BpValue::boolean(true).text() == "true" && BpValue::number(2.5).text() == "2.5");

  // ---- through the editor widget: Simulate runs the VM; collapsed subgraphs still execute; everything persists
  app.closeAllPopups();
  auto bg = make<BlueprintGraph>();
  app.setRoot(bg); app.tick(); app.render();
  bg->clear();
  for (auto& x : n) bg->addNode(x);
  for (auto& x : w) bg->connectPins(x.fromNodeId, x.fromPinIndex, x.toNodeId, x.toPinIndex, BlueprintPinType::Exec);
  bg->addVariable("Score", BlueprintPinType::Int, "0");
  {
    BlueprintVM gv(bg->program());
    gv.fireAll();
    CHECK(gv.variable("Score").number() == 10 && gv.log().size() == 1 && gv.log()[0] == "big");
  }
  bg->clearSelection();
  bg->select(3, true); bg->select(4, true);                        // collapse "Set Score" + "+"
  CHECK(bg->collapseSelected());
  int collapsed = bg->selectedIds()[0];
  {
    BlueprintVM gv(bg->program());                                  // program() flattens the subgraph back in
    gv.fireAll();
    CHECK(gv.program().ok() && gv.variable("Score").number() == 10 && gv.log()[0] == "big");
  }
  bg->toggleBreakpoint(6);
  std::string saved = bg->serialize();
  CHECK(saved.find("\nS\t") != std::string::npos && saved.find("\nB\t6") != std::string::npos);
  auto bg2 = make<BlueprintGraph>();
  CHECK(bg2->deserialize(saved));
  CHECK(bg2->isCollapsed(collapsed) && bg2->hasBreakpoint(6) && bg2->variables().size() == 1);
  {
    BlueprintVM gv(bg2->program());
    gv.fireAll();
    CHECK(gv.variable("Score").number() == 10);                     // the reloaded file runs identically
  }
  CHECK(bg2->expandNode(collapsed) && bg2->nodeCount() == 9 && !bg2->isCollapsed(collapsed));
  CHECK(bg2->undo() && bg2->isCollapsed(collapsed) && bg2->hasBreakpoint(6));   // undo restores the subgraph, keeps breakpoints
  // a corrupted subgraph block rejects the file and leaves the graph untouched
  std::string bad = saved;
  size_t sp = bad.find("\nS\t");
  bad.insert(bad.find('\t', sp + 3) + 1, "garbage");
  int before = bg2->nodeCount();
  CHECK(!bg2->deserialize(bad) && bg2->nodeCount() == before);
  bg->run();
  CHECK(bg->running() && bg->runLog().size() == 1 && bg->runLog()[0] == "big" && bg->runIssues().empty());
  bg->stopRun();
  app.closeAllPopups();
}

static void testGizmo(Application& app) {
  SECTION("gizmo");
  TransformGizmo g;
  GizmoCamera cam;                               // looking down -Z from z = 10 at an 800x600 viewport
  cam.eye = {0, 0, 10}; cam.right = {1, 0, 0}; cam.up = {0, 1, 0}; cam.fwd = {0, 0, -1};
  cam.focal = 600; cam.cx = 400; cam.cy = 300; cam.viewH = 600;
  g.setCamera(cam);
  g.setTarget(Mat4::identity());
  g.snap = false;
  Vec2 o, tipX, tipY;
  CHECK(cam.project({0, 0, 0}, o) && o.x == 400 && o.y == 300);
  float L = g.length();
  CHECK(L > 0.5f && L < 3.f);
  cam.project({L * 0.6f, 0, 0}, tipX); cam.project({0, L * 0.6f, 0}, tipY);
  // Move: X arrow, Y arrow, empty space
  g.mode = GizmoMode::Move;
  CHECK(g.hit(tipX) == 0 && g.hit(tipY) == 1 && g.hit({50, 50}) == -1);
  CHECK(g.begin(0, tipX) && g.dragging() && g.activeOp() == GizmoOp::Move);
  GizmoDelta d = g.drag(tipX + Vec2(60, 0));
  CHECK(d.op == GizmoOp::Move && std::abs(d.translate.x - 1.0f) < 0.02f && d.translate.y == 0 && d.translate.z == 0);   // 60px at z=10, f=600 -> 1 unit
  CHECK(!g.label().empty());
  g.snap = true; g.moveSnap = 0.5f;
  CHECK(std::abs(g.drag(tipX + Vec2(40, 0)).translate.x - 0.5f) < 1e-4f);     // 0.67 snaps to 0.5
  g.end();
  CHECK(!g.dragging() && g.label().empty());
  g.snap = false;
  // Scale: axis bead is per-axis, core / modifier is uniform
  g.mode = GizmoMode::Scale;
  CHECK(g.hit(o) == 3);
  g.begin(0, tipX);
  d = g.drag(tipX + Vec2(60, 0));
  CHECK(d.op == GizmoOp::Scale && d.scale.x > 1.2f && d.scale.y == 1 && d.scale.z == 1);
  d = g.drag(tipX + Vec2(60, 0), true);
  CHECK(d.axis == 3 && d.scale.x == d.scale.y && d.scale.y == d.scale.z);
  g.end();
  // Rotate: a quarter arc exists for each axis; dragging along it yields an angle about that axis only
  g.mode = GizmoMode::Rotate;
  Vec2 arcZ;                                      // arc of axis Z spans X..Y in the screen plane
  cam.project(Vec3{0.7071f, 0.7071f, 0} * (L * 0.85f), arcZ);
  CHECK(g.hit(arcZ) == 2);
  g.begin(2, arcZ);
  d = g.drag(arcZ + Vec2(-30, -30));
  CHECK(d.op == GizmoOp::Rotate && d.axis == 2 && std::abs(d.angleDeg) > 5.f && d.translate.x == 0);
  float a1 = d.angleDeg;
  CHECK(g.drag(arcZ + Vec2(30, 30)).angleDeg * a1 < 0);                       // opposite drag, opposite sign
  g.end();
  // Universal = move + rotate (no scale handles); ids carry the operation
  g.mode = GizmoMode::Universal;
  L = g.length();
  Vec2 mid, arrow, arc;
  cam.project({L * 0.5f, 0, 0}, mid); cam.project({L * 0.9f, 0, 0}, arrow); cam.project(Vec3{0.7071f, 0.7071f, 0} * (L * 0.64f), arc);
  CHECK(g.hit(mid) == 0 && g.hit(arrow) == 0 && g.hit(arc) == 12 && g.hit(o) == -1);   // mid-shaft is just the arrow now
  // camera looks straight down Z: the Z arrow is a dot on screen and the X / Y arcs are lines, none may steal clicks
  Vec2 pz; cam.project({0, 0, L * 0.6f}, pz);
  CHECK(g.hit(pz) == -1);
  CHECK(!g.begin(20, mid) && !g.begin(3, o) && !g.dragging());                         // old scale ids are rejected
  g.begin(12, arc); CHECK(g.activeOp() == GizmoOp::Rotate); g.end();
  g.begin(0, arrow); CHECK(g.activeOp() == GizmoOp::Move); g.end();
  // Dots: only dots, no move gizmo; press + drag scales (faces: own axis, corners: uniform)
  g.mode = GizmoMode::Dots;
  g.snap = false;
  g.setTarget(Mat4::scaling({4, 4, 4}));
  Vec2 top;
  cam.project({0, 2, 0}, top);
  int id = g.hit(top);
  CHECK(TransformGizmo::isDot(id) && TransformGizmo::dotIndex(id) == 3);          // +Y face centre
  CHECK(g.hit(o) != 30);                                                          // pivot is a marker, not a handle (the far corner may sit behind it)
  CHECK(g.hit(top + Vec2(0, -40)) == -1);                                         // no arrows anywhere
  Vec2 sc; cam.project({0, 0, 0}, sc);
  Vec2 dirY = (top - sc) / (top - sc).length();
  CHECK(g.begin(id, top) && g.activeOp() == GizmoOp::Scale);
  GizmoDelta gd = g.drag(top + dirY * 30.f);
  CHECK(gd.op == GizmoOp::Scale && gd.axis == 1 && gd.scale.y > 1.f && gd.scale.x == 1.f && gd.scale.z == 1.f);
  gd = g.drag(top - dirY * 20.f);
  CHECK(gd.scale.y < 1.f);                                                        // inward shrinks
  g.end();
  Vec2 corner; cam.project({2, 2, 2}, corner);
  int cid = g.hit(corner);
  CHECK(TransformGizmo::isDot(cid) && TransformGizmo::dotIndex(cid) >= 7);
  CHECK(g.begin(cid, corner));
  gd = g.drag(corner + (corner - sc) / (corner - sc).length() * 25.f);
  CHECK(gd.axis == 3 && gd.scale.x > 1.f && gd.scale.x == gd.scale.y && gd.scale.y == gd.scale.z);
  g.end();
  g.dotCount = 1;
  CHECK(g.hit(top) == -1 && g.hit(o) == -1);                                      // lights / cameras: nothing to scale
  // drawing never crashes, including with the target behind the camera
  Canvas cv; cv.resize(800, 600); cv.clear(Color(0, 0, 0));
  for (GizmoMode m : {GizmoMode::Move, GizmoMode::Rotate, GizmoMode::Scale, GizmoMode::Universal, GizmoMode::Dots}) { g.mode = m; g.dotCount = 15; g.draw(cv); }
  g.setTarget(Mat4::translation({0, 0, 50}));
  g.draw(cv);
  CHECK(g.hit(o) == -1);
  (void)app;
}

static void testTextLayout(Application& app) {
  SECTION("text_layout");
  const Theme& th = theme();
  FontStack fs; fs.add(*th.ui).add(*th.mono);
  std::u32string heb = U"\u05D0\u05D1\u05D2";
  CHECK(toUtf8(toU32("a\xD7\x90z")) == "a\xD7\x90z" && toU32("\xE6\x97\xA5").size() == 1);
  CHECK(fs.fontFor('a') == th.ui.get());
  CHECK(fs.fontFor(0x05D0) == th.mono.get() || !th.mono->has(0x05D0));      // falls back where the primary lacks the glyph
  CHECK(fs.width("abc") > 0 && fs.lineHeight() >= th.ui->lineHeight());
  // bidi
  auto b0 = resolveBidi(U"abc");
  CHECK(!b0.rtlParagraph && (b0.visual == std::vector<int>{0, 1, 2}));
  auto b1 = resolveBidi(heb);
  CHECK(b1.rtlParagraph && (b1.visual == std::vector<int>{2, 1, 0}));
  auto b2 = resolveBidi(U"ab \u05D0\u05D1 cd");
  CHECK(!b2.rtlParagraph && (b2.visual == std::vector<int>{0, 1, 2, 4, 3, 5, 6, 7}));   // only the Hebrew run flips
  auto b3 = resolveBidi(U"\u05D0\u05D1 123");
  CHECK(b3.rtlParagraph && (b3.visual == std::vector<int>{3, 4, 5, 2, 1, 0}));          // digits stay left-to-right
  CHECK(resolveBidi(U"abc", 1).rtlParagraph && resolveBidi(U"abc", 1).visual.size() == 3);
  CHECK(mirrorOf('(') == ')' && mirrorOf('a') == 'a' && isRtl(0x05D0) && !isRtl('a'));
  // line break opportunities
  auto o1 = lineBreakOps(U"hello world");
  CHECK(o1.size() == 12 && o1[6] == BreakOp::Allowed && o1[3] == BreakOp::None && o1[11] == BreakOp::Mandatory);
  CHECK(lineBreakOps(U"a\nb")[2] == BreakOp::Mandatory);
  auto o2 = lineBreakOps(U"\u65E5\u672C\u8A9E");
  CHECK(o2[1] == BreakOp::Allowed && o2[2] == BreakOp::Allowed);
  CHECK(lineBreakOps(U"\u65E5\u3002\u672C")[1] == BreakOp::None);                    // never start a line with 。
  CHECK(lineBreakOps(U"well-known")[5] == BreakOp::Allowed);
  CHECK(lineBreakOps(U"a\u00A0b")[2] == BreakOp::None);                               // no-break space
  // wrapping
  float w = fs.width("aaa bbb");
  auto l1 = wrapText(fs, "aaa bbb ccc", w + 1);
  CHECK(l1.size() == 2 && l1[0].text == "aaa bbb" && l1[1].text == "ccc");
  auto l2 = wrapText(fs, "wwwwwwwwww", fs.width("www") + 1);
  bool fits = true; std::string joined;
  for (auto& l : l2) { if (l.width > fs.width("www") + 1) fits = false; joined += l.text; }
  CHECK(l2.size() >= 3 && fits && joined == "wwwwwwwwww");                        // unbreakable word is split, nothing lost
  auto l3 = wrapText(fs, "one\ntwo", 1000);
  CHECK(l3.size() == 2 && l3[0].text == "one" && l3[1].text == "two");
  CHECK(wrapText(fs, "", 100).size() == 1);
  // drawing (no crash, width equals the stack's measure for LTR)
  Canvas cv; cv.resize(300, 40); cv.clear(Color(0, 0, 0));
  float dw = drawTextLine(cv, fs, {2, 4}, "abc def", Color(255, 255, 255));
  CHECK(std::abs(dw - fs.width("abc def")) < 0.01f);
  drawTextLine(cv, fs, {2, 20}, "ab \xD7\x90\xD7\x91 (cd)", Color(255, 255, 255));
  (void)app;
}

static void testIconAtlas(Application& app) {
  SECTION("icon_atlas");
  IconAtlas at;
  int n = at.loadManifest("# icons\nplus | 24 | 2 | 0 | M12 5 V19 ; M5 12 H19\nsquare | 24 | 1 | 1 | M5 5 H19 V19 H5 Z\nbad | 24 | 2 | 0 |\n");
  CHECK(n == 2 && at.iconCount() == 2);
  CHECK(at.addSvg("tick", "<svg viewBox=\"0 0 16 16\" stroke-width=\"1.5\" fill=\"none\"><path d=\"M3 8 L7 12 L13 4\"/></svg>"));
  CHECK(!at.addSvg("empty", "<svg viewBox=\"0 0 8 8\"></svg>"));
  at.bake({1.f, 1.5f, 2.f}, 16);
  CHECK(at.baked() && at.width() >= 256);
  const IconAtlas::Entry* e1 = at.find("plus", 1.f);
  const IconAtlas::Entry* e2 = at.find("plus", 2.f);
  CHECK(e1 && e2 && e1->w == 16 && e2->w == 32);
  CHECK(at.find("plus", 1.9f)->w == 32 && !at.find("nope", 1));
  CHECK(at.coverage(*e1, 8, 8) > 200 && at.coverage(*e1, 1, 1) == 0);      // stroke centre is solid, corner empty
  CHECK(at.coverage(*at.find("square", 1), 8, 8) > 200);                    // filled shape
  // no two entries overlap (gutters keep resampling clean)
  bool overlap = false;
  auto names = at.names();
  std::vector<const IconAtlas::Entry*> all;
  for (auto& nm : names) for (float sc : {1.f, 1.5f, 2.f}) all.push_back(at.find(nm, sc));
  for (size_t i = 0; i < all.size(); i++) for (size_t j = i + 1; j < all.size(); j++) {
    auto *a = all[i], *b = all[j];
    if (a != b && a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h) overlap = true;
  }
  CHECK(!overlap);
  // tinted draw, including resampling to a box that differs from the baked size
  Canvas cv; cv.resize(40, 40); cv.clear(Color(0, 0, 0));
  CHECK(at.draw(cv, "plus", {4, 4, 16, 16}, Color(255, 0, 0)));
  uint32_t mid = cv.pixels()[12 * 40 + 12];
  CHECK(((mid >> 16) & 0xFF) > 200 && (mid & 0xFF) < 40);                    // red, not white
  CHECK(at.draw(cv, "plus", {20, 20, 20, 20}, Color(0, 255, 0)) && !at.draw(cv, "missing", {0, 0, 8, 8}, Color(1, 1, 1)));
  CHECK(at.savePng((std::filesystem::temp_directory_path() / "prism_atlas_test.png").string()));
  (void)app;
}

static void testAccessibility(Application& app) {
  SECTION("accessibility");
  auto col = vbox();
  auto lbl = make<Label>("Name");
  auto btn = make<Button>("Apply");
  auto chk = make<CheckBox>("Enabled", true);
  auto ti = make<TextInput>();
  ti->setAccessName("Search");
  auto hidden = make<Button>("Ghost"); hidden->setVisible(false);
  col->add(lbl); col->add(btn); col->add(chk); col->add(ti); col->add(hidden);
  col->measure({300, 300}); col->arrange({0, 0, 300, 300});
  AccessNode t = buildAccessTree(col.get());
  CHECK(t.role == AccessRole::Window && t.children.size() == 4);   // hidden widget omitted
  CHECK(findAccessNode(t, "Apply") && findAccessNode(t, "Apply")->role == AccessRole::Button);
  CHECK(findAccessNode(t, "Enabled")->checked && findAccessNode(t, "Enabled")->role == AccessRole::CheckBox);
  CHECK(findAccessNode(t, "Search")->role == AccessRole::TextBox);
  CHECK(!findAccessNode(t, "Ghost"));
  std::string d = dumpAccessTree(t);
  CHECK(d.find("button \"Apply\"") != std::string::npos && d.find("[checked]") != std::string::npos);
  // keyboard-only: focus order skips non-focusables/hidden, Space activates
  auto order = focusOrder(col.get());
  CHECK(order.size() == 3 && order[0] == btn.get() && order[2] == ti.get());
  int clicks = 0; btn->onClick = [&] { clicks++; };
  KeyEvent sp; sp.key = Key::Space;
  CHECK(btn->onKeyDown(sp) && clicks == 1);
  chk->onKeyDown(sp);
  CHECK(!chk->checked());
  (void)app;
}

// What every OS bridge relies on: the hoisted tree view, actions, values and the event stream.
struct RecordingBridge : AccessibilityBridge {
  std::vector<std::pair<AccessEvent, Widget*>> events;
  int pumps = 0;
  void notify(AccessEvent e, Widget* w) override { events.push_back({e, w}); }
  void pump() override { pumps++; }
};

static void testAccessibilityBridge(Application& app) {
  SECTION("accessibility_bridge");
  auto root = vbox(4);
  auto group = hbox(2);                       // purely structural: hoisted away
  auto ok = make<Button>("OK");
  auto check = make<CheckBox>("Wrap", false);
  auto field = make<TextInput>(); field->setAccessName("Query");
  auto slider = make<Slider>(0.f, 10.f, 2.f);
  auto combo = make<ComboBox>(std::vector<std::string>{"Low", "High"});
  group->add(ok); group->add(check);
  root->add(group); root->add(field); root->add(slider); root->add(combo);
  app.closeAllPopups();
  auto bridge = std::make_shared<RecordingBridge>();
  app.setAccessibilityBridge(bridge);
  app.setRoot(root);
  app.tick(); app.render();
  CHECK(bridge->pumps >= 1 && !bridge->events.empty() && bridge->events.back().first == AccessEvent::StructureChanged);
  auto kids = AccessView::children(root.get());
  CHECK(kids.size() == 5 && kids[0] == ok.get() && kids[1] == check.get());       // the hbox is not in the tree
  CHECK(AccessView::parentOf(ok.get(), root.get()) == root.get() && AccessView::indexInParent(check.get(), root.get()) == 1);
  CHECK(AccessView::actions(ok.get()).front() == "press" && AccessView::actions(check.get()).front() == "toggle");
  int clicks = 0; ok->onClick = [&] { clicks++; };
  CHECK(AccessView::doAction(ok.get(), 0) && clicks == 1);
  CHECK(AccessView::doAction(check.get(), 0) && check->checked());
  CHECK(AccessView::actions(field.get()).size() == 1 && AccessView::doAction(field.get(), 0) && app.focus() == field.get());   // "focus" action
  CHECK(bridge->events.back().first == AccessEvent::FocusChanged && bridge->events.back().second == field.get());
  CHECK(field->accessValueEditable() && field->accessSetValue("abc") && field->text() == "abc");
  CHECK(slider->accessSetValue("7") && std::abs(slider->value() - 7.f) < 1e-4f && !slider->accessSetValue("nope"));
  bool sawValue = false;
  for (auto& [e, w] : bridge->events) if (e == AccessEvent::ValueChanged && w == slider.get()) sawValue = true;
  CHECK(sawValue);
  CHECK(combo->accessSetValue("High") && combo->text() == "High" && !combo->accessSetValue("Missing"));
  CHECK(!AccessView::doAction(ok.get(), 7));
  app.setAccessibilityBridge(nullptr);
}

static void testTableView(Application& app) {
  SECTION("table_view");
  auto t = make<TableView>();
  t->addColumn({"name", "Name", 100});
  t->addColumn({"size", "Size", 80});
  t->addColumn({"kind", "Kind", 80});
  std::vector<std::pair<std::string, std::string>> data{{"tex10", "5"}, {"tex2", "40"}, {"Tex1", "300"}, {"mesh", "7"}};
  t->cellText = [&](int r, int c) { return c == 0 ? data[size_t(r)].first : c == 1 ? data[size_t(r)].second : std::string("x"); };
  t->rowHeightOf = [](int r) { return r == 1 ? 50.f : 20.f; };
  t->setRowCount(4);
  t->arrange({0, 0, 400, 200});
  CHECK(t->contentHeight() == 110.f);                        // variable row heights: 20+50+20+20
  CHECK(t->viewRowAt(25) == 1 && t->viewRowAt(75) == 2 && t->viewRowAt(500) == 3);
  // sorting: natural order, then descending, then back to model order
  t->sortBy(0, SortState::Ascending);
  CHECK(t->dataRow(0) == 3 && t->dataRow(1) == 2 && t->dataRow(2) == 1 && t->dataRow(3) == 0);   // mesh, Tex1, tex2, tex10
  t->sortBy(0, SortState::Descending);
  CHECK(t->dataRow(0) == 0 && t->dataRow(3) == 3);
  t->sortBy(1, SortState::Ascending);
  CHECK(t->dataRow(0) == 0 && t->dataRow(3) == 2);          // 5, 7, 40, 300 numerically
  t->sortBy(1, SortState::None);
  CHECK(t->dataRow(2) == 2 && t->sortColumn() == -1);
  // header click cycles sort; header geometry
  Rect h0 = t->headerCell(0);
  CHECK(h0.w == 100 && h0.h == t->headerHeight());
  MouseEvent md; md.pos = {40, 10}; md.button = MouseButton::Left;
  t->onMouseDown(md); t->onMouseUp(md);
  CHECK(t->sortColumn() == 0 && t->sortState() == SortState::Ascending);
  t->onMouseDown(md); t->onMouseUp(md);
  CHECK(t->sortState() == SortState::Descending);
  // resize by dragging the column edge, honouring minWidth; double click auto-fits
  MouseEvent edge; edge.pos = {100, 10}; edge.button = MouseButton::Left;
  t->onMouseDown(edge);
  MouseEvent mv; mv.pos = {160, 10}; t->onMouseMove(mv);
  CHECK(t->column(0).width == 160);
  mv.pos = {5, 10}; t->onMouseMove(mv);
  CHECK(t->column(0).width == t->column(0).minWidth);
  t->onMouseUp(mv);
  t->setColumnWidth(0, 100);
  // reorder: drag "Kind" header to the front
  int changed = 0; t->onColumnsChanged = [&] { changed++; };
  MouseEvent kd; kd.pos = {t->headerCell(2).x + 20, 10}; kd.button = MouseButton::Left;
  t->onMouseDown(kd);
  MouseEvent km; km.pos = {2, 10}; t->onMouseMove(km);
  t->onMouseUp(km);
  CHECK(t->columnOrder()[0] == 2 && t->columnOrder()[1] == 0 && changed == 1);
  CHECK(t->sortColumn() == 0);                                // a drag must not toggle sorting
  // selection: click, ctrl-toggle, shift range, keyboard
  t->sortBy(0, SortState::None);
  std::vector<int> got; t->onSelect = [&](const std::vector<int>& v) { got = v; };
  MouseEvent rd; rd.button = MouseButton::Left;
  rd.pos = {10, t->headerHeight() + 5}; t->onMouseDown(rd);
  CHECK(got == std::vector<int>{0});
  rd.pos = {10, t->headerHeight() + 75}; rd.mods = ModShift; t->onMouseDown(rd);
  CHECK(got.size() == 3);
  rd.pos = {10, t->headerHeight() + 30}; rd.mods = ModCtrl; t->onMouseDown(rd);
  CHECK(got.size() == 2);
  KeyEvent ke; ke.key = Key::Down; t->onKeyDown(ke);
  CHECK(got.size() == 1);
  ke.key = Key::A; ke.mods = ModCtrl; t->onKeyDown(ke);
  CHECK(got.size() == 4);
  // scrolling a long table stays virtual and clamped
  auto big = make<TableView>();
  big->addColumn({"n", "N", 80});
  big->cellText = [](int r, int) { return std::to_string(r); };
  big->setRowCount(100000);
  big->arrange({0, 0, 200, 200});
  big->setScrollY(1e9f);
  CHECK(big->scrollY() == big->contentHeight() - (200 - big->headerHeight()));
  CHECK(big->viewRowAt(big->scrollY()) > 99000);
  (void)app;
}

static void testMenuExtensions(Application& app) {
  SECTION("menu_extensions");
  MenuExtensions mx;
  int hits = 0;
  mx.add("Tools/Profiler/Open", [&] { hits++; }, "", Icon::None, 2, 7);
  mx.add("Tools/Profiler/Clear", [&] { hits += 10; }, "", Icon::None, 1, 7);
  mx.add("Tools/Rebuild", [&] { hits += 100; }, "F9", Icon::None, 0, 7);
  mx.add("MyPlugin/Run", [] {}, "", Icon::None, 0, 9);
  CHECK(mx.size() == 4);
  auto tops = mx.topLevelMenus();
  CHECK(tops.size() == 2 && tops[0] == "Tools" && tops[1] == "MyPlugin");
  std::vector<MenuItem> items{MenuItem::Item("Existing", [] {})};
  mx.merge("Tools", items);
  // existing item, separator, Rebuild (order 0), Profiler submenu with Clear before Open
  CHECK(items.size() == 4 && items[1].separator && items[2].label == "Rebuild" && items[2].shortcut == "F9");
  CHECK(items[3].label == "Profiler" && items[3].sub.size() == 2 && items[3].sub[0].label == "Clear" && items[3].sub[1].label == "Open");
  items[3].sub[0].action(); items[2].action();
  CHECK(hits == 110);
  std::vector<MenuItem> other;
  mx.merge("Edit", other);
  CHECK(other.empty());                                           // other menus are untouched
  mx.remove(7);
  CHECK(mx.size() == 1 && mx.topLevelMenus().size() == 1);        // unloading a plugin removes its entries
  ToolbarExtensions tb;
  tb.add("main", {"b", "second", Icon::Play, [] {}, 5, 1});
  tb.add("main", {"a", "first", Icon::Stop, [] {}, 1, 1});
  auto es = tb.entries("main");
  CHECK(es.size() == 2 && es[0].id == "a" && tb.entries("none").empty());
  tb.remove(1);
  CHECK(tb.entries("main").empty());
  (void)app;
}

static void testProfiler(Application& app) {
  SECTION("profiler");
  app.hostResize(900, 500);
  auto pf = make<Profiler>();
  app.setRoot(pf);
  app.tick(); app.render();
  CHECK(pf->shownFrame() == nullptr);
  for (int i = 0; i < int(Profiler::kHistory) + 20; i++) {
    ProfileFrame f; f.cpuMs = 5.f + float(i % 7); f.gpuMs = 3.f; f.drawCalls = i;
    f.scopes.push_back({"Frame", 0, 0, 0, f.cpuMs});
    f.scopes.push_back({"Child", 0, 1, 0, 1.f});
    pf->pushFrame(std::move(f));
  }
  app.tick(); app.render();
  CHECK(pf->frameCount() == Profiler::kHistory);              // history is capped
  CHECK(pf->shownFrame()->drawCalls == int(Profiler::kHistory) + 19);
  pf->selectFrame(10);
  CHECK(pf->selectedFrame() == 10 && pf->shownFrame()->drawCalls == 30);
  pf->setPaused(true);
  size_t n = pf->frameCount(); pf->pushFrame({});
  CHECK(pf->frameCount() == n);                                // paused ignores new frames
  pf->setPaused(false); pf->selectFrame(-1);
  CHECK(pf->selectedFrame() == -1);
  pf->setTargetMs(33.3f); app.tick(); app.render();
  pf->clear();
  CHECK(pf->frameCount() == 0 && pf->shownFrame() == nullptr);
  app.tick(); app.render();
}

static void testColorPickerWindow(Application& app) {
  SECTION("color_picker_window");
  app.closeAllPopups();
  app.hostResize(1000, 700);
  auto root = make<Spacer>();
  app.setRoot(root);
  Color live{}, applied{};
  bool cancelled = false;
  showColorPickerWindow(Color(10, 20, 30), [&](Color c) { applied = c; }, [&] { cancelled = true; }, [&](Color c) { live = c; });
  app.tick(); app.render();
  Widget* win = app.topPopup();
  CHECK(win != nullptr);
  Rect r0 = win->rect();
  // drag the title bar: window follows the pointer and stays non-modal
  Vec2 grab{r0.x + 100, r0.y + 12};
  app.hostMouseMove(grab);
  app.hostMouseButton(grab, MouseButton::Left, true);
  app.hostMouseMove(grab + Vec2(-60, 40));
  app.hostMouseButton(grab + Vec2(-60, 40), MouseButton::Left, false);
  Rect r1 = win->rect();
  CHECK(near(r1.x, r0.x - 60, 2.0f) && near(r1.y, r0.y + 40, 2.0f));
  // a click outside must not close or swallow-block the floating window
  app.hostMouseMove({5, 5}); app.hostMouseButton({5, 5}, MouseButton::Left, true); app.hostMouseButton({5, 5}, MouseButton::Left, false);
  CHECK(app.topPopup() == win);
  // Cancel restores the initial colour through the live callback
  Widget* picker = nullptr;
  std::function<void(Widget*)> find = [&](Widget* w) { if (dynamic_cast<ColorPicker*>(w)) picker = w; for (auto& ch : w->children()) find(ch.get()); };
  find(win);
  CHECK(picker != nullptr);
  if (picker) static_cast<ColorPicker*>(picker)->setColor(Color(200, 100, 50));
  CHECK(live == Color(200, 100, 50));
  app.closePopup(win);
  CHECK(cancelled && live == Color(10, 20, 30) && applied == Color());
}

static void testHostGizmos() {
  SECTION("host_gizmos");
  // Orthographic projection: offsets scale by orthoScale, rays run along the view direction.
  GizmoCamera cam;
  cam.eye = {0, 0, 10};
  cam.right = {1, 0, 0}; cam.up = {0, 1, 0}; cam.fwd = {0, 0, -1};
  cam.cx = 100; cam.cy = 100; cam.viewH = 200;
  cam.ortho = true; cam.orthoScale = 20;
  Vec2 p;
  CHECK(cam.project(Vec3{1, 2, 0}, p) && std::fabs(p.x - 120) < 0.01f && std::fabs(p.y - 60) < 0.01f);
  CHECK(cam.project(Vec3{1, 2, -50}, p) && std::fabs(p.x - 120) < 0.01f);          // depth does not shrink it
  Vec3 hitPoint;
  CHECK(cam.rayPlane(Vec2{140, 100}, Vec3{0, 0, 0}, Vec3{0, 0, 1}, hitPoint) && std::fabs(hitPoint.x - 2) < 0.01f);
  // A transform gizmo keeps a fixed on-screen size in orthographic views.
  TransformGizmo g;
  g.setCamera(cam);
  const float l1 = g.length();
  g.setTarget(Mat4::translation(Vec3{0, 0, -30}));
  CHECK(std::fabs(g.length() - l1) < 1e-4f);

  // Navigation gizmo: looking down -Z, the +X ball sits to the right of the centre and +Y above it.
  NavGizmo nav;
  GizmoCamera persp = cam;
  persp.ortho = false;
  nav.setCamera(persp);
  nav.setArea(Rect{0, 0, 100, 100});
  CHECK(nav.hit(Vec2{85, 50}) == 0);
  CHECK(nav.hit(Vec2{50, 15}) == 2);
  CHECK(nav.hit(Vec2{15, 50}) == 1);
  CHECK(nav.contains(Vec2{50, 50}) && !nav.contains(Vec2{0, 0}));
  CHECK(NavGizmo::facingDirection(5).z == -1.f);

  // World lines clip at the near plane rather than wrapping behind the camera.
  Canvas c;
  c.resize(200, 200);
  c.clear(Color(0, 0, 0));
  persp.focal = 100;
  drawWorldLine(c, persp, Vec3{0, 0, 0}, Vec3{0, 0, 20}, Color(255, 255, 255), 2.f);   // crosses the eye plane
  drawWorldLine(c, persp, Vec3{0, 0, 30}, Vec3{1, 0, 30}, Color(255, 0, 0), 2.f);     // fully behind: nothing
  bool anyRed = false;
  for (int i = 0; i < 200 * 200; i++) anyRed |= ((c.pixels()[i] >> 16) & 255) > 200 && ((c.pixels()[i] >> 8) & 255) < 50;
  CHECK(!anyRed);

  // Scene depth: a point in front of the surface shows, one behind it does not; no data means no test.
  std::vector<float> depthTexels(4 * 4, 1e30f);
  depthTexels[1 * 4 + 1] = 10.f;
  GizmoDepth depth;
  depth.data = depthTexels.data(); depth.width = 4; depth.height = 4; depth.area = Rect{0, 0, 200, 200};
  CHECK(depth.visible(Vec2{60, 60}, 9.f) && !depth.visible(Vec2{60, 60}, 12.f));
  CHECK(depth.visible(Vec2{160, 160}, 500.f));                  // nothing drawn there
  depth.zOffset = 5.f;
  CHECK(depth.visible(Vec2{60, 60}, 14.f));                      // the drawing eye sits 5 behind the host's
  depth.zOffset = 0.f;
  // A depth-tested line hides where the surface is in front of it.
  Canvas dc;
  dc.resize(200, 200);
  dc.clear(Color(0, 0, 0));
  GizmoCamera front;
  front.eye = {0, 0, 20}; front.fwd = {0, 0, -1}; front.right = {1, 0, 0}; front.up = {0, 1, 0};
  front.cx = 100; front.cy = 100; front.focal = 100; front.viewH = 200;
  std::vector<float> wall(20 * 20, 1e30f);
  for (int y = 0; y < 20; ++y) for (int x = 0; x < 10; ++x) wall[size_t(y * 20 + x)] = 5.f;   // left half: a wall 5 away
  GizmoDepth wallDepth;
  wallDepth.data = wall.data(); wallDepth.width = 20; wallDepth.height = 20; wallDepth.area = Rect{0, 0, 200, 200};
  drawWorldLine(dc, front, Vec3{-8, 0, 0}, Vec3{8, 0, 0}, Color(255, 255, 255), 2.f, &wallDepth, 0.f);
  const auto lit = [&](int x, int y) { return ((dc.pixels()[size_t(y * 200 + x)] >> 8) & 255) > 128; };
  CHECK(!lit(80, 100) && lit(120, 100));

  // The grid shows its axis lines through the origin (X red, Z blue) in a perspective view looking at it.
  Canvas gc;
  gc.resize(320, 200);
  gc.clear(Color(0, 0, 0));
  GizmoCamera view;
  view.eye = {7.36f, 4.96f, -6.93f};
  view.fwd = (Vec3{0, 0, 0} - view.eye).normalized();
  view.right = view.fwd.cross(Vec3{0, 1, 0}).normalized();
  view.up = view.right.cross(view.fwd).normalized();
  view.cx = 160; view.cy = 100; view.viewH = 200; view.focal = 200;
  WorldGrid grid;
  drawWorldGrid(gc, view, grid, nullptr);
  int red = 0, blue = 0, any = 0;
  for (int i = 0; i < 320 * 200; ++i) {
    const uint32_t px = gc.pixels()[size_t(i)];
    const int r = int((px >> 16) & 255), gg = int((px >> 8) & 255), b = int(px & 255);
    red += (r > 100 && r > gg + 40 && r > b + 40);
    blue += (b > 100 && b > r + 40);
    any += (r + gg + b) > 30;
  }
  if (std::getenv("PRISM_GRID_PNG")) gc.savePng(std::getenv("PRISM_GRID_PNG"));
  // A segment crossing the eye plane still draws its visible part (its clipped end must not round behind it).
  {
    Canvas t;
    t.resize(320, 200);
    t.clear(Color(0, 0, 0));
    drawWorldLine(t, view, Vec3{-50, 0, 0}, Vec3{50, 0, 0}, Color(255, 0, 0), 2.f);
    int lit = 0;
    for (int i = 0; i < 320 * 200; ++i) lit += ((t.pixels()[size_t(i)] >> 16) & 255) > 100;
    CHECK(lit > 100);
  }
  CHECK(red > 50 && blue > 50 && any > 2000);

  // Selection outline: a band around a filled square, never over it.
  std::vector<uint8_t> mask(40 * 40, 0);
  for (int y = 15; y < 25; ++y) for (int x = 15; x < 25; ++x) mask[size_t(y * 40 + x)] = 255;
  std::vector<uint32_t> band;
  int box[4] = {0, 0, 0, 0};
  CHECK(buildSelectionOutline(mask.data(), 40, 40, 2, Color(255, 160, 40), band, box));
  const auto at = [&](int x, int y) { return band[size_t((y - box[1]) * box[2] + (x - box[0]))]; };
  CHECK((at(20, 20) >> 24) == 0 && (at(14, 20) >> 24) > 200 && (at(12, 20) >> 24) == 0);
  // A tree reports the node on the row under a point (drops, context menus).
  TreeView tree;
  TreeNode* first = tree.addNode(nullptr, "First", Icon::Cube, 1);
  tree.addNode(first, "Child", Icon::Cube, 2);
  tree.arrange(Rect{0, 0, 200, 200});
  CHECK(tree.nodeAt(Vec2{20, 5}) != nullptr && tree.nodeAt(Vec2{20, 5})->id == 1);
  CHECK(tree.nodeAt(Vec2{20, 5 + tree.rowHeight()}) != nullptr && tree.nodeAt(Vec2{20, 5 + tree.rowHeight()})->id == 2);
  CHECK(tree.nodeAt(Vec2{20, 190}) == nullptr);
  // A menu item can carry the host's own icon image.
  static const uint32_t kIconPixels[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
  const MenuItem withImage = MenuItem::Item("Add", [] {}).withImage(kIconPixels, 2);
  CHECK(withImage.image == kIconPixels && withImage.imageSize == 2);
  std::vector<uint8_t> empty(16, 0);
  CHECK(!buildSelectionOutline(empty.data(), 4, 4, 2, Color(255, 160, 40), band, box));
}

int main(int argc, char** argv) {
  std::string assets = argc > 1 ? argv[1] : "assets";
  auto plat = std::make_shared<TestPlatform>();
  Application app;
  HostDesc d;
  d.assetDir = assets;
  d.width = 600; d.height = 400;
  if (!app.initHosted(d, plat)) { std::printf("could not initialise PRISM (assets: %s)\n", assets.c_str()); return 2; }
  testChords();
  testCommands();
  testTheme();
  testLayout(app);
  testHitAndRouting(app);
  testDocking(app);
  testToastsAndTimers(app, *plat);
  testDrawList(app);
  testFrameArena();
  testThreadingContract(app);
  testBrushesAndGradients();
  testSvgPath();
  testHiDpiAndScale(app, *plat);
  testZeroAllocations(app);
  testAttributesAndDeclarative();
  testInvalidation();
  testEventRouting(app);
  testLayoutContainers(app);
  testAnimationCurvesAndSprings();
  testDragAndDrop(app);
  testTransactionService();
  testPropertyReflection();
  testDockEdgeSplitting(app);
  testLocalization();
  testCurveEditor();
  testCodeEditor();
  testAssetBrowser();
  testColorPicker();
  testStyleSheet();
  testRuntimeProfile();
  testBlueprintGraph();
  testBlueprintEditing();
  testBlueprintInteraction(app);
  testColorPickerWindow(app);
  testBlueprintGraphTypes(app);
  testBlueprintAdvanced(app);
  testBlueprintEmbedded(app);
  testBlueprintTools(app);
  testLogView(app);
  testBlueprintVM(app);
  testGizmo(app);
  testHostGizmos();
  testTextLayout(app);
  testIconAtlas(app);
  testAccessibility(app);
  testAccessibilityBridge(app);
  testTableView(app);
  testMenuExtensions(app);
  testProfiler(app);
  testComplexText(app);
  testMultiWindow(app);
  testDeviceBackend(app);
  testGoldenImages(app, assets);
  testFuzzLoaders(app, assets);
  std::printf("%d checks, %d failed\n", g_run, g_fail);
  return g_fail ? 1 : 0;
}

