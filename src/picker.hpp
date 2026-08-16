// picker.hpp - a caixa flutuante de busca com lista de resultados.
//
// E generica de proposito: quem abre passa uma funcao que, dada a consulta
// digitada, devolve a lista de itens ja ordenada. O picker so cuida da caixa,
// do texto digitado, da navegacao e do destaque - assim a mesma peca serve
// para "abrir arquivo" (Ctrl+P) e "procurar texto" (Ctrl+T).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui.hpp"

struct PickerItem {
  std::string label;             // o que aparece na lista
  std::vector<size_t> match;     // bytes de 'label' a destacar
  int score = 0;

  // Carga util: quem abriu o picker sabe o que faz com isso.
  std::string path;              // arquivo a abrir
  int pane = -1;                 // painel onde esta o resultado (ordem de leitura)
  int tab = -1;                  // aba, dentro desse painel
  int line = 0;                  // linha (0-based)
  size_t col = 0, len = 0;       // trecho a selecionar
};

class Picker {
 public:
  using Filter = std::function<std::vector<PickerItem>(const std::string&)>;
  using Accept = std::function<void(const PickerItem&)>;

  void open(const std::string& title, const std::string& hint, Filter filter,
            Accept accept);
  void close();
  bool active() const { return active_; }

  // Trata a tecla. Enquanto a caixa esta aberta ela consome tudo.
  bool handle_key(const ui::KeyEvent& ev);

  void draw(int screen_w, int screen_h);
  bool cursor_screen(int* x, int* y) const;

 private:
  void refilter();
  void move(int delta);

  bool active_ = false;
  std::string title_, hint_, query_;
  Filter filter_;
  Accept accept_;
  std::vector<PickerItem> items_;
  int selected_ = 0;
  int scroll_ = 0;
  Rect box_;          // area da caixa no ultimo desenho
  int list_y_ = 0;    // primeira linha da lista na tela
  int list_h_ = 0;
  int cursor_x_ = -1, cursor_y_ = -1;
};
