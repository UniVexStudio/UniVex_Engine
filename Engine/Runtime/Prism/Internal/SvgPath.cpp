// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/SvgPath.h"
#include <cctype>
#include <charconv>
#include <cmath>

namespace prism {

namespace {

void subdivideCubic(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, std::vector<Vec2>& out, int depth = 0) {
  // Flatness check
  float d1 = std::hypot(p1.x - p0.x, p1.y - p0.y);
  float d2 = std::hypot(p2.x - p1.x, p2.y - p1.y);
  float d3 = std::hypot(p3.x - p2.x, p3.y - p2.y);
  float chord = std::hypot(p3.x - p0.x, p3.y - p0.y);

  if (depth > 6 || (d1 + d2 + d3 - chord) < 0.5f) {
    out.push_back(p3);
    return;
  }

  // De Casteljau subdivision at t = 0.5
  Vec2 p01 = (p0 + p1) * 0.5f;
  Vec2 p12 = (p1 + p2) * 0.5f;
  Vec2 p23 = (p2 + p3) * 0.5f;
  Vec2 p012 = (p01 + p12) * 0.5f;
  Vec2 p123 = (p12 + p23) * 0.5f;
  Vec2 p0123 = (p012 + p123) * 0.5f;

  subdivideCubic(p0, p01, p012, p0123, out, depth + 1);
  subdivideCubic(p0123, p123, p23, p3, out, depth + 1);
}

void subdivideQuad(Vec2 p0, Vec2 p1, Vec2 p2, std::vector<Vec2>& out, int depth = 0) {
  float d1 = std::hypot(p1.x - p0.x, p1.y - p0.y);
  float d2 = std::hypot(p2.x - p1.x, p2.y - p1.y);
  float chord = std::hypot(p2.x - p0.x, p2.y - p0.y);

  if (depth > 5 || (d1 + d2 - chord) < 0.5f) {
    out.push_back(p2);
    return;
  }

  Vec2 p01 = (p0 + p1) * 0.5f;
  Vec2 p12 = (p1 + p2) * 0.5f;
  Vec2 p012 = (p01 + p12) * 0.5f;

  subdivideQuad(p0, p01, p012, out, depth + 1);
  subdivideQuad(p012, p12, p2, out, depth + 1);
}

} // namespace

std::vector<std::vector<Vec2>> SvgPath::flatten(float tolerance) const {
  std::vector<std::vector<Vec2>> contours;
  std::vector<Vec2> current;
  Vec2 curPos{0, 0};
  Vec2 startPos{0, 0};

  for (const auto& cmd : m_cmds) {
    switch (cmd.type) {
    case PathCmdType::MoveTo:
      if (!current.empty()) {
        contours.push_back(std::move(current));
        current.clear();
      }
      curPos = startPos = cmd.p0;
      current.push_back(curPos);
      break;

    case PathCmdType::LineTo:
      curPos = cmd.p0;
      current.push_back(curPos);
      break;

    case PathCmdType::QuadTo:
      subdivideQuad(curPos, cmd.p0, cmd.p1, current);
      curPos = cmd.p1;
      break;

    case PathCmdType::CubicTo:
      subdivideCubic(curPos, cmd.p0, cmd.p1, cmd.p2, current);
      curPos = cmd.p2;
      break;

    case PathCmdType::Close:
      if (!current.empty() && (current.front() - curPos).length() > 1e-4f) {
        current.push_back(startPos);
      }
      curPos = startPos;
      if (!current.empty()) {
        contours.push_back(std::move(current));
        current.clear();
      }
      break;
    }
  }

  if (!current.empty()) {
    contours.push_back(std::move(current));
  }
  return contours;
}

void SvgPath::draw(Canvas& c, Color strokeColor, float strokeWidth, Color fillColor, Vec2 offset, float scale) const {
  auto contours = flatten();
  for (auto& contour : contours) {
    if (contour.size() < 2) continue;

    // Apply offset and scale
    for (auto& pt : contour) {
      pt = offset + pt * scale;
    }

    // Fill if requested
    if (fillColor.a > 0 && contour.size() >= 3) {
      c.fillPolygon(contour, fillColor);
    }

    // Stroke
    if (strokeColor.a > 0 && strokeWidth > 0.0f) {
      for (size_t i = 0; i + 1 < contour.size(); i++) {
        c.line(contour[i], contour[i + 1], strokeColor, strokeWidth);
      }
    }
  }
}

namespace {

void skipWs(std::string_view s, size_t& i) {
  while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == ',')) i++;
}

bool readFloat(std::string_view s, size_t& i, float& out) {
  skipWs(s, i);
  if (i >= s.size()) return false;
  size_t start = i;
  if (s[i] == '+' || s[i] == '-') i++;
  bool hasDigits = false;
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) { hasDigits = true; i++; }
  if (i < s.size() && s[i] == '.') {
    i++;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) { hasDigits = true; i++; }
  }
  if (!hasDigits) { i = start; return false; }
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    size_t eStart = i;
    i++;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
    bool expDigits = false;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) { expDigits = true; i++; }
    if (!expDigits) i = eStart;
  }
  std::string_view sub = s.substr(start, i - start);
  float val = 0.0f;
  auto res = std::from_chars(sub.data(), sub.data() + sub.size(), val);
  if (res.ec == std::errc{}) {
    out = val;
    return true;
  }
  return false;
}

} // namespace

SvgPath SvgPath::parse(std::string_view s) {
  SvgPath path;
  size_t i = 0;
  char cmd = 0;
  Vec2 curPos{0, 0};
  Vec2 lastCtrl{0, 0};

  while (i < s.size()) {
    skipWs(s, i);
    if (i >= s.size()) break;

    const size_t at = i;                 // progress guard: every iteration must consume input
    char c = s[i];
    if (std::isalpha(static_cast<unsigned char>(c))) {
      cmd = c;
      i++;
    }

    bool rel = std::islower(static_cast<unsigned char>(cmd));
    char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));

    if (upper == 'Z') {
      path.close();
      lastCtrl = curPos;
    } else if (upper == 'M') {
      float x = 0, y = 0;
      if (readFloat(s, i, x) && readFloat(s, i, y)) {
        Vec2 p = rel ? curPos + Vec2{x, y} : Vec2{x, y};
        path.moveTo(p);
        curPos = lastCtrl = p;
        // Subsequent coordinate pairs after M are treated as lineTo (L)
        cmd = rel ? 'l' : 'L';
      }
    } else if (upper == 'L') {
      float x = 0, y = 0;
      if (readFloat(s, i, x) && readFloat(s, i, y)) {
        Vec2 p = rel ? curPos + Vec2{x, y} : Vec2{x, y};
        path.lineTo(p);
        curPos = lastCtrl = p;
      }
    } else if (upper == 'H') {
      float x = 0;
      if (readFloat(s, i, x)) {
        Vec2 p = rel ? Vec2{curPos.x + x, curPos.y} : Vec2{x, curPos.y};
        path.lineTo(p);
        curPos = lastCtrl = p;
      }
    } else if (upper == 'V') {
      float y = 0;
      if (readFloat(s, i, y)) {
        Vec2 p = rel ? Vec2{curPos.x, curPos.y + y} : Vec2{curPos.x, y};
        path.lineTo(p);
        curPos = lastCtrl = p;
      }
    } else if (upper == 'C') {
      float x1 = 0, y1 = 0, x2 = 0, y2 = 0, x = 0, y = 0;
      if (readFloat(s, i, x1) && readFloat(s, i, y1) &&
          readFloat(s, i, x2) && readFloat(s, i, y2) &&
          readFloat(s, i, x) && readFloat(s, i, y)) {
        Vec2 p1 = rel ? curPos + Vec2{x1, y1} : Vec2{x1, y1};
        Vec2 p2 = rel ? curPos + Vec2{x2, y2} : Vec2{x2, y2};
        Vec2 p = rel ? curPos + Vec2{x, y} : Vec2{x, y};
        path.cubicTo(p1, p2, p);
        lastCtrl = p2;
        curPos = p;
      }
    } else if (upper == 'Q') {
      float x1 = 0, y1 = 0, x = 0, y = 0;
      if (readFloat(s, i, x1) && readFloat(s, i, y1) &&
          readFloat(s, i, x) && readFloat(s, i, y)) {
        Vec2 p1 = rel ? curPos + Vec2{x1, y1} : Vec2{x1, y1};
        Vec2 p = rel ? curPos + Vec2{x, y} : Vec2{x, y};
        path.quadTo(p1, p);
        lastCtrl = p1;
        curPos = p;
      }
    } else {
      // Unrecognized or unsupported command, skip
      i++;
    }
    // Malformed data (a command letter followed by junk, or "Z" followed by numbers) used to loop forever here:
    // skip the offending character instead, and drop the implicit command so it cannot repeat.
    if (i == at) { i++; cmd = 0; }
    else if (upper == 'Z') cmd = 0;
  }

  return path;
}

} // namespace prism
