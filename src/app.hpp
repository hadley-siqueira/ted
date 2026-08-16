// app.hpp - junta tudo: layout dos paineis, loop de eventos e atalhos.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "editorview.hpp"
#include "filetree.hpp"
#include "picker.hpp"
#include "terminal.hpp"
#include "ui.hpp"

// Um painel do editor: sua propria lista de abas, sua aba ativa e os
// retangulos que ele ocupa na tela (a barra de abas em cima, o codigo embaixo).
//
// Hoje existe exatamente um painel. A estrutura ja e a que a divisao de
// paineis vai usar: uma *grade* de colunas, cada coluna com um ou mais paineis
// empilhados - "esquerda | direita-cima / direita-baixo". Veja a secao 7 do NOTAS.md.
struct Pane {
  std::vector<std::unique_ptr<EditorView>> tabs;
  int active_tab = 0;
  Rect tabbar, area;    // preenchidos por App::compute_layout()
  int weight = 100;     // proporcao de altura dentro da coluna
};

// Uma coluna da grade: um ou mais paineis, de cima para baixo.
struct PaneColumn {
  std::vector<Pane> rows;
  int weight = 100;     // proporcao de largura na regiao do editor
  int sep_x = -1;       // coluna do separador "|" a direita (-1 = e a ultima)
};

class App {
 public:
  App(const std::string& root, const std::vector<std::string>& files);

  int run();

 private:
  enum class Focus { Sidebar, Editor, Terminal };

  // Endereco de um painel na grade. col < 0 significa "nenhum".
  struct PaneRef {
    int col = -1, row = -1;
    bool tabbar = false;   // o ponto caiu na barra de abas, nao no codigo
  };

  // --- layout -------------------------------------------------------------
  void compute_layout();
  void layout_panes();
  void draw();
  void draw_tabbar(const Pane& p, bool focused);
  void draw_statusbar();
  void draw_pane_title(const Rect& r, const std::string& text, bool active,
                       const std::string& right = "");
  void draw_help();
  void place_cursor();

  // --- eventos ------------------------------------------------------------
  void handle_key(const ui::KeyEvent& ev);
  bool handle_global_key(const ui::KeyEvent& ev);
  void handle_editor_key(const ui::KeyEvent& ev);
  void handle_mouse(const ui::KeyEvent& ev);
  void handle_paste();

  // --- paineis / abas / arquivos ------------------------------------------
  Pane& pane();                       // o painel com o foco
  EditorView* view_of(Pane& p);       // a aba ativa de um painel (ou nullptr)
  EditorView* active();               // a aba ativa do painel com o foco
  PaneRef pane_at(int x, int y) const;
  int pane_count() const;
  // Divide o painel atual, mostrando o mesmo documento nas duas metades:
  // vertical = uma coluna nova ao lado, horizontal = um painel novo embaixo.
  // As duas recusam (com aviso) se nao houver espaco na tela.
  void split_vertical();
  void split_horizontal();
  void remove_current_pane();
  void close_pane();
  void next_pane(int delta);          // ordem de leitura, dando a volta
  // Os paineis na ordem de leitura (coluna a coluna, de cima para baixo). A
  // posicao nesta lista e o "numero do painel" usado pelo Ctrl+T.
  std::vector<std::pair<int, int>> pane_order() const;
  bool focus_pane_index(int idx);
  // O documento esta aberto em algum painel *diferente* de (col, row)?
  bool doc_open_elsewhere(const Document* d, int col, int row) const;
  int views_of_doc(const Document* d) const;      // quantas views o mostram
  void refresh_language_of(const Document* d);    // redetecta o realce em todas

  void new_tab();
  void open_file(const std::string& path);
  // Um arquivo, um Document: procura o caminho em todos os paineis.
  std::shared_ptr<Document> find_open_doc(const std::string& path) const;
  void add_view(Pane& p, std::shared_ptr<Document> doc);
  void close_tab();
  void drop_active_tab();
  void next_tab(int delta);
  void save(bool ask_name);
  void do_save(const std::string& path);

  // --- fuzzy finder (Ctrl+P) e busca nos abertos (Ctrl+T) ---
  void open_file_picker();
  void open_text_picker();
  std::vector<PickerItem> filter_files(const std::string& query);
  std::vector<PickerItem> search_open_files(const std::string& query);

  // --- prompts ------------------------------------------------------------
  using PromptCb = std::function<void(bool ok, const std::string& value)>;
  void ask(const std::string& label, const std::string& initial, PromptCb cb);
  void ask_yes_no(const std::string& label,
                  std::function<void(char)> cb);  // 's', 'n' ou 'c'
  bool prompt_key(const ui::KeyEvent& ev);        // devolve true se consumiu

  // --- terminais -----------------------------------------------------------
  Terminal* term();                   // o terminal visivel (nullptr se nenhum)
  void draw_terminal_tabs(const std::string& right);
  int terminal_tab_at(int x, int y) const;   // -1 se nao caiu em nenhuma aba
  void new_terminal();
  void close_terminal();
  void next_terminal(int delta);
  void reap_terminals();   // tira da barra os shells que ja encerraram

  void message(const std::string& msg, bool error = false);
  void set_focus(Focus f);
  void ensure_terminal();
  void quit_request();

  // --- estado -------------------------------------------------------------
  FileTree tree_;
  std::vector<PaneColumn> cols_;      // a grade de paineis (hoje: 1x1)
  int cur_col_ = 0, cur_row_ = 0;     // qual painel tem o foco
  // Varios terminais, um por aba do painel de baixo: com "npm run dev" e
  // "node server.js" rodando ainda sobra um shell para npm/git. unique_ptr
  // porque Terminal segura um fd e um pid - nao e copiavel.
  std::vector<std::unique_ptr<Terminal>> terms_;
  int cur_term_ = 0;
  Picker picker_;
  std::vector<std::string> file_cache_;   // caminhos relativos, para o Ctrl+P
  bool file_cache_truncated_ = false;
  Focus focus_ = Focus::Editor;
  Focus focus_before_prompt_ = Focus::Editor;

  std::string clipboard_;
  std::string message_;
  bool message_error_ = false;
  int message_ttl_ = 0;      // em ciclos de redesenho

  // Ctrl+K: a proxima tecla entra como caractere, sem interpretacao (o
  // "literal next" do vim). Serve para digitar um TAB de verdade num arquivo
  // configurado para espacos.
  bool literal_next_ = false;

  bool quit_ = false;
  bool show_help_ = false;
  bool needs_redraw_ = true;

  // Prompt (minibuffer na barra de status).
  bool prompt_active_ = false;
  bool prompt_yes_no_ = false;
  std::string prompt_label_;
  std::string prompt_input_;
  PromptCb prompt_cb_;
  std::function<void(char)> prompt_yn_cb_;

  std::string last_search_;

  // Estado do mouse (duplo clique e arrasto para selecionar).
  std::chrono::steady_clock::time_point last_click_{};
  int last_click_x_ = -1, last_click_y_ = -1;
  bool dragging_editor_ = false;

  // Retangulos calculados a cada redesenho. A regiao do editor e repartida
  // entre os paineis por layout_panes(); cada painel guarda os seus.
  Rect sidebar_title_, sidebar_, editor_region_, term_title_, term_rect_, status_;
  int screen_w_ = 0, screen_h_ = 0;
};
