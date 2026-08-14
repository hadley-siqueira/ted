#include "app.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "config.hpp"
#include "utf8.hpp"

namespace {

// ~/projeto -> /home/aluno/projeto ; caminho relativo -> a partir da raiz.
std::string expand_path(const std::string& p, const std::string& root) {
  if (p.empty()) return p;
  if (p[0] == '~') {
    const char* home = getenv("HOME");
    if (home) return std::string(home) + p.substr(1);
  }
  if (p[0] == '/') return p;
  return root + "/" + p;
}

bool looks_binary(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  char buf[4096];
  in.read(buf, sizeof(buf));
  std::streamsize n = in.gcount();
  for (std::streamsize i = 0; i < n; i++)
    if (buf[i] == '\0') return true;
  return false;
}

std::string lang_name(Lang l) {
  switch (l) {
    case Lang::C: return "C";
    case Lang::Cpp: return "C++";
    case Lang::Python: return "Python";
    case Lang::JavaScript: return "JavaScript";
    case Lang::Shell: return "Shell";
    case Lang::Make: return "Make";
    case Lang::Markdown: return "Markdown";
    case Lang::Json: return "JSON";
    default: return "Texto";
  }
}

}  // namespace

App::App(const std::string& root, const std::vector<std::string>& files)
    : tree_(root) {
  for (const auto& f : files) open_file(expand_path(f, tree_.root()));
  if (tabs_.empty()) new_tab();
  message_ = "F1 mostra a ajuda com todos os atalhos.";
  message_ttl_ = 400;
}

EditorView* App::active() {
  if (tabs_.empty()) return nullptr;
  if (active_tab_ < 0) active_tab_ = 0;
  if (active_tab_ >= static_cast<int>(tabs_.size()))
    active_tab_ = static_cast<int>(tabs_.size()) - 1;
  return tabs_[active_tab_].get();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void App::compute_layout() {
  screen_w_ = COLS;
  screen_h_ = LINES;
  int body_h = std::max(1, screen_h_ - 1);

  status_ = Rect{0, screen_h_ - 1, screen_w_, 1};

  int sw = 0;
  if (g_config.show_sidebar) {
    sw = std::min(std::max(g_config.sidebar_width, 12), screen_w_ / 2);
    if (sw < 12) sw = std::min(12, screen_w_ / 2);
  }
  sidebar_title_ = Rect{0, 0, sw, 1};
  sidebar_ = Rect{0, 1, std::max(0, sw - 1), body_h - 1};

  int rx = sw;
  int rw = std::max(1, screen_w_ - sw);
  tabbar_ = Rect{rx, 0, rw, 1};

  int th = 0;
  if (g_config.show_terminal) {
    th = std::min(std::max(g_config.terminal_height, 3), std::max(3, body_h - 5));
    if (body_h - th - 1 < 3) th = std::max(0, body_h - 4);
  }
  if (th > 0) {
    term_title_ = Rect{rx, body_h - th - 1, rw, 1};
    term_rect_ = Rect{rx, body_h - th, rw, th};
  } else {
    term_title_ = Rect{};
    term_rect_ = Rect{};
  }
  int editor_h = body_h - 1 - (th > 0 ? th + 1 : 0);
  editor_ = Rect{rx, 1, rw, std::max(1, editor_h)};
}

void App::draw_pane_title(const Rect& r, const std::string& text, bool active_p,
                          const std::string& right) {
  if (r.w <= 0 || r.h <= 0) return;
  attrset(COLOR_PAIR(active_p ? ui::kPaneTitleActive : ui::kPaneTitle) | A_BOLD);
  ui::fill(r.y, r.x, r.w);
  ui::put(r.y, r.x + 1, r.w - 2, text);
  if (!right.empty()) {
    int w = utf8::width(right, 4);
    int x = r.x + r.w - w - 1;
    if (x > r.x + static_cast<int>(text.size()) + 2) {
      attrset(COLOR_PAIR(active_p ? ui::kPaneTitleActive : ui::kPaneTitle));
      ui::put(r.y, x, w, right);
    }
  }
  attrset(COLOR_PAIR(ui::kNormal));
}

void App::draw_tabbar() {
  attrset(COLOR_PAIR(ui::kTabBar));
  ui::fill(tabbar_.y, tabbar_.x, tabbar_.w);
  int x = tabbar_.x;
  for (size_t i = 0; i < tabs_.size(); i++) {
    Document& d = tabs_[i]->doc();
    std::string label = " " + d.display_name() + (d.modified() ? " •" : "") + " ";
    int w = utf8::width(label, 4);
    if (x + w > tabbar_.x + tabbar_.w) {
      attrset(COLOR_PAIR(ui::kTabBar));
      ui::put(tabbar_.y, tabbar_.x + tabbar_.w - 1, 1, ">");
      break;
    }
    bool act = (static_cast<int>(i) == active_tab_);
    attrset(COLOR_PAIR(act ? ui::kTabActive
                           : (d.modified() ? ui::kTabModified : ui::kTabBar)) |
            (act ? A_BOLD : 0));
    ui::fill(tabbar_.y, x, w);
    ui::put(tabbar_.y, x, w, label);
    x += w;
  }
  attrset(COLOR_PAIR(ui::kNormal));
}

void App::draw_statusbar() {
  attrset(COLOR_PAIR(ui::kStatus));
  ui::fill(status_.y, status_.x, status_.w);

  if (prompt_active_) {
    std::string text = prompt_label_ + " " + prompt_input_;
    ui::put(status_.y, status_.x + 1, status_.w - 2, text);
    attrset(COLOR_PAIR(ui::kNormal));
    return;
  }

  EditorView* v = active();
  std::string left;
  if (v) {
    Document& d = v->doc();
    if (!d.has_path()) {
      left = d.display_name();
    } else {
      // Mostra o caminho relativo a pasta do projeto, quando der.
      const std::string& root = tree_.root();
      const std::string& p = d.path();
      if (p.size() > root.size() + 1 && p.compare(0, root.size(), root) == 0 &&
          p[root.size()] == '/')
        left = p.substr(root.size() + 1);
      else
        left = p;
    }
    if (d.modified()) left += " *";
  }

  std::string right;
  if (v) {
    right = "Ln " + std::to_string(v->cursor_line()) + ", Col " +
            std::to_string(v->cursor_col());
    if (v->has_selection()) {
      right += "  [sel]";
    }
    right += "  " + lang_name(v->language());
    right += g_config.use_spaces
                 ? "  Espacos:" + std::to_string(g_config.tab_width)
                 : "  Tab:" + std::to_string(g_config.tab_width);
    if (v->overwrite()) right += "  SOBRESCREVER";
  }

  const int rw = utf8::width(right, 4);
  const bool has_msg = !message_.empty() && message_ttl_ > 0;
  const int lw = utf8::width(left, 4);

  // A mensagem tem prioridade e fica logo depois do nome do arquivo; se nao
  // couber, ela ocupa o lugar do nome (nunca desaparece sem aviso).
  int msg_x = status_.x + 1 + lw + 2;
  int mw = has_msg ? utf8::width(message_, 4) : 0;
  bool msg_inline = has_msg && (msg_x + mw <= status_.x + status_.w - rw - 2);

  if (!has_msg || msg_inline)
    ui::put(status_.y, status_.x + 1, status_.w - 2, left);

  if (status_.w - rw - 2 > lw + 2)
    ui::put(status_.y, status_.x + status_.w - rw - 1, rw, right);

  if (has_msg) {
    attrset(COLOR_PAIR(message_error_ ? ui::kSearchHit : ui::kStatusKey) | A_BOLD);
    int x = msg_inline ? msg_x : status_.x + 1;
    int avail = status_.x + status_.w - rw - 2 - x;
    ui::put(status_.y, x, std::max(1, avail), message_);
  }
  attrset(COLOR_PAIR(ui::kNormal));
}

void App::draw_help() {
  // Duas colunas para caber em uma tela de 80x24.
  static const char* kLines[] = {
      "ATALHOS DO ted",
      "",
      "ARQUIVO                          EDICAO",
      "Ctrl+S  salvar                   Ctrl+C/X/V  copiar, recortar e colar",
      "Alt+S   salvar como              Ctrl+A  selecionar tudo",
      "Ctrl+O  abrir    Ctrl+N  novo    Ctrl+Z / Ctrl+Y  desfazer / refazer",
      "Ctrl+W  fechar aba               Ctrl+D  duplicar a linha",
      "Ctrl+Q  sair  (ou F10)           Tab / Shift+Tab  indenta / desindenta",
      "Ctrl+PgUp/PgDn  troca de aba     Alt+Shift+Cima/Baixo  move a linha",
      "",
      "BUSCA E NAVEGACAO                PAINEIS",
      "Ctrl+F  buscar                   F2/F3/F4  arquivos, editor, terminal",
      "F3 / Shift+F3  proxima / ant.    F6  alterna entre os paineis",
      "Ctrl+R  substituir tudo          Ctrl+B/Ctrl+J  esconde arquivos/terminal",
      "Ctrl+G  ir para a linha          Alt+Setas  redimensiona os paineis",
      "Ctrl+Setas  anda por palavra     F5 recarrega a lista de arquivos",
      "Shift+Setas  seleciona           F9 liga/desliga o mouse",
      "Home/End, PgUp/PgDn              Shift+PgUp/PgDn  historico do terminal",
      "",
      "No terminal tudo vai para o shell (inclusive Ctrl+C). F4/F6 saem de la.",
      "Qualquer tecla fecha esta ajuda.",
  };
  const int n = static_cast<int>(sizeof(kLines) / sizeof(kLines[0]));
  int content_w = 0;
  for (int i = 0; i < n; i++)
    content_w = std::max(content_w, static_cast<int>(strlen(kLines[i])));

  int w = std::min(content_w + 4, screen_w_);
  int h = std::min(n + 2, screen_h_);
  int x = std::max(0, (screen_w_ - w) / 2);
  int y = std::max(0, (screen_h_ - h) / 2);

  attrset(COLOR_PAIR(ui::kDialog));
  for (int r = 0; r < h; r++) ui::fill(y + r, x, w);
  // Borda simples.
  std::string top;
  for (int i = 0; i < w - 2; i++) top += "─";
  ui::put(y, x, w, "┌" + top + "┐");
  ui::put(y + h - 1, x, w, "└" + top + "┘");
  for (int r = 1; r < h - 1; r++) {
    ui::put(y + r, x, 1, "│");
    ui::put(y + r, x + w - 1, 1, "│");
  }
  auto is_header = [](const char* s) {
    for (const char* p = s; *p; p++)
      if (*p >= 'a' && *p <= 'z') return false;
    return *s != '\0';
  };
  for (int i = 0; i < n && i < h - 2; i++) {
    attrset(COLOR_PAIR(ui::kDialog) |
            ((i == 0 || is_header(kLines[i])) ? A_BOLD : 0));
    ui::put(y + 1 + i, x + 2, w - 4, kLines[i]);
  }
  attrset(COLOR_PAIR(ui::kNormal));
}

void App::draw() {
  compute_layout();
  erase();

  // --- barra lateral ---
  if (g_config.show_sidebar && sidebar_.w > 0) {
    std::string root_name = tree_.root();
    size_t slash = root_name.find_last_of('/');
    if (slash != std::string::npos && root_name.size() > 1)
      root_name = root_name.substr(slash + 1);
    draw_pane_title(sidebar_title_, "ARQUIVOS: " + root_name,
                    focus_ == Focus::Sidebar);
    tree_.draw(sidebar_, focus_ == Focus::Sidebar);
    // separador vertical
    attrset(COLOR_PAIR(ui::kPaneTitle));
    for (int y = 1; y < screen_h_ - 1; y++) mvaddstr(y, sidebar_.w, "│");
    attrset(COLOR_PAIR(ui::kNormal));
  }

  // --- abas + editor ---
  draw_tabbar();
  if (EditorView* v = active()) v->draw(editor_, focus_ == Focus::Editor);

  // --- terminal ---
  if (g_config.show_terminal && term_rect_.h > 0) {
    std::string right;
    if (!term_.running()) right = "encerrado - Enter reinicia";
    else if (term_.view_offset() > 0)
      right = "historico -" + std::to_string(term_.view_offset());
    draw_pane_title(term_title_, "TERMINAL", focus_ == Focus::Terminal, right);
    term_.draw(term_rect_, focus_ == Focus::Terminal);
  }

  draw_statusbar();
  if (show_help_) draw_help();
  place_cursor();
  refresh();
}

void App::place_cursor() {
  int x = 0, y = 0;
  if (show_help_) {
    curs_set(0);
    return;
  }
  if (prompt_active_) {
    int lx = status_.x + 1 + utf8::width(prompt_label_, 4) + 1 +
             utf8::width(prompt_input_, 4);
    move(status_.y, std::min(lx, status_.x + status_.w - 1));
    curs_set(1);
    return;
  }
  switch (focus_) {
    case Focus::Editor:
      if (active() && active()->cursor_screen(&x, &y)) {
        move(y, x);
        curs_set(1);
      } else {
        curs_set(0);
      }
      break;
    case Focus::Terminal:
      if (term_.cursor_screen(&x, &y)) {
        move(y, x);
        curs_set(1);
      } else {
        curs_set(0);
      }
      break;
    case Focus::Sidebar:
    default:
      curs_set(0);
      break;
  }
}

// ---------------------------------------------------------------------------
// Mensagens, foco, prompts
// ---------------------------------------------------------------------------

void App::message(const std::string& msg, bool error) {
  message_ = msg;
  message_error_ = error;
  message_ttl_ = error ? 400 : 250;
  needs_redraw_ = true;
}

void App::set_focus(Focus f) {
  if (f == Focus::Terminal) {
    if (!g_config.show_terminal) g_config.show_terminal = true;
    ensure_terminal();
  }
  if (f == Focus::Sidebar && !g_config.show_sidebar) g_config.show_sidebar = true;
  focus_ = f;
  needs_redraw_ = true;
}

void App::ensure_terminal() {
  if (term_.running() || !g_config.show_terminal) return;
  compute_layout();
  std::string err;
  if (!term_.start(tree_.root(), std::max(2, term_rect_.w ? term_rect_.w : 80),
                   std::max(2, term_rect_.h ? term_rect_.h : 10), &err)) {
    message("Terminal: " + err, true);
    g_config.show_terminal = false;
  }
}

void App::ask(const std::string& label, const std::string& initial,
              PromptCb cb) {
  prompt_active_ = true;
  prompt_yes_no_ = false;
  prompt_label_ = label;
  prompt_input_ = initial;
  prompt_cb_ = std::move(cb);
  focus_before_prompt_ = focus_;
  needs_redraw_ = true;
}

void App::ask_yes_no(const std::string& label, std::function<void(char)> cb) {
  prompt_active_ = true;
  prompt_yes_no_ = true;
  prompt_label_ = label;
  prompt_input_.clear();
  prompt_yn_cb_ = std::move(cb);
  focus_before_prompt_ = focus_;
  needs_redraw_ = true;
}

bool App::prompt_key(const ui::KeyEvent& ev) {
  if (!prompt_active_) return false;
  needs_redraw_ = true;

  if (prompt_yes_no_) {
    char answer = 0;
    if (!ev.is_code) {
      wint_t c = ev.ch;
      if (c == 's' || c == 'S' || c == 'y' || c == 'Y') answer = 's';
      else if (c == 'n' || c == 'N') answer = 'n';
      else if (c == 27 || c == 'c' || c == 'C') answer = 'c';
    }
    if (!answer) return true;
    auto cb = prompt_yn_cb_;
    prompt_active_ = false;
    prompt_yn_cb_ = nullptr;
    focus_ = focus_before_prompt_;
    if (cb) cb(answer);
    return true;
  }

  if (ev.is_code) {
    switch (static_cast<int>(ev.ch)) {
      case KEY_BACKSPACE:
        if (!prompt_input_.empty())
          prompt_input_.erase(utf8::prev(prompt_input_, prompt_input_.size()));
        return true;
      case KEY_ENTER: {
        auto cb = prompt_cb_;
        std::string val = prompt_input_;
        prompt_active_ = false;
        prompt_cb_ = nullptr;
        focus_ = focus_before_prompt_;
        if (cb) cb(true, val);
        return true;
      }
      default:
        return true;   // demais teclas especiais: ignoradas no prompt
    }
  }

  switch (ev.ch) {
    case 27: {   // Esc cancela
      auto cb = prompt_cb_;
      prompt_active_ = false;
      prompt_cb_ = nullptr;
      focus_ = focus_before_prompt_;
      if (cb) cb(false, std::string());
      return true;
    }
    case '\r': case '\n': {
      auto cb = prompt_cb_;
      std::string val = prompt_input_;
      prompt_active_ = false;
      prompt_cb_ = nullptr;
      focus_ = focus_before_prompt_;
      if (cb) cb(true, val);
      return true;
    }
    case 8: case 127:
      if (!prompt_input_.empty())
        prompt_input_.erase(utf8::prev(prompt_input_, prompt_input_.size()));
      return true;
    case 21:   // Ctrl+U limpa
      prompt_input_.clear();
      return true;
    default:
      if (ev.ch >= 32) prompt_input_ += utf8::encode(static_cast<uint32_t>(ev.ch));
      return true;
  }
}

// ---------------------------------------------------------------------------
// Abas e arquivos
// ---------------------------------------------------------------------------

void App::new_tab() {
  tabs_.push_back(std::make_unique<EditorView>(std::make_shared<Document>()));
  active_tab_ = static_cast<int>(tabs_.size()) - 1;
  set_focus(Focus::Editor);
}

void App::open_file(const std::string& path) {
  // Ja esta aberto? So ativa a aba.
  for (size_t i = 0; i < tabs_.size(); i++) {
    if (tabs_[i]->doc().path() == path) {
      active_tab_ = static_cast<int>(i);
      set_focus(Focus::Editor);
      return;
    }
  }
  if (access(path.c_str(), F_OK) == 0 && looks_binary(path)) {
    message("Arquivo binario - nao da para editar aqui.", true);
    return;
  }

  auto doc = std::make_shared<Document>();
  std::string err;
  if (access(path.c_str(), F_OK) == 0) {
    if (!doc->load(path, &err)) {
      message(err, true);
      return;
    }
  } else {
    // Arquivo novo: fica em branco com o nome definido, mas so vai para o
    // disco quando o aluno apertar Ctrl+S.
    doc->set_path(path);
    message("Arquivo novo: " + path + " (Ctrl+S para criar)");
  }

  // Se a aba atual esta vazia e sem nome, reaproveita.
  EditorView* cur = active();
  if (cur && !cur->doc().has_path() && !cur->doc().modified() &&
      cur->doc().line_count() == 1 && cur->doc().line(0).empty()) {
    tabs_[active_tab_] = std::make_unique<EditorView>(doc);
  } else {
    tabs_.push_back(std::make_unique<EditorView>(doc));
    active_tab_ = static_cast<int>(tabs_.size()) - 1;
  }
  set_focus(Focus::Editor);
  needs_redraw_ = true;
}

void App::close_tab() {
  EditorView* v = active();
  if (!v) return;
  if (v->doc().modified()) {
    std::string name = v->doc().display_name();
    ask_yes_no("Salvar alteracoes em " + name + "? (s/n/c)", [this](char a) {
      if (a == 'c') return;
      if (a == 's') {
        EditorView* vv = active();
        if (vv && !vv->doc().has_path()) { save(true); return; }
        save(false);
        if (active() && active()->doc().modified()) return;
      }
      tabs_.erase(tabs_.begin() + active_tab_);
      if (tabs_.empty()) new_tab();
      if (active_tab_ >= static_cast<int>(tabs_.size()))
        active_tab_ = static_cast<int>(tabs_.size()) - 1;
      needs_redraw_ = true;
    });
    return;
  }
  tabs_.erase(tabs_.begin() + active_tab_);
  if (tabs_.empty()) new_tab();
  if (active_tab_ >= static_cast<int>(tabs_.size()))
    active_tab_ = static_cast<int>(tabs_.size()) - 1;
  needs_redraw_ = true;
}

void App::next_tab(int delta) {
  if (tabs_.empty()) return;
  int n = static_cast<int>(tabs_.size());
  active_tab_ = ((active_tab_ + delta) % n + n) % n;
  needs_redraw_ = true;
}

void App::do_save(const std::string& path) {
  EditorView* v = active();
  if (!v) return;
  std::string err;
  if (!v->doc().save_as(path, &err)) {
    message(err, true);
    return;
  }
  // O arquivo pode ter ganhado nome agora (ou outro nome): o realce de
  // sintaxe precisa acompanhar a nova extensao.
  v->refresh_language();
  message("Salvo: " + v->doc().display_name());
  tree_.refresh();
  tree_.reveal(path);
}

void App::save(bool ask_name) {
  EditorView* v = active();
  if (!v) return;
  if (ask_name || !v->doc().has_path()) {
    std::string initial = v->doc().has_path() ? v->doc().path()
                                              : tree_.root() + "/";
    ask("Salvar como:", initial, [this](bool ok, const std::string& val) {
      if (!ok || val.empty()) return;
      do_save(expand_path(val, tree_.root()));
    });
    return;
  }
  do_save(v->doc().path());
}

// ---------------------------------------------------------------------------
// Teclado
// ---------------------------------------------------------------------------

void App::handle_paste() {
  std::string text;
  ui::KeyEvent ev;
  timeout(200);
  while (ui::read_key(&ev)) {
    if (ev.is_code && static_cast<int>(ev.ch) == ui::K_PASTE_END) break;
    if (ev.is_code) continue;
    if (ev.ch == '\r') text += '\n';
    else text += utf8::encode(static_cast<uint32_t>(ev.ch));
    if (text.size() > (1u << 22)) break;   // 4 MB e mais que suficiente
  }
  timeout(20);
  if (text.empty()) return;
  if (focus_ == Focus::Terminal) term_.send_text(text, true);
  else if (EditorView* v = active()) v->insert_text(text);
  needs_redraw_ = true;
}

void App::handle_mouse(const ui::KeyEvent& ev) {
  needs_redraw_ = true;
  const int x = ev.mx, y = ev.my;

  if (ev.wheel_up || ev.wheel_down) {
    int dir = ev.wheel_up ? -3 : 3;
    if (g_config.show_sidebar && sidebar_.contains(x, y)) tree_.scroll_by(dir);
    else if (g_config.show_terminal && term_rect_.contains(x, y))
      term_.scroll_view(dir);
    else if (active()) active()->scroll_by(dir);
    return;
  }

  // Arrastar com o botao apertado dentro do editor estende a selecao.
  if (ev.drag) {
    if (dragging_editor_ && active()) active()->click(x, y, true);
    return;
  }
  if (ev.release) {
    dragging_editor_ = false;
    return;
  }
  if (!ev.press || ev.button != 0) return;

  // Duplo clique: mesmo lugar, menos de 400 ms depois.
  auto now = std::chrono::steady_clock::now();
  bool dbl = (x == last_click_x_ && y == last_click_y_) &&
             (now - last_click_ < std::chrono::milliseconds(400));
  last_click_ = now;
  last_click_x_ = x;
  last_click_y_ = y;

  if (g_config.show_sidebar && sidebar_.contains(x, y)) {
    set_focus(Focus::Sidebar);
    std::string path;
    if (tree_.click(x, y, &path) && !path.empty()) open_file(path);
    return;
  }
  if (tabbar_.contains(x, y)) {
    int tx = tabbar_.x;
    for (size_t i = 0; i < tabs_.size(); i++) {
      Document& d = tabs_[i]->doc();
      int w = utf8::width(" " + d.display_name() + (d.modified() ? " •" : "") + " ", 4);
      if (x >= tx && x < tx + w) {
        active_tab_ = static_cast<int>(i);
        set_focus(Focus::Editor);
        return;
      }
      tx += w;
    }
    return;
  }
  if (g_config.show_terminal &&
      (term_rect_.contains(x, y) || term_title_.contains(x, y))) {
    set_focus(Focus::Terminal);
    return;
  }
  if (editor_.contains(x, y)) {
    set_focus(Focus::Editor);
    if (EditorView* v = active()) {
      v->click(x, y, false);
      if (dbl) v->select_word_at_cursor();
      else dragging_editor_ = true;
    }
  }
}

bool App::handle_global_key(const ui::KeyEvent& ev) {
  if (ev.is_code) {
    switch (static_cast<int>(ev.ch)) {
      case KEY_F(1): show_help_ = true; return true;
      case KEY_F(2): set_focus(Focus::Sidebar); return true;
      case KEY_F(3):
        if (focus_ == Focus::Editor && active() && !active()->search().empty()) {
          if (!active()->find_next(true, true)) message("Nao encontrado.", true);
        } else {
          set_focus(Focus::Editor);
        }
        return true;
      case KEY_F(15):   // Shift+F3
        if (active() && !active()->search().empty() &&
            !active()->find_next(false, true))
          message("Nao encontrado.", true);
        return true;
      case KEY_F(4): set_focus(Focus::Terminal); return true;
      case KEY_F(5): tree_.refresh(); message("Lista de arquivos atualizada."); return true;
      case KEY_F(6): {
        Focus order[3] = {Focus::Sidebar, Focus::Editor, Focus::Terminal};
        int idx = focus_ == Focus::Sidebar ? 0 : (focus_ == Focus::Editor ? 1 : 2);
        for (int k = 1; k <= 3; k++) {
          Focus f = order[(idx + k) % 3];
          if (f == Focus::Sidebar && !g_config.show_sidebar) continue;
          if (f == Focus::Terminal && !g_config.show_terminal) continue;
          set_focus(f);
          break;
        }
        return true;
      }
      case KEY_F(9):
        g_config.mouse = !g_config.mouse;
        ui::set_mouse(g_config.mouse);
        message(g_config.mouse
                    ? "Mouse ligado."
                    : "Mouse desligado (agora da para selecionar com o mouse "
                      "do proprio terminal para copiar).");
        return true;
      case KEY_F(10): quit_request(); return true;
      case ui::K_ALT_LEFT:
        g_config.sidebar_width = std::max(12, g_config.sidebar_width - 2);
        return true;
      case ui::K_ALT_RIGHT:
        g_config.sidebar_width = std::min(80, g_config.sidebar_width + 2);
        return true;
      case ui::K_ALT_UP:
        g_config.terminal_height = std::min(60, g_config.terminal_height + 1);
        return true;
      case ui::K_ALT_DOWN:
        g_config.terminal_height = std::max(3, g_config.terminal_height - 1);
        return true;
      default: break;
    }
  }

  if (ev.alt && !ev.is_code) {
    switch (ev.ch) {
      case '1': set_focus(Focus::Sidebar); return true;
      case '2': set_focus(Focus::Editor); return true;
      case '3': set_focus(Focus::Terminal); return true;
      case 's': case 'S': save(true); return true;
      default: break;
    }
  }

  // Atalhos que nao valem dentro do terminal (la o Ctrl e do shell).
  if (focus_ != Focus::Terminal) {
    if (ev.ctrl('B')) {
      g_config.show_sidebar = !g_config.show_sidebar;
      if (!g_config.show_sidebar && focus_ == Focus::Sidebar)
        focus_ = Focus::Editor;
      return true;
    }
    if (ev.ctrl('J')) {
      g_config.show_terminal = !g_config.show_terminal;
      if (g_config.show_terminal) set_focus(Focus::Terminal);
      else if (focus_ == Focus::Terminal) focus_ = Focus::Editor;
      return true;
    }
    if (ev.ctrl('Q')) { quit_request(); return true; }
  }
  return false;
}

void App::handle_editor_key(const ui::KeyEvent& ev) {
  EditorView* v = active();
  if (!v) return;

  // --- atalhos com Ctrl ---
  if (ev.ctrl('S')) { save(false); return; }
  if (ev.ctrl('O')) {
    ask("Abrir arquivo:", "", [this](bool ok, const std::string& val) {
      if (ok && !val.empty()) open_file(expand_path(val, tree_.root()));
    });
    return;
  }
  if (ev.ctrl('N')) { new_tab(); return; }
  if (ev.ctrl('W')) { close_tab(); return; }
  if (ev.ctrl('A')) { v->select_all(); return; }
  if (ev.ctrl('C')) {
    if (v->has_selection()) {
      clipboard_ = v->selected_text();
      message("Copiado.");
    } else {
      clipboard_ = v->current_line_text();
      message("Linha copiada.");
    }
    return;
  }
  if (ev.ctrl('X')) {
    if (v->has_selection()) {
      clipboard_ = v->selected_text();
      v->delete_selection();
    } else {
      clipboard_ = v->current_line_text();
      v->delete_line();
    }
    message("Recortado.");
    return;
  }
  if (ev.ctrl('V')) {
    if (!clipboard_.empty()) v->insert_text(clipboard_);
    return;
  }
  if (ev.ctrl('Z')) {
    Pos c = v->cursor();
    if (v->doc().undo(&c)) { v->clear_selection(); v->set_cursor(c); }
    else message("Nada para desfazer.");
    return;
  }
  if (ev.ctrl('Y')) {
    Pos c = v->cursor();
    if (v->doc().redo(&c)) { v->clear_selection(); v->set_cursor(c); }
    else message("Nada para refazer.");
    return;
  }
  if (ev.ctrl('F')) {
    ask("Buscar:", last_search_, [this](bool ok, const std::string& val) {
      if (!ok || val.empty()) return;
      last_search_ = val;
      if (EditorView* vv = active()) {
        vv->set_search(val);
        if (!vv->find_next(true, true)) message("Nao encontrado.", true);
      }
    });
    return;
  }
  if (ev.ctrl('R')) {
    ask("Substituir - buscar:", last_search_,
        [this](bool ok, const std::string& what) {
          if (!ok || what.empty()) return;
          last_search_ = what;
          ask("Substituir por:", "", [this, what](bool ok2, const std::string& by) {
            if (!ok2) return;
            EditorView* vv = active();
            if (!vv) return;
            Document& d = vv->doc();
            d.begin_edit(EditKind::Other, vv->cursor());
            int count = 0;
            for (int l = 0; l < d.line_count(); l++) {
              size_t pos = 0;
              while (true) {
                const std::string& line = d.line(l);
                size_t p = line.find(what, pos);
                if (p == std::string::npos) break;
                d.erase(Pos{l, p}, Pos{l, p + what.size()});
                if (!by.empty()) d.insert(Pos{l, p}, by);
                pos = p + by.size();
                count++;
                if (count > 100000) break;
              }
            }
            vv->set_cursor(vv->cursor());
            message(std::to_string(count) + " substituicao(oes).");
          });
        });
    return;
  }
  if (ev.ctrl('G')) {
    ask("Ir para linha:", "", [this](bool ok, const std::string& val) {
      if (!ok || val.empty()) return;
      int n = std::atoi(val.c_str());
      if (n > 0 && active()) active()->goto_line(n);
    });
    return;
  }
  if (ev.ctrl('D')) { v->duplicate_line(); return; }

  if (ev.is_code) {
    switch (static_cast<int>(ev.ch)) {
      case ui::K_CTRL_PGUP: next_tab(-1); return;
      case ui::K_CTRL_PGDN: next_tab(+1); return;
      default: break;
    }
  }
  v->handle_key(ev);
}

void App::quit_request() {
  bool dirty = false;
  for (auto& t : tabs_)
    if (t->doc().modified()) dirty = true;
  if (!dirty) {
    quit_ = true;
    return;
  }
  ask_yes_no("Ha arquivos nao salvos. Salvar antes de sair? (s/n/c)",
             [this](char a) {
               if (a == 'c') return;
               if (a == 'n') { quit_ = true; return; }
               bool all_ok = true;
               for (size_t i = 0; i < tabs_.size(); i++) {
                 if (!tabs_[i]->doc().modified()) continue;
                 if (!tabs_[i]->doc().has_path()) {
                   active_tab_ = static_cast<int>(i);
                   save(true);
                   all_ok = false;
                   break;
                 }
                 std::string err;
                 if (!tabs_[i]->doc().save(&err)) {
                   message(err, true);
                   all_ok = false;
                 }
               }
               if (all_ok) quit_ = true;
             });
}

void App::handle_key(const ui::KeyEvent& ev) {
  needs_redraw_ = true;

  if (show_help_) {
    show_help_ = false;
    return;
  }
  if (ev.is_mouse) {
    if (prompt_active_) return;   // com um prompt aberto o mouse nao age
    handle_mouse(ev);
    return;
  }
  if (prompt_key(ev)) return;

  if (ev.is_code) {
    int code = static_cast<int>(ev.ch);
    if (code == KEY_RESIZE) {
      compute_layout();
      if (term_rect_.w > 0 && term_rect_.h > 0)
        term_.resize(term_rect_.w, term_rect_.h);
      clear();
      return;
    }
    if (code == ui::K_PASTE_BEGIN) { handle_paste(); return; }
  }

  if (handle_global_key(ev)) return;

  switch (focus_) {
    case Focus::Sidebar: {
      std::string path;
      if (tree_.handle_key(ev, &path)) {
        if (!path.empty()) open_file(path);
        return;
      }
      handle_editor_key(ev);   // atalhos globais de arquivo continuam valendo
      return;
    }
    case Focus::Terminal: {
      if (!term_.running()) {
        if ((!ev.is_code && (ev.ch == '\r' || ev.ch == '\n')) ||
            (ev.is_code && static_cast<int>(ev.ch) == KEY_ENTER)) {
          ensure_terminal();
        }
        return;
      }
      if (ev.is_code) {
        int code = static_cast<int>(ev.ch);
        if (code == ui::K_SHIFT_PGUP) { term_.scroll_view(-term_rect_.h / 2); return; }
        if (code == ui::K_SHIFT_PGDN) { term_.scroll_view(term_rect_.h / 2); return; }
      }
      if (ev.ctrl('V') && !clipboard_.empty()) {
        term_.send_text(clipboard_, true);
        return;
      }
      term_.clear_view_scroll();
      term_.send_key(ev);
      return;
    }
    case Focus::Editor:
    default:
      handle_editor_key(ev);
      return;
  }
}

// ---------------------------------------------------------------------------
// Loop principal
// ---------------------------------------------------------------------------

int App::run() {
  compute_layout();
  if (g_config.show_terminal) ensure_terminal();

  while (!quit_) {
    if (term_.running() && term_.poll_output()) needs_redraw_ = true;

    if (needs_redraw_) {
      draw();
      needs_redraw_ = false;
    }

    ui::KeyEvent ev;
    if (ui::read_key(&ev)) {
      handle_key(ev);
      // Consome rapidamente o que mais estiver na fila (digitacao rapida).
      int guard = 0;
      while (!quit_ && guard++ < 64) {
        nodelay(stdscr, TRUE);
        bool got = ui::read_key(&ev);
        nodelay(stdscr, FALSE);
        timeout(20);
        if (!got) break;
        handle_key(ev);
      }
    }

    if (message_ttl_ > 0 && --message_ttl_ == 0) needs_redraw_ = true;
  }
  return 0;
}
