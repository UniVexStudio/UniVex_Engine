// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Code & Shader Editor Implementation.
#include "prism/CodeEditor.h"
#include "prism/Application.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace prism {

static const char* kDefaultGlsl =
    "#version 330 core\n"
    "// PRISM Engine Standard PBR Shader\n"
    "layout(location = 0) in vec3 inPosition;\n"
    "layout(location = 1) in vec3 inNormal;\n"
    "layout(location = 2) in vec2 inTexCoord;\n"
    "\n"
    "uniform mat4 uModelViewProj;\n"
    "uniform vec3 uSunDirection;\n"
    "uniform vec4 uAlbedoColor;\n"
    "\n"
    "out vec4 fragColor;\n"
    "\n"
    "void main() {\n"
    "    vec3 N = normalize(inNormal);\n"
    "    vec3 L = normalize(-uSunDirection);\n"
    "    float NdotL = max(dot(N, L), 0.05);\n"
    "    vec3 diffuse = uAlbedoColor.rgb * NdotL;\n"
    "    fragColor = vec4(diffuse, uAlbedoColor.a);\n"
    "}\n";

CodeEditor::CodeEditor(std::string initialCode, SyntaxLanguage lang)
    : m_language(lang) {
  if (initialCode.empty()) {
    setText(kDefaultGlsl);
  } else {
    setText(std::move(initialCode));
  }
}

void CodeEditor::setText(std::string text) {
  splitLines(text);
  m_cursorLine = 0;
  m_cursorCol = 0;
  m_scrollY = 0.0f;
  invalidate();
  if (onTextChanged) onTextChanged(this->text());
}

std::string CodeEditor::text() const {
  std::string result;
  for (size_t i = 0; i < m_lines.size(); i++) {
    result += m_lines[i];
    if (i + 1 < m_lines.size()) result += '\n';
  }
  return result;
}

void CodeEditor::splitLines(const std::string& text) {
  m_lines.clear();
  std::stringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    m_lines.push_back(line);
  }
  if (m_lines.empty()) m_lines.push_back("");
}

std::vector<SyntaxToken> CodeEditor::tokenizeLine(const std::string& line) const {
  std::vector<SyntaxToken> tokens;
  if (line.empty()) return tokens;

  size_t i = 0;
  while (i < line.size()) {
    // Skip whitespace and collect as default
    if (std::isspace(static_cast<unsigned char>(line[i]))) {
      size_t start = i;
      while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) i++;
      tokens.push_back({TokenType::Default, line.substr(start, i - start)});
      continue;
    }

    // Preprocessor
    if (line[i] == '#') {
      tokens.push_back({TokenType::Preprocessor, line.substr(i)});
      break;
    }

    // Single-line comment
    if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
      tokens.push_back({TokenType::Comment, line.substr(i)});
      break;
    }

    // String literal
    if (line[i] == '"') {
      size_t start = i++;
      while (i < line.size() && line[i] != '"') {
        if (line[i] == '\\' && i + 1 < line.size()) i++;
        i++;
      }
      if (i < line.size()) i++; // include closing quote
      tokens.push_back({TokenType::String, line.substr(start, i - start)});
      continue;
    }

    // Number literal
    if (std::isdigit(static_cast<unsigned char>(line[i])) ||
        (line[i] == '.' && i + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[i + 1])))) {
      size_t start = i;
      while (i < line.size() && (std::isdigit(static_cast<unsigned char>(line[i])) ||
                                 line[i] == '.' || line[i] == 'f' || line[i] == 'u')) i++;
      tokens.push_back({TokenType::Number, line.substr(start, i - start)});
      continue;
    }

    // Identifier / Keyword / Type
    if (std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_') {
      size_t start = i;
      while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) i++;
      std::string word = line.substr(start, i - start);

      TokenType type = TokenType::Default;
      static const std::vector<std::string> keywords = {
          "struct", "class", "void", "return", "if", "else", "for", "while",
          "in", "out", "uniform", "layout", "const", "discard", "break", "continue",
          "public", "private", "true", "false"};
      static const std::vector<std::string> types = {
          "vec2", "vec3", "vec4", "mat2", "mat3", "mat4", "float", "int", "uint",
          "bool", "sampler2D", "Texture", "Color", "Vec2", "Vec3", "Mat4", "StringTable"};

      if (std::find(keywords.begin(), keywords.end(), word) != keywords.end()) {
        type = TokenType::Keyword;
      } else if (std::find(types.begin(), types.end(), word) != types.end()) {
        type = TokenType::Type;
      }
      tokens.push_back({type, std::move(word)});
      continue;
    }

    // Any other single symbol (+, -, *, =, ;, {, }, etc.)
    tokens.push_back({TokenType::Default, line.substr(i, 1)});
    i++;
  }

  return tokens;
}

Vec2 CodeEditor::measure(Vec2 avail) {
  return m_desired = constrain(avail);
}

void CodeEditor::arrange(Rect r) {
  m_rect = r;
}

void CodeEditor::ensureCaretVisible() {
  float cursorY = float(m_cursorLine) * m_lineHeight;
  if (cursorY < m_scrollY) {
    m_scrollY = cursorY;
  } else if (cursorY + m_lineHeight > m_scrollY + m_rect.h) {
    m_scrollY = cursorY + m_lineHeight - m_rect.h;
  }
}

void CodeEditor::paint(Canvas& c) {
  const Theme& t = theme();
  c.fillRect(m_rect, Color(22, 25, 32));

  // Margin for line numbers
  Rect marginRect{m_rect.x, m_rect.y, m_marginW, m_rect.h};
  c.fillRect(marginRect, Color(27, 30, 39));
  c.line(Vec2(marginRect.right(), m_rect.y), Vec2(marginRect.right(), m_rect.bottom()), t.border, 1.0f);

  int totalLines = int(m_lines.size());
  int startLine = std::clamp(int(m_scrollY / m_lineHeight), 0, std::max(0, totalLines - 1));
  int endLine = std::clamp(int((m_scrollY + m_rect.h) / m_lineHeight) + 1, 0, totalLines);

  float textStartX = m_rect.x + m_marginW + 10.0f;

  for (int lineIdx = startLine; lineIdx < endLine; lineIdx++) {
    float lineY = m_rect.y + float(lineIdx) * m_lineHeight - m_scrollY;
    bool isCur = (lineIdx == m_cursorLine);

    // Active line subtle highlight
    if (isCur) {
      Rect curRect{m_rect.x + m_marginW + 1.0f, lineY, m_rect.w - m_marginW - 1.0f, m_lineHeight};
      c.fillRect(curRect, Color(255, 255, 255, 8));
    }

    // Line number in margin
    char numBuf[16];
    std::snprintf(numBuf, sizeof(numBuf), "%d", lineIdx + 1);
    c.textIn(*t.small, Rect(m_rect.x, lineY + 2.0f, m_marginW - 8.0f, m_lineHeight),
             numBuf, isCur ? t.textBright : t.textDim, HAlign::Right);

    // Tokenize and paint line tokens
    auto tokens = tokenizeLine(m_lines[size_t(lineIdx)]);
    float tokenX = textStartX;

    for (const auto& tok : tokens) {
      Color col = t.text;
      switch (tok.type) {
        case TokenType::Keyword:      col = Color(198, 120, 221); break; // Purple
        case TokenType::Type:         col = Color(229, 192, 123); break; // Gold
        case TokenType::String:       col = Color(152, 195, 121); break; // Green
        case TokenType::Number:       col = Color(209, 154, 102); break; // Orange
        case TokenType::Comment:      col = Color(92, 99, 112);   break; // Muted Grey
        case TokenType::Preprocessor: col = Color(224, 108, 117); break; // Red
        case TokenType::Default:      col = Color(171, 178, 191); break;
      }
      c.text(*t.mono, Vec2(tokenX, lineY + 3.0f), tok.text, col);
      tokenX += t.mono->width(tok.text);
    }

    // Paint Caret if on this line
    if (isCur && focused()) {
      float caretX = textStartX + t.mono->widthTo(m_lines[size_t(lineIdx)], size_t(std::min(m_cursorCol, int(m_lines[size_t(lineIdx)].size()))));
      c.line(Vec2(caretX, lineY + 2.0f), Vec2(caretX, lineY + m_lineHeight - 2.0f), Color(255, 215, 0), 1.5f);
    }
  }

  // Border around code editor
  c.strokeRect(m_rect, t.border);
}

bool CodeEditor::onMouseDown(const MouseEvent& e) {
  if (e.button == MouseButton::Left && m_rect.contains(e.pos)) {
    float clickY = e.pos.y - m_rect.y + m_scrollY;
    int line = std::clamp(int(clickY / m_lineHeight), 0, std::max(0, int(m_lines.size()) - 1));
    m_cursorLine = line;

    // Calculate column with font indexAt
    const Theme& t = theme();
    float textStartX = m_rect.x + m_marginW + 10.0f;
    float clickX = e.pos.x - textStartX;
    if (clickX <= 0.0f) {
      m_cursorCol = 0;
    } else {
      const auto& lineStr = m_lines[size_t(line)];
      m_cursorCol = int(t.mono->indexAt(lineStr, clickX));
    }
    ensureCaretVisible();
    invalidate();
    return true;
  }
  return false;
}

bool CodeEditor::onMouseMove(const MouseEvent&) {
  return false;
}

bool CodeEditor::onMouseUp(const MouseEvent&) {
  return false;
}

bool CodeEditor::onMouseWheel(const MouseEvent& e) {
  m_scrollY = std::max(0.0f, m_scrollY - e.wheel.y * m_lineHeight * 3.0f);
  invalidate();
  return true;
}

void CodeEditor::insertText(const std::string& s) {
  if (m_readOnly || m_cursorLine >= int(m_lines.size())) return;
  auto& cur = m_lines[size_t(m_cursorLine)];
  m_cursorCol = std::clamp(m_cursorCol, 0, int(cur.size()));
  cur.insert(size_t(m_cursorCol), s);
  m_cursorCol += int(s.size());
  ensureCaretVisible();
  invalidate();
  if (onTextChanged) onTextChanged(text());
}

void CodeEditor::handleBackspace() {
  if (m_readOnly || m_cursorLine >= int(m_lines.size())) return;
  auto& cur = m_lines[size_t(m_cursorLine)];
  if (m_cursorCol > 0) {
    cur.erase(size_t(m_cursorCol - 1), 1);
    m_cursorCol--;
  } else if (m_cursorLine > 0) {
    // Join with previous line
    auto& prev = m_lines[size_t(m_cursorLine - 1)];
    int prevLen = int(prev.size());
    prev += cur;
    m_lines.erase(m_lines.begin() + m_cursorLine);
    m_cursorLine--;
    m_cursorCol = prevLen;
  }
  ensureCaretVisible();
  invalidate();
  if (onTextChanged) onTextChanged(text());
}

void CodeEditor::handleDelete() {
  if (m_readOnly || m_cursorLine >= int(m_lines.size())) return;
  auto& cur = m_lines[size_t(m_cursorLine)];
  if (m_cursorCol < int(cur.size())) {
    cur.erase(size_t(m_cursorCol), 1);
  } else if (m_cursorLine + 1 < int(m_lines.size())) {
    // Join next line into this one
    cur += m_lines[size_t(m_cursorLine + 1)];
    m_lines.erase(m_lines.begin() + m_cursorLine + 1);
  }
  ensureCaretVisible();
  invalidate();
  if (onTextChanged) onTextChanged(text());
}

void CodeEditor::handleEnter() {
  if (m_readOnly || m_cursorLine >= int(m_lines.size())) return;
  auto& cur = m_lines[size_t(m_cursorLine)];
  std::string remainder = cur.substr(size_t(m_cursorCol));
  cur = cur.substr(0, size_t(m_cursorCol));
  m_lines.insert(m_lines.begin() + m_cursorLine + 1, remainder);
  m_cursorLine++;
  m_cursorCol = 0;
  ensureCaretVisible();
  invalidate();
  if (onTextChanged) onTextChanged(text());
}

bool CodeEditor::onKeyDown(const KeyEvent& e) {
  if (e.key == Key::Left) {
    if (m_cursorCol > 0) m_cursorCol--;
    else if (m_cursorLine > 0) {
      m_cursorLine--;
      m_cursorCol = int(m_lines[size_t(m_cursorLine)].size());
    }
    ensureCaretVisible();
    invalidate();
    return true;
  }
  if (e.key == Key::Right) {
    if (m_cursorCol < int(m_lines[size_t(m_cursorLine)].size())) m_cursorCol++;
    else if (m_cursorLine + 1 < int(m_lines.size())) {
      m_cursorLine++;
      m_cursorCol = 0;
    }
    ensureCaretVisible();
    invalidate();
    return true;
  }
  if (e.key == Key::Up) {
    if (m_cursorLine > 0) {
      m_cursorLine--;
      m_cursorCol = std::min(m_cursorCol, int(m_lines[size_t(m_cursorLine)].size()));
    }
    ensureCaretVisible();
    invalidate();
    return true;
  }
  if (e.key == Key::Down) {
    if (m_cursorLine + 1 < int(m_lines.size())) {
      m_cursorLine++;
      m_cursorCol = std::min(m_cursorCol, int(m_lines[size_t(m_cursorLine)].size()));
    }
    ensureCaretVisible();
    invalidate();
    return true;
  }
  if (e.key == Key::Home) {
    m_cursorCol = 0;
    ensureCaretVisible();
    invalidate();
    return true;
  }
  if (e.key == Key::End) {
    m_cursorCol = int(m_lines[size_t(m_cursorLine)].size());
    ensureCaretVisible();
    invalidate();
    return true;
  }
  if (e.key == Key::Backspace) {
    handleBackspace();
    return true;
  }
  if (e.key == Key::Delete) {
    handleDelete();
    return true;
  }
  if (e.key == Key::Enter) {
    handleEnter();
    return true;
  }
  if (e.key == Key::Tab) {
    insertText("  "); // 2 spaces
    return true;
  }
  return false;
}

bool CodeEditor::onTextInput(const std::string& s) {
  insertText(s);
  return true;
}

} // namespace prism
