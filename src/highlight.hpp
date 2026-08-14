// highlight.hpp - realce de sintaxe simples, feito linha a linha.
//
// Para cada linha devolvemos um vetor com uma cor por byte. O "estado" que
// entra e sai de cada linha permite que comentarios de bloco (/* ... */) e
// strings de multiplas linhas atravessem varias linhas.
#pragma once

#include <string>
#include <vector>

enum class Lang { None, C, Cpp, Python, JavaScript, Shell, Make, Markdown, Json };

class Highlighter {
 public:
  static Lang detect(const std::string& path);

  explicit Highlighter(Lang lang = Lang::None) : lang_(lang) {}
  void set_lang(Lang lang) { lang_ = lang; }
  Lang lang() const { return lang_; }
  bool enabled() const { return lang_ != Lang::None; }

  // Estados que atravessam linhas.
  enum State { kNormal = 0, kBlockComment = 1, kPyString1 = 2, kPyString2 = 3 };

  // Preenche 'out' (uma cor por byte, 0 = cor normal) e devolve o estado
  // que vale para a proxima linha.
  int highlight(const std::string& line, int state_in, std::vector<int>* out) const;

 private:
  Lang lang_ = Lang::None;
};
