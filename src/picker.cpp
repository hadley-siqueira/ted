#include "picker.hpp"

#include <algorithm>

#include "utf8.hpp"

namespace {
constexpr int kMaxRows = 14;    // altura maxima da lista
constexpr int kMaxWidth = 100;  // largura maxima da caixa
}  // namespace

void Picker::open(const std::string& title, const std::string& hint,
                  Filter filter, Accept accept) {
  active_ = true;
  title_ = title;
  hint_ = hint;
  query_.clear();
  filter_ = std::move(filter);
  accept_ = std::move(accept);
  selected_ = 0;
  scroll_ = 0;
  refilter();
}

void Picker::close() {
  active_ = false;
  items_.clear();
  filter_ = nullptr;
  accept_ = nullptr;
  cursor_x_ = cursor_y_ = -1;
}

void Picker::refilter() {
  items_ = filter_ ? filter_(query_) : std::vector<PickerItem>();
  selected_ = 0;
  scroll_ = 0;
}

void Picker::move(int delta) {
  if (items_.empty()) return;
  selected_ += delta;
  if (selected_ < 0) selected_ = 0;
  if (selected_ >= static_cast<int>(items_.size()))
    selected_ = static_cast<int>(items_.size()) - 1;
  if (selected_ < scroll_) scroll_ = selected_;
  if (list_h_ > 0 && selected_ >= scroll_ + list_h_)
    scroll_ = selected_ - list_h_ + 1;
  if (scroll_ < 0) scroll_ = 0;
}

bool Picker::handle_key(const ui::KeyEvent& ev) {
  if (!active_) return false;

  if (ev.is_mouse) {
    if (ev.wheel_up) { move(-3); return true; }
    if (ev.wheel_down) { move(+3); return true; }
    if (ev.press) {
      if (!box_.contains(ev.mx, ev.my)) { close(); return true; }
      int idx = scroll_ + (ev.my - list_y_);
      if (ev.my >= list_y_ && idx >= 0 && idx < static_cast<int>(items_.size())) {
        selected_ = idx;
        PickerItem chosen = items_[selected_];
        Accept cb = accept_;
        close();
        if (cb) cb(chosen);
      }
    }
    return true;
  }

  if (ev.is_code) {
    switch (static_cast<int>(ev.ch)) {
      case KEY_UP: move(-1); return true;
      case KEY_DOWN: move(+1); return true;
      case KEY_PPAGE: move(-std::max(1, list_h_ - 1)); return true;
      case KEY_NPAGE: move(+std::max(1, list_h_ - 1)); return true;
      case KEY_HOME: selected_ = 0; scroll_ = 0; return true;
      case KEY_END: move(static_cast<int>(items_.size())); return true;
      case KEY_BACKSPACE:
        if (!query_.empty()) {
          query_.erase(utf8::prev(query_, query_.size()));
          refilter();
        }
        return true;
      case KEY_ENTER:
        if (!items_.empty()) {
          PickerItem chosen = items_[selected_];
          Accept cb = accept_;
          close();
          if (cb) cb(chosen);
        } else {
          close();
        }
        return true;
      default:
        return true;   // qualquer outra tecla especial nao vaza para o editor
    }
  }

  switch (ev.ch) {
    case 27:            // Esc
      close();
      return true;
    case '\r': case '\n':
      if (!items_.empty()) {
        PickerItem chosen = items_[selected_];
        Accept cb = accept_;
        close();
        if (cb) cb(chosen);
      } else {
        close();
      }
      return true;
    case 8: case 127:
      if (!query_.empty()) {
        query_.erase(utf8::prev(query_, query_.size()));
        refilter();
      }
      return true;
    case 21:            // Ctrl+U limpa a consulta
      query_.clear();
      refilter();
      return true;
    case 14: move(+1); return true;   // Ctrl+N
    case 16: move(-1); return true;   // Ctrl+P
    default:
      if (ev.ch >= 32 && !ev.alt) {
        query_ += utf8::encode(static_cast<uint32_t>(ev.ch));
        refilter();
      }
      return true;
  }
}

bool Picker::cursor_screen(int* x, int* y) const {
  if (!active_ || cursor_x_ < 0) return false;
  *x = cursor_x_;
  *y = cursor_y_;
  return true;
}

void Picker::draw(int screen_w, int screen_h) {
  if (!active_) return;

  const int w = std::min(std::max(40, screen_w - 8), kMaxWidth);
  // Altura fixa: se ela acompanhasse o numero de resultados, a caixa mudaria
  // de tamanho e de lugar a cada tecla digitada.
  const int rows = std::max(3, std::min(kMaxRows, screen_h - 8));
  const int h = rows + 4;   // borda + titulo + consulta + borda
  const int x = std::max(0, (screen_w - w) / 2);
  const int y = std::max(0, (screen_h - h) / 3);   // um pouco acima do centro
  box_ = Rect{x, y, w, h};
  list_y_ = y + 3;
  list_h_ = rows;

  attrset(COLOR_PAIR(ui::kDialog));
  for (int r = 0; r < h; r++) ui::fill(y + r, x, w);
  std::string bar;
  for (int i = 0; i < w - 2; i++) bar += "─";
  ui::put(y, x, w, "┌" + bar + "┐");
  ui::put(y + h - 1, x, w, "└" + bar + "┘");
  for (int r = 1; r < h - 1; r++) {
    ui::put(y + r, x, 1, "│");
    ui::put(y + r, x + w - 1, 1, "│");
  }

  // --- titulo e contador ---
  attrset(COLOR_PAIR(ui::kDialog) | A_BOLD);
  ui::put(y, x + 2, w - 4, " " + title_ + " ");
  std::string count = items_.empty()
                          ? (query_.empty() ? std::string() : " nada encontrado ")
                          : " " + std::to_string(items_.size()) +
                                (items_.size() == 1 ? " resultado " : " resultados ");
  if (!count.empty()) {
    int cw = utf8::width(count, 4);
    if (cw < w - 6) ui::put(y, x + w - cw - 2, cw, count);
  }

  // --- campo de busca ---
  attrset(COLOR_PAIR(ui::kDialog) | A_BOLD);
  ui::put(y + 1, x + 2, 2, "> ");
  attrset(COLOR_PAIR(ui::kDialog));
  int qw = ui::put(y + 1, x + 4, w - 6, query_);
  cursor_x_ = std::min(x + 4 + qw, x + w - 2);
  cursor_y_ = y + 1;
  if (query_.empty() && !hint_.empty()) {
    attrset(COLOR_PAIR(ui::kPaneTitle));
    ui::put(y + 1, x + 4, w - 6, hint_);
  }
  attrset(COLOR_PAIR(ui::kDialog));
  ui::put(y + 2, x + 1, w - 2, std::string(static_cast<size_t>(w - 2), ' '));

  // --- lista ---
  for (int r = 0; r < rows; r++) {
    const int idx = scroll_ + r;
    const int line_y = list_y_ + r;
    const bool sel = (idx == selected_);
    attrset(COLOR_PAIR(sel ? ui::kSidebarSel : ui::kDialog));
    ui::fill(line_y, x + 1, w - 2);
    if (idx >= static_cast<int>(items_.size())) continue;

    const PickerItem& item = items_[idx];
    const int text_x = x + 2;
    const int text_w = w - 4;

    // Desenha caractere a caractere para poder destacar o que casou.
    int col = 0;
    size_t i = 0;
    while (i < item.label.size() && col < text_w) {
      const size_t len = utf8::char_len(item.label, i);
      const uint32_t cp = utf8::decode(item.label, i);
      const int cw = (cp < 32) ? 1 : utf8::cp_width(cp);
      const bool hit =
          std::find(item.match.begin(), item.match.end(), i) != item.match.end();
      if (col + cw > text_w) break;
      attrset(COLOR_PAIR(sel ? ui::kSidebarSel : ui::kDialog) |
              (hit ? A_BOLD : 0));
      if (hit && !sel) attrset(COLOR_PAIR(ui::kSearchHit) | A_BOLD);
      std::string ch = (cp < 32) ? " " : item.label.substr(i, len);
      mvaddstr(line_y, text_x + col, ch.c_str());
      col += cw;
      i += len;
    }
    if (sel) {
      attrset(COLOR_PAIR(ui::kSidebarSel) | A_BOLD);
      ui::put(line_y, x + 1, 1, "›");
    }
  }
  attrset(COLOR_PAIR(ui::kNormal));
}
