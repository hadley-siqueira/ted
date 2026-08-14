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

class App {
 public:
  App(const std::string& root, const std::vector<std::string>& files);

  int run();

 private:
  enum class Focus { Sidebar, Editor, Terminal };

  // --- layout -------------------------------------------------------------
  void compute_layout();
  void draw();
  void draw_tabbar();
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

  // --- abas / arquivos ----------------------------------------------------
  EditorView* active();
  void new_tab();
  void open_file(const std::string& path);
  void close_tab();
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

  void message(const std::string& msg, bool error = false);
  void set_focus(Focus f);
  void ensure_terminal();
  void quit_request();

  // --- estado -------------------------------------------------------------
  FileTree tree_;
  std::vector<std::unique_ptr<EditorView>> tabs_;
  int active_tab_ = 0;
  Terminal term_;
  Picker picker_;
  std::vector<std::string> file_cache_;   // caminhos relativos, para o Ctrl+P
  bool file_cache_truncated_ = false;
  Focus focus_ = Focus::Editor;
  Focus focus_before_prompt_ = Focus::Editor;

  std::string clipboard_;
  std::string message_;
  bool message_error_ = false;
  int message_ttl_ = 0;      // em ciclos de redesenho

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

  // Retangulos calculados a cada redesenho.
  Rect sidebar_title_, sidebar_, tabbar_, editor_, term_title_, term_rect_, status_;
  int screen_w_ = 0, screen_h_ = 0;
};
