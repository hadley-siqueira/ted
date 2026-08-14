#include "terminal.hpp"

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "theme.hpp"
#include "utf8.hpp"

namespace {
constexpr size_t kScrollbackMax = 3000;
}

Terminal::Terminal() {
  screen_.assign(rows_, std::vector<Cell>(cols_));
  alt_.assign(rows_, std::vector<Cell>(cols_));
  scroll_bot_ = rows_ - 1;
}

Terminal::~Terminal() { stop(); }

Terminal::Cell Terminal::blank_cell() const {
  Cell c;
  c.ch = " ";
  c.fg = -1;
  c.bg = bg_;   // o fundo atual "pinta" o espaco (usado por ls, vim, etc.)
  c.attrs = 0;
  return c;
}

// ---------------------------------------------------------------------------
// Processo filho
// ---------------------------------------------------------------------------

bool Terminal::start(const std::string& cwd, int cols, int rows,
                     std::string* error) {
  if (running()) return true;
  cols_ = std::max(2, cols);
  rows_ = std::max(2, rows);
  screen_.assign(rows_, std::vector<Cell>(cols_));
  alt_.assign(rows_, std::vector<Cell>(cols_));
  scrollback_.clear();
  cx_ = cy_ = 0;
  scroll_top_ = 0;
  scroll_bot_ = rows_ - 1;
  in_alt_ = false;
  view_offset_ = 0;
  exit_status_ = 0;

  struct winsize ws;
  std::memset(&ws, 0, sizeof(ws));
  ws.ws_col = static_cast<unsigned short>(cols_);
  ws.ws_row = static_cast<unsigned short>(rows_);

  int master = -1;
  pid_t pid = forkpty(&master, nullptr, nullptr, &ws);
  if (pid < 0) {
    if (error) *error = std::string("forkpty falhou: ") + strerror(errno);
    return false;
  }
  if (pid == 0) {
    // --- processo filho: vira o shell ---
    if (!cwd.empty()) { if (chdir(cwd.c_str()) != 0) { /* segue mesmo assim */ } }
    setenv("TERM", "xterm-256color", 1);
    setenv("COLORTERM", "truecolor", 1);
    setenv("TED", "1", 1);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);

    const char* shell = getenv("SHELL");
    if (!shell || !*shell) shell = "/bin/bash";
    execl(shell, shell, "-i", nullptr);
    execl("/bin/sh", "sh", nullptr);
    _exit(127);
  }

  master_ = master;
  pid_ = pid;
  int flags = fcntl(master_, F_GETFL, 0);
  fcntl(master_, F_SETFL, flags | O_NONBLOCK);
  return true;
}

void Terminal::stop() {
  if (pid_ > 0) {
    ::kill(pid_, SIGHUP);
    int status = 0;
    for (int i = 0; i < 20; i++) {
      pid_t r = waitpid(pid_, &status, WNOHANG);
      if (r == pid_ || r < 0) break;
      usleep(5000);
    }
    pid_ = -1;
  }
  if (master_ >= 0) {
    close(master_);
    master_ = -1;
  }
}

bool Terminal::poll_output() {
  if (master_ < 0) return false;

  // Primeiro tenta esvaziar o que esta esperando para ser enviado ao shell.
  if (!pending_out_.empty()) {
    ssize_t n = ::write(master_, pending_out_.data(), pending_out_.size());
    if (n > 0) pending_out_.erase(0, static_cast<size_t>(n));
  }

  bool changed = false;
  char buf[8192];
  for (int i = 0; i < 64; i++) {   // limite por rodada: mantem a UI responsiva
    ssize_t n = ::read(master_, buf, sizeof(buf));
    if (n > 0) {
      feed(buf, static_cast<size_t>(n));
      changed = true;
      if (n < static_cast<ssize_t>(sizeof(buf))) break;
      continue;
    }
    if (n == 0) {                  // shell fechou
      int status = 0;
      if (pid_ > 0 && waitpid(pid_, &status, WNOHANG) == pid_) {
        exit_status_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        pid_ = -1;
      }
      close(master_);
      master_ = -1;
      if (pid_ > 0) pid_ = -1;
      return true;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    if (errno == EINTR) continue;
    // Erro real (normalmente EIO quando o filho morre).
    int status = 0;
    if (pid_ > 0 && waitpid(pid_, &status, WNOHANG) == pid_) {
      exit_status_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    pid_ = -1;
    close(master_);
    master_ = -1;
    return true;
  }
  if (changed) view_offset_ = 0;   // saida nova traz a visao para o fim
  return changed;
}

void Terminal::resize(int cols, int rows) {
  cols = std::max(2, cols);
  rows = std::max(2, rows);
  if (cols == cols_ && rows == rows_) return;

  resize_screen(&screen_, cols, rows);
  resize_screen(&alt_, cols, rows);
  cols_ = cols;
  rows_ = rows;
  scroll_top_ = 0;
  scroll_bot_ = rows_ - 1;
  cx_ = std::max(0, std::min(cx_, cols_ - 1));
  cy_ = std::max(0, std::min(cy_, rows_ - 1));

  if (master_ >= 0) {
    struct winsize ws;
    std::memset(&ws, 0, sizeof(ws));
    ws.ws_col = static_cast<unsigned short>(cols_);
    ws.ws_row = static_cast<unsigned short>(rows_);
    ioctl(master_, TIOCSWINSZ, &ws);
    if (pid_ > 0) ::kill(pid_, SIGWINCH);
  }
}

void Terminal::resize_screen(std::vector<std::vector<Cell>>* screen, int cols,
                             int rows) {
  std::vector<std::vector<Cell>> out(rows, std::vector<Cell>(cols));
  int copy_rows = std::min<int>(rows, static_cast<int>(screen->size()));
  // Mantem as ultimas linhas (o que esta perto do prompt e o que importa).
  int src_start = static_cast<int>(screen->size()) - copy_rows;
  for (int r = 0; r < copy_rows; r++) {
    const auto& src = (*screen)[src_start + r];
    int copy_cols = std::min<int>(cols, static_cast<int>(src.size()));
    for (int c = 0; c < copy_cols; c++) out[r][c] = src[c];
  }
  // O cursor acompanha as linhas preservadas. Se ele estava acima delas (o
  // painel encolheu e a linha do cursor foi descartada), ele gruda no topo -
  // sem o piso aqui, cy_ ficaria negativo e o proximo ESC[K acessaria
  // screen_[-1].
  if (screen == &screen_) cy_ = std::max(0, cy_ - src_start);
  *screen = std::move(out);
}

// ---------------------------------------------------------------------------
// Envio de teclas
// ---------------------------------------------------------------------------

void Terminal::send_bytes(const std::string& bytes) {
  if (master_ < 0) return;
  pending_out_ += bytes;
  ssize_t n = ::write(master_, pending_out_.data(), pending_out_.size());
  if (n > 0) pending_out_.erase(0, static_cast<size_t>(n));
  view_offset_ = 0;
}

void Terminal::send_text(const std::string& text, bool paste) {
  if (paste && bracketed_paste_)
    send_bytes("\033[200~" + text + "\033[201~");
  else
    send_bytes(text);
}

void Terminal::send_key(const ui::KeyEvent& ev) {
  std::string seq;
  const char* ss3 = app_cursor_keys_ ? "\033O" : "\033[";

  if (ev.is_code) {
    switch (static_cast<int>(ev.ch)) {
      case KEY_UP: seq = std::string(ss3) + "A"; break;
      case KEY_DOWN: seq = std::string(ss3) + "B"; break;
      case KEY_RIGHT: seq = std::string(ss3) + "C"; break;
      case KEY_LEFT: seq = std::string(ss3) + "D"; break;
      case KEY_HOME: seq = "\033OH"; break;
      case KEY_END: seq = "\033OF"; break;
      case KEY_PPAGE: seq = "\033[5~"; break;
      case KEY_NPAGE: seq = "\033[6~"; break;
      case KEY_IC: seq = "\033[2~"; break;
      case KEY_DC: seq = "\033[3~"; break;
      case KEY_BACKSPACE: seq = "\177"; break;
      case KEY_ENTER: seq = "\r"; break;
      case KEY_BTAB: case ui::K_SHIFT_TAB: seq = "\033[Z"; break;
      case ui::K_CTRL_LEFT: seq = "\033[1;5D"; break;
      case ui::K_CTRL_RIGHT: seq = "\033[1;5C"; break;
      case ui::K_CTRL_UP: seq = "\033[1;5A"; break;
      case ui::K_CTRL_DOWN: seq = "\033[1;5B"; break;
      case ui::K_CTRL_DEL: seq = "\033[3;5~"; break;
      case ui::K_SHIFT_HOME: seq = "\033[1;2H"; break;
      case ui::K_SHIFT_END: seq = "\033[1;2F"; break;
      default: return;
    }
  } else {
    if (ev.ch == '\n' || ev.ch == '\r') seq = "\r";
    else if (ev.ch == 127 || ev.ch == 8) seq = "\177";
    else seq = utf8::encode(static_cast<uint32_t>(ev.ch));
    if (ev.alt) seq = "\033" + seq;
  }
  send_bytes(seq);
}

// ---------------------------------------------------------------------------
// Emulador: entrada de bytes
// ---------------------------------------------------------------------------

void Terminal::feed(const char* data, size_t len) {
  for (size_t i = 0; i < len; i++)
    process_byte(static_cast<unsigned char>(data[i]));
}

void Terminal::process_byte(unsigned char c) {
  switch (pstate_) {
    case PState::Ground:
      break;

    case PState::Esc:
      dispatch_esc(c);
      return;

    case PState::Csi:
      if (c >= '0' && c <= '9') { param_buf_ += static_cast<char>(c); return; }
      if (c == ';') {
        params_.push_back(param_buf_.empty() ? -1 : std::atoi(param_buf_.c_str()));
        param_buf_.clear();
        return;
      }
      if (c == '?' || c == '>' || c == '!' || c == '$' || c == '"' || c == '\'' ||
          c == ' ' || c == '*') {
        intermediates_ += static_cast<char>(c);
        return;
      }
      if (c >= 0x40 && c <= 0x7E) {
        params_.push_back(param_buf_.empty() ? -1 : std::atoi(param_buf_.c_str()));
        param_buf_.clear();
        dispatch_csi(c);
        pstate_ = PState::Ground;
        return;
      }
      if (c < 0x20) { execute_control(c); return; }
      return;

    case PState::Osc:
      if (c == 0x07) { pstate_ = PState::Ground; osc_buf_.clear(); return; }
      if (c == 0x1B) { pstate_ = PState::OscEsc; return; }
      osc_buf_ += static_cast<char>(c);
      if (osc_buf_.size() > 4096) osc_buf_.clear();
      return;

    case PState::OscEsc:
      // ESC \ termina o OSC; qualquer outra coisa volta ao normal.
      pstate_ = PState::Ground;
      osc_buf_.clear();
      return;

    case PState::Dcs:
      if (c == 0x1B) { pstate_ = PState::DcsEsc; return; }
      return;
    case PState::DcsEsc:
      pstate_ = PState::Ground;
      return;

    case PState::Charset:
      pstate_ = PState::Ground;   // ignora a designacao de charset
      return;
  }

  // --- estado Ground ---
  if (c == 0x1B) {
    pstate_ = PState::Esc;
    intermediates_.clear();
    params_.clear();
    param_buf_.clear();
    utf8_buf_.clear();
    utf8_need_ = 0;
    return;
  }
  if (c < 0x20 || c == 0x7F) {
    execute_control(c);
    return;
  }

  // --- UTF-8 ---
  if (utf8_need_ > 0) {
    if ((c & 0xC0) == 0x80) {
      utf8_buf_ += static_cast<char>(c);
      if (--utf8_need_ == 0) {
        put_codepoint(utf8::decode(utf8_buf_, 0));
        utf8_buf_.clear();
      }
      return;
    }
    utf8_buf_.clear();
    utf8_need_ = 0;
  }
  if (c < 0x80) {
    put_codepoint(c);
  } else if ((c & 0xE0) == 0xC0) {
    utf8_buf_ = std::string(1, static_cast<char>(c));
    utf8_need_ = 1;
  } else if ((c & 0xF0) == 0xE0) {
    utf8_buf_ = std::string(1, static_cast<char>(c));
    utf8_need_ = 2;
  } else if ((c & 0xF8) == 0xF0) {
    utf8_buf_ = std::string(1, static_cast<char>(c));
    utf8_need_ = 3;
  }
}

void Terminal::execute_control(unsigned char c) {
  switch (c) {
    case '\r': cx_ = 0; wrap_pending_ = false; break;
    case '\n': case 0x0B: case 0x0C: line_feed(); break;
    case '\b':
      if (wrap_pending_) wrap_pending_ = false;
      else if (cx_ > 0) cx_--;
      break;
    case '\t': {
      int next = ((cx_ / 8) + 1) * 8;
      cx_ = std::min(next, cols_ - 1);
      break;
    }
    case 0x07: break;   // BEL: silencioso
    default: break;
  }
}

void Terminal::put_codepoint(uint32_t cp) {
  int w = utf8::cp_width(cp);
  if (w <= 0) return;   // combinantes: ignorados (simplificacao)

  if (wrap_pending_ && autowrap_) {
    cx_ = 0;
    line_feed();
    wrap_pending_ = false;
  }
  if (cx_ + w > cols_) {
    if (autowrap_) {
      cx_ = 0;
      line_feed();
    } else {
      cx_ = cols_ - w;
    }
  }
  if (cy_ < 0) cy_ = 0;
  if (cy_ >= rows_) cy_ = rows_ - 1;

  auto& row = row_at(cy_);
  Cell cell;
  cell.ch = utf8::encode(cp);
  cell.fg = fg_;
  cell.bg = bg_;
  cell.attrs = attrs_;
  row[cx_] = cell;
  if (w == 2 && cx_ + 1 < cols_) {
    Cell pad = cell;
    pad.ch.clear();   // metade direita de um caractere largo
    row[cx_ + 1] = pad;
  }
  cx_ += w;
  if (cx_ >= cols_) {
    cx_ = cols_ - 1;
    wrap_pending_ = true;
  }
}

void Terminal::line_feed() {
  wrap_pending_ = false;
  if (cy_ == scroll_bot_) {
    scroll_region_up(1);
  } else if (cy_ < rows_ - 1) {
    cy_++;
  }
}

void Terminal::scroll_region_up(int n) {
  for (int k = 0; k < n; k++) {
    // So guarda no historico quando a regiao e a tela toda (comportamento
    // esperado de um terminal comum) e nao estamos na tela alternativa.
    if (!in_alt_ && scroll_top_ == 0 && scroll_bot_ == rows_ - 1) {
      scrollback_.push_back(screen_[0]);
      if (scrollback_.size() > kScrollbackMax) scrollback_.pop_front();
    }
    for (int r = scroll_top_; r < scroll_bot_; r++)
      screen_[r] = screen_[r + 1];
    screen_[scroll_bot_].assign(cols_, blank_cell());
  }
}

void Terminal::scroll_region_down(int n) {
  for (int k = 0; k < n; k++) {
    for (int r = scroll_bot_; r > scroll_top_; r--) screen_[r] = screen_[r - 1];
    screen_[scroll_top_].assign(cols_, blank_cell());
  }
}

// ---------------------------------------------------------------------------
// Emulador: sequencias de escape
// ---------------------------------------------------------------------------

void Terminal::dispatch_esc(unsigned char c) {
  switch (c) {
    case '[':
      pstate_ = PState::Csi;
      params_.clear();
      param_buf_.clear();
      intermediates_.clear();
      return;
    case ']':
      pstate_ = PState::Osc;
      osc_buf_.clear();
      return;
    case 'P': case '^': case '_':
      pstate_ = PState::Dcs;
      return;
    case '(': case ')': case '*': case '+':
      pstate_ = PState::Charset;
      return;
    case 'M':   // Reverse Index
      if (cy_ == scroll_top_) scroll_region_down(1);
      else if (cy_ > 0) cy_--;
      break;
    case 'D':
      line_feed();
      break;
    case 'E':
      cx_ = 0;
      line_feed();
      break;
    case '7':
      saved_cx_ = cx_; saved_cy_ = cy_;
      saved_fg_ = fg_; saved_bg_ = bg_; saved_attrs_ = attrs_;
      break;
    case '8':
      cx_ = std::max(0, std::min(saved_cx_, cols_ - 1));
      cy_ = std::max(0, std::min(saved_cy_, rows_ - 1));
      fg_ = saved_fg_; bg_ = saved_bg_; attrs_ = saved_attrs_;
      break;
    case 'c':   // reset
      screen_.assign(rows_, std::vector<Cell>(cols_));
      cx_ = cy_ = 0;
      fg_ = bg_ = -1;
      attrs_ = 0;
      break;
    case '=': case '>': break;   // keypad modes
    default: break;
  }
  pstate_ = PState::Ground;
}

int Terminal::param(size_t i, int def) const {
  if (i >= params_.size() || params_[i] < 0) return def;
  return params_[i];
}

void Terminal::dispatch_csi(unsigned char final_ch) {
  const bool priv = intermediates_.find('?') != std::string::npos;

  switch (final_ch) {
    case 'A': cy_ = std::max(scroll_top_, cy_ - param(0, 1)); wrap_pending_ = false; break;
    case 'B': cy_ = std::min(rows_ - 1, cy_ + param(0, 1)); wrap_pending_ = false; break;
    case 'C': cx_ = std::min(cols_ - 1, cx_ + param(0, 1)); wrap_pending_ = false; break;
    case 'D': cx_ = std::max(0, cx_ - param(0, 1)); wrap_pending_ = false; break;
    case 'E': cy_ = std::min(rows_ - 1, cy_ + param(0, 1)); cx_ = 0; break;
    case 'F': cy_ = std::max(0, cy_ - param(0, 1)); cx_ = 0; break;
    case 'G': case '`': cx_ = std::min(cols_ - 1, std::max(0, param(0, 1) - 1)); break;
    case 'd': cy_ = std::min(rows_ - 1, std::max(0, param(0, 1) - 1)); break;
    case 'H': case 'f':
      cy_ = std::min(rows_ - 1, std::max(0, param(0, 1) - 1));
      cx_ = std::min(cols_ - 1, std::max(0, param(1, 1) - 1));
      wrap_pending_ = false;
      break;
    case 'J': erase_display(param(0, 0)); break;
    case 'K': erase_line(param(0, 0)); break;
    case 'L': insert_lines(param(0, 1)); break;
    case 'M': delete_lines(param(0, 1)); break;
    case 'P': delete_chars(param(0, 1)); break;
    case '@': insert_chars(param(0, 1)); break;
    case 'X': erase_chars(param(0, 1)); break;
    case 'S': scroll_region_up(param(0, 1)); break;
    case 'T': scroll_region_down(param(0, 1)); break;
    case 'm': apply_sgr(); break;
    case 'h': set_mode(true); break;
    case 'l': set_mode(false); break;
    case 'r':
      scroll_top_ = std::max(0, param(0, 1) - 1);
      scroll_bot_ = std::min(rows_ - 1, param(1, rows_) - 1);
      if (scroll_top_ >= scroll_bot_) { scroll_top_ = 0; scroll_bot_ = rows_ - 1; }
      cx_ = 0;
      cy_ = scroll_top_;
      break;
    case 's': saved_cx_ = cx_; saved_cy_ = cy_; break;
    case 'u':
      cx_ = std::max(0, std::min(saved_cx_, cols_ - 1));
      cy_ = std::max(0, std::min(saved_cy_, rows_ - 1));
      break;
    case 'n':
      if (param(0, 0) == 6) {   // pedido de posicao do cursor
        char buf[32];
        snprintf(buf, sizeof(buf), "\033[%d;%dR", cy_ + 1, cx_ + 1);
        send_bytes(buf);
      }
      break;
    case 'c':
      if (!priv) send_bytes("\033[?1;2c");   // "sou um VT100 com opcoes"
      break;
    default: break;
  }
}

void Terminal::set_mode(bool set) {
  bool priv = intermediates_.find('?') != std::string::npos;
  for (size_t i = 0; i < params_.size(); i++) {
    int p = param(i, 0);
    if (!priv) continue;   // modos ANSI nao-privados: nao usamos
    switch (p) {
      case 1: app_cursor_keys_ = set; break;
      case 7: autowrap_ = set; break;
      case 25: cursor_visible_ = set; break;
      case 1049: case 1047: case 47: use_alt_screen(set); break;
      case 2004: bracketed_paste_ = set; break;
      case 1000: case 1002: case 1003: case 1005: case 1006: case 1015:
        break;   // mouse do programa filho: ignorado
      default: break;
    }
  }
}

void Terminal::use_alt_screen(bool alt) {
  if (alt == in_alt_) return;
  if (alt) {
    saved_cx_ = cx_; saved_cy_ = cy_;
    std::swap(screen_, alt_);
    screen_.assign(rows_, std::vector<Cell>(cols_));
    cx_ = cy_ = 0;
  } else {
    std::swap(screen_, alt_);
    cx_ = std::max(0, std::min(saved_cx_, cols_ - 1));
    cy_ = std::max(0, std::min(saved_cy_, rows_ - 1));
  }
  in_alt_ = alt;
  scroll_top_ = 0;
  scroll_bot_ = rows_ - 1;
  view_offset_ = 0;
}

// Linha da tela com o indice preso ao intervalo valido. Todas as operacoes
// que escrevem em screen_[cy_] passam por aqui: se algum caminho deixar o
// cursor fora da tela, o resultado e um desenho errado - nunca um acesso
// invalido de memoria.
std::vector<Terminal::Cell>& Terminal::row_at(int y) {
  if (y < 0) y = 0;
  if (y >= static_cast<int>(screen_.size())) y = static_cast<int>(screen_.size()) - 1;
  return screen_[y];
}

void Terminal::erase_display(int mode) {
  Cell b = blank_cell();
  if (mode == 0) {
    for (int c = cx_; c < cols_; c++) row_at(cy_)[c] = b;
    for (int r = cy_ + 1; r < rows_; r++) screen_[r].assign(cols_, b);
  } else if (mode == 1) {
    for (int c = 0; c <= cx_ && c < cols_; c++) row_at(cy_)[c] = b;
    for (int r = 0; r < cy_; r++) screen_[r].assign(cols_, b);
  } else {
    for (int r = 0; r < rows_; r++) screen_[r].assign(cols_, b);
  }
}

void Terminal::erase_line(int mode) {
  Cell b = blank_cell();
  if (mode == 0) {
    for (int c = cx_; c < cols_; c++) row_at(cy_)[c] = b;
  } else if (mode == 1) {
    for (int c = 0; c <= cx_ && c < cols_; c++) row_at(cy_)[c] = b;
  } else {
    row_at(cy_).assign(cols_, b);
  }
}

void Terminal::insert_lines(int n) {
  if (cy_ < scroll_top_ || cy_ > scroll_bot_) return;
  for (int k = 0; k < n; k++) {
    for (int r = scroll_bot_; r > cy_; r--) screen_[r] = screen_[r - 1];
    screen_[cy_].assign(cols_, blank_cell());
  }
}

void Terminal::delete_lines(int n) {
  if (cy_ < scroll_top_ || cy_ > scroll_bot_) return;
  for (int k = 0; k < n; k++) {
    for (int r = cy_; r < scroll_bot_; r++) screen_[r] = screen_[r + 1];
    screen_[scroll_bot_].assign(cols_, blank_cell());
  }
}

void Terminal::insert_chars(int n) {
  auto& row = row_at(cy_);
  for (int k = 0; k < n; k++) {
    for (int c = cols_ - 1; c > cx_; c--) row[c] = row[c - 1];
    row[cx_] = blank_cell();
  }
}

void Terminal::delete_chars(int n) {
  auto& row = row_at(cy_);
  for (int k = 0; k < n; k++) {
    for (int c = cx_; c < cols_ - 1; c++) row[c] = row[c + 1];
    row[cols_ - 1] = blank_cell();
  }
}

void Terminal::erase_chars(int n) {
  auto& row = row_at(cy_);
  for (int c = cx_; c < std::min(cols_, cx_ + n); c++) row[c] = blank_cell();
}

void Terminal::apply_sgr() {
  if (params_.empty()) params_.push_back(0);
  for (size_t i = 0; i < params_.size(); i++) {
    int p = param(i, 0);
    switch (p) {
      case 0: fg_ = bg_ = -1; attrs_ = 0; break;
      case 1: attrs_ |= kBold; break;
      case 2: attrs_ |= kDim; break;
      case 3: attrs_ |= kItalic; break;
      case 4: attrs_ |= kUnderline; break;
      case 7: attrs_ |= kReverse; break;
      case 21: case 22: attrs_ &= ~(kBold | kDim); break;
      case 23: attrs_ &= ~kItalic; break;
      case 24: attrs_ &= ~kUnderline; break;
      case 27: attrs_ &= ~kReverse; break;
      case 39: fg_ = -1; break;
      case 49: bg_ = -1; break;
      case 38: case 48: {
        int mode = param(i + 1, 0);
        short color = -1;
        if (mode == 5) {
          color = static_cast<short>(param(i + 2, 0));
          i += 2;
        } else if (mode == 2) {
          int r = param(i + 2, 0), g = param(i + 3, 0), b = param(i + 4, 0);
          i += 4;
          color = static_cast<short>(rgb_to_256(r, g, b));
        }
        if (p == 38) fg_ = color; else bg_ = color;
        break;
      }
      default:
        if (p >= 30 && p <= 37) fg_ = static_cast<short>(p - 30);
        else if (p >= 40 && p <= 47) bg_ = static_cast<short>(p - 40);
        else if (p >= 90 && p <= 97) fg_ = static_cast<short>(p - 90 + 8);
        else if (p >= 100 && p <= 107) bg_ = static_cast<short>(p - 100 + 8);
        break;
    }
  }
}

// ---------------------------------------------------------------------------
// Desenho
// ---------------------------------------------------------------------------

void Terminal::scroll_view(int lines) {
  view_offset_ -= lines;   // lines < 0 = subir no historico
  int max_off = static_cast<int>(scrollback_.size());
  if (view_offset_ > max_off) view_offset_ = max_off;
  if (view_offset_ < 0) view_offset_ = 0;
}

bool Terminal::cursor_screen(int* x, int* y) const {
  if (cursor_x_ < 0 || !cursor_visible_ || view_offset_ != 0) return false;
  *x = cursor_x_;
  *y = cursor_y_;
  return true;
}

void Terminal::draw(const Rect& area, bool focused) {
  cursor_x_ = cursor_y_ = -1;
  if (area.h <= 0 || area.w <= 0) return;

  // Ajusta o tamanho do PTY ao tamanho do painel.
  if (area.w != cols_ || area.h != rows_) resize(area.w, area.h);

  const int sb = static_cast<int>(scrollback_.size());
  for (int row = 0; row < area.h; row++) {
    int y = area.y + row;
    // Indice logico considerando o historico que estamos olhando.
    int idx = row - view_offset_;
    const std::vector<Cell>* line = nullptr;
    if (idx >= 0) {
      if (idx < rows_) line = &screen_[idx];
    } else {
      int hist = sb + idx;
      if (hist >= 0 && hist < sb) line = &scrollback_[hist];
    }

    attrset(COLOR_PAIR(ui::terminal_pair(-1, -1)));
    ui::fill(y, area.x, area.w);
    if (!line) continue;

    for (int c = 0; c < area.w && c < static_cast<int>(line->size()); c++) {
      const Cell& cell = (*line)[c];
      if (cell.ch.empty()) continue;   // metade direita de caractere largo
      short fg = cell.fg, bg = cell.bg;
      if (cell.attrs & kReverse) std::swap(fg, bg);
      int attr = COLOR_PAIR(ui::terminal_pair(fg, bg));
      if (cell.attrs & kBold) attr |= A_BOLD;
      if (cell.attrs & kUnderline) attr |= A_UNDERLINE;
      if (cell.attrs & kDim) attr |= A_DIM;
      attrset(attr);
      mvaddstr(y, area.x + c, cell.ch.c_str());
    }
  }

  if (view_offset_ == 0 && focused) {
    cursor_x_ = area.x + std::min(cx_, area.w - 1);
    cursor_y_ = area.y + std::min(cy_, area.h - 1);
  }
  attrset(COLOR_PAIR(ui::kNormal));
}
