// terminal.hpp - o painel de terminal embutido.
//
// Duas coisas acontecem aqui:
//   1) abrimos um pseudo-terminal (PTY) e rodamos o shell do usuario nele;
//   2) interpretamos os bytes que o shell devolve (texto + sequencias de
//      escape ANSI/VT100) montando uma "tela" de celulas que desenhamos.
// E um emulador de terminal pequeno, mas suficiente para bash, ls colorido,
// make, gcc, python, htop simples, etc.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include "ui.hpp"

class Terminal {
 public:
  struct Cell {
    std::string ch = " ";   // um caractere UTF-8 (pode ser vazio p/ metade larga)
    short fg = -1;          // -1 = cor padrao, 0..255 = paleta
    short bg = -1;
    unsigned char attrs = 0;
  };
  enum Attr { kBold = 1, kUnderline = 2, kReverse = 4, kDim = 8, kItalic = 16 };

  Terminal();
  ~Terminal();

  bool start(const std::string& cwd, int cols, int rows, std::string* error);
  void stop();
  bool running() const { return pid_ > 0; }
  int fd() const { return master_; }
  int exit_status() const { return exit_status_; }

  // Le tudo que o shell escreveu (nao bloqueia). Devolve true se algo mudou.
  bool poll_output();

  void resize(int cols, int rows);
  void send_key(const ui::KeyEvent& ev);
  void send_text(const std::string& text, bool paste = false);
  void send_bytes(const std::string& bytes);

  void draw(const Rect& area, bool focused);
  bool cursor_screen(int* x, int* y) const;
  void scroll_view(int lines);   // rolar o historico
  void clear_view_scroll() { view_offset_ = 0; }
  int view_offset() const { return view_offset_; }

  int cols() const { return cols_; }
  int rows() const { return rows_; }

 private:
  void feed(const char* data, size_t len);
  void process_byte(unsigned char c);
  void put_codepoint(uint32_t cp);
  void execute_control(unsigned char c);
  void dispatch_csi(unsigned char final_ch);
  void dispatch_esc(unsigned char c);
  void apply_sgr();
  void set_mode(bool set);

  void scroll_region_up(int n);
  void scroll_region_down(int n);
  void line_feed();
  void erase_display(int mode);
  void erase_line(int mode);
  void insert_lines(int n);
  void delete_lines(int n);
  void insert_chars(int n);
  void delete_chars(int n);
  void erase_chars(int n);
  void resize_screen(std::vector<std::vector<Cell>>* screen, int cols, int rows);
  void use_alt_screen(bool alt);
  Cell blank_cell() const;
  int param(size_t i, int def) const;

  // --- processo -----------------------------------------------------------
  int master_ = -1;
  int pid_ = -1;
  int exit_status_ = 0;
  std::string pending_out_;   // bytes ainda nao entregues ao shell

  // --- tela ---------------------------------------------------------------
  int cols_ = 80, rows_ = 24;
  std::vector<std::vector<Cell>> screen_;
  std::vector<std::vector<Cell>> alt_;
  std::deque<std::vector<Cell>> scrollback_;
  bool in_alt_ = false;

  int cx_ = 0, cy_ = 0;
  int saved_cx_ = 0, saved_cy_ = 0;
  int scroll_top_ = 0, scroll_bot_ = 23;
  bool autowrap_ = true;
  bool wrap_pending_ = false;
  bool cursor_visible_ = true;
  bool app_cursor_keys_ = false;
  bool bracketed_paste_ = false;
  int view_offset_ = 0;       // quantas linhas de historico estamos subindo

  // --- atributos atuais ---------------------------------------------------
  short fg_ = -1, bg_ = -1;
  unsigned char attrs_ = 0;
  short saved_fg_ = -1, saved_bg_ = -1;
  unsigned char saved_attrs_ = 0;

  // --- parser -------------------------------------------------------------
  enum class PState { Ground, Esc, Csi, Osc, OscEsc, Charset, Dcs, DcsEsc };
  PState pstate_ = PState::Ground;
  std::vector<int> params_;
  std::string param_buf_;
  std::string intermediates_;
  std::string osc_buf_;
  std::string utf8_buf_;
  int utf8_need_ = 0;

  int cursor_x_ = -1, cursor_y_ = -1;   // posicao na tela real (ultimo draw)
};
