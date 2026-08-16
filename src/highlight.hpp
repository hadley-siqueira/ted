// highlight.hpp - realce de sintaxe simples, feito linha a linha.
//
// Para cada linha devolvemos um vetor com uma cor por byte. O "estado" que
// entra e sai de cada linha permite que comentarios de bloco (/* ... */) e
// strings de multiplas linhas atravessem varias linhas.
#pragma once

#include <string>
#include <vector>

enum class Lang {
  None, C, Cpp, Python, JavaScript, Shell, Make, Markdown, Json, Html, Css, Sql
};

// Como se comenta em cada linguagem. 'line' vazio significa que a linguagem
// nao tem comentario de linha (CSS e HTML, por exemplo) - ai usa-se o bloco.
struct CommentSyntax {
  std::string line;         // "//", "#", "--"
  std::string block_open;   // "/*", "<!--"
  std::string block_close;  // "*/", "-->"
  bool empty() const { return line.empty() && block_open.empty(); }
};
CommentSyntax comment_syntax(Lang lang);

// O que cada linguagem fecha sozinha quando 'auto_close' esta ligado. Os pares
// ( [ { valem em toda linguagem; as aspas nao: em Markdown e texto puro o
// apostrofo e pontuacao de prosa ("nao e" viraria "nao e''"), nao delimitador.
struct AutoCloseSyntax {
  bool double_quote = true;   // "
  bool single_quote = true;   // '
  bool backtick = false;      // ` : template literal do JS, cerca do Markdown
  bool tags = false;          // <div> vira <div></div>
};
AutoCloseSyntax auto_close_syntax(Lang lang);

class Highlighter {
 public:
  static Lang detect(const std::string& path);

  explicit Highlighter(Lang lang = Lang::None) : lang_(lang) {}
  void set_lang(Lang lang) { lang_ = lang; }
  Lang lang() const { return lang_; }
  bool enabled() const { return lang_ != Lang::None; }

  // Estados que atravessam linhas. O estado que entra e sai de highlight() e
  // um inteiro com dois campos: os 8 bits de baixo sao um destes valores e os
  // 8 bits de cima guardam o estado da linguagem *embutida* (o CSS ou o
  // JavaScript que vive dentro de um arquivo HTML).
  enum State {
    kNormal = 0,
    kBlockComment,      // /* ... */ de C, C++, JS e CSS fora de um bloco
    kPyString1,         // ''' ... ''' do Python
    kPyString2,         // """ ... """ do Python
    kJsTemplate,        // `...` do JavaScript
    kHtmlComment,       // <!-- ... -->
    kHtmlTag,           // dentro de <tag ...> ainda sem o '>'
    kHtmlTagScript,     // idem, e a tag aberta e <script>
    kHtmlTagStyle,      // idem, e a tag aberta e <style>
    kHtmlScript,        // conteudo de <script>: destacado como JavaScript
    kHtmlStyle,         // conteudo de <style>: destacado como CSS
    kCssBlock,          // dentro das { } de uma regra CSS
    kCssBlockComment,   // /* ... */ dentro das { } de uma regra CSS
  };

  // Preenche 'out' (uma cor por byte, 0 = cor normal) e devolve o estado
  // que vale para a proxima linha.
  int highlight(const std::string& line, int state_in, std::vector<int>* out) const;

 private:
  // Cada scanner pinta o trecho [from, to) da linha e devolve o estado final.
  // Sao separados porque o HTML chama os outros dois para o conteudo de
  // <style> e <script>.
  int scan_code(const std::string& line, size_t from, size_t to, int state_in,
                std::vector<int>* out, Lang lang) const;
  int scan_css(const std::string& line, size_t from, size_t to, int state_in,
               std::vector<int>* out) const;
  int scan_html(const std::string& line, int state_in, std::vector<int>* out) const;
  int scan_markdown(const std::string& line, std::vector<int>* out) const;

  // Pinta atributos (nome=valor) ate encontrar '>' ou '/>'. Em JSX tambem
  // para em '{', devolvendo o controle para o scanner de JavaScript.
  size_t scan_attributes(const std::string& line, size_t i, size_t to,
                         std::vector<int>* out, bool* closed, bool html) const;

  Lang lang_ = Lang::None;
};
