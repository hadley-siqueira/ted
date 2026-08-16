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

// Atencao: todo caractere aceito por ident_start() precisa ser aceito por
// ident_char(), senao a regra de identificador nao consome nada e o scanner
// fica parado no mesmo byte. O '$' aparece em $(CXX) do Make, $HOME do shell
// e $elemento do JavaScript.
bool ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
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
  // SQL nao diferencia maiusculas de minusculas: guardamos tudo em minusculo
  // e a palavra lida do texto e convertida antes da busca.
  static const std::set<std::string> sql_kw = {
      "add", "all", "alter", "analyze", "and", "any", "as", "asc", "begin",
      "between", "by", "cascade", "case", "cast", "check", "column", "commit",
      "constraint", "create", "cross", "database", "declare", "default",
      "delete", "desc", "describe", "distinct", "drop", "else", "end",
      "except", "execute", "exists", "explain", "false", "foreign", "from",
      "full", "grant", "group", "having", "if", "ilike", "in", "index",
      "inner", "insert", "intersect", "into", "is", "join", "key", "left",
      "like", "limit", "not", "null", "offset", "on", "or", "order", "outer",
      "over", "partition", "primary", "procedure", "recursive", "references",
      "rename", "replace", "restrict", "return", "returning", "revoke",
      "right", "rollback", "schema", "select", "set", "show", "some", "table",
      "temporary", "then", "to", "transaction", "trigger", "true", "truncate",
      "union", "unique", "update", "use", "using", "values", "view", "when",
      "where", "window", "with"};
  static const std::set<std::string> ruby_kw = {
      "alias", "and", "begin", "break", "case", "class", "def", "defined?",
      "do", "else", "elsif", "end", "ensure", "extend", "false", "for",
      "if", "in", "include", "lambda", "module", "next", "nil", "not", "or",
      "proc", "raise", "redo", "require", "require_relative", "rescue",
      "retry", "return", "self", "super", "then", "true", "undef", "unless",
      "until", "when", "while", "yield",
      "attr_accessor", "attr_reader", "attr_writer", "private", "protected",
      "public", "puts", "print", "new"};
  static const std::set<std::string> cs_kw = {
      "abstract", "as", "async", "await", "base", "break", "case", "catch",
      "checked", "class", "const", "continue", "default", "delegate", "do",
      "else", "enum", "event", "explicit", "extern", "false", "finally",
      "fixed", "for", "foreach", "get", "goto", "if", "implicit", "in",
      "init", "interface", "internal", "is", "lock", "namespace", "nameof",
      "new", "null", "operator", "out", "override", "params", "partial",
      "private", "protected", "public", "readonly", "record", "ref", "return",
      "sealed", "set", "sizeof", "stackalloc", "static", "struct", "switch",
      "this", "throw", "true", "try", "typeof", "unchecked", "unsafe",
      "using", "value", "virtual", "volatile", "when", "where", "while",
      "yield"};
  static const std::set<std::string> hs_kw = {
      "case", "class", "data", "default", "deriving", "do", "else", "foreign",
      "if", "import", "in", "infix", "infixl", "infixr", "instance", "let",
      "module", "newtype", "of", "then", "type", "where"};
  static const std::set<std::string> ml_kw = {
      "and", "as", "assert", "begin", "class", "constraint", "do", "done",
      "downto", "else", "end", "exception", "external", "false", "for", "fun",
      "function", "functor", "if", "in", "include", "inherit", "initializer",
      "lazy", "let", "match", "method", "module", "mutable", "new", "nonrec",
      "object", "of", "open", "or", "private", "rec", "sig", "struct", "then",
      "to", "true", "try", "type", "val", "virtual", "when", "while", "with"};
  static const std::set<std::string> verilog_kw = {
      "always", "always_comb", "always_ff", "always_latch", "assign",
      "automatic", "begin", "case", "casex", "casez", "class", "default",
      "defparam", "disable", "else", "end", "endcase", "endclass",
      "endfunction", "endgenerate", "endinterface", "endmodule", "endpackage",
      "endtask", "extends", "for", "forever", "fork", "function", "generate",
      "genvar", "if", "implements", "import", "initial", "inout", "input",
      "interface", "join", "localparam", "modport", "module", "negedge",
      "output", "package", "parameter", "posedge", "repeat", "return",
      "signed", "task", "typedef", "unsigned", "virtual", "wait", "while",
      "assert", "property", "endproperty", "covergroup", "endgroup"};
  // VHDL nao diferencia maiusculas de minusculas: guardamos em minusculo, como
  // no SQL, e a palavra lida do texto e convertida antes da busca.
  static const std::set<std::string> vhdl_kw = {
      "abs", "access", "after", "alias", "all", "and", "architecture", "array",
      "assert", "attribute", "begin", "block", "body", "buffer", "bus", "case",
      "component", "configuration", "constant", "disconnect", "downto", "else",
      "elsif", "end", "entity", "exit", "file", "for", "function", "generate",
      "generic", "group", "guarded", "if", "impure", "in", "inertial", "inout",
      "is", "label", "library", "linkage", "literal", "loop", "map", "mod",
      "nand", "new", "next", "nor", "not", "null", "of", "on", "open", "or",
      "others", "out", "package", "port", "postponed", "procedure", "process",
      "pure", "range", "record", "register", "reject", "rem", "report",
      "return", "rol", "ror", "select", "severity", "shared", "signal", "sla",
      "sll", "sra", "srl", "subtype", "then", "to", "transport", "type",
      "unaffected", "units", "until", "use", "variable", "wait", "when",
      "while", "with", "xnor", "xor"};
  static const std::set<std::string> none;

  switch (lang) {
    case Lang::C: return c_kw;
    case Lang::Cpp: return cpp_kw;
    case Lang::Python: return py_kw;
    case Lang::JavaScript: return js_kw;
    case Lang::Shell:
    case Lang::Make: return sh_kw;
    case Lang::Sql: return sql_kw;
    case Lang::Ruby:
    case Lang::Erb: return ruby_kw;
    case Lang::CSharp: return cs_kw;
    case Lang::Haskell: return hs_kw;
    case Lang::OCaml: return ml_kw;
    case Lang::Verilog: return verilog_kw;
    case Lang::Vhdl: return vhdl_kw;
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
  // Tipos de coluna e as funcoes mais comuns (tudo em minusculo, como acima).
  static const std::set<std::string> sql_types = {
      "array", "bigint", "bigserial", "binary", "bit", "blob", "bool",
      "boolean", "char", "clob", "date", "datetime", "decimal", "double",
      "enum", "float", "int", "integer", "interval", "json", "jsonb", "money",
      "nchar", "numeric", "nvarchar", "precision", "real", "serial",
      "smallint", "text", "time", "timestamp", "tinyint", "uuid", "varbinary",
      "varchar", "year",
      "abs", "avg", "coalesce", "concat", "count", "current_date",
      "current_timestamp", "date_trunc", "extract", "greatest", "least",
      "length", "lower", "max", "min", "now", "nullif", "round", "row_number",
      "substring", "sum", "trim", "upper"};
  // Ruby: as classes do nucleo e os metodos que a turma usa desde o primeiro
  // dia. Constantes definidas pelo usuario ja ficam destacadas pela regra da
  // maiuscula, entao aqui vao so as embutidas.
  static const std::set<std::string> ruby_types = {
      "Array", "BasicObject", "Class", "Comparable", "Dir", "Enumerable",
      "Exception", "File", "Float", "Hash", "IO", "Integer", "Kernel",
      "Module", "Numeric", "Object", "Proc", "Range", "Regexp", "RuntimeError",
      "Set", "StandardError", "String", "Struct", "Symbol", "Time",
      "each", "map", "select", "reject", "reduce", "inject", "length", "size",
      "push", "pop", "first", "last", "to_s", "to_i", "to_a", "to_sym"};
  static const std::set<std::string> cs_types = {
      "bool", "byte", "char", "decimal", "double", "dynamic", "float", "int",
      "long", "object", "sbyte", "short", "string", "uint", "ulong", "ushort",
      "var", "void",
      "Console", "Dictionary", "Exception", "IEnumerable", "List", "Nullable",
      "Task", "String", "Int32", "Int64", "Boolean", "Double", "DateTime",
      "Guid", "Math", "Convert", "LINQ"};
  static const std::set<std::string> hs_types = {
      "Bool", "Char", "Double", "Either", "Float", "IO", "Int", "Integer",
      "Maybe", "Ordering", "Rational", "String", "Word",
      "True", "False", "Just", "Nothing", "Left", "Right", "LT", "EQ", "GT",
      "map", "filter", "foldr", "foldl", "length", "return", "putStrLn",
      "print", "show", "read"};
  static const std::set<std::string> ml_types = {
      "array", "bool", "bytes", "char", "exn", "float", "int", "list", "option",
      "ref", "string", "unit",
      "None", "Some", "List", "Array", "String", "Printf", "Hashtbl", "Map",
      "print_endline", "print_string", "failwith", "raise", "ignore"};
  static const std::set<std::string> verilog_kw_types = {
      "bit", "byte", "chandle", "event", "int", "integer", "logic", "longint",
      "real", "realtime", "reg", "shortint", "shortreal", "string", "supply0",
      "supply1", "time", "tri", "triand", "trior", "wand", "wire", "wor"};
  static const std::set<std::string> vhdl_types = {
      "bit", "bit_vector", "boolean", "character", "delay_length", "integer",
      "natural", "positive", "real", "severity_level", "signed", "std_logic",
      "std_logic_vector", "std_ulogic", "std_ulogic_vector", "string", "time",
      "unsigned", "ieee", "numeric_std", "std_logic_1164", "work", "textio"};
  static const std::set<std::string> none;
  switch (lang) {
    case Lang::Sql: return sql_types;
    case Lang::C: return c_types;
    case Lang::Cpp: return cpp_types;
    case Lang::Python: return py_types;
    case Lang::JavaScript: return js_types;
    case Lang::Ruby:
    case Lang::Erb: return ruby_types;
    case Lang::CSharp: return cs_types;
    case Lang::Haskell: return hs_types;
    case Lang::OCaml: return ml_types;
    case Lang::Verilog: return verilog_kw_types;
    case Lang::Vhdl: return vhdl_types;
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

// Em Haskell, OCaml, VHDL e Verilog o apostrofo nao delimita so caractere: ele
// entra em nomes (foo'), em variaveis de tipo ('a), em atributos (clk'event) e
// em bases numericas (8'hFF). Nessas linguagens so tratamos como caractere a
// forma curta 'x' ou '\n' - o resto e deixado para as outras regras.
bool is_char_literal(const std::string& s, size_t i, size_t to) {
  if (i + 3 < to && s[i + 1] == '\\') {
    for (size_t j = i + 2; j < to && j <= i + 4; j++)
      if (s[j] == '\'') return true;
    return false;
  }
  return i + 2 < to && s[i + 2] == '\'';
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

CommentSyntax comment_syntax(Lang lang) {
  switch (lang) {
    case Lang::C:
    case Lang::Cpp:
    case Lang::CSharp:
    case Lang::Verilog:
    case Lang::JavaScript: return {"//", "/*", "*/"};
    case Lang::Python:
    case Lang::Shell:
    case Lang::Make: return {"#", "", ""};
    case Lang::Ruby: return {"#", "=begin", "=end"};
    case Lang::Erb: return {"", "<%#", "%>"};
    case Lang::Haskell: return {"--", "{-", "-}"};
    case Lang::OCaml: return {"", "(*", "*)"};
    case Lang::Vhdl:
    case Lang::Sql: return {"--", "/*", "*/"};
    case Lang::Css: return {"", "/*", "*/"};
    case Lang::Html:
    case Lang::Markdown: return {"", "<!--", "-->"};
    default: return {"", "", ""};   // texto puro e JSON nao tem comentario
  }
}

AutoCloseSyntax auto_close_syntax(Lang lang) {
  AutoCloseSyntax a;
  switch (lang) {
    case Lang::JavaScript:      // cobre .js .mjs .cjs .ts .jsx .tsx .vue .svelte
      a.backtick = true;        // `template ${literal}`
      a.tags = true;            // JSX
      break;
    case Lang::Html:
      a.tags = true;
      break;
    case Lang::Shell:
    case Lang::Ruby:
      a.backtick = true;        // `comando` (substituicao)
      break;
    case Lang::Erb:
      a.tags = true;            // e HTML por baixo
      break;
    case Lang::OCaml:
    case Lang::Haskell:
      // O apostrofo faz parte do nome (foo', 'a): fechar sozinho atrapalha.
      a.single_quote = false;
      break;
    case Lang::Vhdl:
      // 'clk'event' e atributo, nao caractere.
      a.single_quote = false;
      break;
    case Lang::Json:
      a.single_quote = false;   // JSON nao tem string de aspas simples
      break;
    case Lang::Markdown:
    case Lang::None:
      // Prosa: aspas e apostrofo sao pontuacao. Fechar sozinho atrapalha mais
      // do que ajuda ("nao e" nao pode virar "nao e''").
      a.double_quote = false;
      a.single_quote = false;
      a.backtick = true;        // mas `codigo` entre crases vale a pena
      break;
    default:                    // C, C++, Python, SQL, CSS, Make
      break;
  }
  return a;
}

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
  if (e == "sql" || e == "psql" || e == "pgsql" || e == "mysql" || e == "ddl")
    return Lang::Sql;
  if (e == "sh" || e == "bash" || e == "zsh" || e == "bashrc" ||
      e == ".bashrc" || e == "bash_profile" || e == "profile" ||
      e == "zshrc" || e == "ksh")
    return Lang::Shell;
  if (e == "rb" || e == "rake" || e == "gemspec" || e == "ru" ||
      e == "Gemfile" || e == "Rakefile" || e == "podfile")
    return Lang::Ruby;
  if (e == "erb" || e == "rhtml") return Lang::Erb;
  if (e == "cs" || e == "csx") return Lang::CSharp;
  if (e == "hs" || e == "lhs") return Lang::Haskell;
  if (e == "ml" || e == "mli" || e == "mll" || e == "mly") return Lang::OCaml;
  if (e == "v" || e == "sv" || e == "svh" || e == "vh") return Lang::Verilog;
  if (e == "vhd" || e == "vhdl") return Lang::Vhdl;
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
    case Lang::Erb:
      return scan_erb(line, state_in, out);
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
// ERB (.erb, .rhtml): HTML com Ruby dentro de <% ... %>
// ---------------------------------------------------------------------------
//
// Duas passadas: primeiro o HTML da linha inteira, depois os trechos de Ruby
// repintados por cima. As duas escrevem no mesmo vetor por indice, entao a
// segunda simplesmente vence - sai bem mais simples que interromper o scanner
// de HTML no meio.
//
// O estado normal e o do proprio HTML (16 bits). So quando um <% fica aberto no
// fim da linha e que trocamos para kErbTag, guardando o estado principal do
// HTML nos bits de cima.
int Highlighter::scan_erb(const std::string& line, int state_in,
                          std::vector<int>* out) const {
  const bool dentro = (main_state(state_in) == kErbTag);
  const int html_in = dentro ? sub_state(state_in) : state_in;
  const int html_out = scan_html(line, html_in, out);

  size_t i = 0;
  bool aberto = false;

  // Continuacao de um <% que ficou aberto na linha anterior.
  if (dentro) {
    size_t end = line.find("%>");
    if (end == std::string::npos) {
      scan_code(line, 0, line.size(), kNormal, out, Lang::Ruby);
      return pack(kErbTag, main_state(html_out));
    }
    scan_code(line, 0, end, kNormal, out, Lang::Ruby);
    paint(out, end, std::min(end + 2, line.size()), ui::kSynPreproc);
    i = end + 2;
  }

  while (i < line.size()) {
    size_t open = line.find("<%", i);
    if (open == std::string::npos) break;
    // Marcadores logo depois do <%: "=" imprime, "-" apara espacos, "#" e
    // comentario.
    size_t rb = open + 2;
    const bool comentario = (rb < line.size() && line[rb] == '#');
    while (rb < line.size() && std::strchr("=-#", line[rb])) rb++;

    const size_t end = line.find("%>", rb);
    const size_t fim = (end == std::string::npos) ? line.size() : end;

    paint(out, open, rb, ui::kSynPreproc);
    if (comentario) paint(out, rb, fim, ui::kSynComment);
    else scan_code(line, rb, fim, kNormal, out, Lang::Ruby);

    if (end == std::string::npos) { aberto = true; break; }
    paint(out, end, std::min(end + 2, line.size()), ui::kSynPreproc);
    i = end + 2;
  }

  if (aberto) return pack(kErbTag, main_state(html_out));
  return html_out;
}

// ---------------------------------------------------------------------------
// Linguagens "de codigo": C, C++, Python, JavaScript/JSX, Shell, Make,
// Ruby, C#, Haskell, OCaml, Verilog e VHDL
// ---------------------------------------------------------------------------

int Highlighter::scan_code(const std::string& line, size_t from, size_t to,
                           int state_in, std::vector<int>* out,
                           Lang lang) const {
  const auto& kw = keywords_for(lang);
  const auto& types = types_for(lang);
  const bool csharp = (lang == Lang::CSharp);
  const bool verilog = (lang == Lang::Verilog);
  const bool c_like = lang == Lang::C || lang == Lang::Cpp ||
                      lang == Lang::JavaScript || csharp || verilog;
  const bool jsx = (lang == Lang::JavaScript);
  const bool sql = (lang == Lang::Sql);
  const bool ruby = (lang == Lang::Ruby || lang == Lang::Erb);
  const bool haskell = (lang == Lang::Haskell);
  const bool ocaml = (lang == Lang::OCaml);
  const bool vhdl = (lang == Lang::Vhdl);
  const bool shell = (lang == Lang::Shell);
  const bool hash_comment = lang == Lang::Python || shell ||
                            lang == Lang::Make || ruby;
  const bool dash_comment = sql || haskell || vhdl;   // -- ate o fim da linha
  const bool block_comment = c_like || sql;           // /* ... */
  const bool ci_words = sql || vhdl;   // SELECT e select sao a mesma coisa
  // Linguagens em que o apostrofo tem outros usos (veja is_char_literal).
  const bool narrow_char = haskell || ocaml || vhdl || verilog;

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
  } else if (state == kRubyComment) {
    // =end tem que estar no comeco da linha para encerrar o bloco.
    if (line.compare(0, 4, "=end") == 0) {
      paint(out, i, to, ui::kSynComment);
      return kNormal;
    }
    paint(out, i, to, ui::kSynComment);
    return kRubyComment;
  } else if (state == kHsComment || state == kMlComment) {
    const char* term = (state == kHsComment) ? "-}" : "*)";
    size_t end = find_in(line, term, i, to);
    if (end == std::string::npos) {
      paint(out, i, to, ui::kSynComment);
      return state;
    }
    paint(out, i, end + 2, ui::kSynComment);
    i = end + 2;
    state = kNormal;
  } else if (state == kCsVerbatim) {
    // Numa string @"..." as aspas se escapam dobrando ("").
    size_t j = i;
    while (j < to) {
      if (line[j] == '"') {
        if (j + 1 < to && line[j + 1] == '"') { j += 2; continue; }
        j++;
        break;
      }
      j++;
    }
    if (j >= to && (to == 0 || line[to - 1] != '"')) {
      paint(out, i, to, ui::kSynString);
      return kCsVerbatim;
    }
    paint(out, i, std::min(j, to), ui::kSynString);
    i = j;
    state = kNormal;
  } else {
    state = kNormal;
  }

  // --- comentario de bloco do Ruby: =begin / =end coluna 0 ---
  if (ruby && from == 0 && i == 0 && line.compare(0, 6, "=begin") == 0) {
    paint(out, 0, to, ui::kSynComment);
    return kRubyComment;
  }

  // --- diretiva de pre-processador ocupa a linha toda ---
  if ((lang == Lang::C || lang == Lang::Cpp || csharp) && from == 0 && i == 0) {
    size_t f = line.find_first_not_of(" \t");
    if (f != std::string::npos && f < to && line[f] == '#') {
      size_t cmt = find_in(line, "//", f, to);
      paint(out, f, cmt == std::string::npos ? to : cmt, ui::kSynPreproc);
      if (cmt != std::string::npos) paint(out, cmt, to, ui::kSynComment);
      return kNormal;
    }
  }

  size_t last_i = to + 1;   // onde estavamos na iteracao anterior
  while (i < to) {
    // Se alguma regra nao consumiu nenhum byte, anda um para nao travar o
    // editor. Serve de rede para qualquer regra nova que erre a conta.
    if (i == last_i) { i++; continue; }
    last_i = i;

    const char c = line[i];

    // Comentarios.
    if (dash_comment && c == '-' && i + 1 < to && line[i + 1] == '-') {
      paint(out, i, to, ui::kSynComment);
      return kNormal;
    }
    // {- ... -} do Haskell e (* ... *) do OCaml: atravessam linhas.
    if ((haskell && c == '{' && i + 1 < to && line[i + 1] == '-') ||
        (ocaml && c == '(' && i + 1 < to && line[i + 1] == '*')) {
      const char* term = haskell ? "-}" : "*)";
      size_t end = find_in(line, term, i + 2, to);
      if (end == std::string::npos) {
        paint(out, i, to, ui::kSynComment);
        return haskell ? kHsComment : kMlComment;
      }
      paint(out, i, end + 2, ui::kSynComment);
      i = end + 2;
      continue;
    }
    if (block_comment && c == '/' && i + 1 < to) {
      if (c_like && line[i + 1] == '/') {
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

    // No SQL as aspas se invertem: 'texto' e uma string, "coluna" (ou
    // `coluna`, no MySQL) e o nome de uma tabela ou coluna. E a aspa simples
    // se escapa dobrando: 'nao e' vira 'nao e''.
    if (sql && (c == '"' || c == '`')) {
      size_t j = i + 1;
      while (j < to && line[j] != c) j++;
      j = std::min(j + 1, to);
      paint(out, i, j, ui::kSynType);
      i = j;
      continue;
    }
    if (sql && c == '\'') {
      size_t j = i + 1;
      while (j < to) {
        if (line[j] == '\'') {
          if (j + 1 < to && line[j + 1] == '\'') { j += 2; continue; }
          j++;
          break;
        }
        j++;
      }
      paint(out, i, std::min(j, to), ui::kSynString);
      i = j;
      continue;
    }
    // Parametros: :nome, @variavel, $1.
    if (sql && (c == ':' || c == '@' || c == '$') && i + 1 < to &&
        (ident_char(line[i + 1]))) {
      size_t j = i + 1;
      while (j < to && ident_char(line[j])) j++;
      paint(out, i, j, ui::kSynPreproc);
      i = j;
      continue;
    }

    // C#: @"literal" (sem escapes, pode atravessar linhas) e $"interpolada".
    if (csharp && (c == '@' || c == '$') && i + 1 < to && line[i + 1] == '"') {
      if (c == '@') {
        size_t j = i + 2;
        while (j < to) {
          if (line[j] == '"') {
            if (j + 1 < to && line[j + 1] == '"') { j += 2; continue; }
            j++;
            break;
          }
          j++;
        }
        if (j >= to && (to == 0 || line[to - 1] != '"')) {
          paint(out, i, to, ui::kSynString);
          return kCsVerbatim;
        }
        paint(out, i, std::min(j, to), ui::kSynString);
        i = j;
        continue;
      }
      paint(out, i, i + 1, ui::kSynPreproc);
      i++;
      continue;   // a aspa seguinte cai na regra normal de string
    }

    // Verilog: diretivas `define, `include, `timescale.
    if (verilog && c == '`' && i + 1 < to && ident_start(line[i + 1])) {
      size_t j = i + 1;
      while (j < to && ident_char(line[j])) j++;
      paint(out, i, j, ui::kSynPreproc);
      i = j;
      continue;
    }
    // Verilog: base numerica, como 8'hFF, 4'b1010 ou 'd15.
    if (verilog && c == '\'' && i + 1 < to &&
        std::strchr("bodhBODHsS", line[i + 1])) {
      size_t j = i + 1;
      while (j < to && (std::isalnum(static_cast<unsigned char>(line[j])) ||
                        line[j] == '_'))
        j++;
      paint(out, i, j, ui::kSynNumber);
      i = j;
      continue;
    }

    // Ruby: variaveis de instancia (@x), de classe (@@x) e globais ($x).
    if (ruby && (c == '@' || c == '$') && i + 1 < to) {
      size_t j = i + 1;
      if (c == '@' && j < to && line[j] == '@') j++;
      size_t name = j;
      while (j < to && ident_char(line[j])) j++;
      if (j > name) {
        paint(out, i, j, ui::kSynPreproc);
        i = j;
        continue;
      }
    }
    // Ruby: simbolo :nome. O "::" e escopo e "chave: valor" e hash - por isso
    // exigimos que o caractere anterior nao faca parte de um nome nem seja ':'.
    if (ruby && c == ':' && i + 1 < to && ident_start(line[i + 1]) &&
        (i == from || (!ident_char(line[i - 1]) && line[i - 1] != ':'))) {
      size_t j = i + 1;
      while (j < to && ident_char(line[j])) j++;
      if (j < to && (line[j] == '?' || line[j] == '!')) j++;
      paint(out, i, j, ui::kSynType);
      i = j;
      continue;
    }

    // Shell: $VAR, ${...} e $1.
    if (shell && c == '$' && i + 1 < to) {
      size_t j = i + 1;
      if (line[j] == '{') {
        while (j < to && line[j] != '}') j++;
        j = std::min(j + 1, to);
      } else {
        while (j < to && (ident_char(line[j]) || line[j] == '?' ||
                          line[j] == '#' || line[j] == '@'))
          j++;
      }
      if (j > i + 1) {
        paint(out, i, j, ui::kSynPreproc);
        i = j;
        continue;
      }
    }

    // Strings e caracteres.
    if (c == '"' || (c == '\'' && (!narrow_char || is_char_literal(line, i, to)))) {
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
      // Sufixos que fazem parte do nome: empty?/save! no Ruby, foo' em
      // Haskell e OCaml.
      if (ruby && j < to && (line[j] == '?' || line[j] == '!')) j++;
      if (haskell || ocaml)
        while (j < to && line[j] == '\'') j++;

      std::string word = line.substr(i, j - i);
      if (ci_words) word = lower(word);
      if (kw.count(word)) {
        paint(out, i, j, ui::kSynKeyword);
      } else if (types.count(word)) {
        paint(out, i, j, ui::kSynType);
      } else if ((ruby || haskell || ocaml) &&
                 std::isupper(static_cast<unsigned char>(c))) {
        // Nessas tres a maiuscula tem significado: constante e classe no Ruby,
        // tipo e construtor em Haskell, modulo e construtor em OCaml. Em C# e
        // nas de hardware nao ha essa convencao, entao a regra nao vale la.
        paint(out, i, j, ui::kSynType);
      }
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
      if (j == i) { i++; continue; }   // nunca fica parado
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
