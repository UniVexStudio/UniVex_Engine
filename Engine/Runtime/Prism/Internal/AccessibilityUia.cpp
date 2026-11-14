// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Microsoft UI Automation bridge (Windows): Narrator, NVDA, JAWS, Inspect.exe and UI test frameworks see
// PRISM widgets as native UIA elements with names, control types, focus, bounds, Invoke / Toggle / Value patterns.
//
// The bridge subclasses the main window (Platform::nativeWindowHandle) to answer WM_GETOBJECT. Engines with their
// own WndProc can instead call prism::uiaHandleWindowMessage() from it.
#if defined(_WIN32)
#include "prism/AccessibilityBridge.h"
#include "prism/Application.h"
#include <windows.h>
#include <ole2.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <atomic>
#include <string>
#include <unordered_map>

#if defined(__MINGW32__)
// Present in the Windows SDK's UIAutomationCoreApi.h and exported by UIAutomationCore.dll; missing from MinGW headers.
extern "C" BOOL WINAPI UiaClientsAreListening(void);
#endif

namespace prism {
namespace {

BSTR toBstr(const std::string& s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
  BSTR b = SysAllocStringLen(nullptr, UINT(n));
  if (b && n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), b, n);
  return b;
}
std::string fromWide(LPCWSTR w) {
  if (!w) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
  if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}

long controlType(const Widget* w) {
  switch (w->accessRole()) {
    case AccessRole::Window: return UIA_WindowControlTypeId;
    case AccessRole::Group: return UIA_GroupControlTypeId;
    case AccessRole::Label: return UIA_TextControlTypeId;
    case AccessRole::Button: case AccessRole::ToggleButton: return UIA_ButtonControlTypeId;
    case AccessRole::CheckBox: return UIA_CheckBoxControlTypeId;
    case AccessRole::Slider: return UIA_SliderControlTypeId;
    case AccessRole::ComboBox: return UIA_ComboBoxControlTypeId;
    case AccessRole::TextBox: return UIA_EditControlTypeId;
    case AccessRole::ProgressBar: return UIA_ProgressBarControlTypeId;
    case AccessRole::Tab: return UIA_TabItemControlTypeId;
    case AccessRole::TabList: return UIA_TabControlTypeId;
    case AccessRole::Menu: return UIA_MenuControlTypeId;
    case AccessRole::MenuItem: return UIA_MenuItemControlTypeId;
    case AccessRole::Table: return UIA_TableControlTypeId;
    case AccessRole::Row: return UIA_DataItemControlTypeId;
    case AccessRole::ColumnHeader: return UIA_HeaderItemControlTypeId;
    case AccessRole::Cell: return UIA_DataItemControlTypeId;
    case AccessRole::Tree: return UIA_TreeControlTypeId;
    case AccessRole::TreeItem: return UIA_TreeItemControlTypeId;
    case AccessRole::List: return UIA_ListControlTypeId;
    case AccessRole::ListItem: return UIA_ListItemControlTypeId;
    case AccessRole::Separator: return UIA_SeparatorControlTypeId;
    case AccessRole::Image: return UIA_ImageControlTypeId;
    default: return UIA_PaneControlTypeId;
  }
}

class UiaBridge;
UiaBridge* g_uia = nullptr;

class Node : public IRawElementProviderSimple, public IRawElementProviderFragment, public IRawElementProviderFragmentRoot,
             public IInvokeProvider, public IToggleProvider, public IValueProvider {
public:
  Node(Widget* w, bool root, int id) : m_weak(w->weak_from_this()), m_root(root), m_id(id) {}
  Widget* widget() const { auto s = m_weak.lock(); return s.get(); }

  // IUnknown
  ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(++m_refs); }
  ULONG STDMETHODCALLTYPE Release() override { ULONG r = ULONG(--m_refs); if (!r) delete this; return r; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (!out) return E_INVALIDARG;
    *out = nullptr;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
    else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
    else if (riid == __uuidof(IRawElementProviderFragmentRoot) && m_root) *out = static_cast<IRawElementProviderFragmentRoot*>(this);
    else if (riid == __uuidof(IInvokeProvider)) *out = static_cast<IInvokeProvider*>(this);
    else if (riid == __uuidof(IToggleProvider)) *out = static_cast<IToggleProvider*>(this);
    else if (riid == __uuidof(IValueProvider)) *out = static_cast<IValueProvider*>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }

  // IRawElementProviderSimple
  HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* o) override {
    *o = ProviderOptions(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** out) override;
  HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* v) override;
  HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override;

  // IRawElementProviderFragment
  HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection dir, IRawElementProviderFragment** out) override;
  HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override;
  HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* r) override;
  HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override { *out = nullptr; return S_OK; }
  HRESULT STDMETHODCALLTYPE SetFocus() override {
    Widget* w = widget();
    if (!w) return UIA_E_ELEMENTNOTAVAILABLE;
    if (w->focusable()) Application::get().setFocus(w);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override;

  // IRawElementProviderFragmentRoot
  HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override;
  HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override;

  // IInvokeProvider
  HRESULT STDMETHODCALLTYPE Invoke() override {
    Widget* w = widget();
    if (!w) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!w->enabled()) return UIA_E_ELEMENTNOTENABLED;
    w->accessActivate();
    Application::get().invalidate();
    return S_OK;
  }
  // IToggleProvider
  HRESULT STDMETHODCALLTYPE Toggle() override { return Invoke(); }
  HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* s) override {
    Widget* w = widget();
    if (!w) return UIA_E_ELEMENTNOTAVAILABLE;
    *s = w->accessChecked() ? ToggleState_On : ToggleState_Off;
    return S_OK;
  }
  // IValueProvider
  HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR v) override {
    Widget* w = widget();
    if (!w) return UIA_E_ELEMENTNOTAVAILABLE;
    if (!w->accessValueEditable() || !w->enabled()) return UIA_E_ELEMENTNOTENABLED;
    w->accessSetValue(fromWide(v));
    Application::get().invalidate();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE get_Value(BSTR* v) override { Widget* w = widget(); *v = toBstr(w ? w->accessValue() : std::string()); return S_OK; }
  HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* r) override { Widget* w = widget(); *r = !(w && w->accessValueEditable()); return S_OK; }

  int id() const { return m_id; }
private:
  virtual ~Node() = default;
  std::weak_ptr<Widget> m_weak;
  bool m_root;
  int m_id;
  std::atomic<long> m_refs{1};
};

class UiaBridge : public AccessibilityBridge {
public:
  HWND hwnd = nullptr;
  WNDPROC oldProc = nullptr;
  std::unordered_map<Widget*, Node*> nodes;
  int nextId = 1;

  Node* nodeFor(Widget* w) {
    if (!w) return nullptr;
    auto it = nodes.find(w);
    if (it != nodes.end() && it->second->widget() == w) return it->second;
    if (it != nodes.end()) { it->second->Release(); nodes.erase(it); }
    Node* n = new Node(w, w == Application::get().root(), nextId++);
    nodes[w] = n;                                      // the cache owns one reference
    return n;
  }
  Node* rootNode() { return nodeFor(Application::get().root()); }
  void prune() {
    for (auto it = nodes.begin(); it != nodes.end();) {
      if (!it->second->widget()) { it->second->Release(); it = nodes.erase(it); } else ++it;
    }
  }
  void notify(AccessEvent e, Widget* w) override {
    if (!UiaClientsAreListening()) return;
    if (e == AccessEvent::StructureChanged) {
      prune();
      if (Node* r = rootNode()) UiaRaiseStructureChangedEvent(r, StructureChangeType_ChildrenInvalidated, nullptr, 0);
      return;
    }
    Node* n = nodeFor(w);
    if (!n) return;
    VARIANT oldV, newV;
    VariantInit(&oldV); VariantInit(&newV);
    switch (e) {
      case AccessEvent::FocusChanged: UiaRaiseAutomationEvent(n, UIA_AutomationFocusChangedEventId); break;
      case AccessEvent::NameChanged:
        newV.vt = VT_BSTR; newV.bstrVal = toBstr(w->accessName());
        UiaRaiseAutomationPropertyChangedEvent(n, UIA_NamePropertyId, oldV, newV);
        break;
      case AccessEvent::ValueChanged:
        newV.vt = VT_BSTR; newV.bstrVal = toBstr(w->accessValue());
        UiaRaiseAutomationPropertyChangedEvent(n, UIA_ValueValuePropertyId, oldV, newV);
        break;
      case AccessEvent::StateChanged:
        newV.vt = VT_I4; newV.lVal = w->accessChecked() ? ToggleState_On : ToggleState_Off;
        UiaRaiseAutomationPropertyChangedEvent(n, UIA_ToggleToggleStatePropertyId, oldV, newV);
        break;
      default: break;
    }
    VariantClear(&newV);
  }
  ~UiaBridge() override {
    if (hwnd && oldProc) SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(oldProc));
    if (hwnd) UiaReturnRawElementProvider(hwnd, 0, 0, nullptr);     // tell UIA the providers are gone
    for (auto& [w, n] : nodes) n->Release();
    if (g_uia == this) g_uia = nullptr;
  }
};

// ---- Node methods that need the bridge
HRESULT Node::GetPatternProvider(PATTERNID id, IUnknown** out) {
  *out = nullptr;
  Widget* w = widget();
  if (!w) return UIA_E_ELEMENTNOTAVAILABLE;
  AccessRole r = w->accessRole();
  if (id == UIA_InvokePatternId && r == AccessRole::Button) { *out = static_cast<IInvokeProvider*>(this); AddRef(); }
  else if (id == UIA_TogglePatternId && (r == AccessRole::CheckBox || r == AccessRole::ToggleButton)) { *out = static_cast<IToggleProvider*>(this); AddRef(); }
  else if (id == UIA_ValuePatternId && w->accessValueEditable()) { *out = static_cast<IValueProvider*>(this); AddRef(); }
  return S_OK;
}

HRESULT Node::GetPropertyValue(PROPERTYID id, VARIANT* v) {
  VariantInit(v);
  Widget* w = widget();
  if (!w) return UIA_E_ELEMENTNOTAVAILABLE;
  switch (id) {
    case UIA_NamePropertyId: v->vt = VT_BSTR; v->bstrVal = toBstr(w->accessName()); break;
    case UIA_HelpTextPropertyId: v->vt = VT_BSTR; v->bstrVal = toBstr(w->accessDescription()); break;
    case UIA_ControlTypePropertyId: v->vt = VT_I4; v->lVal = controlType(w); break;
    case UIA_LocalizedControlTypePropertyId: break;                          // UIA derives it from the control type
    case UIA_IsKeyboardFocusablePropertyId: v->vt = VT_BOOL; v->boolVal = w->focusable() ? VARIANT_TRUE : VARIANT_FALSE; break;
    case UIA_HasKeyboardFocusPropertyId: v->vt = VT_BOOL; v->boolVal = w->focused() ? VARIANT_TRUE : VARIANT_FALSE; break;
    case UIA_IsEnabledPropertyId: v->vt = VT_BOOL; v->boolVal = w->enabled() ? VARIANT_TRUE : VARIANT_FALSE; break;
    case UIA_IsOffscreenPropertyId: v->vt = VT_BOOL; v->boolVal = w->visible() ? VARIANT_FALSE : VARIANT_TRUE; break;
    case UIA_IsControlElementPropertyId: case UIA_IsContentElementPropertyId: v->vt = VT_BOOL; v->boolVal = VARIANT_TRUE; break;
    case UIA_AutomationIdPropertyId: v->vt = VT_BSTR; v->bstrVal = toBstr("prism." + std::to_string(m_id)); break;
    case UIA_FrameworkIdPropertyId: v->vt = VT_BSTR; v->bstrVal = SysAllocString(L"PRISM"); break;
    case UIA_ProviderDescriptionPropertyId: v->vt = VT_BSTR; v->bstrVal = SysAllocString(L"PRISM UI Automation bridge"); break;
    default: break;                                                          // VT_EMPTY: UIA uses its default
  }
  return S_OK;
}

HRESULT Node::get_HostRawElementProvider(IRawElementProviderSimple** out) {
  *out = nullptr;
  if (m_root && g_uia && g_uia->hwnd) return UiaHostProviderFromHwnd(g_uia->hwnd, out);
  return S_OK;
}

HRESULT Node::Navigate(NavigateDirection dir, IRawElementProviderFragment** out) {
  *out = nullptr;
  Widget* w = widget();
  if (!w || !g_uia) return UIA_E_ELEMENTNOTAVAILABLE;
  Widget* root = Application::get().root();
  Widget* target = nullptr;
  switch (dir) {
    case NavigateDirection_Parent: if (w != root) target = AccessView::parentOf(w, root); break;
    case NavigateDirection_FirstChild: { auto k = AccessView::children(w); if (!k.empty()) target = k.front(); break; }
    case NavigateDirection_LastChild: { auto k = AccessView::children(w); if (!k.empty()) target = k.back(); break; }
    case NavigateDirection_NextSibling: case NavigateDirection_PreviousSibling: {
      if (w == root) break;
      Widget* p = AccessView::parentOf(w, root);
      if (!p) break;
      auto k = AccessView::children(p);
      int i = AccessView::indexInParent(w, root) + (dir == NavigateDirection_NextSibling ? 1 : -1);
      if (i >= 0 && i < int(k.size())) target = k[size_t(i)];
      break;
    }
  }
  if (Node* n = target ? g_uia->nodeFor(target) : nullptr) { *out = static_cast<IRawElementProviderFragment*>(n); n->AddRef(); }
  return S_OK;
}

HRESULT Node::GetRuntimeId(SAFEARRAY** out) {
  *out = nullptr;
  if (m_root) return S_OK;                                  // the host HWND provides the root's id
  int ids[2] = {UiaAppendRuntimeId, m_id};
  SAFEARRAY* sa = SafeArrayCreateVector(VT_I4, 0, 2);
  if (!sa) return E_OUTOFMEMORY;
  for (LONG i = 0; i < 2; i++) SafeArrayPutElement(sa, &i, &ids[i]);
  *out = sa;
  return S_OK;
}

HRESULT Node::get_BoundingRectangle(UiaRect* r) {
  Widget* w = widget();
  if (!w || !g_uia) return UIA_E_ELEMENTNOTAVAILABLE;
  Rect rc = w->rect();
  POINT p{LONG(rc.x), LONG(rc.y)};
  if (g_uia->hwnd) ClientToScreen(g_uia->hwnd, &p);
  r->left = p.x; r->top = p.y; r->width = rc.w; r->height = rc.h;
  return S_OK;
}

HRESULT Node::get_FragmentRoot(IRawElementProviderFragmentRoot** out) {
  *out = nullptr;
  if (!g_uia) return UIA_E_ELEMENTNOTAVAILABLE;
  if (Node* r = g_uia->rootNode()) { *out = static_cast<IRawElementProviderFragmentRoot*>(r); r->AddRef(); }
  return S_OK;
}

HRESULT Node::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) {
  *out = nullptr;
  if (!g_uia) return UIA_E_ELEMENTNOTAVAILABLE;
  POINT p{LONG(x), LONG(y)};
  if (g_uia->hwnd) ScreenToClient(g_uia->hwnd, &p);
  Widget* w = Application::get().widgetAt({float(p.x), float(p.y)});
  Widget* root = Application::get().root();
  while (w && w != root && !AccessView::meaningful(w)) w = w->parent();
  if (Node* n = g_uia->nodeFor(w ? w : root)) { *out = static_cast<IRawElementProviderFragment*>(n); n->AddRef(); }
  return S_OK;
}

HRESULT Node::GetFocus(IRawElementProviderFragment** out) {
  *out = nullptr;
  if (!g_uia) return UIA_E_ELEMENTNOTAVAILABLE;
  Widget* f = Application::get().focus();
  if (f && f != Application::get().root()) if (Node* n = g_uia->nodeFor(f)) { *out = static_cast<IRawElementProviderFragment*>(n); n->AddRef(); }
  return S_OK;
}

LRESULT CALLBACK subclassProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  LRESULT r = 0;
  if (uiaHandleWindowMessage(h, msg, wp, lp, &r)) return r;
  return g_uia && g_uia->oldProc ? CallWindowProcW(g_uia->oldProc, h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

} // namespace

// For engines with their own window procedure: call first; returns true when the message was answered.
bool uiaHandleWindowMessage(void* hwnd, unsigned msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
  if (msg != WM_GETOBJECT || !g_uia || LONG(lp) != LONG(UiaRootObjectId)) return false;
  Node* root = g_uia->rootNode();
  if (!root) return false;
  *result = UiaReturnRawElementProvider(static_cast<HWND>(hwnd), WPARAM(wp), LPARAM(lp), root);
  return true;
}

std::shared_ptr<AccessibilityBridge> makePlatformAccessibilityBridge(Application& app, const std::string&) {
  if (g_uia) return nullptr;
  auto b = std::make_shared<UiaBridge>();
  b->hwnd = static_cast<HWND>(app.platform().nativeWindowHandle());
  g_uia = b.get();
  if (b->hwnd)   // windowed mode: answer WM_GETOBJECT ourselves; hosted engines call uiaHandleWindowMessage instead
    b->oldProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(b->hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(subclassProc)));
  return b;
}

} // namespace prism
#endif
