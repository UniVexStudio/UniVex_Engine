// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - BlueprintGraph rendering: grid, comments, spline wires, nodes, pins, toolbar and minimap.
#include "BlueprintInternal.h"
#include "prism/Application.h"
#include <cmath>
#include <cstdio>

namespace prism {

using PT = BlueprintPinType;
using namespace bp;

struct BlueprintPaint {
  static void node(const BlueprintGraph& g, Canvas& c, const BlueprintNode& n);
  static void pin(Canvas& c, Vec2 at, PT type, bool connected, float s, Color hollowFill, bool hot, bool chevron, bool exec);
};

namespace {

Color bgColor() { return Color(36, 37, 40); }
constexpr float kPi = 3.14159265f;

Vec2 bez(Vec2 a, Vec2 b, Vec2 c, Vec2 d, float t) {
  float u = 1 - t, w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
  return {w0 * a.x + w1 * b.x + w2 * c.x + w3 * d.x, w0 * a.y + w1 * b.y + w2 * c.y + w3 * d.y};
}

void polyline(Canvas& c, const std::vector<Vec2>& p, Color col, float th) {
  for (size_t i = 0; i + 1 < p.size(); i++) c.line(p[i], p[i + 1], col, th);
  if (th > 2.4f) for (size_t i = 1; i + 1 < p.size(); i++) c.fillCircle(p[i], th * 0.5f, col);   // round joins
}

bool parseColor(const std::string& s, Color& out) {
  float v[3];
  if (std::sscanf(s.c_str(), "%f,%f,%f", &v[0], &v[1], &v[2]) != 3) return false;
  bool unit = v[0] <= 1.f && v[1] <= 1.f && v[2] <= 1.f;
  auto q = [&](float x) { return uint8_t(std::clamp(unit ? x * 255.f : x, 0.f, 255.f)); };
  out = Color(q(v[0]), q(v[1]), q(v[2]));
  return true;
}

// Each node gets its own glyph, chosen from its name (title / subtitle), so the tile says what the node does.
Icon nodeIconFor(const BlueprintNode& n) {
  if (n.icon != Icon::None) return n.icon;
  std::string t = n.title;
  for (char& ch : t) ch = char(std::tolower(static_cast<unsigned char>(ch)));
  auto has = [&](const char* k) { return t.find(k) != std::string::npos; };
  // a whole word: "ik" names IK, not the middle of "pick"
  auto word = [&](const char* k) {
    const std::string key(k);
    for (size_t at = t.find(key); at != std::string::npos; at = t.find(key, at + 1)) {
      const bool startOk = at == 0 || !std::isalnum(static_cast<unsigned char>(t[at - 1]));
      const size_t end = at + key.size();
      if (startOk && (end >= t.size() || !std::isalnum(static_cast<unsigned char>(t[end])))) return true;
    }
    return false;
  };
  // the animation graph's own spellings first
  if (n.category == BlueprintNodeCategory::Animation) {
    if (has("sequence player") || t == "clip" || word("clip")) return Icon::AnimClipNode;
    if (has("blend space") || has("blendspace")) return Icon::NodeBlendSpace;
    if (has("layered blend")) return Icon::LayeredBlend;
    if (has("blend poses by bool") || has("by bool")) return Icon::BlendByBool;
    if (has("blend") || has("blend2")) return Icon::NodeBlend;
    if (has("additive")) return Icon::AdditivePose;
    if (has("one shot") || has("oneshot")) return Icon::OneShot;
    if (has("time scale") || has("timescale")) return Icon::TimeScale;
    if (has("time seek") || has("timeseek")) return Icon::TimeSeek;
    if (word("select")) return Icon::AnimSelect;
    if (has("look at") || has("lookat")) return Icon::LookAt;
    if (has("modify bone")) return Icon::ModifyBone;
    if (word("ik") || has("two bone") || has("bone")) return Icon::TwoBoneIK;
    if (word("slot") || has("slot ")) return Icon::NodeSlot;
    if (has("output")) return Icon::NodeOutput;
    if (has("state machine") || has("statemachine")) return Icon::StateMachine;
  }
  // events
  if (has("overlap")) return Icon::NodeOverlap;
  if (has("event hit")) return Icon::NodeImpact;
  if (has("anydamage") || has("damage")) return Icon::NodeDamage;
  if (has("beginplay")) return Icon::NodeFlag;
  if (has("event tick")) return Icon::NodeTick;
  if (has("endplay")) return Icon::NodeStop;
  if (has("input action") || has("input axis")) return Icon::NodeInput;
  if (has("construct")) return Icon::NodeSpawn;
  if (has("custom event")) return Icon::EventGraph;
  // flow / utility
  if (has("branch")) return Icon::NodeBranch;
  if (has("sequence") && n.category == BlueprintNodeCategory::Flow) return Icon::NodeSequence;
  if (has("for loop") || word("loop")) return Icon::NodeLoop;
  if (has("timer")) return Icon::NodeTimer;
  if (has("delay")) return Icon::NodeClock;
  if (has("do once")) return Icon::NodeGate;
  if (has("is valid")) return Icon::Check;
  if (has("print")) return Icon::NodeText;
  if (has("cast")) return Icon::NodeCast;
  if (has("destroy")) return Icon::Trash;
  if (has("launch")) return Icon::NodeLaunch;
  if (has("movement") || has("location")) return Icon::Move;
  if (has("rotation")) return Icon::Rotate;
  if (has("spawn")) return Icon::NodeSpawn;
  if (has("sound") || has("audio output")) return Icon::NodeSound;
  if (has("visible")) return Icon::Eye;
  if (has("set text")) return Icon::NodeText;
  if (has("enabled")) return Icon::NodeToggle;
  // math
  if (has("lerp")) return Icon::NodeLerp;
  if (has("clamp")) return Icon::NodeClamp;
  if (has("random float") || has("random")) return Icon::NodeRandom;
  if (has("vector")) return Icon::NodeVector;
  if (has("and") && t.size() <= 4) return Icon::NodeLogic;
  if (t == "or" || t == "not") return Icon::NodeLogic;
  if (t == ">" || t == "<" || t == "==") return Icon::NodeCompare;
  if (n.category == BlueprintNodeCategory::PureMath && n.style == BlueprintNodeStyle::Compact) return Icon::NodeMath;
  // animation
  if (has("sequence player")) return Icon::NodeClip;
  if (has("blend space")) return Icon::NodeBlendSpace;
  if (has("layered blend") || has("blend poses")) return Icon::NodeBlend;
  if (word("ik") || has("bone")) return Icon::NodeBone;
  if (has("slot")) return Icon::NodeSlot;
  if (has("output pose")) return Icon::NodeOutput;
  if (has("state machine")) return Icon::StateGraph;
  // state machine
  if (t == "entry") return Icon::NodeEntry;
  if (has("transition") || n.subtitle.find("->") != std::string::npos) return Icon::NodeTransition;
  if (n.category == BlueprintNodeCategory::State) return Icon::StateGraph;
  // behavior tree
  if (t == "root") return Icon::BehaviorGraph;
  if (has("selector")) return Icon::NodeSelector;
  if (n.category == BlueprintNodeCategory::Behavior && (has("sequence"))) return Icon::NodeChain;
  if (has("blackboard") || has("?")) return Icon::NodeDecorator;
  if (has("move to")) return Icon::NodeMoveTo;
  if (has("attack")) return Icon::NodeAttack;
  if (has("wait")) return Icon::NodeHourglass;
  if (has("patrol")) return Icon::NodeRoute;
  // vfx
  if (has("emitter")) return Icon::NodeEmitter;
  if (has("initialize")) return Icon::NodeSpawn;
  if (has("velocity")) return Icon::NodeLaunch;
  if (has("gravity")) return Icon::NodeGravity;
  if (has("noise")) return Icon::NodeNoise;
  if (has("color over")) return Icon::NodeGradient;
  if (has("size over")) return Icon::NodeScale;
  if (has("sprite")) return Icon::NodeSprite;
  // audio
  if (has("wave player")) return Icon::NodeWave;
  if (has("adsr")) return Icon::NodeEnvelope;
  if (has("low pass")) return Icon::NodeFilter;
  if (has("reverb")) return Icon::NodeEcho;
  if (has("mixer") || has("gain")) return Icon::NodeMixer;
  // material
  if (has("scalar")) return Icon::NodeSlider;
  if (has("color param")) return Icon::NodePalette;
  if (has("texture")) return Icon::NodeTexture;
  if (has("fresnel")) return Icon::NodeFresnel;
  // fall back to the category
  switch (n.category) {
    case BlueprintNodeCategory::Event: return Icon::EventGraph;
    case BlueprintNodeCategory::Function: return Icon::NodeFunction;
    case BlueprintNodeCategory::PureMath: return Icon::NodeFunction;
    case BlueprintNodeCategory::Variable: return Icon::Dot;
    case BlueprintNodeCategory::Material: return Icon::Material;
    case BlueprintNodeCategory::Texture: return Icon::NodeTexture;
    case BlueprintNodeCategory::Flow: return Icon::NodeSequence;
    case BlueprintNodeCategory::Animation: return Icon::AnimGraph;
    case BlueprintNodeCategory::State: return Icon::StateGraph;
    case BlueprintNodeCategory::Behavior: return Icon::BehaviorGraph;
    case BlueprintNodeCategory::Effect: return Icon::VfxGraph;
    case BlueprintNodeCategory::Audio: return Icon::AudioGraph;
  }
  return Icon::Blueprint;
}


} // namespace

// ---------------------------------------------------------------------------- pins
void BlueprintPaint::pin(Canvas& c, Vec2 at, PT type, bool connected, float s, Color hollowFill, bool hot, bool chevron, bool exec) {
  Color col = blueprintPinColor(type);
  if (hot) c.fillCircle(at, (kPinR + 4.5f) * s, col.alpha(0.28f));
  if (exec) {
    Vec2 p[5] = {{-5.f, -6.5f}, {1.f, -6.5f}, {6.8f, 0.f}, {1.f, 6.5f}, {-5.f, 6.5f}};
    Vec2 big[5], small[5];
    for (int i = 0; i < 5; i++) {
      big[i] = {at.x + p[i].x * s, at.y + p[i].y * s};
      small[i] = {at.x + 0.6f * s + (p[i].x - 0.6f) * 0.55f * s, at.y + p[i].y * 0.55f * s};
    }
    c.fillPolygon(big, col);
    if (!connected) c.fillPolygon(small, hollowFill);
    return;
  }
  c.fillCircle(at, kPinR * s, col);
  if (!connected) {
    c.fillCircle(at, (kPinR - 2.0f) * s, hollowFill);
  }
  if (chevron) {
    Vec2 t[3] = {{at.x + 8.2f * s, at.y - 3.3f * s}, {at.x + 11.8f * s, at.y}, {at.x + 8.2f * s, at.y + 3.3f * s}};
    c.fillPolygon(t, col);
  }
}

// ---------------------------------------------------------------------------- nodes
void BlueprintPaint::node(const BlueprintGraph& g, Canvas& c, const BlueprintNode& n) {
  const float s = g.m_zoom;
  const Rect r = g.nodeScreenRect(n);
  const Chrome ch = chromeFor(n.category);
  const float rad = std::max(2.f, kRadius * s);
  const bool labels = s >= 0.5f;
  const bool hover = g.m_hoverNode == n.id;

  // run-trace state
  float pulse = 0, done = 0;
  if (g.m_runActive) {
    auto it = std::find(g.m_runOrder.begin(), g.m_runOrder.end(), n.id);
    if (it != g.m_runOrder.end()) {
      float start = float(it - g.m_runOrder.begin()) * 0.42f;
      if (g.m_runClock >= start) { pulse = std::clamp(1.f - (g.m_runClock - start) / 0.75f, 0.f, 1.f); done = 1; }
    }
  }

  c.shadow(r, rad, std::max(2, int(10 * s)), Color(0, 0, 0, 90));
  c.fillRoundRect(r, rad, ch.bodyDark);
  float inset = std::max(1.f, rad * 0.35f);
  c.gradientV(r.deflated(inset), ch.body, ch.bodyDark);

  if (n.style == BlueprintNodeStyle::Reroute) {
    c.fillRoundRect(r, r.h * 0.5f, Color(30, 33, 40));
    c.strokeRoundRect(r, r.h * 0.5f, n.selected ? Color(255, 176, 48) : Color(150, 156, 168, 200), 1);
  } else if (n.style == BlueprintNodeStyle::Header) {
    float hh = (n.subtitle.empty() ? kHeaderShort : kHeaderTall) * s;
    c.pushClip({r.x, r.y, r.w, hh});
    float cap = rad * 2 + 2;
    c.fillRoundRect({r.x, r.y, cap, hh + rad}, rad, ch.left);
    c.fillRoundRect({r.right() - cap, r.y, cap, hh + rad}, rad, ch.right);
    c.gradientH({r.x + rad, r.y, r.w - 2 * rad, hh}, ch.left, ch.right);
    float sheen = std::clamp(theme().gloss, 0.f, 1.f);          // matte by default; raise Theme::gloss for the glossy look
    if (sheen > 0.02f) c.gradientV({r.x + rad, r.y, r.w - 2 * rad, hh * 0.5f}, Color(255, 255, 255, uint8_t(100 * sheen)), Color(255, 255, 255, 0));
    c.fillRect({r.x, r.y + hh - 1, r.w, 1}, Color(0, 0, 0, 90));
    c.popClip();
    drawIcon(c, nodeIconFor(n), {r.x + 9 * s, r.y + (hh - 20 * s) * 0.5f, 20 * s, 20 * s}, Color(250, 251, 253), 1.7f * s);
    if (labels) {
      float tx = r.x + 36 * s;
      const Font& tf = font(kTitlePx, Weight::Medium, s);
      if (n.subtitle.empty()) {
        c.textIn(tf, {tx, r.y, r.w - 44 * s, hh}, n.title, Color(255, 255, 255), HAlign::Left, true);
      } else {
        c.textIn(tf, {tx, r.y + 4 * s, r.w - 44 * s, 20 * s}, n.title, Color(255, 255, 255), HAlign::Left, true);
        c.textIn(font(kSubPx, Weight::Regular, s), {tx, r.y + 23 * s, r.w - 44 * s, 15 * s}, n.subtitle, ch.sub, HAlign::Left, true);
      }
    }
  } else if (n.style == BlueprintNodeStyle::Compact && !n.title.empty() && labels) {
    const Font& bf = font(kBigPx, Weight::Semibold, s);
    float gx = r.x + n.colL * s, gw = std::max(0.f, r.w - (n.colL + n.colR) * s);
    c.textIn(bf, {gx, r.y, gw, r.h}, n.title, Color(236, 240, 246, 235), HAlign::Center, false);
  }

  // PRISM signature: a slim category rail on header-less nodes (Get, Branch, maths), so they read at a glance without a header.
  if (n.style == BlueprintNodeStyle::Compact) {
    c.fillRoundRect({r.x + 1.5f * s, r.y + rad * 0.9f, 3.2f * s, std::max(2.f, r.h - rad * 1.8f)}, 1.6f * s, ch.left.lighten(0.15f));
  }

  // material preview band
  if (n.preview != BlueprintNode::Preview::None && n.style == BlueprintNodeStyle::Header) {
    float hh = (n.subtitle.empty() ? kHeaderShort : kHeaderTall) * s;
    Rect pv{r.x + 10 * s, r.y + hh + 6 * s, r.w - 20 * s, (kPreviewH - 12) * s};
    c.fillRoundRect(pv, 3 * s, Color(10, 12, 16));
    Rect in = pv.deflated(std::max(1.f, 1.5f * s));
    if (n.preview == BlueprintNode::Preview::Color) {
      Color col;
      if (!parseColor(n.previewValue, col)) col = Color(200, 200, 200);
      c.fillRoundRect(in, 2 * s, col);
    } else if (n.preview == BlueprintNode::Preview::Scalar) {
      float v = 0.5f;
      std::sscanf(n.previewValue.c_str(), "%f", &v);
      v = std::clamp(v, 0.f, 1.f);
      c.pushClip(in);
      c.gradientH(in, Color(14, 14, 16), Color(236, 236, 240));
      float mx = in.x + in.w * v;
      c.fillRect({mx - 1.2f * s, in.y, 2.4f * s, in.h}, Color(255, 176, 48));
      c.popClip();
    } else {
      c.pushClip(in);
      const bool normal = n.preview == BlueprintNode::Preview::Normal;
      c.fillRect(in, normal ? Color(128, 128, 255) : Color(54, 58, 66));
      if (normal) {
        c.fillCircle(in.center(), in.h * 0.36f, Color(150, 150, 255));
        c.strokeCircle(in.center(), in.h * 0.36f, Color(200, 110, 255), std::max(1.f, 1.4f * s));
      } else {
        float cs = std::max(3.f, 8 * s);
        for (float y = in.y, j = 0; y < in.bottom(); y += cs, j++)
          for (float x = in.x, i = 0; x < in.right(); x += cs, i++)
            if (int(i + j) % 2 == 0) c.fillRect({x, y, cs, cs}, Color(190, 194, 204));
      }
      c.popClip();
    }
    c.strokeRoundRect(pv, 3 * s, Color(90, 98, 112, 200), 1);
  }

  // outline
  Color border = n.selected ? Color(255, 176, 48) : hover ? Color(214, 226, 244, 235) : Color(170, 178, 192, 150);
  if (n.style != BlueprintNodeStyle::Reroute) {
    if (n.selected) c.strokeRoundRect(r.inflated(2 * s), rad + 2 * s, Color(255, 176, 48, 70), std::max(2, int(3 * s)));
    c.strokeRoundRect(r, rad, border, n.selected ? std::max(2, int(std::lround(1.6f * s))) : 1);
  }
  if (done > 0) {
    if (pulse > 0.01f) c.strokeRoundRect(r.inflated(3 * s), rad + 3 * s, Color(255, 214, 90, uint8_t(60 + 190 * pulse)), std::max(2, int(3 * s)));
    c.strokeRoundRect(r, rad, Color(110, 230, 140, uint8_t(120 + 100 * pulse)), 2);
  }

  // pins
  const bool reroute = n.style == BlueprintNodeStyle::Reroute;
  for (int pass = 0; pass < 2; pass++) {
    const bool input = pass == 0;
    const auto& pins = input ? n.inputs : n.outputs;
    for (int i = 0; i < int(pins.size()); i++) {
      const BlueprintPin& p = pins[size_t(i)];
      Vec2 at = g.pinScreen(n, i, input);
      bool conn = g.isPinConnected(n.id, i, input);
      bool hot = (g.m_hoverPin.node == n.id && g.m_hoverPin.pin == i && g.m_hoverPin.input == input) ||
                 (g.m_drag == BlueprintGraph::Drag::Wire && g.m_wireTarget.node == n.id && g.m_wireTarget.pin == i && g.m_wireTarget.input == input && g.m_wireValid);
      PT t = p.type == PT::Wildcard ? g.resolvedType(n.id, i, input) : p.type;
      BlueprintPaint::pin(c, at, t, conn, reroute ? s * 0.8f : s, ch.bodyDark.mix(Color(8, 12, 18), 0.4f), hot, !reroute && t != PT::Exec, t == PT::Exec);
      if (!labels || reroute) continue;
      if (!p.hideLabel && !p.name.empty()) {
        const Font& lf = font(kLabelPx, Weight::Regular, s);
        float lw = textWidth(p.name, kLabelPx, Weight::Regular) * s + 4 * s;
        Color lc = hot ? Color(255, 255, 255) : Color(222, 228, 238);
        if (input) c.textIn(lf, {at.x + (kPinR + 7) * s + (t == PT::Exec ? 0 : 0.f), at.y - kRowH * 0.5f * s, lw, kRowH * s}, p.name, lc, HAlign::Left, false);
        else c.textIn(lf, {at.x - (kPinR + 7) * s - lw, at.y - kRowH * 0.5f * s, lw, kRowH * s}, p.name, lc, HAlign::Right, false);
      }
      if (input && !conn && t != PT::Exec && !p.defaultValue.empty()) {
        Rect lb = g.defaultBoxLocal(n, i);
        Vec2 o = g.worldToScreen(n.pos);
        Rect b{o.x + lb.x * s, o.y + lb.y * s, lb.w * s, lb.h * s};
        bool editing = false;
        if (t == PT::Bool) {
          c.fillRoundRect(b, 3 * s, Color(20, 22, 26));
          c.strokeRoundRect(b, 3 * s, Color(120, 126, 138), 1);
          if (p.defaultValue == "true") {
            Vec2 a{b.x + b.w * 0.24f, b.y + b.h * 0.54f}, m{b.x + b.w * 0.44f, b.y + b.h * 0.74f}, e{b.x + b.w * 0.78f, b.y + b.h * 0.26f};
            c.line(a, m, Color(240, 244, 250), std::max(1.2f, 1.8f * s)); c.line(m, e, Color(240, 244, 250), std::max(1.2f, 1.8f * s));
          }
        } else if (t == PT::Color) {
          Color sw;
          c.fillRoundRect(b, 3 * s, Color(20, 22, 26));
          if (parseColor(p.defaultValue, sw)) c.fillRoundRect(b.deflated(2 * s), 2 * s, sw);
          c.strokeRoundRect(b, 3 * s, Color(120, 126, 138), 1);
        } else {
          bool chip = t == PT::Object;
          c.fillRoundRect(b, 3 * s, chip ? Color(92, 94, 100, 235) : Color(18, 20, 24, 235));
          c.strokeRoundRect(b, 3 * s, chip ? Color(150, 152, 160, 160) : Color(96, 102, 114), 1);
          c.textIn(font(kLabelPx - 1, Weight::Regular, s), b, p.defaultValue, chip ? Color(246, 248, 252) : Color(210, 216, 226), HAlign::Center, true);
        }
        (void)editing;
      }
    }
  }

  // validation badge
  if (g.m_compiled) {
    auto it = g.m_report.nodeIssues.find(n.id);
    if (it != g.m_report.nodeIssues.end()) {
      bool err = it->second.find("required input") != std::string::npos;
      Vec2 b{r.right() - 10 * s, r.y - 8 * s};
      Color col = err ? Color(235, 64, 64) : Color(246, 190, 44);
      Vec2 tri[3] = {{b.x, b.y - 9 * s}, {b.x + 10 * s, b.y + 8 * s}, {b.x - 10 * s, b.y + 8 * s}};
      c.fillPolygon(tri, col);
      c.fillRect({b.x - 0.9f * s, b.y - 3 * s, 1.8f * s, 6 * s}, Color(30, 20, 0));
    }
  }
}

// ---------------------------------------------------------------------------- grid
void BlueprintGraph::paintGrid(Canvas& c, Rect view) {
  c.fillRect(view, bgColor());
  auto lines = [&](float step, Color col) {
    float ss = step * m_zoom;
    if (ss < 5.5f) return;
    float ox = std::fmod(m_pan.x, ss), oy = std::fmod(m_pan.y, ss);
    if (ox < 0) ox += ss;
    if (oy < 0) oy += ss;
    for (float x = view.x + ox; x < view.right(); x += ss) c.fillRect({std::floor(x), view.y, 1, view.h}, col);
    for (float y = view.y + oy; y < view.bottom(); y += ss) c.fillRect({view.x, std::floor(y), view.w, 1}, col);
  };
  lines(16, Color(255, 255, 255, 7));
  lines(128, Color(255, 255, 255, 16));
  // origin axes
  Vec2 o = worldToScreen({0, 0});
  if (o.x > view.x && o.x < view.right()) c.fillRect({std::floor(o.x), view.y, 1, view.h}, Color(255, 255, 255, 26));
  if (o.y > view.y && o.y < view.bottom()) c.fillRect({view.x, std::floor(o.y), view.w, 1}, Color(255, 255, 255, 26));
}

// ---------------------------------------------------------------------------- main paint
void BlueprintGraph::paint(Canvas& c) {
  const float s = m_zoom;
  c.pushClip(m_rect);
  paintGrid(c, m_graphRect);
  c.pushClip(m_graphRect);
  if (m_features.watermark) paintWatermark(c);
  updateRelated();
  const bool dimOn = m_hideUnrelated && !m_related.empty();

  // comments sit behind everything
  for (const auto& cm : m_comments) {
    Vec2 p = worldToScreen(cm.bounds.pos());
    Rect r{p.x, p.y, cm.bounds.w * s, cm.bounds.h * s};
    if (!r.intersects(m_graphRect)) continue;
    float rad = 5 * s;
    Color base = cm.color.mix(Color(110, 114, 122, cm.color.a), 0.72f);   // comment boxes stay neutral
    Color body = base.withAlpha(uint8_t(std::max<int>(base.a, 34)));
    c.fillRoundRect(r, rad, body.mix(Color(0, 0, 0, body.a), 0.0f));
    float th = 26 * s;
    c.pushClip({r.x, r.y, r.w, th});
    c.fillRoundRect({r.x, r.y, r.w, th + rad}, rad, base.withAlpha(150).mix(Color(10, 14, 20, 150), 0.25f));
    c.popClip();
    c.strokeRoundRect(r, rad, cm.selected ? Color(255, 176, 48) : base.withAlpha(170).lighten(0.25f), cm.selected ? 2 : 1);
    if (s >= 0.45f) c.textIn(font(kTitlePx, Weight::Medium, s), {r.x + 10 * s, r.y, r.w - 20 * s, th}, cm.title, Color(240, 244, 250), HAlign::Left, true);
    Vec2 h0{r.right() - 4 * s, r.bottom() - 4 * s};
    for (int k = 0; k < 3; k++) c.line({h0.x - float(k) * 4 * s - 3 * s, h0.y}, {h0.x, h0.y - float(k) * 4 * s - 3 * s}, Color(220, 226, 236, 130), 1);
  }

  // wires
  const Rect cull = m_graphRect.inflated(80);
  for (size_t wi = 0; wi < m_wires.size(); wi++) {
    const BlueprintWire& w = m_wires[wi];
    const BlueprintNode* a = findNode(w.fromNodeId);
    const BlueprintNode* b = findNode(w.toNodeId);
    if (!a || !b) continue;
    Vec2 pa = pinScreen(*a, w.fromPinIndex, false), pb = pinScreen(*b, w.toPinIndex, true);
    Rect bb{std::min(pa.x, pb.x) - 120 * s, std::min(pa.y, pb.y) - 60 * s, std::fabs(pa.x - pb.x) + 240 * s, std::fabs(pa.y - pb.y) + 120 * s};
    if (!bb.intersects(cull)) continue;
    PT t = resolvedType(w.fromNodeId, w.fromPinIndex, false);
    if (t == PT::Wildcard) t = resolvedType(w.toNodeId, w.toPinIndex, true);
    bool exec = t == PT::Exec;
    Color col = blueprintPinColor(t);
    bool lit = a->selected || b->selected || int(wi) == m_hoverWire;
    float th = (exec ? 2.8f : 2.3f) * std::max(0.55f, s) + (lit ? 1.0f : 0.f);
    if (lit) col = col.lighten(0.22f);
    if (dimOn && !(m_related.count(w.fromNodeId) && m_related.count(w.toNodeId))) col = col.alpha(0.18f);
    auto pts = wirePoints(pa, pb, 40);
    polyline(c, pts, col, th);
    if (m_runActive && (exec || !usesExecFlow())) {
      auto it = std::find(m_runOrder.begin(), m_runOrder.end(), w.fromNodeId);
      if (it != m_runOrder.end()) {
        float start = float(it - m_runOrder.begin()) * 0.42f;
        float prog = (m_runClock - start) / 0.42f;
        if (prog >= 0 && prog <= 1.2f) {
          Vec2 d = bez(pa, pts[pts.size() / 3], pts[2 * pts.size() / 3], pb, std::min(prog, 1.f));
          c.fillCircle(d, 6 * s, Color(255, 232, 140, 70));
          c.fillCircle(d, 3.4f * s, Color(255, 250, 220));
        }
      }
    }
  }

  // nodes (selected ones last so their outline is never covered)
  for (const auto& n : m_nodes)
    if (!n.selected && nodeScreenRect(n).inflated(20).intersects(m_graphRect)) BlueprintPaint::node(*this, c, n), dimOn && !m_related.count(n.id) ? c.fillRoundRect(nodeScreenRect(n), 6 * m_zoom, theme().window.alpha(0.72f)) : void();
  for (const auto& n : m_nodes)
    if (n.selected && nodeScreenRect(n).inflated(20).intersects(m_graphRect)) BlueprintPaint::node(*this, c, n);

  // wire being dragged
  if (m_drag == Drag::Wire) {
    const BlueprintNode* a = findNode(m_wireFrom.node);
    if (a) {
      Vec2 pa = pinScreen(*a, m_wireFrom.pin, m_wireFrom.input);
      PT t = resolvedType(m_wireFrom.node, m_wireFrom.pin, m_wireFrom.input);
      Vec2 end = m_dragCurrent;
      if (m_wireTarget) { if (const BlueprintNode* tn = findNode(m_wireTarget.node)) end = pinScreen(*tn, m_wireTarget.pin, m_wireTarget.input); }
      Color col = blueprintPinColor(t);
      if (m_wireTarget && !m_wireValid) col = Color(235, 70, 70);
      Vec2 from = m_wireFrom.input ? end : pa, to = m_wireFrom.input ? pa : end;
      polyline(c, wirePoints(from, to, 36), col, 2.8f * std::max(0.6f, s));
      std::string tip = m_wireTarget ? (m_wireValid ? "" : m_wireWhy) : std::string("Drop to search for a ") + blueprintPinTypeName(t) + " node";
      if (!tip.empty()) {
        const Font& f = font(12, Weight::Regular, 1.0f);
        float tw = f.width(tip) + 18;
        Rect tb{m_dragCurrent.x + 16, m_dragCurrent.y + 12, tw, 24};
        c.shadow(tb, 4, 6, Color(0, 0, 0, 120));
        c.fillRoundRect(tb, 4, Color(24, 26, 32, 240));
        c.strokeRoundRect(tb, 4, m_wireTarget && !m_wireValid ? Color(235, 70, 70) : Color(110, 118, 132), 1);
        if (m_wireTarget && !m_wireValid) {
          c.line({tb.x + 8, tb.y + 8}, {tb.x + 16, tb.y + 16}, Color(235, 70, 70), 2); c.line({tb.x + 16, tb.y + 8}, {tb.x + 8, tb.y + 16}, Color(235, 70, 70), 2);
          c.textIn(f, {tb.x + 22, tb.y, tb.w - 22, tb.h}, tip, Color(244, 220, 220), HAlign::Left, false);
          tb.w += 6;
        } else c.textIn(f, tb, tip, Color(214, 220, 232), HAlign::Center, false);
      }
    }
  }
  if (m_drag == Drag::Marquee) {
    Rect m{std::min(m_dragStartScreen.x, m_dragCurrent.x), std::min(m_dragStartScreen.y, m_dragCurrent.y), std::fabs(m_dragCurrent.x - m_dragStartScreen.x), std::fabs(m_dragCurrent.y - m_dragStartScreen.y)};
    c.fillRect(m, Color(80, 140, 255, 34));
    c.strokeRect(m, Color(120, 176, 255, 210), 1);
  }
  // host-owned node bodies: the settings inside each node, laid out where the node is now
  // A body under a node drawn later (selected nodes are drawn last) would paint and take clicks
  // over that node: it steps aside until the nodes no longer overlap.
  auto drawnAfter = [this](const BlueprintNode& a, const BlueprintNode& b) {
    if (a.selected != b.selected) return b.selected;
    return &b > &a;   // same pass: later in m_nodes
  };
  for (auto& [id, body] : m_bodies) {
    const BlueprintNode* n = findNode(id);
    if (!n || n->bodyH <= 0) { body->setVisible(false); continue; }
    Rect br = bodyScreenRect(*n);
    bool covered = false;
    for (const auto& other : m_nodes)
      if (other.id != n->id && drawnAfter(*n, other) && nodeScreenRect(other).intersects(br)) { covered = true; break; }
    if (covered) { body->setVisible(false); continue; }
    const bool fits = s >= 0.7f && br.w > 20 && br.h > 6 && m_graphRect.contains(br.pos()) && m_graphRect.contains({br.right(), br.bottom()});
    body->setVisible(fits);
    if (fits) {
      body->measure({br.w, br.h});   // desired sizes first: the body's rows share its width by them
      body->arrange(br);
      body->paint(c);
    } else if (br.intersects(m_graphRect) && !n->bodySummary.empty() && s >= 0.35f) {
      c.textIn(font(kLabelPx, Weight::Regular, s), br, n->bodySummary, Color(196, 204, 216), HAlign::Left, true);
    }
  }
  c.popClip();

  paintDebugMarks(c);
  if (m_features.minimap && m_showMinimap) paintMinimap(c);
  if (m_features.toolbar) paintToolbar(c);
  else if (m_features.crumbs) paintCrumbs(c);
  c.popClip();
}

// ---------------------------------------------------------------------------- wires
std::vector<Vec2> BlueprintGraph::wirePoints(Vec2 a, Vec2 b, int segments) const {
  float dx = b.x - a.x;
  float k = dx >= 0 ? std::max(dx * 0.5f, 40.f * m_zoom) : std::min(std::fabs(dx) * 0.85f + 70.f * m_zoom, 260.f * m_zoom);
  Vec2 p1{a.x + k, a.y}, p2{b.x - k, b.y};
  std::vector<Vec2> pts;
  pts.reserve(size_t(segments) + 1);
  for (int i = 0; i <= segments; i++) pts.push_back(bez(a, p1, p2, b, float(i) / float(segments)));
  return pts;
}

// ---------------------------------------------------------------------------- toolbar
void BlueprintGraph::paintToolbar(Canvas& c) {
  const Theme& th = theme();
  Rect bar{m_rect.x, m_rect.y, m_rect.w, m_toolbarH};
  c.gradientV(bar, th.header.lighten(0.16f), th.header.lighten(0.08f));
  c.fillRect({bar.x, bar.bottom() - 1, bar.w, 1}, th.borderStrong);
  m_btns.clear();
  const Font& f = *th.ui;
  float x = bar.x + 8;
  const float by = bar.y + 5, bh = m_toolbarH - 10;
  auto button = [&](const std::string& id, const std::string& label, bool active, const std::string& tip, Color tint = Color(0, 0, 0, 0), int icon = 0, bool enabled = true, Icon glyph = Icon::None, Color glyphTint = Color(0, 0, 0, 0)) {
    float tw = label.empty() ? 0.f : f.width(label);
    float w = (label.empty() ? 12.f : tw + 20) + (icon ? 18 : 0) + (glyph != Icon::None ? (label.empty() ? 16 : 22) : 0);
    Rect r{x, by, w, bh};
    bool hot = m_toolbarHover >= 0 && m_toolbarHover < int(m_btns.size()) && m_btns[size_t(m_toolbarHover)].id == id;
    Color bg = active ? th.accent : hot ? th.controlHover.lighten(0.30f) : th.control.lighten(0.24f);
    if (!enabled) bg = th.control.lighten(0.10f);
    c.fillRoundRect(r, 4, bg);
    if (!active) c.gradientV(r.deflated(1), Color(255, 255, 255, 20), Color(255, 255, 255, 0));
    c.strokeRoundRect(r, 4, tint.a ? tint.alpha(0.85f) : Color(255, 255, 255, active ? 60 : 36), 1);
    float tx = r.x + (label.empty() ? 6 : 10);
    Color fg = !enabled ? th.textDisabled.lighten(0.15f) : active ? Color(255, 255, 255) : (tint.a ? tint : th.textBright);
    if (icon == 1) {                       // play
      Vec2 t[3] = {{tx, r.y + bh * 0.5f - 5}, {tx + 9, r.y + bh * 0.5f}, {tx, r.y + bh * 0.5f + 5}};
      c.fillPolygon(t, fg);
      tx += 16;
    } else if (icon == 3) {                // dropdown chevron
      Vec2 t[3] = {{tx, r.y + bh * 0.5f - 2.5f}, {tx + 8, r.y + bh * 0.5f - 2.5f}, {tx + 4, r.y + bh * 0.5f + 3}};
      c.fillPolygon(t, fg);
      tx += 14;
    } else if (icon == 2) {                // check
      Vec2 m = {tx + 3, r.y + bh * 0.5f + 3};
      c.line({tx, r.y + bh * 0.5f}, m, fg, 1.8f); c.line(m, {tx + 10, r.y + bh * 0.5f - 5}, fg, 1.8f);
      tx += 16;
    }
    if (glyph != Icon::None) {
      drawIcon(c, glyph, {tx - 2, r.y + (bh - 18) * 0.5f, 18, 18}, !enabled ? th.textDisabled : active ? Color(255, 255, 255) : (glyphTint.a ? glyphTint : fg), 1.4f);
      tx += label.empty() ? 0 : 22;
    }
    if (!label.empty()) c.textIn(f, {tx, r.y, tw + 2, bh}, label, fg, HAlign::Left, false);
    m_btns.push_back({r, id, tip});
    x += w + 5;
  };
  auto sep = [&] { c.fillRect({x + 3, by + 3, 1, bh - 6}, th.border); x += 10; };

  // Toolbar layout: Compile (+ options), Save, Browse | Find, Hide Unrelated, Diff | Class Settings, Class Defaults | Simulate
  std::string cl = "Compile";
  Color tint;
  Icon cg = Icon::Compile;
  if (m_compiled) {
    if (m_report.errors) { cl = std::to_string(m_report.errors) + (m_report.errors == 1 ? " error" : " errors"); tint = Color(240, 90, 90); cg = Icon::Error; }
    else if (m_report.warnings) { cl = std::to_string(m_report.warnings) + (m_report.warnings == 1 ? " warning" : " warnings"); tint = Color(246, 190, 60); cg = Icon::Warning; }
    else { cl = "Compiled"; tint = Color(110, 220, 130); cg = Icon::Check; }
  }
  button("compile", cl, false, "Compile: validate the graph (state icon: needs compile / ok / warnings / errors)", tint, 0, true, cg, tint.a ? tint : Color(150, 200, 255));
  x -= 3;
  button("compile:opts", "", false, "Compile options: Save on Compile, Jump to Error Node", {}, 0, true, Icon::ChevronDown);
  button("save", "Save", false, "Save (Ctrl+S)", {}, 0, true, Icon::Save);
  button("browse", "Browse", false, "Find this graph in the Content Browser", {}, 0, true, Icon::Browse);
  sep();
  button("find", "Find", false, "Find nodes, pins and values (Ctrl+F). Ctrl+Shift+F searches every saved graph", {}, 0, true, Icon::Search);
  button("hide", "", m_hideUnrelated, "Hide Unrelated: fade nodes that are not selected or linked to the selection", {}, 0, true, Icon::EyeOff);
  button("diff", "Diff", false, "Compare with the saved file", {}, 0, true, Icon::Duplicate);
  sep();
  button("classsettings", "", false, "Class Settings", {}, 0, true, Icon::Settings);
  button("classdefaults", "", false, "Class Defaults", {}, 0, true, Icon::Properties);
  sep();
  button("layout", "", false, "Auto Layout: arrange nodes left-to-right (selection only if 2+ are selected)", {}, 0, true, Icon::Grid);
  button("variables", "", false, "Variables panel: add variables, drop Get / Set nodes", {}, 0, true, Icon::Script);
  button("collapse", "", false, "Collapse the selected nodes into one subgraph node (select it and press again to expand)", {}, 0, true, Icon::Layers);
  sep();
  button("run", m_runActive ? "Running" : "Simulate", m_runActive, usesExecFlow() ? "Animate execution from the Event nodes" : "Animate the data flow through the graph", Color(110, 220, 130), 0, true, Icon::Play);
  sep();
  button("undo", "", false, "Undo (Ctrl+Z)", {}, 0, canUndo(), Icon::Undo);
  button("redo", "", false, "Redo (Ctrl+Y)", {}, 0, canRedo(), Icon::Redo);
  button("snap", "", m_snap, "Snap nodes to the 8px grid", {}, 0, true, Icon::Magnet);
  button("minimap", "", m_showMinimap, "Show / hide the overview map", {}, 0, true, Icon::Grid);
  button("frame", "", false, "Frame all (Home) or the selection (F)", {}, 0, true, Icon::Frame);

  char buf[96];
  std::snprintf(buf, sizeof buf, "%d nodes   %d wires   %d%%", nodeCount(), wireCount(), int(std::lround(m_zoom * 100)));
  float tw = f.width(buf);
  if (x + 16 < bar.right() - tw - 12) c.textIn(f, {bar.right() - tw - 12, bar.y, tw + 2, m_toolbarH}, buf, th.textDim, HAlign::Left, false);
  if (m_features.crumbs) paintCrumbs(c);
}

// ---------------------------------------------------------------------------- tab chip + breadcrumb strip
void BlueprintGraph::paintCrumbs(Canvas& c) {
  const Theme& th = theme();
  Rect bar{m_rect.x, m_rect.y + m_toolbarH, m_rect.w, m_crumbH};
  c.fillRect(bar, th.panel.lighten(0.04f));
  c.fillRect({bar.x, bar.bottom() - 1, bar.w, 1}, th.borderStrong);
  const Font& f = *th.ui;
  Color tintc = blueprintPresetTint(m_preset);
  float x = bar.x + 8;
  auto navBtn = [&](const char* id, Icon ic, bool enabled, const char* tip) {
    Rect r{x, bar.y + 5, 22, bar.h - 10};
    bool hot = m_toolbarHover >= 0 && m_toolbarHover < int(m_btns.size()) && m_btns[size_t(m_toolbarHover)].id == id;
    if (hot && enabled) c.fillRoundRect(r, 4, th.controlHover);
    drawIcon(c, ic, r.inflated(-3), enabled ? th.text : th.textDisabled, 1.5f);
    m_btns.push_back({r, id, tip});
    x += 24;
  };
  navBtn("nav:back", Icon::ChevronLeft, !m_navBack.empty(), "Back to the previous graph");
  navBtn("nav:fwd", Icon::ChevronRight, !m_navFwd.empty(), "Forward");
  x += 6;
  // tab chip: icon + graph name + chevron (click to switch graph type)
  std::string name = blueprintPresetName(m_preset);
  float tw = f.width(name);
  Rect chip{x, bar.y + 3, tw + 62, bar.h - 3};
  bool hot = m_toolbarHover >= 0 && m_toolbarHover < int(m_btns.size()) && m_btns[size_t(m_toolbarHover)].id == "preset:menu";
  Vec2 q[4] = {{chip.x, chip.bottom()}, {chip.x + 6, chip.y}, {chip.right() - 6, chip.y}, {chip.right(), chip.bottom()}};
  c.fillPolygon(q, hot ? th.controlHover : th.panel, false);
  c.fillRect({chip.x + 6, chip.y, chip.w - 12, 2}, tintc);
  drawIcon(c, blueprintPresetIcon(m_preset), {chip.x + 12, chip.y + 5, 18, 18}, tintc, 1.5f);
  c.textIn(*th.semibold, {chip.x + 36, chip.y, tw + 4, chip.h}, name, th.textBright, HAlign::Left, false);
  drawIcon(c, Icon::ChevronDown, {chip.right() - 22, chip.y + 7, 14, 14}, th.textDim, 1.4f);
  m_btns.push_back({chip, "preset:menu", "Switch graph type (Widget / Event / Animation / State Machine / Behavior Tree / VFX / Audio / Material)"});
  x = chip.right() + 14;
  // breadcrumb: group > graph
  std::string g = blueprintPresetGroup(m_preset);
  c.textIn(f, {x, bar.y, 120, bar.h}, "PRISM", th.textDim, HAlign::Left, false); x += f.width("PRISM") + 8;
  auto sepch = [&] { drawIcon(c, Icon::ChevronRight, {x, bar.y + 8, 12, 12}, th.textDisabled, 1.3f); x += 14; };
  sepch();
  c.textIn(f, {x, bar.y, 160, bar.h}, g, th.textDim, HAlign::Left, false); x += f.width(g) + 8;
  sepch();
  c.textIn(*th.medium, {x, bar.y, 220, bar.h}, name, th.text, HAlign::Left, false);
  // zoom factor, right aligned
  char z[32];
  std::snprintf(z, sizeof z, "Zoom %d%%", int(std::lround(m_zoom * 100)));
  c.textIn(f, {bar.right() - 90, bar.y, 80, bar.h}, z, th.textDim, HAlign::Right, false);
}

// Big faded graph-type label in the corner of the canvas (the graph-type watermark).
void BlueprintGraph::paintWatermark(Canvas& c) {
  const bool own = !m_watermarkTitle.empty();
  std::string label = own ? m_watermarkTitle : std::string(blueprintPresetName(m_preset));
  for (char& ch : label) ch = char(std::toupper(static_cast<unsigned char>(ch)));
  const Font& f = font(46, Weight::Semibold, 1.0f);
  float w = f.width(label);
  Rect r{m_graphRect.x + 18, m_graphRect.bottom() - 74, w + 80, 62};
  if (m_graphRect.w < w + 140 || m_graphRect.h < 200) return;
  drawIcon(c, own ? m_watermarkIcon : blueprintPresetIcon(m_preset), {r.x, r.y + 2, 52, 52}, Color(58, 61, 67), 3.2f);   // opaque: translucent strokes would double-blend at the round joins
  c.textIn(f, {r.x + 62, r.y, w + 6, 56}, label, Color(200, 204, 212, 34), HAlign::Left, false);
  c.textIn(*theme().ui, {r.x + 64, r.y + 50, 300, 14}, own ? m_watermarkCaption : std::string(blueprintPresetGroup(m_preset)) + " graph", theme().textDim.alpha(0.55f), HAlign::Left, false);
}

// ---------------------------------------------------------------------------- minimap
void BlueprintGraph::paintMinimap(Canvas& c) {
  if (m_nodes.empty()) { m_minimapRect = {}; return; }
  const float W = 190, H = 118;
  m_minimapRect = {m_graphRect.right() - W - 12, m_graphRect.bottom() - H - 12, W, H};
  if (m_graphRect.w < W * 2 || m_graphRect.h < H * 2) { m_minimapRect = {}; return; }
  c.shadow(m_minimapRect, 5, 8, Color(0, 0, 0, 110));
  c.fillRoundRect(m_minimapRect, 5, Color(20, 22, 26, 225));
  c.strokeRoundRect(m_minimapRect, 5, Color(90, 96, 108, 200), 1);
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (auto& n : m_nodes) { x0 = std::min(x0, n.pos.x); y0 = std::min(y0, n.pos.y); x1 = std::max(x1, n.pos.x + n.size.x); y1 = std::max(y1, n.pos.y + n.size.y); }
  Vec2 vtl = screenToWorld(m_graphRect.pos()), vbr = screenToWorld({m_graphRect.right(), m_graphRect.bottom()});
  x0 = std::min(x0, vtl.x); y0 = std::min(y0, vtl.y); x1 = std::max(x1, vbr.x); y1 = std::max(y1, vbr.y);
  Rect in = m_minimapRect.deflated(6);
  float k = std::min(in.w / std::max(1.f, x1 - x0), in.h / std::max(1.f, y1 - y0));
  Vec2 off{in.x + (in.w - (x1 - x0) * k) * 0.5f - x0 * k, in.y + (in.h - (y1 - y0) * k) * 0.5f - y0 * k};
  c.pushClip(m_minimapRect.deflated(1));
  for (auto& cm : m_comments) c.fillRect({off.x + cm.bounds.x * k, off.y + cm.bounds.y * k, cm.bounds.w * k, cm.bounds.h * k}, cm.color.withAlpha(60));
  for (auto& n : m_nodes) {
    Rect r{off.x + n.pos.x * k, off.y + n.pos.y * k, std::max(2.f, n.size.x * k), std::max(2.f, n.size.y * k)};
    c.fillRect(r, n.selected ? Color(255, 176, 48) : chromeFor(n.category).left);
  }
  Rect vr{off.x + vtl.x * k, off.y + vtl.y * k, (vbr.x - vtl.x) * k, (vbr.y - vtl.y) * k};
  c.strokeRect(vr, Color(255, 255, 255, 220), 1);
  c.popClip();
}

} // namespace prism
