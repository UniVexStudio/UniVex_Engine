// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Code & Shader Editor.
// Multi-line code editor with line numbers, syntax highlighting (C++/GLSL/HLSL/JSON), and scrolling.
#pragma once
#include "Widget.h"
#include <string>
#include <vector>

namespace prism {

enum class SyntaxLanguage : uint8_t {
  Cpp,
  Glsl,
  Json
};

enum class TokenType : uint8_t {
  Default,
  Keyword,
  Type,
  String,
  Number,
  Comment,
  Preprocessor
};

struct SyntaxToken {
  TokenType type = TokenType::Default;
  std::string text;
};

class CodeEditor : public Widget {
public:
  explicit CodeEditor(std::string initialCode = "", SyntaxLanguage lang = SyntaxLanguage::Glsl);

  void setText(std::string text);
  std::string text() const;

  void setLanguage(SyntaxLanguage lang) { m_language = lang; invalidate(); }
  SyntaxLanguage language() const { return m_language; }

  void setReadOnly(bool ro) { m_readOnly = ro; }
  bool isReadOnly() const { return m_readOnly; }

  Callback<const std::string&> onTextChanged;

  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  bool onMouseWheel(const MouseEvent& e) override;
  bool onKeyDown(const KeyEvent& e) override;
  bool onTextInput(const std::string& s) override;
  bool focusable() const override { return true; }
  bool capturesTyping() const override { return true; }
  Cursor cursor() const override { return Cursor::IBeam; }

  std::vector<SyntaxToken> tokenizeLine(const std::string& line) const;

private:
  void splitLines(const std::string& text);
  void ensureCaretVisible();
  void insertText(const std::string& s);
  void handleBackspace();
  void handleDelete();
  void handleEnter();

  std::vector<std::string> m_lines;
  SyntaxLanguage m_language = SyntaxLanguage::Glsl;
  bool m_readOnly = false;

  int m_cursorLine = 0;
  int m_cursorCol = 0;
  float m_scrollY = 0.0f;
  float m_lineHeight = 20.0f;
  float m_marginW = 46.0f;
};

} // namespace prism
