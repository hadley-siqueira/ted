#include "highlight.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#include "ui.hpp"

namespace {

const std::set<std::string>& keywords_for(Lang lang) {
  static const std::set<std::string> c_kw = {
      "auto", "break", "case", "const", "continue", "default", "do", "else",
      "enum", "extern", "for", "goto", "if", "inline", "register", "restrict",
      "return", "sizeof", "static", "struct", "switch", "typedef", "union",
      "volatile", "while", "NULL"};
  static const std::set<std::string> cpp_kw = {
      "alignas", "alignof", "and", "auto", "break", "case", "catch", "class",
      "const", "constexpr", "const_cast", "continue", "decltype", "default",
      "delete", "do", "dynamic_cast", "else", "enum", "explicit", "export",
      "extern", "false", "final", "for", "friend", "goto", "if", "inline",
      "mutable", "namespace", "new", "noexcept", "not", "nullptr", "operator",
      "or", "override", "private", "protected", "public", "reinterpret_cast",
      "return", "sizeof", "static", "static_assert", "static_cast", "struct",
      "switch", "template", "this", "throw", "true", "try", "typedef",
      "typeid", "typename", "union", "using", "virtual", "volatile", "while"};
  static const std::set<std::string> py_kw = {
      "and", "as", "assert", "async", "await", "break", "class", "continue",
      "def", "del", "elif", "else", "except", "False", "finally", "for",
      "from", "global", "if", "import", "in", "is", "lambda", "None",
      "nonlocal", "not", "or", "pass", "raise", "return", "True", "try",
      "while", "with", "yield", "self"};
  static const std::set<std::string> js_kw = {
      "async", "await", "break", "case", "catch", "class", "const", "continue",
      "default", "delete", "do", "else", "export", "extends", "false",
      "finally", "for", "function", "if", "import", "in", "instanceof", "let",
      "new", "null", "of", "return", "static", "super", "switch", "this",
      "throw", "true", "try", "typeof", "undefined", "var", "void", "while",
      "yield"};
  static const std::set<std::string> sh_kw = {
      "case", "do", "done", "elif", "else", "esac", "fi", "for", "function",
      "if", "in", "local", "return", "then", "until", "while", "export",
      "echo", "read", "source"};
  static const std::set<std::string> none;

  switch (lang) {
    case Lang::C: return c_kw;
    case Lang::Cpp: return cpp_kw;
    case Lang::Python: return py_kw;
    case Lang::JavaScript: return js_kw;
    case Lang::Shell:
    case Lang::Make: return sh_kw;
    default: return none;
  }
}

const std::set<std::string>& types_for(Lang lang) {
  static const std::set<std::string> c_types = {
      "bool", "char", "double", "float", "int", "long", "short", "signed",
      "size_t", "ssize_t", "unsigned", "void", "int8_t", "int16_t", "int32_t",
      "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "FILE"};
  static const std::set<std::string> cpp_types = {
      "bool", "char", "char16_t", "char32_t", "double", "float", "int", "long",
      "short", "signed", "size_t", "string", "unsigned", "void", "wchar_t",
      "vector", "map", "set", "pair", "int8_t", "int16_t", "int32_t",
      "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t"};
  static const std::set<std::string> py_types = {
      "bool", "bytes", "dict", "float", "int", "list", "object", "set", "str",
      "tuple", "print", "len", "range", "input", "open"};
  static const std::set<std::string> none;
  switch (lang) {
    case Lang::C: return c_types;
    case Lang::Cpp: return cpp_types;
    case Lang::Python: return py_types;
    default: return none;
  }
}

bool ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string ext_of(const std::string& path) {
  size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  size_t dot = name.find_last_of('.');
  if (dot == std::string::npos) return name;  // sem extensao: devolve o nome
  std::string e = name.substr(dot + 1);
  std::transform(e.begin(), e.end(), e.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return e;
}

}  // namespace

Lang Highlighter::detect(const std::string& path) {
  std::string e = ext_of(path);
  if (e == "c" || e == "h") return Lang::C;
  if (e == "cpp" || e == "cc" || e == "cxx" || e == "hpp" || e == "hh" ||
      e == "hxx" || e == "ino")
    return Lang::Cpp;
  if (e == "py" || e == "pyw") return Lang::Python;
  if (e == "js" || e == "mjs" || e == "ts" || e == "jsx" || e == "tsx")
    return Lang::JavaScript;
  if (e == "sh" || e == "bash" || e == "zsh" || e == ".bashrc") return Lang::Shell;
  if (e == "Makefile" || e == "makefile" || e == "mk") return Lang::Make;
  if (e == "md" || e == "markdown") return Lang::Markdown;
  if (e == "json") return Lang::Json;
  return Lang::None;
}

int Highlighter::highlight(const std::string& line, int state_in,
                           std::vector<int>* out) const {
  out->assign(line.size(), 0);
  if (lang_ == Lang::None) return kNormal;

  const auto& kw = keywords_for(lang_);
  const auto& types = types_for(lang_);
  const bool c_like = lang_ == Lang::C || lang_ == Lang::Cpp ||
                      lang_ == Lang::JavaScript;
  const bool hash_comment = lang_ == Lang::Python || lang_ == Lang::Shell ||
                            lang_ == Lang::Make;
  int state = state_in;
  size_t i = 0;

  auto paint = [&](size_t from, size_t to, int color) {
    for (size_t k = from; k < to && k < out->size(); k++) (*out)[k] = color;
  };

  // Markdown tem regras proprias e bem simples.
  if (lang_ == Lang::Markdown) {
    if (!line.empty() && line[0] == '#') {
      paint(0, line.size(), ui::kSynKeyword);
      return kNormal;
    }
    for (i = 0; i < line.size(); i++) {
      if (line[i] == '`') {
        size_t j = line.find('`', i + 1);
        if (j == std::string::npos) j = line.size() - 1;
        paint(i, j + 1, ui::kSynString);
        i = j;
      }
    }
    return kNormal;
  }

  // Continuacao de comentario de bloco vindo da linha anterior.
  if (state == kBlockComment) {
    size_t end = line.find("*/");
    if (end == std::string::npos) {
      paint(0, line.size(), ui::kSynComment);
      return kBlockComment;
    }
    paint(0, end + 2, ui::kSynComment);
    i = end + 2;
    state = kNormal;
  } else if (state == kPyString1 || state == kPyString2) {
    const char* term = (state == kPyString1) ? "'''" : "\"\"\"";
    size_t end = line.find(term);
    if (end == std::string::npos) {
      paint(0, line.size(), ui::kSynString);
      return state;
    }
    paint(0, end + 3, ui::kSynString);
    i = end + 3;
    state = kNormal;
  }

  // Diretiva de pre-processador ocupa a linha toda (menos comentarios).
  if (c_like && lang_ != Lang::JavaScript) {
    size_t f = line.find_first_not_of(" \t");
    if (f != std::string::npos && line[f] == '#' && i == 0) {
      size_t cmt = line.find("//", f);
      paint(f, cmt == std::string::npos ? line.size() : cmt, ui::kSynPreproc);
      if (cmt != std::string::npos) paint(cmt, line.size(), ui::kSynComment);
      return kNormal;
    }
  }

  while (i < line.size()) {
    char c = line[i];

    // Comentarios.
    if (c_like && c == '/' && i + 1 < line.size()) {
      if (line[i + 1] == '/') {
        paint(i, line.size(), ui::kSynComment);
        return kNormal;
      }
      if (line[i + 1] == '*') {
        size_t end = line.find("*/", i + 2);
        if (end == std::string::npos) {
          paint(i, line.size(), ui::kSynComment);
          return kBlockComment;
        }
        paint(i, end + 2, ui::kSynComment);
        i = end + 2;
        continue;
      }
    }
    if (hash_comment && c == '#') {
      paint(i, line.size(), ui::kSynComment);
      return kNormal;
    }

    // Strings de tres aspas (Python).
    if (lang_ == Lang::Python && (c == '\'' || c == '"') && i + 2 < line.size() &&
        line[i + 1] == c && line[i + 2] == c) {
      const char* term = (c == '\'') ? "'''" : "\"\"\"";
      size_t end = line.find(term, i + 3);
      if (end == std::string::npos) {
        paint(i, line.size(), ui::kSynString);
        return c == '\'' ? kPyString1 : kPyString2;
      }
      paint(i, end + 3, ui::kSynString);
      i = end + 3;
      continue;
    }

    // Strings e caracteres.
    if (c == '"' || c == '\'' || (lang_ == Lang::JavaScript && c == '`')) {
      size_t j = i + 1;
      while (j < line.size()) {
        if (line[j] == '\\') { j += 2; continue; }
        if (line[j] == c) { j++; break; }
        j++;
      }
      paint(i, std::min(j, line.size()), ui::kSynString);
      i = j;
      continue;
    }

    // Numeros.
    if (std::isdigit(static_cast<unsigned char>(c)) &&
        (i == 0 || !ident_char(line[i - 1]))) {
      size_t j = i;
      while (j < line.size() &&
             (std::isalnum(static_cast<unsigned char>(line[j])) ||
              line[j] == '.' || line[j] == 'x' || line[j] == 'X'))
        j++;
      paint(i, j, ui::kSynNumber);
      i = j;
      continue;
    }

    // Identificadores e palavras-chave.
    if (ident_char(c) && !std::isdigit(static_cast<unsigned char>(c))) {
      size_t j = i;
      while (j < line.size() && ident_char(line[j])) j++;
      std::string word = line.substr(i, j - i);
      if (kw.count(word)) paint(i, j, ui::kSynKeyword);
      else if (types.count(word)) paint(i, j, ui::kSynType);
      i = j;
      continue;
    }

    i++;
  }
  return state;
}
