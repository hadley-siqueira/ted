#include "highlight.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

#include "ui.hpp"

// Realce de sintaxe: um "lexer" de uma passada por linha.
//
// Cada scanner percorre os bytes da esquerda para a direita, pinta o que
// reconhece e devolve o estado em que a linha terminou - e esse estado que
// permite que comentarios de bloco, strings de varias linhas e o conteudo de
// <script>/<style> atravessem varias linhas. Nao ha gramatica nem regex: as
// regras sao heuristicas simples, na ordem em que aparecem no laco.

namespace {

// ---------------------------------------------------------------------------
// Estado empacotado: 8 bits para a linguagem principal, 8 para a embutida.
// ---------------------------------------------------------------------------
constexpr int kSubShift = 8;
constexpr int kMainMask = 0xFF;

int main_state(int s) { return s & kMainMask; }
int sub_state(int s) { return (s >> kSubShift) & kMainMask; }
int pack(int main, int sub) {
  return (main & kMainMask) | ((sub & kMainMask) << kSubShift);
}

void paint(std::vector<int>* out, size_t from, size_t to, int color) {
  for (size_t k = from; k < to && k < out->size(); k++) (*out)[k] = color;
}

bool ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}
bool ident_start(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

// Busca limitada a [from, to): devolve npos se nao achou dentro do trecho.
size_t find_in(const std::string& s, const char* pat, size_t from, size_t to) {
  size_t p = s.find(pat, from);
  if (p == std::string::npos || p >= to) return std::string::npos;
  return p;
}

// Busca ignorando maiusculas/minusculas (as tags de HTML podem vir de
// qualquer jeito: </SCRIPT>, </Script>...).
size_t find_ci(const std::string& hay, const char* needle, size_t from) {
  size_t n = std::strlen(needle);
  if (n == 0 || hay.size() < n) return std::string::npos;
  for (size_t i = from; i + n <= hay.size(); i++) {
    size_t k = 0;
    while (k < n && std::tolower(static_cast<unsigned char>(hay[i + k])) ==
                        std::tolower(static_cast<unsigned char>(needle[k])))
      k++;
    if (k == n) return i;
  }
  return std::string::npos;
}

std::string lower(const std::string& s) {
  std::string o = s;
  for (char& c : o) c = static_cast<char>(std::tolower((unsigned char)c));
  return o;
}

// ---------------------------------------------------------------------------
// Vocabulario de cada linguagem
// ---------------------------------------------------------------------------

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
  // JavaScript + TypeScript (arquivos .ts/.tsx usam as mesmas regras).
  static const std::set<std::string> js_kw = {
      "abstract", "as", "async", "await", "break", "case", "catch", "class",
      "const", "continue", "declare", "default", "delete", "do", "else",
      "enum", "export", "extends", "false", "finally", "for", "from",
      "function", "get", "if", "implements", "import", "in", "instanceof",
      "interface", "let", "new", "null", "of", "private", "protected",
      "public", "readonly", "return", "satisfies", "set", "static", "super",
      "switch", "this", "throw", "true", "try", "type", "typeof", "undefined",
      "var", "void", "while", "yield"};
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
  // Tipos do TypeScript + o vocabulario do React (hooks e afins), para que
  // componentes e hooks fiquem visualmente distintos numa aula de front-end.
  static const std::set<std::string> js_types = {
      "any", "bigint", "boolean", "never", "number", "object", "string",
      "symbol", "unknown", "Array", "Map", "Promise", "Set", "JSON", "Math",
      "console", "document", "window",
      "React", "ReactDOM", "Component", "Fragment", "StrictMode",
      "useState", "useEffect", "useRef", "useMemo", "useCallback",
      "useContext", "useReducer", "useLayoutEffect", "useId", "memo",
      "createContext", "createRoot", "props", "children"};
  static const std::set<std::string> none;
  switch (lang) {
    case Lang::C: return c_types;
    case Lang::Cpp: return cpp_types;
    case Lang::Python: return py_types;
    case Lang::JavaScript: return js_types;
    default: return none;
  }
}

std::string ext_of(const std::string& path) {
  size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  size_t dot = name.find_last_of('.');
  if (dot == std::string::npos) return name;  // sem extensao: devolve o nome
  return lower(name.substr(dot + 1));
}

// Uma tag JSX so e uma tag se o '<' aparecer onde um *valor* pode comecar
// (depois de '(', '=', '{', 'return', virgula...). Sem isso, um "a < b"
// viraria uma tag.
bool jsx_context(const std::string& line, size_t from, size_t lt) {
  size_t k = lt;
  while (k > from) {
    char c = line[k - 1];
    if (c == ' ' || c == '\t') { k--; continue; }
    return std::strchr("(=,{[:;&|?!}>+", c) != nullptr;
  }
  return true;   // comeco do trecho: quase sempre e JSX indentado
}

}  // namespace

// ---------------------------------------------------------------------------
// Deteccao da linguagem pelo nome do arquivo
// ---------------------------------------------------------------------------

Lang Highlighter::detect(const std::string& path) {
  std::string e = ext_of(path);
  if (e == "c" || e == "h") return Lang::C;
  if (e == "cpp" || e == "cc" || e == "cxx" || e == "hpp" || e == "hh" ||
      e == "hxx" || e == "ino")
    return Lang::Cpp;
  if (e == "py" || e == "pyw") return Lang::Python;
  if (e == "js" || e == "mjs" || e == "cjs" || e == "ts" || e == "jsx" ||
      e == "tsx")
    return Lang::JavaScript;
  if (e == "html" || e == "htm" || e == "xhtml" || e == "xml" || e == "svg" ||
      e == "vue" || e == "svelte")
    return Lang::Html;
  if (e == "css" || e == "scss" || e == "sass" || e == "less") return Lang::Css;
  if (e == "sh" || e == "bash" || e == "zsh" || e == ".bashrc") return Lang::Shell;
  if (e == "Makefile" || e == "makefile" || e == "mk") return Lang::Make;
  if (e == "md" || e == "markdown") return Lang::Markdown;
  if (e == "json") return Lang::Json;
  return Lang::None;
}

// ---------------------------------------------------------------------------
// Ponto de entrada
// ---------------------------------------------------------------------------

int Highlighter::highlight(const std::string& line, int state_in,
                           std::vector<int>* out) const {
  out->assign(line.size(), 0);
  switch (lang_) {
    case Lang::None:
      return kNormal;
    case Lang::Markdown:
      return scan_markdown(line, out);
    case Lang::Html:
      return scan_html(line, state_in, out);
    case Lang::Css:
      return scan_css(line, 0, line.size(), state_in, out);
    default:
      return scan_code(line, 0, line.size(), state_in, out, lang_);
  }
}

// ---------------------------------------------------------------------------
// Markdown
// ---------------------------------------------------------------------------

int Highlighter::scan_markdown(const std::string& line,
                               std::vector<int>* out) const {
  if (!line.empty() && line[0] == '#') {
    paint(out, 0, line.size(), ui::kSynKeyword);
    return kNormal;
  }
  for (size_t i = 0; i < line.size(); i++) {
    if (line[i] == '`') {
      size_t j = line.find('`', i + 1);
      if (j == std::string::npos) j = line.size() - 1;
      paint(out, i, j + 1, ui::kSynString);
      i = j;
    }
  }
  return kNormal;
}

// ---------------------------------------------------------------------------
// Linguagens "de codigo": C, C++, Python, JavaScript/JSX, Shell, Make
// ---------------------------------------------------------------------------

int Highlighter::scan_code(const std::string& line, size_t from, size_t to,
                           int state_in, std::vector<int>* out,
                           Lang lang) const {
  const auto& kw = keywords_for(lang);
  const auto& types = types_for(lang);
  const bool c_like =
      lang == Lang::C || lang == Lang::Cpp || lang == Lang::JavaScript;
  const bool jsx = (lang == Lang::JavaScript);
  const bool hash_comment =
      lang == Lang::Python || lang == Lang::Shell || lang == Lang::Make;

  int state = main_state(state_in);
  size_t i = from;
  if (to > line.size()) to = line.size();

  // --- continuacao do que ficou aberto na linha anterior ---
  if (state == kBlockComment) {
    size_t end = find_in(line, "*/", i, to);
    if (end == std::string::npos) {
      paint(out, i, to, ui::kSynComment);
      return kBlockComment;
    }
    paint(out, i, end + 2, ui::kSynComment);
    i = end + 2;
    state = kNormal;
  } else if (state == kPyString1 || state == kPyString2) {
    const char* term = (state == kPyString1) ? "'''" : "\"\"\"";
    size_t end = find_in(line, term, i, to);
    if (end == std::string::npos) {
      paint(out, i, to, ui::kSynString);
      return state;
    }
    paint(out, i, end + 3, ui::kSynString);
    i = end + 3;
    state = kNormal;
  } else if (state == kJsTemplate) {
    size_t j = i;
    while (j < to && line[j] != '`') {
      if (line[j] == '\\') j++;
      j++;
    }
    if (j >= to) {
      paint(out, i, to, ui::kSynString);
      return kJsTemplate;
    }
    paint(out, i, j + 1, ui::kSynString);
    i = j + 1;
    state = kNormal;
  } else {
    state = kNormal;
  }

  // --- diretiva de pre-processador ocupa a linha toda ---
  if ((lang == Lang::C || lang == Lang::Cpp) && from == 0 && i == 0) {
    size_t f = line.find_first_not_of(" \t");
    if (f != std::string::npos && f < to && line[f] == '#') {
      size_t cmt = find_in(line, "//", f, to);
      paint(out, f, cmt == std::string::npos ? to : cmt, ui::kSynPreproc);
      if (cmt != std::string::npos) paint(out, cmt, to, ui::kSynComment);
      return kNormal;
    }
  }

  while (i < to) {
    const char c = line[i];

    // Comentarios.
    if (c_like && c == '/' && i + 1 < to) {
      if (line[i + 1] == '/') {
        paint(out, i, to, ui::kSynComment);
        return kNormal;
      }
      if (line[i + 1] == '*') {
        size_t end = find_in(line, "*/", i + 2, to);
        if (end == std::string::npos) {
          paint(out, i, to, ui::kSynComment);
          return kBlockComment;
        }
        paint(out, i, end + 2, ui::kSynComment);
        i = end + 2;
        continue;
      }
    }
    if (hash_comment && c == '#') {
      paint(out, i, to, ui::kSynComment);
      return kNormal;
    }

    // Strings de tres aspas (Python).
    if (lang == Lang::Python && (c == '\'' || c == '"') && i + 2 < to &&
        line[i + 1] == c && line[i + 2] == c) {
      const char* term = (c == '\'') ? "'''" : "\"\"\"";
      size_t end = find_in(line, term, i + 3, to);
      if (end == std::string::npos) {
        paint(out, i, to, ui::kSynString);
        return c == '\'' ? kPyString1 : kPyString2;
      }
      paint(out, i, end + 3, ui::kSynString);
      i = end + 3;
      continue;
    }

    // Template literal do JavaScript: pode atravessar linhas.
    if (jsx && c == '`') {
      size_t j = i + 1;
      while (j < to && line[j] != '`') {
        if (line[j] == '\\') j++;
        j++;
      }
      if (j >= to) {
        paint(out, i, to, ui::kSynString);
        return kJsTemplate;
      }
      paint(out, i, j + 1, ui::kSynString);
      i = j + 1;
      continue;
    }

    // Strings e caracteres.
    if (c == '"' || c == '\'') {
      size_t j = i + 1;
      while (j < to) {
        if (line[j] == '\\') { j += 2; continue; }
        if (line[j] == c) { j++; break; }
        j++;
      }
      paint(out, i, std::min(j, to), ui::kSynString);
      i = j;
      continue;
    }

    // Tag JSX: <div className="x"> ou <MeuComponente ... />
    // "</" so existe em JSX, entao nao precisa da checagem de contexto - o
    // que faz "{lista})</div>" funcionar depois de um parentese.
    if (jsx && c == '<' && i + 1 < to &&
        (ident_start(line[i + 1]) || line[i + 1] == '/') &&
        (line[i + 1] == '/' || jsx_context(line, from, i))) {
      size_t j = i + 1;
      if (line[j] == '/') j++;
      size_t name = j;
      while (j < to && (ident_char(line[j]) || line[j] == '.' || line[j] == '-'))
        j++;
      if (j > name) {
        paint(out, i, name, ui::kSynOperator);
        // Componente do React comeca com maiuscula; tag do HTML, minuscula.
        paint(out, name, j,
              std::isupper(static_cast<unsigned char>(line[name]))
                  ? ui::kSynType
                  : ui::kSynKeyword);
        bool closed = false;
        i = scan_attributes(line, j, to, out, &closed, /*html=*/false);
        continue;
      }
    }

    // Numeros.
    if (std::isdigit(static_cast<unsigned char>(c)) &&
        (i == from || !ident_char(line[i - 1]))) {
      size_t j = i;
      while (j < to && (std::isalnum(static_cast<unsigned char>(line[j])) ||
                        line[j] == '.' || line[j] == 'x' || line[j] == 'X'))
        j++;
      paint(out, i, j, ui::kSynNumber);
      i = j;
      continue;
    }

    // Identificadores, palavras-chave e tipos.
    if (ident_start(c)) {
      size_t j = i;
      while (j < to && ident_char(line[j])) j++;
      std::string word = line.substr(i, j - i);
      if (kw.count(word)) paint(out, i, j, ui::kSynKeyword);
      else if (types.count(word)) paint(out, i, j, ui::kSynType);
      i = j;
      continue;
    }

    i++;
  }
  return state;
}

// ---------------------------------------------------------------------------
// Atributos de uma tag (HTML e JSX)
// ---------------------------------------------------------------------------

size_t Highlighter::scan_attributes(const std::string& line, size_t i, size_t to,
                                    std::vector<int>* out, bool* closed,
                                    bool html) const {
  *closed = false;
  while (i < to) {
    const char c = line[i];
    if (c == '>') {
      paint(out, i, i + 1, ui::kSynOperator);
      *closed = true;
      return i + 1;
    }
    if (c == '/' && i + 1 < to && line[i + 1] == '>') {
      paint(out, i, i + 2, ui::kSynOperator);
      *closed = true;
      return i + 2;
    }
    if (c == '"' || c == '\'') {
      size_t j = i + 1;
      while (j < to && line[j] != c) {
        if (line[j] == '\\') j++;
        j++;
      }
      j = std::min(j + 1, to);
      paint(out, i, j, ui::kSynString);
      i = j;
      continue;
    }
    // Em JSX o valor pode ser uma expressao: devolvemos o controle para o
    // scanner de JavaScript, que sabe destacar o que esta dentro das chaves.
    if (!html && c == '{') return i;
    if (ident_start(c) || c == '@' || c == ':') {
      size_t j = i;
      while (j < to && (ident_char(line[j]) || std::strchr("-:.@", line[j])))
        j++;
      paint(out, i, j, ui::kSynType);
      i = j;
      continue;
    }
    i++;
  }
  return i;
}

// ---------------------------------------------------------------------------
// CSS (e o basico de SCSS/LESS)
// ---------------------------------------------------------------------------

int Highlighter::scan_css(const std::string& line, size_t from, size_t to,
                          int state_in, std::vector<int>* out) const {
  int state = main_state(state_in);
  if (state != kCssBlock && state != kBlockComment && state != kCssBlockComment)
    state = kNormal;
  if (to > line.size()) to = line.size();
  size_t i = from;

  // Dentro de uma regra, o que vem depois de ':' e um valor (e nao uma
  // propriedade). Vale por linha, que cobre a esmagadora maioria dos casos.
  bool in_value = false;

  if (state == kBlockComment || state == kCssBlockComment) {
    size_t end = find_in(line, "*/", i, to);
    if (end == std::string::npos) {
      paint(out, i, to, ui::kSynComment);
      return state;
    }
    paint(out, i, end + 2, ui::kSynComment);
    i = end + 2;
    state = (state == kCssBlockComment) ? kCssBlock : kNormal;
  }

  while (i < to) {
    const char c = line[i];

    if (c == '/' && i + 1 < to && line[i + 1] == '*') {
      size_t end = find_in(line, "*/", i + 2, to);
      if (end == std::string::npos) {
        paint(out, i, to, ui::kSynComment);
        return (state == kCssBlock) ? kCssBlockComment : kBlockComment;
      }
      paint(out, i, end + 2, ui::kSynComment);
      i = end + 2;
      continue;
    }
    if (c == '/' && i + 1 < to && line[i + 1] == '/') {   // comentario do SCSS
      paint(out, i, to, ui::kSynComment);
      return state;
    }
    if (c == '"' || c == '\'') {
      size_t j = i + 1;
      while (j < to && line[j] != c) {
        if (line[j] == '\\') j++;
        j++;
      }
      j = std::min(j + 1, to);
      paint(out, i, j, ui::kSynString);
      i = j;
      continue;
    }
    if (c == '{') { state = kCssBlock; in_value = false; i++; continue; }
    if (c == '}') { state = kNormal; in_value = false; i++; continue; }
    if (c == ';') { in_value = false; i++; continue; }
    if (c == ':') {
      if (state == kCssBlock) in_value = true;
      i++;
      continue;
    }
    if (c == '@') {   // @media, @import, @keyframes...
      size_t j = i + 1;
      while (j < to && (ident_char(line[j]) || line[j] == '-')) j++;
      paint(out, i, j, ui::kSynPreproc);
      i = j;
      continue;
    }
    if (c == '!') {   // !important
      size_t j = i + 1;
      while (j < to && ident_char(line[j])) j++;
      paint(out, i, j, ui::kSynPreproc);
      i = j;
      continue;
    }
    if (c == '#') {   // cor #ff8800 dentro da regra, seletor #id fora dela
      size_t j = i + 1;
      while (j < to && (ident_char(line[j]) || line[j] == '-')) j++;
      paint(out, i, j, in_value ? ui::kSynNumber : ui::kSynType);
      i = j;
      continue;
    }
    if (c == '.' && i + 1 < to && ident_start(line[i + 1]) && !in_value) {
      size_t j = i + 1;                     // seletor de classe
      while (j < to && (ident_char(line[j]) || line[j] == '-')) j++;
      paint(out, i, j, ui::kSynType);
      i = j;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '.' && i + 1 < to &&
         std::isdigit(static_cast<unsigned char>(line[i + 1])))) {
      size_t j = i;                         // numero com unidade: 16px, 1.5rem
      while (j < to && (std::isalnum(static_cast<unsigned char>(line[j])) ||
                        line[j] == '.' || line[j] == '%'))
        j++;
      paint(out, i, j, ui::kSynNumber);
      i = j;
      continue;
    }
    if (ident_start(c) || c == '-') {
      size_t j = i;
      while (j < to && (ident_char(line[j]) || line[j] == '-')) j++;
      if (j == i) { i++; continue; }
      if (state == kCssBlock && !in_value) {
        // E propriedade se o proximo caractere util for ':' ... mas nao se a
        // linha ainda abrir um bloco depois disso: ai era um seletor com
        // pseudo-classe dentro de um @media ("#menu a:hover { ... }").
        size_t k = j;
        while (k < to && (line[k] == ' ' || line[k] == '\t')) k++;
        if (k < to && line[k] == ':') {
          bool selector = find_in(line, "{", k, to) != std::string::npos;
          paint(out, i, j, selector ? ui::kSynKeyword : ui::kSynType);
        }
      } else if (state != kCssBlock) {
        paint(out, i, j, ui::kSynKeyword);   // seletor (div, a, body...)
      }
      i = j;
      continue;
    }
    i++;
  }
  return state;
}

// ---------------------------------------------------------------------------
// HTML (com CSS e JavaScript embutidos)
// ---------------------------------------------------------------------------

int Highlighter::scan_html(const std::string& line, int state_in,
                           std::vector<int>* out) const {
  int state = main_state(state_in);
  int sub = sub_state(state_in);
  const size_t n = line.size();
  size_t i = 0;

  while (i < n) {
    // --- dentro de um comentario <!-- ... --> ---
    if (state == kHtmlComment) {
      size_t end = line.find("-->", i);
      if (end == std::string::npos) {
        paint(out, i, n, ui::kSynComment);
        return pack(kHtmlComment, 0);
      }
      paint(out, i, end + 3, ui::kSynComment);
      i = end + 3;
      state = kNormal;
      continue;
    }

    // --- conteudo de <script> ou <style> ---
    if (state == kHtmlScript || state == kHtmlStyle) {
      const bool script = (state == kHtmlScript);
      size_t close = find_ci(line, script ? "</script" : "</style", i);
      size_t end = (close == std::string::npos) ? n : close;
      if (end > i) {
        sub = script ? scan_code(line, i, end, sub, out, Lang::JavaScript)
                     : scan_css(line, i, end, sub, out);
      }
      if (close == std::string::npos) return pack(state, sub);
      i = end;          // a tag de fechamento cai no caso normal, abaixo
      state = kNormal;
      sub = 0;
      continue;
    }

    // --- dentro de <tag ...> ainda sem o '>' ---
    if (state == kHtmlTag || state == kHtmlTagScript || state == kHtmlTagStyle) {
      bool closed = false;
      size_t next = scan_attributes(line, i, n, out, &closed, /*html=*/true);
      const bool self_closing = closed && next >= 2 && line[next - 2] == '/';
      i = next;
      if (!closed) return pack(state, 0);
      if (self_closing) state = kNormal;
      else if (state == kHtmlTagScript) state = kHtmlScript;
      else if (state == kHtmlTagStyle) state = kHtmlStyle;
      else state = kNormal;
      continue;
    }

    // --- texto comum ---
    const char c = line[i];
    if (c == '<') {
      if (line.compare(i, 4, "<!--") == 0) {
        paint(out, i, std::min(i + 4, n), ui::kSynComment);
        i += 4;
        state = kHtmlComment;
        continue;
      }
      if (i + 1 < n && (line[i + 1] == '!' || line[i + 1] == '?')) {
        size_t end = line.find('>', i);   // <!DOCTYPE html>, <?xml ... ?>
        size_t stop = (end == std::string::npos) ? n : end + 1;
        paint(out, i, stop, ui::kSynPreproc);
        i = stop;
        continue;
      }
      size_t j = i + 1;
      const bool closing = (j < n && line[j] == '/');
      if (closing) j++;
      size_t name = j;
      while (j < n && (ident_char(line[j]) || line[j] == '-' || line[j] == ':'))
        j++;
      if (j == name) { i++; continue; }   // '<' solto no meio do texto
      paint(out, i, name, ui::kSynOperator);
      paint(out, name, j, ui::kSynKeyword);
      std::string tag = lower(line.substr(name, j - name));
      i = j;
      if (closing) state = kHtmlTag;      // depois do '>' volta ao texto
      else if (tag == "script") state = kHtmlTagScript;
      else if (tag == "style") state = kHtmlTagStyle;
      else state = kHtmlTag;
      continue;
    }
    if (c == '&') {                        // entidade: &nbsp; &amp; &#39;
      size_t end = line.find(';', i);
      if (end != std::string::npos && end - i <= 10) {
        paint(out, i, end + 1, ui::kSynNumber);
        i = end + 1;
        continue;
      }
    }
    i++;
  }
  return pack(state, sub);
}
