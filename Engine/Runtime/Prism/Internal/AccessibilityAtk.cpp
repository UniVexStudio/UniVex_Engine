// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - AT-SPI bridge (Linux): publishes the accessibility tree over D-Bus through ATK + atk-bridge, so Orca,
// accerciser and any AT-SPI client can read names / roles / states / extents, run actions and edit values.
#if defined(PRISM_HAS_ATK)
#include "prism/AccessibilityBridge.h"
#include "prism/Application.h"
#include <atk/atk.h>
#include <atk-bridge.h>
#include <string>
#include <unordered_map>

namespace prism {
namespace {

struct PrismAtk {
  AtkObject parent;
  std::weak_ptr<Widget>* weak;       // the widget this object mirrors (defunct once it is gone)
  std::string* name;                 // storage for get_name / get_text results
  std::string* text;
};
struct PrismAtkClass { AtkObjectClass parent; };

class AtkBridge;
AtkBridge* g_bridge = nullptr;
AtkObject* g_appRoot = nullptr;
std::string g_appName = "prism";

Widget* widgetOf(AtkObject* o) {
  auto* p = reinterpret_cast<PrismAtk*>(o);
  if (!p->weak) return nullptr;
  auto sp = p->weak->lock();
  return sp.get();
}

AtkRole roleFor(const Widget* w) {
  switch (w->accessRole()) {
    case AccessRole::Window: return ATK_ROLE_FRAME;
    case AccessRole::Group: return ATK_ROLE_PANEL;
    case AccessRole::Label: return ATK_ROLE_LABEL;
    case AccessRole::Button: return ATK_ROLE_PUSH_BUTTON;
    case AccessRole::ToggleButton: return ATK_ROLE_TOGGLE_BUTTON;
    case AccessRole::CheckBox: return ATK_ROLE_CHECK_BOX;
    case AccessRole::Slider: return ATK_ROLE_SLIDER;
    case AccessRole::ComboBox: return ATK_ROLE_COMBO_BOX;
    case AccessRole::TextBox: return ATK_ROLE_ENTRY;
    case AccessRole::ProgressBar: return ATK_ROLE_PROGRESS_BAR;
    case AccessRole::Tab: return ATK_ROLE_PAGE_TAB;
    case AccessRole::TabList: return ATK_ROLE_PAGE_TAB_LIST;
    case AccessRole::Menu: return ATK_ROLE_MENU;
    case AccessRole::MenuItem: return ATK_ROLE_MENU_ITEM;
    case AccessRole::Table: return ATK_ROLE_TABLE;
    case AccessRole::Row: return ATK_ROLE_TABLE_ROW;
    case AccessRole::ColumnHeader: return ATK_ROLE_COLUMN_HEADER;
    case AccessRole::Cell: return ATK_ROLE_TABLE_CELL;
    case AccessRole::Tree: return ATK_ROLE_TREE;
    case AccessRole::TreeItem: return ATK_ROLE_TREE_ITEM;
    case AccessRole::List: return ATK_ROLE_LIST;
    case AccessRole::ListItem: return ATK_ROLE_LIST_ITEM;
    case AccessRole::Separator: return ATK_ROLE_SEPARATOR;
    case AccessRole::Image: return ATK_ROLE_IMAGE;
    case AccessRole::Canvas: return ATK_ROLE_CANVAS;
    default: return w->focusable() ? ATK_ROLE_UNKNOWN : ATK_ROLE_PANEL;
  }
}

AtkObject* objectFor(Widget* w);   // cache lookup / creation

// ---- AtkObject vfuncs
const gchar* get_name(AtkObject* o) {
  auto* p = reinterpret_cast<PrismAtk*>(o);
  Widget* w = widgetOf(o);
  *p->name = w ? w->accessName() : std::string();
  return p->name->c_str();
}
const gchar* get_description(AtkObject* o) { Widget* w = widgetOf(o); return w ? w->accessDescription().c_str() : ""; }
AtkRole get_role(AtkObject* o) { Widget* w = widgetOf(o); return w ? roleFor(w) : ATK_ROLE_INVALID; }
gint get_n_children(AtkObject* o) { Widget* w = widgetOf(o); return w ? gint(AccessView::children(w).size()) : 0; }
AtkObject* ref_child(AtkObject* o, gint i) {
  Widget* w = widgetOf(o);
  if (!w) return nullptr;
  auto kids = AccessView::children(w);
  if (i < 0 || i >= gint(kids.size())) return nullptr;
  AtkObject* c = objectFor(kids[size_t(i)]);
  return c ? ATK_OBJECT(g_object_ref(c)) : nullptr;
}
AtkObject* get_parent(AtkObject* o) {
  Widget* w = widgetOf(o);
  if (!w) return nullptr;
  Widget* root = Application::get().root();
  if (w == root) return g_appRoot;
  Widget* p = AccessView::parentOf(w, root);
  return p ? objectFor(p) : g_appRoot;
}
gint get_index_in_parent(AtkObject* o) {
  Widget* w = widgetOf(o);
  if (!w) return -1;
  Widget* root = Application::get().root();
  return w == root ? 0 : AccessView::indexInParent(w, root);
}
AtkStateSet* ref_state_set(AtkObject* o) {
  AtkStateSet* s = atk_state_set_new();
  Widget* w = widgetOf(o);
  if (!w) { atk_state_set_add_state(s, ATK_STATE_DEFUNCT); return s; }
  if (w->enabled()) { atk_state_set_add_state(s, ATK_STATE_ENABLED); atk_state_set_add_state(s, ATK_STATE_SENSITIVE); }
  if (w->visible()) { atk_state_set_add_state(s, ATK_STATE_VISIBLE); atk_state_set_add_state(s, ATK_STATE_SHOWING); }
  if (w->focusable()) atk_state_set_add_state(s, ATK_STATE_FOCUSABLE);
  if (w->focused()) atk_state_set_add_state(s, ATK_STATE_FOCUSED);
  AccessRole r = w->accessRole();
  if (r == AccessRole::CheckBox || r == AccessRole::ToggleButton) {
    atk_state_set_add_state(s, ATK_STATE_CHECKABLE);
    if (w->accessChecked()) atk_state_set_add_state(s, ATK_STATE_CHECKED);
  }
  if (w->accessValueEditable() && r == AccessRole::TextBox) { atk_state_set_add_state(s, ATK_STATE_EDITABLE); atk_state_set_add_state(s, ATK_STATE_SINGLE_LINE); }
  return s;
}

// ---- AtkComponent
void get_extents(AtkComponent* c, gint* x, gint* y, gint* w, gint* h, AtkCoordType) {
  Widget* wd = widgetOf(ATK_OBJECT(c));
  Rect r = wd ? wd->rect() : Rect{};
  if (x) *x = gint(r.x);
  if (y) *y = gint(r.y);
  if (w) *w = gint(r.w);
  if (h) *h = gint(r.h);
}
gboolean grab_focus(AtkComponent* c) {
  Widget* w = widgetOf(ATK_OBJECT(c));
  if (!w || !w->focusable()) return FALSE;
  Application::get().setFocus(w);
  return TRUE;
}
void component_init(AtkComponentIface* i) { i->get_extents = get_extents; i->grab_focus = grab_focus; }

// ---- AtkAction
gint get_n_actions(AtkAction* a) { Widget* w = widgetOf(ATK_OBJECT(a)); return w ? gint(AccessView::actions(w).size()) : 0; }
gboolean do_action(AtkAction* a, gint i) { Widget* w = widgetOf(ATK_OBJECT(a)); return w && AccessView::doAction(w, i) ? TRUE : FALSE; }
const gchar* action_name(AtkAction* a, gint i) {
  static const char* names[] = {"press", "toggle", "focus"};
  Widget* w = widgetOf(ATK_OBJECT(a));
  if (!w) return nullptr;
  auto acts = AccessView::actions(w);
  if (i < 0 || i >= gint(acts.size())) return nullptr;
  for (auto* n : names) if (acts[size_t(i)] == n) return n;
  return nullptr;
}
void action_init(AtkActionIface* i) { i->get_n_actions = get_n_actions; i->do_action = do_action; i->get_name = action_name; }

// ---- AtkText (read) + AtkEditableText (write) for every widget with a value
gchar* get_text(AtkText* t, gint start, gint end) {
  Widget* w = widgetOf(ATK_OBJECT(t));
  std::string v = w ? w->accessValue() : std::string();
  glong n = g_utf8_strlen(v.c_str(), -1);
  if (end < 0 || end > n) end = gint(n);
  if (start < 0) start = 0;
  if (start > end) start = end;
  const gchar* a = g_utf8_offset_to_pointer(v.c_str(), start);
  const gchar* b = g_utf8_offset_to_pointer(v.c_str(), end);
  return g_strndup(a, gsize(b - a));
}
gint get_character_count(AtkText* t) { Widget* w = widgetOf(ATK_OBJECT(t)); return w ? gint(g_utf8_strlen(w->accessValue().c_str(), -1)) : 0; }
gint get_caret_offset(AtkText*) { return 0; }
void text_init(AtkTextIface* i) { i->get_text = get_text; i->get_character_count = get_character_count; i->get_caret_offset = get_caret_offset; }
void set_text_contents(AtkEditableText* t, const gchar* s) {
  Widget* w = widgetOf(ATK_OBJECT(t));
  if (w && w->accessValueEditable()) { w->accessSetValue(s ? s : ""); Application::get().invalidate(); }
}
void editable_init(AtkEditableTextIface* i) { i->set_text_contents = set_text_contents; }

void prism_atk_init(PrismAtk* p) { p->weak = nullptr; p->name = new std::string; p->text = new std::string; }
void prism_atk_finalize(GObject* o);
void prism_atk_class_init(PrismAtkClass* k);
GType prism_atk_get_type();
gpointer prism_atk_parent_class = nullptr;

void prism_atk_finalize(GObject* o) {
  auto* p = reinterpret_cast<PrismAtk*>(o);
  delete p->weak; delete p->name; delete p->text;
  G_OBJECT_CLASS(prism_atk_parent_class)->finalize(o);
}
void prism_atk_class_init(PrismAtkClass* k) {
  prism_atk_parent_class = g_type_class_peek_parent(k);
  G_OBJECT_CLASS(k)->finalize = prism_atk_finalize;
  AtkObjectClass* a = ATK_OBJECT_CLASS(k);
  a->get_name = get_name;
  a->get_description = get_description;
  a->get_role = get_role;
  a->get_n_children = get_n_children;
  a->ref_child = ref_child;
  a->get_parent = get_parent;
  a->get_index_in_parent = get_index_in_parent;
  a->ref_state_set = ref_state_set;
}
GType prism_atk_get_type() {
  static GType t = 0;
  if (t) return t;
  GTypeInfo info{};
  info.class_size = sizeof(PrismAtkClass);
  info.class_init = reinterpret_cast<GClassInitFunc>(prism_atk_class_init);
  info.instance_size = sizeof(PrismAtk);
  info.instance_init = reinterpret_cast<GInstanceInitFunc>(prism_atk_init);
  t = g_type_register_static(ATK_TYPE_OBJECT, "PrismAccessible", &info, GTypeFlags(0));
  GInterfaceInfo comp{reinterpret_cast<GInterfaceInitFunc>(component_init), nullptr, nullptr};
  GInterfaceInfo act{reinterpret_cast<GInterfaceInitFunc>(action_init), nullptr, nullptr};
  GInterfaceInfo txt{reinterpret_cast<GInterfaceInitFunc>(text_init), nullptr, nullptr};
  GInterfaceInfo edt{reinterpret_cast<GInterfaceInitFunc>(editable_init), nullptr, nullptr};
  g_type_add_interface_static(t, ATK_TYPE_COMPONENT, &comp);
  g_type_add_interface_static(t, ATK_TYPE_ACTION, &act);
  g_type_add_interface_static(t, ATK_TYPE_TEXT, &txt);
  g_type_add_interface_static(t, ATK_TYPE_EDITABLE_TEXT, &edt);
  return t;
}

// ---- application root (AtkUtil::get_root): role "application", child 0 = the main window's root widget
gint app_n_children(AtkObject*) { return Application::exists() && Application::get().root() ? 1 : 0; }
AtkObject* app_ref_child(AtkObject*, gint i) {
  if (i != 0 || !Application::get().root()) return nullptr;
  AtkObject* c = objectFor(Application::get().root());
  return c ? ATK_OBJECT(g_object_ref(c)) : nullptr;
}
const gchar* app_name(AtkObject*) { return g_appName.c_str(); }
AtkObject* util_get_root() { return g_appRoot; }
const gchar* util_toolkit_name() { return "PRISM"; }
const gchar* util_toolkit_version() { return "0.1"; }

class AtkBridge : public AccessibilityBridge {
public:
  std::unordered_map<Widget*, AtkObject*> objects;
  ~AtkBridge() override {
    for (auto& [w, o] : objects) g_object_unref(o);
    if (g_bridge == this) g_bridge = nullptr;
  }
  void notify(AccessEvent e, Widget* w) override {
    if (e == AccessEvent::StructureChanged) { prune(); return; }
    auto it = objects.find(w);
    if (it == objects.end()) return;          // nobody has asked for this object yet: nothing to announce
    AtkObject* o = it->second;
    switch (e) {
      case AccessEvent::FocusChanged: atk_object_notify_state_change(o, ATK_STATE_FOCUSED, TRUE); break;
      case AccessEvent::NameChanged: g_object_notify(G_OBJECT(o), "accessible-name"); break;
      case AccessEvent::StateChanged: atk_object_notify_state_change(o, ATK_STATE_CHECKED, w->accessChecked()); break;
      case AccessEvent::ValueChanged: g_signal_emit_by_name(o, "visible-data-changed"); break;
      default: break;
    }
  }
  void pump() override {
    for (int i = 0; i < 64 && g_main_context_iteration(nullptr, FALSE); i++) {}
  }
  void prune() {
    for (auto it = objects.begin(); it != objects.end();) {
      if (!widgetOf(it->second)) { atk_object_notify_state_change(it->second, ATK_STATE_DEFUNCT, TRUE); g_object_unref(it->second); it = objects.erase(it); }
      else ++it;
    }
  }
};

AtkObject* objectFor(Widget* w) {
  if (!w || !g_bridge) return nullptr;
  auto it = g_bridge->objects.find(w);
  if (it != g_bridge->objects.end() && widgetOf(it->second) == w) return it->second;
  auto* o = static_cast<PrismAtk*>(g_object_new(prism_atk_get_type(), nullptr));
  o->weak = new std::weak_ptr<Widget>(w->weak_from_this());
  AtkObject* a = ATK_OBJECT(o);
  atk_object_initialize(a, nullptr);
  if (it != g_bridge->objects.end()) g_object_unref(it->second);
  g_bridge->objects[w] = a;
  return a;
}

} // namespace

std::shared_ptr<AccessibilityBridge> makePlatformAccessibilityBridge(Application&, const std::string& appName) {
  if (g_bridge) return nullptr;                    // one bridge per process
  g_appName = appName;
  auto b = std::make_shared<AtkBridge>();
  g_bridge = b.get();
  if (!g_appRoot) {
    // a plain AtkObject subclass for the application node
    static GTypeInfo info{};
    info.class_size = sizeof(AtkObjectClass);
    info.instance_size = sizeof(AtkObject);
    info.class_init = [](gpointer k, gpointer) {
      auto* a = ATK_OBJECT_CLASS(k);
      a->get_n_children = app_n_children;
      a->ref_child = app_ref_child;
      a->get_name = app_name;
    };
    GType t = g_type_register_static(ATK_TYPE_OBJECT, "PrismApplication", &info, GTypeFlags(0));
    g_appRoot = ATK_OBJECT(g_object_new(t, nullptr));
    atk_object_initialize(g_appRoot, nullptr);
    atk_object_set_role(g_appRoot, ATK_ROLE_APPLICATION);
    AtkUtilClass* uc = ATK_UTIL_CLASS(g_type_class_ref(ATK_TYPE_UTIL));
    uc->get_root = util_get_root;
    uc->get_toolkit_name = util_toolkit_name;
    uc->get_toolkit_version = util_toolkit_version;
    if (atk_bridge_adaptor_init(nullptr, nullptr) != 0) { g_bridge = nullptr; return nullptr; }   // no a11y bus
  }
  return b;
}

} // namespace prism
#endif
