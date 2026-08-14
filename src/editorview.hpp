// editorview.hpp - o painel onde se digita o codigo.
//
// Guarda o estado *visual* de um documento aberto: posicao do cursor, selecao,
// rolagem e realce de sintaxe. Um Document pode, em tese, ser mostrado por
// mais de uma EditorView (nao usamos isso ainda, mas a separacao ajuda).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "document.hpp"
#include "highlight.hpp"
#include "ui.hpp"

class EditorView {
 public:
  explicit EditorView(std::shared_ptr<Document> doc);

  Document& doc() { return *doc_; }
  const Document& doc() const { return *doc_; }
  std::shared_ptr<Document> doc_ptr() const { return doc_; }

  // Linguagem em uso no realce. Redetectada quando o arquivo ganha um nome
  // novo (arquivo novo salvo como "algo.c" passa a ter realce de C na hora).
  Lang language() const { return hl_.lang(); }
  void refresh_language();

  // ---- desenho -----------------------------------------------------------
  void draw(const Rect& area, bool focused);
  // Onde o cursor de verdade deve aparecer (calculado no ultimo draw).
  bool cursor_screen(int* x, int* y) const;

  // ---- teclado -----------------------------------------------------------
  // Trata teclas de edicao/navegacao. Devolve false se nao souber tratar.
  bool handle_key(const ui::KeyEvent& ev);
  void insert_text(const std::string& text);   // usado tambem pelo "colar"
  void delete_line();                          // usado pelo "recortar linha"
  void duplicate_line();                       // Ctrl+D

  // ---- selecao e area de transferencia -----------------------------------
  bool has_selection() const { return sel_active_ && sel_anchor_ != cursor_; }
  std::string selected_text() const;
  std::string current_line_text() const;
  void select_all();
  void clear_selection() { sel_active_ = false; }
  bool delete_selection();

  // ---- navegacao ---------------------------------------------------------
  Pos cursor() const { return cursor_; }
  void set_cursor(Pos p, bool extend_selection = false);
  void goto_line(int line_1based);
  // Seleciona um trecho e traz para a tela (usado pela busca do Ctrl+T).
  void select_range(Pos a, Pos b);
  void ensure_visible();

  // ---- busca -------------------------------------------------------------
  void set_search(const std::string& term) { search_ = term; }
  const std::string& search() const { return search_; }
  bool find_next(bool forward, bool from_cursor);

  // ---- mouse -------------------------------------------------------------
  void click(int screen_x, int screen_y, bool extend);
  void select_word_at_cursor();
  void scroll_by(int lines);

  // Informacao para a barra de status.
  int cursor_line() const { return cursor_.line + 1; }
  int cursor_col() const;
  bool overwrite() const { return overwrite_; }
  void toggle_overwrite() { overwrite_ = !overwrite_; }

 private:
  int gutter_width() const;
  void move_horizontal(int dir, bool extend, bool by_word);
  void move_vertical(int delta, bool extend);
  void move_home(bool extend);
  void move_end(bool extend);
  void insert_newline();
  void backspace();
  void delete_forward(bool by_word);
  void indent_selection(bool remove);
  void insert_tab();
  void move_lines(int delta);
  bool erase_selection_raw();   // apaga a selecao sem empilhar undo
  Pos sel_begin() const;
  Pos sel_end() const;
  void begin_move(bool extend);
  void update_highlight_states();
  std::string indent_of(const std::string& line) const;

  std::shared_ptr<Document> doc_;
  Highlighter hl_;

  Pos cursor_;
  Pos sel_anchor_;
  bool sel_active_ = false;
  int desired_col_ = 0;       // coluna "desejada" ao andar para cima/baixo
  bool overwrite_ = false;

  int scroll_row_ = 0;
  int scroll_col_ = 0;
  Rect area_;
  int cursor_x_ = -1, cursor_y_ = -1;

  std::string search_;

  // Cache do estado do realce no *inicio* de cada linha.
  std::vector<int> hl_states_;
  size_t hl_version_ = 0;
};
