#include "ui.hpp"

#include <clocale>
#include <cstdio>
#include <map>
#include <string>

#include "config.hpp"
#include "utf8.hpp"

namespace ui {
namespace {

std::map<int, int> g_dyn_pairs;   // chave: (fg+1)*257 + (bg+1)
int g_next_pair = kDynamicPairStart;

void pair(int id, int fg, int bg) { init_pair(static_cast<short>(id), fg, bg); }

// Registra a sequencia de escape 'seq' como a tecla 'code'.
void bind(const char* seq, int code) { define_key(seq, code); }

// Registra ESC[1;<mod><letra> e variantes para setas/Home/End.
void bind_modified_keys() {
  struct { char final_ch; int shift, ctrl, alt, ctrl_shift; } arrows[] = {
      {'A', K_SHIFT_UP, K_CTRL_UP, K_ALT_UP, K_CTRL_SHIFT_UP},
      {'B', K_SHIFT_DOWN, K_CTRL_DOWN, K_ALT_DOWN, K_CTRL_SHIFT_DOWN},
      {'C', K_SHIFT_RIGHT, K_CTRL_RIGHT, K_ALT_RIGHT, K_CTRL_SHIFT_RIGHT},
      {'D', K_SHIFT_LEFT, K_CTRL_LEFT, K_ALT_LEFT, K_CTRL_SHIFT_LEFT},
      {'H', K_SHIFT_HOME, K_CTRL_HOME, 0, K_CTRL_SHIFT_HOME},
      {'F', K_SHIFT_END, K_CTRL_END, 0, K_CTRL_SHIFT_END},
  };
  for (auto& a : arrows) {
    char buf[32];
    const int mods[4] = {2, 5, 3, 6};
    const int codes[4] = {a.shift, a.ctrl, a.alt, a.ctrl_shift};
    for (int i = 0; i < 4; i++) {
      if (!codes[i]) continue;
      snprintf(buf, sizeof(buf), "\033[1;%d%c", mods[i], a.final_ch);
      bind(buf, codes[i]);
      // Alguns terminais mandam a forma "curta" ESC[<mod><letra>.
      snprintf(buf, sizeof(buf), "\033[%d%c", mods[i], a.final_ch);
      bind(buf, codes[i]);
    }
  }
  // Alt+Shift+setas verticais (mover linha).
  bind("\033[1;4A", K_ALT_SHIFT_UP);
  bind("\033[1;4B", K_ALT_SHIFT_DOWN);

  // Home/End tambem aparecem como ESC[7~ / ESC[8~ (rxvt) e ESC O H/F.
  bind("\033[1;2~", K_SHIFT_HOME);
  bind("\033[5;2~", K_SHIFT_PGUP);
  bind("\033[6;2~", K_SHIFT_PGDN);
  bind("\033[5;5~", K_CTRL_PGUP);
  bind("\033[6;5~", K_CTRL_PGDN);
  bind("\033[3;5~", K_CTRL_DEL);
  bind("\033[Z", K_SHIFT_TAB);
  bind("\033[200~", K_PASTE_BEGIN);
  bind("\033[201~", K_PASTE_END);
}

// Em terminais de 8 cores nao da para reproduzir uma paleta: usamos um
// esquema simples e legivel para qualquer tema escolhido.
void apply_basic_theme() {
  pair(kNormal, -1, -1);
  pair(kStatus, COLOR_WHITE, COLOR_BLUE);
  pair(kStatusKey, COLOR_YELLOW, COLOR_BLUE);
  pair(kSidebar, COLOR_WHITE, COLOR_BLACK);
  pair(kSidebarDir, COLOR_CYAN, COLOR_BLACK);
  pair(kSidebarSel, COLOR_BLACK, COLOR_CYAN);
  pair(kSidebarSelInactive, COLOR_WHITE, COLOR_BLACK);
  pair(kTabBar, COLOR_WHITE, COLOR_BLACK);
  pair(kTabActive, COLOR_WHITE, COLOR_BLUE);
  pair(kTabActiveDim, COLOR_CYAN, COLOR_BLACK);
  pair(kTabModified, COLOR_YELLOW, COLOR_BLACK);
  pair(kLineNo, COLOR_BLUE, -1);
  pair(kLineNoCur, COLOR_WHITE, -1);
  pair(kSelection, COLOR_WHITE, COLOR_BLUE);
  pair(kSearchHit, COLOR_BLACK, COLOR_YELLOW);
  pair(kDialog, COLOR_WHITE, COLOR_BLUE);
  pair(kPaneTitle, COLOR_WHITE, COLOR_BLACK);
  pair(kPaneTitleActive, COLOR_WHITE, COLOR_BLUE);
  pair(kSynKeyword, COLOR_MAGENTA, -1);
  pair(kSynType, COLOR_CYAN, -1);
  pair(kSynString, COLOR_GREEN, -1);
  pair(kSynComment, COLOR_BLUE, -1);
  pair(kSynNumber, COLOR_YELLOW, -1);
  pair(kSynPreproc, COLOR_YELLOW, -1);
  pair(kSynOperator, COLOR_WHITE, -1);
}

}  // namespace

void apply_theme(const Theme& t) {
  if (!has_colors()) return;
  if (COLORS < 256) {
    apply_basic_theme();
    return;
  }
  auto c = [](Color x) { return x == kDefaultColor ? -1 : rgb_to_256(x); };
  const int bg = c(t.bg), fg = c(t.fg);
  const int dim = c(t.fg_dim);
  const int accent = c(t.accent), accent_fg = c(t.accent_fg);

  // Paletas costumam usar tons de fundo muito proximos (no Rose Pine, o fundo
  // do editor e o da barra lateral diferem em 6 pontos de brilho). Depois de
  // reduzir para 256 cores eles podem virar o mesmo indice, e a divisao entre
  // os paineis desapareceria - por isso o empurrao abaixo.
  const int bg_alt = shade_apart(t.bg_alt, t.fg, bg);
  const int bg_sel = shade_apart(t.bg_sel, t.fg, bg);

  // Na maioria das paletas o comentario e o texto apagado sao a mesma cor.
  // Como os numeros de linha aparecem em toda linha, eles vao um pouco na
  // direcao do fundo para ficarem mais discretos que os comentarios.
  const int lineno = shade_apart(t.fg_dim, t.bg, c(t.comment));

  pair(kNormal, fg, bg);
  pair(kStatus, accent_fg, accent);
  pair(kStatusKey, c(t.modified), accent);
  pair(kSidebar, fg, bg_alt);
  pair(kSidebarDir, c(t.accent2), bg_alt);
  pair(kSidebarSel, accent_fg, accent);
  pair(kSidebarSelInactive, fg, bg_sel);
  pair(kTabBar, dim, bg_alt);
  pair(kTabActive, accent_fg, accent);
  // A aba ativa de um painel sem foco: a cor de destaque vira *letra*, em vez
  // de fundo. Da para ver que arquivo cada painel mostra sem competir com o
  // painel que esta com o foco.
  pair(kTabActiveDim, accent, bg_alt);
  pair(kTabModified, c(t.modified), bg_alt);
  pair(kLineNo, lineno, bg);
  pair(kLineNoCur, fg, bg);
  pair(kSelection, fg, bg_sel);
  pair(kSearchHit, c(t.search_fg), c(t.search_bg));
  pair(kDialog, fg, bg_sel);
  pair(kPaneTitle, dim, bg_alt);
  pair(kPaneTitleActive, accent_fg, accent);

  pair(kSynKeyword, c(t.keyword), bg);
  pair(kSynType, c(t.type), bg);
  pair(kSynString, c(t.string), bg);
  pair(kSynComment, c(t.comment), bg);
  pair(kSynNumber, c(t.number), bg);
  pair(kSynPreproc, c(t.preproc), bg);
  pair(kSynOperator, c(t.punct), bg);

  // Faz o fundo do tema valer para a tela inteira (inclusive o que o erase()
  // limpa a cada redesenho).
  bkgd(' ' | COLOR_PAIR(kNormal));
}

bool init() {
  std::setlocale(LC_ALL, "");
  if (!initscr()) return false;

  raw();                  // entrega Ctrl+C, Ctrl+S, Ctrl+Z, Ctrl+Q para nos
  noecho();
  nonl();
  keypad(stdscr, TRUE);
  meta(stdscr, TRUE);
  curs_set(1);
  set_escdelay(25);
  timeout(20);            // loop de eventos nao bloqueante (20 ms)

  if (has_colors()) {
    start_color();
    use_default_colors();
    apply_theme(theme_by_name(g_config.theme));
  }

  bind_modified_keys();

  // O mouse tem dois caminhos possiveis, e aceitamos os dois:
  //   - o formato antigo (ESC[M...) o proprio ncurses decodifica;
  //   - o formato SGR (ESC[<...), que varias versoes do ncurses pedem ao
  //     terminal mas nao sabem ler, decodificamos em read_key().
  if (g_config.mouse) {
    mousemask(ALL_MOUSE_EVENTS, nullptr);
    mouseinterval(0);   // o duplo clique quem detecta somos nos
    set_mouse(true);
  }
  set_bracketed_paste(true);
  return true;
}

void shutdown() {
  set_bracketed_paste(false);
  set_mouse(false);
  curs_set(1);
  endwin();
}

void set_mouse(bool on) {
  mousemask(on ? ALL_MOUSE_EVENTS : 0, nullptr);
  // 1000 = clique, 1002 = clique+arrasto, 1006 = coordenadas em formato SGR
  // (necessario para telas com mais de 223 colunas).
  std::printf(on ? "\033[?1000;1002;1006h" : "\033[?1000;1002;1006l");
  std::fflush(stdout);
}

void set_bracketed_paste(bool on) {
  std::printf(on ? "\033[?2004h" : "\033[?2004l");
  std::fflush(stdout);
}

namespace {

// Le um caractere "cru" esperando ate 30 ms (usado no meio de uma sequencia
// de escape, cujos bytes chegam todos juntos).
int read_raw(wint_t* out) {
  timeout(30);
  int r = get_wch(out);
  timeout(20);
  return r;
}

// Preenche 'ev' a partir do byte de botao e da posicao. 'final' e 'M' (aperta)
// ou 'm' (solta), no formato SGR; no formato antigo so existe o aperto.
void fill_mouse(KeyEvent* ev, int b, int x, int y, char final_ch) {
  ev->is_mouse = true;
  ev->mx = x;
  ev->my = y;
  if (b & 64) {   // roda do mouse
    ev->wheel_up = ((b & 3) == 0);
    ev->wheel_down = ((b & 3) == 1);
    return;
  }
  ev->button = b & 3;
  ev->drag = (b & 32) != 0;
  if (final_ch == 'm') ev->release = true;
  else if (ev->drag) ev->drag = true;
  else ev->press = true;
}

// Chamado logo depois de termos lido ESC e '['. Tenta decodificar um evento
// de mouse. Devolve false se nao era um (nesse caso a tecla e descartada).
bool parse_mouse(KeyEvent* ev) {
  wint_t c = 0;
  if (read_raw(&c) == ERR) return false;

  if (c == '<') {                     // formato SGR: ESC[<b;x;yM
    std::string buf;
    while (buf.size() < 32) {
      if (read_raw(&c) == ERR) return false;
      if (c == 'M' || c == 'm') break;
      buf += static_cast<char>(c);
    }
    int b = 0, x = 0, y = 0;
    if (sscanf(buf.c_str(), "%d;%d;%d", &b, &x, &y) != 3) return false;
    fill_mouse(ev, b, x - 1, y - 1, static_cast<char>(c));
    return true;
  }
  if (c == 'M') {                     // formato antigo: ESC[M b x y
    wint_t b = 0, x = 0, y = 0;
    if (read_raw(&b) == ERR || read_raw(&x) == ERR || read_raw(&y) == ERR)
      return false;
    fill_mouse(ev, static_cast<int>(b) - 32, static_cast<int>(x) - 33,
               static_cast<int>(y) - 33, 'M');
    return true;
  }
  return false;
}

}  // namespace

bool read_key(KeyEvent* ev) {
  *ev = KeyEvent{};
  wint_t wch = 0;
  int r = get_wch(&wch);
  if (r == ERR) return false;
  ev->is_code = (r == KEY_CODE_YES);
  ev->ch = wch;

  // Caminho 1: o ncurses decodificou o evento de mouse por nos.
  if (ev->is_code && static_cast<int>(wch) == KEY_MOUSE) {
    MEVENT me;
    if (getmouse(&me) != OK) return false;
    ev->is_code = false;
    ev->is_mouse = true;
    ev->mx = me.x;
    ev->my = me.y;
    if (me.bstate & BUTTON4_PRESSED) ev->wheel_up = true;
    else if (me.bstate & BUTTON5_PRESSED) ev->wheel_down = true;
    else if (me.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED |
                          BUTTON1_DOUBLE_CLICKED)) ev->press = true;
    else if (me.bstate & BUTTON1_RELEASED) ev->release = true;
    else if (me.bstate & (BUTTON2_PRESSED | BUTTON3_PRESSED)) {
      ev->press = true;
      ev->button = (me.bstate & BUTTON3_PRESSED) ? 2 : 1;
    } else {
      ev->drag = true;
    }
    return true;
  }

  // ESC seguido imediatamente de outra tecla = Alt+tecla (ou mouse).
  if (!ev->is_code && wch == 27) {
    nodelay(stdscr, TRUE);
    wint_t w2 = 0;
    int r2 = get_wch(&w2);
    nodelay(stdscr, FALSE);
    timeout(20);
    if (r2 == ERR) return true;       // Esc sozinho
    if (r2 == OK && w2 == '[') {
      KeyEvent m;
      if (parse_mouse(&m)) {
        *ev = m;
        return true;
      }
      return false;                   // sequencia desconhecida: ignora
    }
    ev->is_code = (r2 == KEY_CODE_YES);
    ev->ch = w2;
    ev->alt = true;
  }
  return true;
}

int terminal_pair(int fg, int bg) {
  if (!has_colors()) return 0;
  int key = (fg + 1) * 257 + (bg + 1);
  auto it = g_dyn_pairs.find(key);
  if (it != g_dyn_pairs.end()) return it->second;
  if (g_next_pair >= COLOR_PAIRS) return 0;  // acabaram os pares: cor padrao
  int id = g_next_pair++;
  init_pair(static_cast<short>(id), fg, bg);
  g_dyn_pairs[key] = id;
  return id;
}

void reset_dynamic_pairs() {
  g_dyn_pairs.clear();
  g_next_pair = kDynamicPairStart;
}

int put(int y, int x, int max_w, const std::string& text) {
  if (max_w <= 0) return 0;
  int used = 0;
  size_t i = 0;
  std::string out;
  while (i < text.size()) {
    uint32_t cp = utf8::decode(text, i);
    int w = utf8::cp_width(cp);
    if (cp < 32) w = 1;
    if (used + w > max_w) break;
    size_t len = utf8::char_len(text, i);
    if (cp < 32 || cp == 127) out += ' ';
    else out.append(text, i, len);
    used += w;
    i += len;
  }
  mvaddstr(y, x, out.c_str());
  return used;
}

void fill(int y, int x, int w) {
  if (w <= 0) return;
  std::string spaces(static_cast<size_t>(w), ' ');
  mvaddstr(y, x, spaces.c_str());
}

}  // namespace ui
