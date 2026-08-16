// ui.hpp - camada fina sobre o ncurses: inicializacao, cores e teclas.
#pragma once

#include <ncurses.h>

#include <string>

#include "theme.hpp"

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

namespace ui {

// Pares de cor fixos da interface. Os pares a partir de kDynamicPairStart sao
// alocados sob demanda pelo emulador de terminal.
enum ColorPair {
  kNormal = 1,
  kStatus,
  kStatusKey,
  kSidebar,
  kSidebarDir,
  kSidebarSel,
  kSidebarSelInactive,
  kTabBar,
  kTabActive,
  kTabActiveDim,   // aba ativa de um painel que NAO esta com o foco
  kTabModified,
  kLineNo,
  kLineNoCur,
  kSelection,
  kSearchHit,
  kDialog,
  kPaneTitle,
  kPaneTitleActive,
  kSynKeyword,
  kSynType,
  kSynString,
  kSynComment,
  kSynNumber,
  kSynPreproc,
  kSynOperator,
  kDynamicPairStart,
};

// Codigos proprios para teclas com modificadores (ncurses nao tem todos).
enum Key {
  K_CUSTOM_BASE = KEY_MAX + 1,
  K_SHIFT_UP, K_SHIFT_DOWN, K_SHIFT_LEFT, K_SHIFT_RIGHT,
  K_SHIFT_HOME, K_SHIFT_END, K_SHIFT_PGUP, K_SHIFT_PGDN,
  K_CTRL_UP, K_CTRL_DOWN, K_CTRL_LEFT, K_CTRL_RIGHT,
  K_CTRL_HOME, K_CTRL_END, K_CTRL_PGUP, K_CTRL_PGDN, K_CTRL_DEL,
  K_CTRL_SHIFT_UP, K_CTRL_SHIFT_DOWN, K_CTRL_SHIFT_LEFT, K_CTRL_SHIFT_RIGHT,
  K_CTRL_SHIFT_HOME, K_CTRL_SHIFT_END,
  K_ALT_UP, K_ALT_DOWN, K_ALT_LEFT, K_ALT_RIGHT,
  K_ALT_SHIFT_UP, K_ALT_SHIFT_DOWN,
  K_SHIFT_TAB,
  K_PASTE_BEGIN,       // ESC[200~ : inicio de texto colado pelo terminal
  K_PASTE_END,         // ESC[201~ : fim do texto colado
};

// Uma tecla lida do teclado. Ou e um "codigo" do ncurses (setas, F1, ...), ou
// um caractere de verdade (letra, acento, Ctrl+letra vem como 1..26), ou ainda
// um evento de mouse (que decodificamos por conta propria - veja ui.cpp).
struct KeyEvent {
  bool is_code = false;   // true  -> ch e um KEY_* / ui::K_*
  wint_t ch = 0;
  bool alt = false;       // veio precedido de ESC (Alt+tecla)

  // Mouse.
  bool is_mouse = false;
  int mx = 0, my = 0;        // coluna e linha da tela (0-based)
  int button = 0;            // 0 = esquerdo, 1 = meio, 2 = direito
  bool press = false, release = false, drag = false;
  bool wheel_up = false, wheel_down = false;

  bool code(int c) const { return is_code && static_cast<int>(ch) == c; }
  bool chr(wint_t c) const { return !is_code && ch == c; }
  bool ctrl(char letter) const {  // ctrl('S') -> caractere 19
    return !is_code && !alt && !is_mouse &&
           ch == static_cast<wint_t>(letter - 'A' + 1);
  }
};

// Monta os pares de cor a partir de uma paleta (veja theme.hpp).
void apply_theme(const Theme& t);

bool init();
void shutdown();

// Le uma tecla (ou evento de mouse). Devolve false se nada chegou.
bool read_key(KeyEvent* ev);

// Liga/desliga o relato de eventos de mouse pelo terminal.
void set_mouse(bool on);

// Alterna o modo "cru" do terminal para o painel embutido nao brigar com o app.
void set_bracketed_paste(bool on);

// Par de cor para um fundo/frente do emulador de terminal (indices 0-255 ou -1
// para a cor padrao). Reaproveita pares ja alocados.
int terminal_pair(int fg, int bg);
void reset_dynamic_pairs();

// Escreve 'text' em (x, y) limitado a 'max_w' colunas, respeitando UTF-8.
// Retorna quantas colunas foram usadas.
int put(int y, int x, int max_w, const std::string& text);

// Preenche uma faixa horizontal com espacos usando o atributo atual.
void fill(int y, int x, int w);

}  // namespace ui
