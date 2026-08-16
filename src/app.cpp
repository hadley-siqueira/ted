#include "app.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "config.hpp"
#include "fuzzy.hpp"
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

// Acima disso o editor recusa abrir: o desfazer guarda uma copia do arquivo a
// cada grupo de edicao, entao um arquivo de centenas de MB consumiria toda a
// memoria da maquina e daria a impressao de travamento.
constexpr long kMaxFileSize = 16L * 1024 * 1024;

// Tamanho minimo de um painel dividido. Abaixo disso o split e recusado: e
// melhor avisar que nao cabe do que dividir e desenhar torto. A altura conta a
// barra de abas do painel (1) mais tres linhas de codigo.
constexpr int kMinPaneW = 24;
constexpr int kMinPaneH = 4;

long file_size(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return -1;
  return static_cast<long>(st.st_size);
}

// Um arquivo e "binario" se tiver byte zero ou muito caractere de controle no
// comeco. So procurar por '\0' deixa passar coisas como .zip e .jpg, que nao
// tem zeros logo no inicio.
bool looks_binary(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;

  auto suspicious = [&](std::streamoff at) {
    char buf[4096];
    in.clear();
    in.seekg(at);
    in.read(buf, sizeof(buf));
    std::streamsize n = in.gcount();
    int control = 0;
    for (std::streamsize i = 0; i < n; i++) {
      unsigned char c = static_cast<unsigned char>(buf[i]);
      if (c == '\0') return true;
      if (c < 32 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != 27)
        control++;
    }
    return n > 0 && control * 10 > static_cast<int>(n);   // mais de 10%
  };

  if (suspicious(0)) return true;
  // Alguns arquivos comecam com texto e viram binario depois (PDF, por
  // exemplo), entao damos uma segunda olhada no meio.
  const long size = file_size(path);
  if (size > 65536 && suspicious(size / 2)) return true;
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
    case Lang::Html: return "HTML";
    case Lang::Css: return "CSS";
    case Lang::Sql: return "SQL";
    default: return "Texto";
  }
}

}  // namespace

App::App(const std::string& root, const std::vector<std::string>& files)
    : tree_(root) {
  cols_.push_back(PaneColumn{});
  cols_[0].rows.push_back(Pane{});

  for (const auto& f : files) open_file(expand_path(f, tree_.root()));
  if (pane().tabs.empty()) new_tab();
  if (!message_error_) {   // nao apaga um aviso vindo de open_file()
    message_ = "F1 mostra a ajuda com todos os atalhos.";
    message_ttl_ = 400;
  }

  if (!theme_exists(g_config.theme)) {
    message("Tema \"" + g_config.theme +
                "\" nao existe (use: ted --themes). Usando o padrao.",
            true);
  }
}

// ---------------------------------------------------------------------------
// Paineis
// ---------------------------------------------------------------------------

// Os tres acessores abaixo prendem os indices a faixa valida em vez de confiar
// em quem chamou: e a mesma rede que o antigo active() ja tinha, agora tambem
// para a coluna e a linha da grade.
Pane& App::pane() {
  if (cols_.empty()) cols_.push_back(PaneColumn{});
  cur_col_ = std::min(std::max(cur_col_, 0), static_cast<int>(cols_.size()) - 1);
  PaneColumn& col = cols_[cur_col_];
  if (col.rows.empty()) col.rows.push_back(Pane{});
  cur_row_ = std::min(std::max(cur_row_, 0), static_cast<int>(col.rows.size()) - 1);
  return col.rows[cur_row_];
}

EditorView* App::view_of(Pane& p) {
  if (p.tabs.empty()) return nullptr;
  p.active_tab =
      std::min(std::max(p.active_tab, 0), static_cast<int>(p.tabs.size()) - 1);
  return p.tabs[p.active_tab].get();
}

EditorView* App::active() { return view_of(pane()); }

int App::pane_count() const {
  int n = 0;
  for (const auto& c : cols_) n += static_cast<int>(c.rows.size());
  return n;
}

// Quantas views, em toda a grade, mostram este documento. Depois de um Alt+V
// sao duas; fechar uma delas nao perde nada.
int App::views_of_doc(const Document* d) const {
  int n = 0;
  for (const auto& col : cols_)
    for (const auto& p : col.rows)
      for (const auto& t : p.tabs)
        if (&t->doc() == d) n++;
  return n;
}

// "Salvar como" pode mudar a extensao do arquivo. Todas as views daquele
// documento precisam redetectar a linguagem do realce, nao so a que salvou.
void App::refresh_language_of(const Document* d) {
  for (auto& col : cols_)
    for (auto& p : col.rows)
      for (auto& t : p.tabs)
        if (&t->doc() == d) t->refresh_language();
}

bool App::doc_open_elsewhere(const Document* d, int col, int row) const {
  for (size_t ci = 0; ci < cols_.size(); ci++) {
    for (size_t ri = 0; ri < cols_[ci].rows.size(); ri++) {
      if (static_cast<int>(ci) == col && static_cast<int>(ri) == row) continue;
      for (const auto& t : cols_[ci].rows[ri].tabs)
        if (&t->doc() == d) return true;
    }
  }
  return false;
}

// Uma coluna nova a direita da atual, mostrando o *mesmo* documento numa view
// nova - e por isso que EditorView guarda um shared_ptr<Document>: as duas
// metades editam o mesmo texto, com cursor e rolagem independentes.
void App::split_vertical() {
  const int ncols = static_cast<int>(cols_.size());
  // Estimativa de "cabe?": com pesos iguais (o caso normal) e exatamente a
  // largura que cada coluna teria depois da divisao, ja descontados os
  // separadores.
  if ((editor_region_.w - ncols) / (ncols + 1) < kMinPaneW) {
    message("Sem espaco para dividir. Ctrl+B esconde os arquivos.", true);
    return;
  }

  EditorView* v = active();
  Pane novo;
  novo.tabs.push_back(v ? std::make_unique<EditorView>(v->doc_ptr())
                        : std::make_unique<EditorView>(std::make_shared<Document>()));

  PaneColumn col;
  // Mesmo peso da coluna atual: as duas ficam do mesmo tamanho.
  col.weight = cols_[cur_col_].weight;
  col.rows.push_back(std::move(novo));
  cols_.insert(cols_.begin() + cur_col_ + 1, std::move(col));

  cur_col_++;
  cur_row_ = 0;
  set_focus(Focus::Editor);
  compute_layout();
  needs_redraw_ = true;
}

// Um painel novo embaixo do atual, dentro da mesma coluna. Aqui nao existe
// linha de separador: a barra de abas do painel de baixo ja separa os dois.
void App::split_horizontal() {
  // active() passa por pane(), que e quem prende cur_col_/cur_row_ a faixa
  // valida - so depois disso da para indexar cols_ com seguranca.
  EditorView* v = active();
  auto doc = v ? v->doc_ptr() : std::make_shared<Document>();

  const int nrows = static_cast<int>(cols_[cur_col_].rows.size());
  // Mesma conta da largura, agora na altura. Na horizontal o espaco e bem mais
  // curto, por isso a dica sugere esconder o terminal e nao a barra lateral.
  if (editor_region_.h / (nrows + 1) < kMinPaneH) {
    message("Sem espaco para dividir. Ctrl+J esconde o terminal.", true);
    return;
  }

  PaneColumn& col = cols_[cur_col_];
  Pane novo;
  novo.weight = col.rows[cur_row_].weight;   // as duas metades ficam iguais
  novo.tabs.push_back(std::make_unique<EditorView>(doc));
  col.rows.insert(col.rows.begin() + cur_row_ + 1, std::move(novo));

  cur_row_++;
  set_focus(Focus::Editor);
  compute_layout();
  needs_redraw_ = true;
}

// Tira o painel com o foco da grade, sem verificar nada: quem chama garante
// que ha mais de um painel e que nada de nao salvo se perde.
void App::remove_current_pane() {
  PaneColumn& col = cols_[cur_col_];
  col.rows.erase(col.rows.begin() + cur_row_);
  if (col.rows.empty()) cols_.erase(cols_.begin() + cur_col_);
  pane();                 // prende cur_col_/cur_row_ a faixa valida
  compute_layout();
  needs_redraw_ = true;
}

// Fechar um painel joga fora as abas dele. Se alguma tem alteracao nao salva
// que nao esta aberta em nenhum outro painel, recusamos em vez de perguntar:
// o aluno salva (Ctrl+S) ou fecha a aba (Ctrl+W) primeiro. Nunca perder texto.
void App::close_pane() {
  if (pane_count() <= 1) {
    message("So ha um painel - nada para fechar (Ctrl+W fecha a aba).");
    return;
  }
  const Pane& p = pane();
  for (const auto& t : p.tabs) {
    const Document* d = &t->doc();
    if (!d->modified() || doc_open_elsewhere(d, cur_col_, cur_row_)) continue;
    message("\"" + d->display_name() +
                "\" tem alteracoes nao salvas. Salve (Ctrl+S) ou feche a aba "
                "(Ctrl+W) antes de fechar o painel.",
            true);
    return;
  }
  remove_current_pane();
  set_focus(Focus::Editor);
}

// Os paineis na ordem de leitura: coluna a coluna, de cima para baixo. A
// posicao nesta lista e o "numero do painel" que o Ctrl+T guarda no PickerItem.
std::vector<std::pair<int, int>> App::pane_order() const {
  std::vector<std::pair<int, int>> order;
  for (size_t ci = 0; ci < cols_.size(); ci++)
    for (size_t ri = 0; ri < cols_[ci].rows.size(); ri++)
      order.emplace_back(static_cast<int>(ci), static_cast<int>(ri));
  return order;
}

// Leva o foco para o painel de numero 'idx' na ordem de leitura.
bool App::focus_pane_index(int idx) {
  const auto order = pane_order();
  if (idx < 0 || idx >= static_cast<int>(order.size())) return false;
  cur_col_ = order[idx].first;
  cur_row_ = order[idx].second;
  return true;
}

// Anda entre os paineis na ordem de leitura, dando a volta no fim.
void App::next_pane(int delta) {
  const auto order = pane_order();
  if (order.size() < 2) {
    message("So ha um painel (Alt+V ou Alt+H divide a tela).");
    return;
  }

  int at = 0;
  for (size_t i = 0; i < order.size(); i++)
    if (order[i].first == cur_col_ && order[i].second == cur_row_)
      at = static_cast<int>(i);
  const int n = static_cast<int>(order.size());
  focus_pane_index(((at + delta) % n + n) % n);
  set_focus(Focus::Editor);
  needs_redraw_ = true;
}

// Qual painel esta sob um ponto da tela (e se o ponto caiu na barra de abas).
App::PaneRef App::pane_at(int x, int y) const {
  for (size_t ci = 0; ci < cols_.size(); ci++) {
    for (size_t ri = 0; ri < cols_[ci].rows.size(); ri++) {
      const Pane& p = cols_[ci].rows[ri];
      const int c = static_cast<int>(ci), r = static_cast<int>(ri);
      if (p.tabbar.contains(x, y)) return PaneRef{c, r, true};
      if (p.area.contains(x, y)) return PaneRef{c, r, false};
    }
  }
  return PaneRef{};
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
  // O que sobra e a regiao da grade de paineis: cada painel gasta 1 linha com
  // a propria barra de abas e fica com o resto para o codigo.
  editor_region_ = Rect{rx, 0, rw, body_h - (th > 0 ? th + 1 : 0)};
  layout_panes();
}

// Reparte a regiao do editor entre as colunas (por 'weight') e, dentro de cada
// coluna, entre os paineis empilhados. A ultima coluna/linha leva a sobra da
// divisao inteira, para nao deixar buraco na tela.
void App::layout_panes() {
  const int ncols = static_cast<int>(cols_.size());
  if (ncols == 0) return;
  const Rect& r = editor_region_;

  // Uma coluna de separador entre colunas vizinhas (nenhuma se so ha uma).
  const int avail_w = std::max(ncols, r.w - (ncols - 1));
  int total_w = 0;
  for (const auto& c : cols_) total_w += c.weight;
  if (total_w <= 0) total_w = ncols;

  int x = r.x;
  for (int ci = 0; ci < ncols; ci++) {
    PaneColumn& col = cols_[ci];
    const int w = (ci == ncols - 1) ? (r.x + r.w - x)
                                    : avail_w * col.weight / total_w;

    const int nrows = static_cast<int>(col.rows.size());
    int total_h = 0;
    for (const auto& p : col.rows) total_h += p.weight;
    if (total_h <= 0) total_h = std::max(1, nrows);

    int y = r.y;
    for (int ri = 0; ri < nrows; ri++) {
      Pane& p = col.rows[ri];
      const int h = (ri == nrows - 1) ? (r.y + r.h - y) : r.h * p.weight / total_h;
      p.tabbar = Rect{x, y, w, 1};
      p.area = Rect{x, y + 1, w, std::max(1, h - 1)};
      y += h;
    }
    col.sep_x = (ci == ncols - 1) ? -1 : x + w;
    x += w + 1;   // +1 = separador (irrelevante depois da ultima coluna)
  }
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

// Rotulo de uma aba na barra: o nome do arquivo, com um ponto quando esta
// modificado.
static std::string tab_label(Document& d) {
  return " " + d.display_name() + (d.modified() ? " •" : "") + " ";
}

// 'focused' diz se este e o painel que recebe as teclas. A aba ativa do painel
// com o foco fica com o fundo de destaque; a dos outros so com a letra colorida
// - da para ver que arquivo cada painel mostra sem disputar a atencao.
void App::draw_tabbar(const Pane& p, bool focused) {
  const Rect& bar = p.tabbar;
  if (bar.w <= 0 || bar.h <= 0) return;
  attrset(COLOR_PAIR(ui::kTabBar));
  ui::fill(bar.y, bar.x, bar.w);
  if (p.tabs.empty()) {
    attrset(COLOR_PAIR(ui::kNormal));
    return;
  }

  const int n = static_cast<int>(p.tabs.size());
  const int at = std::min(std::max(p.active_tab, 0), n - 1);

  // A barra rola para a aba ativa: num painel estreito ela nao caberia se
  // comecassemos sempre da primeira, e o painel ficaria mostrando um arquivo
  // sem dizer qual e. Recua a partir da ativa enquanto couber.
  int first = at;
  int usado = utf8::width(tab_label(p.tabs[at]->doc()), 4);
  while (first > 0) {
    const int w = utf8::width(tab_label(p.tabs[first - 1]->doc()), 4);
    if (usado + w > bar.w) break;
    usado += w;
    first--;
  }

  int x = bar.x;
  int ultima = first - 1;
  for (int i = first; i < n; i++) {
    Document& d = p.tabs[i]->doc();
    const std::string label = tab_label(d);
    const int w = utf8::width(label, 4);
    const int cabe = bar.x + bar.w - x;
    if (cabe <= 0) break;
    // A primeira desenhada aparece nem que seja cortada; as outras so inteiras.
    if (w > cabe && i != first) break;
    const int draw_w = std::min(w, cabe);

    const bool act = (i == at);
    int cp;
    if (act) cp = focused ? ui::kTabActive : ui::kTabActiveDim;
    else cp = d.modified() ? ui::kTabModified : ui::kTabBar;
    attrset(COLOR_PAIR(cp) | (act ? A_BOLD : 0));
    ui::fill(bar.y, x, draw_w);
    ui::put(bar.y, x, draw_w, label);
    x += draw_w;
    ultima = i;
    if (w > cabe) break;
  }

  // Setas avisando que ha abas fora da faixa visivel.
  attrset(COLOR_PAIR(ui::kTabBar) | A_BOLD);
  if (first > 0) ui::put(bar.y, bar.x, 1, "<");
  if (ultima < n - 1) ui::put(bar.y, bar.x + bar.w - 1, 1, ">");
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
    right += v->uses_tabs()
                 ? "  Tab:" + std::to_string(g_config.tab_width)
                 : "  Espacos:" + std::to_string(g_config.tab_width);
    if (v->overwrite()) right += "  SOBRESCREVER";
    if (literal_next_) right += "  ^K";
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
      "Ctrl+K  insere a tecla literal   Ctrl+/ (ou Alt+C)  comenta a selecao",
      "Ctrl+Q  sair  (ou F10)           Tab / Shift+Tab  indenta / desindenta",
      "Ctrl+PgUp/PgDn  troca de aba     Alt+Shift+Cima/Baixo  move a linha",
      "",
      "BUSCA E NAVEGACAO                PAINEIS",
      "Ctrl+P  abrir arquivo pelo nome  F2/F3/F4  arquivos, editor, terminal",
      "Ctrl+T  procurar nos abertos     F6 alterna regioes  F7 painel dividido",
      "Ctrl+F  buscar no arquivo        Alt+V/Alt+H divide  Alt+W fecha painel",
      "F3 / Shift+F3  proxima / ant.    Ctrl+B/Ctrl+J esconde arquivos/terminal",
      "Ctrl+R  substituir tudo          Alt+Setas redimensiona  F5 recarrega",
      "Ctrl+G  ir para a linha          F9 mouse  Shift+PgUp/PgDn  historico",
      "Ctrl+Setas por palavra, Shift+Setas seleciona, Home/End, PgUp/PgDn",
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

  // --- abas + editor (um par por painel da grade) ---
  for (int ci = 0; ci < static_cast<int>(cols_.size()); ci++) {
    for (int ri = 0; ri < static_cast<int>(cols_[ci].rows.size()); ri++) {
      Pane& p = cols_[ci].rows[ri];
      const bool cur = (ci == cur_col_ && ri == cur_row_);
      draw_tabbar(p, cur && focus_ == Focus::Editor);
      if (EditorView* v = view_of(p))
        v->draw(p.area, cur && focus_ == Focus::Editor);
    }
    // Separador entre esta coluna e a proxima, da barra de abas ate embaixo.
    if (cols_[ci].sep_x >= 0) {
      attrset(COLOR_PAIR(ui::kPaneTitle));
      for (int y = editor_region_.y; y < editor_region_.y + editor_region_.h; y++)
        mvaddstr(y, cols_[ci].sep_x, "│");
      attrset(COLOR_PAIR(ui::kNormal));
    }
  }

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
  if (picker_.active()) picker_.draw(screen_w_, screen_h_);
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
  if (picker_.active()) {
    if (picker_.cursor_screen(&x, &y)) {
      move(y, x);
      curs_set(1);
    } else {
      curs_set(0);
    }
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
  Pane& p = pane();
  p.tabs.push_back(std::make_unique<EditorView>(std::make_shared<Document>()));
  p.active_tab = static_cast<int>(p.tabs.size()) - 1;
  set_focus(Focus::Editor);
}

// Procura um arquivo ja aberto em *qualquer* painel e devolve o Document dele.
// Serve para manter a invariante "um arquivo, um Document": se o mesmo arquivo
// virasse dois buffers, editar nos dois e salvar faria o segundo Ctrl+S apagar
// o trabalho do primeiro, sem aviso nenhum.
std::shared_ptr<Document> App::find_open_doc(const std::string& path) const {
  if (path.empty()) return nullptr;   // "sem nome" nao casa com "sem nome"
  for (const auto& col : cols_)
    for (const auto& pn : col.rows)
      for (const auto& t : pn.tabs)
        if (t->doc().path() == path) return t->doc_ptr();
  return nullptr;
}

// Poe uma view do documento no painel: reaproveita a aba atual se ela estiver
// vazia e sem nome, senao abre uma aba nova.
void App::add_view(Pane& p, std::shared_ptr<Document> doc) {
  EditorView* cur = view_of(p);
  if (cur && !cur->doc().has_path() && !cur->doc().modified() &&
      cur->doc().line_count() == 1 && cur->doc().line(0).empty()) {
    p.tabs[p.active_tab] = std::make_unique<EditorView>(std::move(doc));
  } else {
    p.tabs.push_back(std::make_unique<EditorView>(std::move(doc)));
    p.active_tab = static_cast<int>(p.tabs.size()) - 1;
  }
}

void App::open_file(const std::string& path) {
  Pane& p = pane();
  // Ja esta aberto neste painel? So ativa a aba.
  for (size_t i = 0; i < p.tabs.size(); i++) {
    if (p.tabs[i]->doc().path() == path) {
      p.active_tab = static_cast<int>(i);
      set_focus(Focus::Editor);
      return;
    }
  }
  // Aberto em *outro* painel? Mostra o mesmo Document aqui, como o Alt+V faz.
  // Carregar o arquivo de novo criaria um segundo buffer do mesmo arquivo.
  if (auto shared = find_open_doc(path)) {
    add_view(p, std::move(shared));
    set_focus(Focus::Editor);
    needs_redraw_ = true;
    return;
  }
  if (access(path.c_str(), F_OK) == 0) {
    const long size = file_size(path);
    if (size > kMaxFileSize) {
      message("Arquivo muito grande (" + std::to_string(size / (1024 * 1024)) +
                  " MB). O ted abre ate " +
                  std::to_string(kMaxFileSize / (1024 * 1024)) + " MB.",
              true);
      return;
    }
    if (looks_binary(path)) {
      message("Arquivo binario - nao da para editar aqui.", true);
      return;
    }
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

  add_view(p, std::move(doc));
  set_focus(Focus::Editor);
  needs_redraw_ = true;
}

// Tira a aba ativa do painel com o foco. Fechar a *ultima* aba de um painel
// fecha o painel junto - do contrario a tela dividida ficaria com uma metade
// mostrando "[sem nome]" em branco. Se o painel e o unico, ele continua vivo
// com uma aba nova em branco (o editor nunca fica sem nenhum documento).
void App::drop_active_tab() {
  Pane& p = pane();
  if (p.tabs.empty()) return;
  p.tabs.erase(p.tabs.begin() + p.active_tab);

  if (!p.tabs.empty()) {
    if (p.active_tab >= static_cast<int>(p.tabs.size()))
      p.active_tab = static_cast<int>(p.tabs.size()) - 1;
    needs_redraw_ = true;
    return;
  }
  if (pane_count() > 1) remove_current_pane();   // invalida 'p'
  else new_tab();
  needs_redraw_ = true;
}

void App::close_tab() {
  EditorView* v = active();
  if (!v) return;
  // So pergunta se esta aba e a *unica* que mostra o documento. Com a tela
  // dividida o mesmo arquivo costuma estar em dois paineis; ali fechar uma das
  // abas nao perde nada, e perguntar "salvar alteracoes?" seria mentira.
  if (v->doc().modified() && views_of_doc(&v->doc()) == 1) {
    std::string name = v->doc().display_name();
    ask_yes_no("Salvar alteracoes em " + name + "? (s/n/c)", [this](char a) {
      if (a == 'c') return;
      if (a == 's') {
        EditorView* vv = active();
        if (vv && !vv->doc().has_path()) { save(true); return; }
        save(false);
        if (active() && active()->doc().modified()) return;
      }
      drop_active_tab();
    });
    return;
  }
  drop_active_tab();
}

void App::next_tab(int delta) {
  Pane& p = pane();
  if (p.tabs.empty()) return;
  int n = static_cast<int>(p.tabs.size());
  p.active_tab = ((p.active_tab + delta) % n + n) % n;
  needs_redraw_ = true;
}

void App::do_save(const std::string& path) {
  EditorView* v = active();
  if (!v) return;

  // "Salvar como" por cima de um arquivo que ja esta aberto em outra aba
  // deixaria dois Documents com o mesmo caminho - e o proximo Ctrl+S de
  // qualquer um dos dois apagaria o trabalho do outro, sem aviso. Recusamos,
  // pela mesma razao do fechar painel. (Salvar por cima do proprio caminho,
  // que e o Ctrl+S normal, continua passando.)
  if (auto outro = find_open_doc(path)) {
    if (outro.get() != &v->doc()) {
      const size_t slash = path.find_last_of('/');
      const std::string nome =
          (slash == std::string::npos) ? path : path.substr(slash + 1);
      message("\"" + nome +
                  "\" ja esta aberto em outra aba. Feche-a antes (Ctrl+W).",
              true);
      return;
    }
  }

  std::string err;
  if (!v->doc().save_as(path, &err)) {
    message(err, true);
    return;
  }
  // O arquivo pode ter ganhado nome agora (ou outro nome): o realce de
  // sintaxe precisa acompanhar a nova extensao - em todos os paineis que
  // mostram este documento, nao so neste.
  refresh_language_of(&v->doc());
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
// Fuzzy finder de arquivos (Ctrl+P) e busca nos arquivos abertos (Ctrl+T)
// ---------------------------------------------------------------------------

namespace {
constexpr size_t kMaxProjectFiles = 20000;
constexpr size_t kMaxPickerResults = 300;
constexpr size_t kMaxSearchResults = 500;
}  // namespace

std::vector<PickerItem> App::filter_files(const std::string& query) {
  std::vector<PickerItem> out;
  out.reserve(std::min(file_cache_.size(), kMaxPickerResults));

  std::vector<size_t> positions;
  int score = 0;
  for (const std::string& rel : file_cache_) {
    if (!fuzzy_match(rel, query, &score, &positions)) continue;
    PickerItem item;
    item.label = rel;
    item.match = positions;
    item.score = score;
    item.path = tree_.root() + "/" + rel;
    out.push_back(std::move(item));
  }
  // Nota maior primeiro; empate resolve pelo caminho mais curto (costuma ser
  // o arquivo "principal") e depois em ordem alfabetica.
  std::sort(out.begin(), out.end(), [](const PickerItem& a, const PickerItem& b) {
    if (a.score != b.score) return a.score > b.score;
    if (a.label.size() != b.label.size()) return a.label.size() < b.label.size();
    return a.label < b.label;
  });
  if (out.size() > kMaxPickerResults) out.resize(kMaxPickerResults);
  return out;
}

void App::open_file_picker() {
  file_cache_ = tree_.list_all_files(kMaxProjectFiles, &file_cache_truncated_);
  if (file_cache_.empty()) {
    message("Nenhum arquivo encontrado em " + tree_.root(), true);
    return;
  }
  if (file_cache_truncated_)
    message("Projeto grande: mostrando os primeiros " +
            std::to_string(kMaxProjectFiles) + " arquivos.");

  picker_.open("Abrir arquivo", "digite parte do nome do arquivo",
               [this](const std::string& q) { return filter_files(q); },
               [this](const PickerItem& item) { open_file(item.path); });
  needs_redraw_ = true;
}

std::vector<PickerItem> App::search_open_files(const std::string& query) {
  std::vector<PickerItem> out;
  if (query.empty()) return out;

  // Mesma regra do Ctrl+F: so diferencia maiusculas se voce digitar alguma.
  const bool case_sensitive =
      std::any_of(query.begin(), query.end(),
                  [](unsigned char c) { return std::isupper(c); });
  auto fold = [&](std::string s) {
    if (!case_sensitive)
      for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
    return s;
  };
  const std::string needle = fold(query);

  // Varre todos os paineis. Um mesmo Document costuma estar aberto em mais de
  // um painel (foi isso que o Alt+V fez), e listar os resultados dele duas
  // vezes seria so barulho: cada documento e procurado uma vez, e o resultado
  // aponta para o primeiro painel que o mostra, na ordem de leitura.
  std::vector<const Document*> ja_vistos;
  const auto order = pane_order();

  for (size_t pi = 0; pi < order.size(); pi++) {
    const Pane& pane_atual = cols_[order[pi].first].rows[order[pi].second];
    for (size_t t = 0; t < pane_atual.tabs.size(); t++) {
      Document& d = pane_atual.tabs[t]->doc();
      if (std::find(ja_vistos.begin(), ja_vistos.end(), &d) != ja_vistos.end())
        continue;
      ja_vistos.push_back(&d);

      const std::string name = d.display_name();
      for (int l = 0; l < d.line_count(); l++) {
        const std::string& line = d.line(l);
        size_t hit = fold(line).find(needle);
        if (hit == std::string::npos) continue;

        // Rotulo: "arquivo:linha  trecho da linha (sem a indentacao)".
        std::string trecho = line;
        size_t first = trecho.find_first_not_of(" \t");
        size_t removed = (first == std::string::npos) ? 0 : first;
        trecho =
            (first == std::string::npos) ? std::string() : trecho.substr(first);

        const std::string prefix = name + ":" + std::to_string(l + 1) + "  ";
        PickerItem item;
        item.label = prefix + trecho;
        for (size_t k = 0; k < needle.size(); k++)
          item.match.push_back(prefix.size() + (hit - removed) + k);
        item.pane = static_cast<int>(pi);
        item.tab = static_cast<int>(t);
        item.line = l;
        item.col = hit;
        item.len = needle.size();
        out.push_back(std::move(item));
        if (out.size() >= kMaxSearchResults) return out;
      }
    }
  }
  return out;
}

void App::open_text_picker() {
  // Conta documentos distintos, nao abas: o mesmo arquivo em dois paineis e
  // um arquivo so (e a busca tambem o trata assim).
  std::vector<const Document*> vistos;
  for (const auto& col : cols_)
    for (const auto& p : col.rows)
      for (const auto& t : p.tabs)
        if (std::find(vistos.begin(), vistos.end(), &t->doc()) == vistos.end())
          vistos.push_back(&t->doc());

  const size_t n = vistos.size();
  std::string hint = "procurar em " + std::to_string(n) +
                     (n == 1 ? " arquivo aberto" : " arquivos abertos");
  picker_.open("Procurar texto", hint,
               [this](const std::string& q) { return search_open_files(q); },
               [this](const PickerItem& item) {
                 if (!focus_pane_index(item.pane)) return;
                 Pane& p = pane();
                 if (item.tab < 0 ||
                     item.tab >= static_cast<int>(p.tabs.size()))
                   return;
                 p.active_tab = item.tab;
                 set_focus(Focus::Editor);
                 EditorView* v = active();
                 if (!v) return;
                 v->select_range(Pos{item.line, item.col},
                                 Pos{item.line, item.col + item.len});
               });
  needs_redraw_ = true;
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
    else {
      // A roda age no painel sob o ponteiro; fora deles, no painel com o foco.
      const PaneRef pr = pane_at(x, y);
      Pane& p = pr.col >= 0 ? cols_[pr.col].rows[pr.row] : pane();
      if (EditorView* v = view_of(p)) v->scroll_by(dir);
    }
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

  const PaneRef pr = pane_at(x, y);
  if (pr.col >= 0 && pr.tabbar) {
    Pane& p = cols_[pr.col].rows[pr.row];
    int tx = p.tabbar.x;
    for (size_t i = 0; i < p.tabs.size(); i++) {
      int w = utf8::width(tab_label(p.tabs[i]->doc()), 4);
      if (x >= tx && x < tx + w) {
        cur_col_ = pr.col;
        cur_row_ = pr.row;
        p.active_tab = static_cast<int>(i);
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
  if (pr.col >= 0) {
    cur_col_ = pr.col;
    cur_row_ = pr.row;
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
      case KEY_F(7): next_pane(+1); return true;
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
      case 'v': case 'V': split_vertical(); return true;
      case 'h': case 'H': split_horizontal(); return true;
      case 'w': case 'W': close_pane(); return true;
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
  if (ev.ctrl('P')) { open_file_picker(); return; }
  if (ev.ctrl('T')) { open_text_picker(); return; }
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
  if (ev.ctrl('K')) {
    literal_next_ = true;
    message("Ctrl+K: a proxima tecla entra como caractere (Tab = TAB real).");
    return;
  }
  // Ctrl+/ chega como o caractere 31 na maioria dos terminais; Alt+C fica
  // como alternativa para os que nao mandam nada.
  if ((!ev.is_code && !ev.alt && ev.ch == 31) ||
      (ev.alt && (ev.ch == 'c' || ev.ch == 'C'))) {
    if (!v->toggle_comment())
      message("Este tipo de arquivo nao tem comentario.", true);
    return;
  }

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
  for (auto& col : cols_)
    for (auto& p : col.rows)
      for (auto& t : p.tabs)
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
               // O primeiro arquivo *sem nome* interrompe a varredura: ele
               // abre o "salvar como", e o Ctrl+Q seguinte continua daqui.
               bool asked_name = false;
               for (size_t ci = 0; ci < cols_.size() && !asked_name; ci++) {
                 for (size_t ri = 0; ri < cols_[ci].rows.size() && !asked_name;
                      ri++) {
                   Pane& p = cols_[ci].rows[ri];
                   for (size_t i = 0; i < p.tabs.size(); i++) {
                     Document& d = p.tabs[i]->doc();
                     if (!d.modified()) continue;
                     if (!d.has_path()) {
                       cur_col_ = static_cast<int>(ci);
                       cur_row_ = static_cast<int>(ri);
                       p.active_tab = static_cast<int>(i);
                       save(true);
                       all_ok = false;
                       asked_name = true;
                       break;
                     }
                     std::string err;
                     if (!d.save(&err)) {
                       message(err, true);
                       all_ok = false;
                     }
                   }
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
  // O literal vem antes de tudo: e justamente para escapar dos atalhos.
  if (literal_next_ && !ev.is_mouse) {
    literal_next_ = false;
    EditorView* v = active();
    if (!v || focus_ != Focus::Editor) return;
    if (ev.is_code) {
      message("Essa tecla nao tem um caractere para inserir.", true);
      return;
    }
    v->insert_literal(utf8::encode(static_cast<uint32_t>(ev.ch)));
    return;
  }

  if (picker_.active()) {
    // KEY_RESIZE precisa passar para o layout se ajustar.
    if (!(ev.is_code && static_cast<int>(ev.ch) == KEY_RESIZE)) {
      picker_.handle_key(ev);
      return;
    }
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
