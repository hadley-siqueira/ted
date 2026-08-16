#include "editorview.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "config.hpp"
#include "utf8.hpp"

namespace {

// Classes de caractere usadas no movimento por palavra (Ctrl+seta).
enum class CharClass { Space, Word, Symbol };

CharClass classify(uint32_t cp) {
  if (cp == ' ' || cp == '\t') return CharClass::Space;
  if (cp >= 128) return CharClass::Word;  // acentos contam como letra
  if (std::isalnum(static_cast<int>(cp)) || cp == '_') return CharClass::Word;
  return CharClass::Symbol;
}

int digits(int n) {
  int d = 1;
  while (n >= 10) { n /= 10; d++; }
  return d;
}

const char* kOpen = "([{";
const char* kClose = ")]}";

}  // namespace

EditorView::EditorView(std::shared_ptr<Document> doc) : doc_(std::move(doc)) {
  hl_.set_lang(Highlighter::detect(doc_->path()));
}

void EditorView::refresh_language() {
  Lang lang = Highlighter::detect(doc_->path());
  if (lang == hl_.lang()) return;
  hl_.set_lang(lang);
  hl_states_.clear();   // o cache de estados valia para a linguagem anterior
}

// ---------------------------------------------------------------------------
// Selecao
// ---------------------------------------------------------------------------

Pos EditorView::sel_begin() const {
  return sel_anchor_ < cursor_ ? sel_anchor_ : cursor_;
}
Pos EditorView::sel_end() const {
  return sel_anchor_ < cursor_ ? cursor_ : sel_anchor_;
}

std::string EditorView::selected_text() const {
  if (!has_selection()) return std::string();
  return doc_->text_range(sel_begin(), sel_end());
}

std::string EditorView::current_line_text() const {
  return doc_->line(cursor_.line) + "\n";
}

void EditorView::select_all() {
  sel_anchor_ = doc_->begin_pos();
  cursor_ = doc_->end_pos();
  sel_active_ = true;
  ensure_visible();
}

bool EditorView::erase_selection_raw() {
  if (!has_selection()) return false;
  Pos a = sel_begin();
  doc_->erase(a, sel_end());
  cursor_ = a;
  sel_active_ = false;
  return true;
}

bool EditorView::delete_selection() {
  if (!has_selection()) return false;
  doc_->begin_edit(EditKind::Other, cursor_);
  erase_selection_raw();
  ensure_visible();
  return true;
}

void EditorView::begin_move(bool extend) {
  if (extend) {
    if (!sel_active_) {
      sel_anchor_ = cursor_;
      sel_active_ = true;
    }
  } else {
    sel_active_ = false;
  }
}

void EditorView::set_cursor(Pos p, bool extend) {
  begin_move(extend);
  cursor_ = doc_->clamp(p);
  desired_col_ = utf8::byte_to_col(doc_->line(cursor_.line), cursor_.byte,
                                   g_config.tab_width);
  ensure_visible();
}

// ---------------------------------------------------------------------------
// Movimentacao
// ---------------------------------------------------------------------------

void EditorView::move_horizontal(int dir, bool extend, bool by_word) {
  // Sem shift e com selecao ativa, as setas "colapsam" a selecao nas pontas.
  if (!extend && has_selection() && !by_word) {
    Pos p = dir > 0 ? sel_end() : sel_begin();
    sel_active_ = false;
    cursor_ = p;
    desired_col_ = utf8::byte_to_col(doc_->line(cursor_.line), cursor_.byte,
                                     g_config.tab_width);
    ensure_visible();
    return;
  }
  begin_move(extend);

  auto step = [&]() -> bool {
    const std::string& line = doc_->line(cursor_.line);
    if (dir > 0) {
      if (cursor_.byte < line.size()) {
        cursor_.byte = utf8::next(line, cursor_.byte);
      } else if (cursor_.line + 1 < doc_->line_count()) {
        cursor_.line++;
        cursor_.byte = 0;
      } else {
        return false;
      }
    } else {
      if (cursor_.byte > 0) {
        cursor_.byte = utf8::prev(line, cursor_.byte);
      } else if (cursor_.line > 0) {
        cursor_.line--;
        cursor_.byte = doc_->line(cursor_.line).size();
      } else {
        return false;
      }
    }
    return true;
  };

  if (!by_word) {
    step();
  } else {
    // Pula espacos e depois um "bloco" de caracteres da mesma classe.
    auto cur_class = [&]() -> CharClass {
      const std::string& line = doc_->line(cursor_.line);
      size_t b = cursor_.byte;
      if (dir < 0) {
        if (b == 0) return CharClass::Space;
        b = utf8::prev(line, b);
      }
      if (b >= line.size()) return CharClass::Space;
      return classify(utf8::decode(line, b));
    };
    while (cur_class() == CharClass::Space) {
      Pos before = cursor_;
      if (!step()) break;
      if (before.line != cursor_.line) break;  // parou na quebra de linha
    }
    CharClass start = cur_class();
    while (cur_class() == start && start != CharClass::Space) {
      Pos before = cursor_;
      if (!step()) break;
      if (before.line != cursor_.line) break;
    }
  }
  desired_col_ = utf8::byte_to_col(doc_->line(cursor_.line), cursor_.byte,
                                   g_config.tab_width);
  ensure_visible();
}

void EditorView::move_vertical(int delta, bool extend) {
  begin_move(extend);
  int target = cursor_.line + delta;
  if (target < 0) target = 0;
  if (target >= doc_->line_count()) target = doc_->line_count() - 1;
  cursor_.line = target;
  cursor_.byte =
      utf8::col_to_byte(doc_->line(target), desired_col_, g_config.tab_width);
  ensure_visible();
}

void EditorView::move_home(bool extend) {
  begin_move(extend);
  // Primeiro vai para o inicio do texto; se ja estiver la, para a coluna 0.
  const std::string& line = doc_->line(cursor_.line);
  size_t first = line.find_first_not_of(" \t");
  if (first == std::string::npos) first = 0;
  cursor_.byte = (cursor_.byte == first) ? 0 : first;
  desired_col_ = utf8::byte_to_col(line, cursor_.byte, g_config.tab_width);
  ensure_visible();
}

void EditorView::move_end(bool extend) {
  begin_move(extend);
  cursor_.byte = doc_->line(cursor_.line).size();
  desired_col_ = utf8::byte_to_col(doc_->line(cursor_.line), cursor_.byte,
                                   g_config.tab_width);
  ensure_visible();
}

void EditorView::goto_line(int line_1based) {
  Pos p{line_1based - 1, 0};
  sel_active_ = false;
  cursor_ = doc_->clamp(p);
  desired_col_ = 0;
  // Centraliza a linha na tela.
  if (area_.h > 0) {
    scroll_row_ = std::max(0, cursor_.line - area_.h / 2);
  }
  ensure_visible();
}

void EditorView::select_range(Pos a, Pos b) {
  sel_anchor_ = doc_->clamp(a);
  cursor_ = doc_->clamp(b);
  sel_active_ = (sel_anchor_ != cursor_);
  desired_col_ = utf8::byte_to_col(doc_->line(cursor_.line), cursor_.byte,
                                   g_config.tab_width);
  if (area_.h > 0 &&
      (cursor_.line < scroll_row_ || cursor_.line >= scroll_row_ + area_.h))
    scroll_row_ = std::max(0, cursor_.line - area_.h / 2);
  ensure_visible();
}

void EditorView::ensure_visible() {
  if (area_.h <= 0 || area_.w <= 0) return;
  if (cursor_.line < scroll_row_) scroll_row_ = cursor_.line;
  if (cursor_.line >= scroll_row_ + area_.h) scroll_row_ = cursor_.line - area_.h + 1;
  if (scroll_row_ < 0) scroll_row_ = 0;

  int text_w = area_.w - gutter_width();
  if (text_w < 1) return;
  int col = utf8::byte_to_col(doc_->line(cursor_.line), cursor_.byte,
                              g_config.tab_width);
  if (col < scroll_col_) scroll_col_ = col;
  if (col >= scroll_col_ + text_w) scroll_col_ = col - text_w + 1;
  if (scroll_col_ < 0) scroll_col_ = 0;
}

void EditorView::scroll_by(int lines) {
  scroll_row_ += lines;
  int max_row = std::max(0, doc_->line_count() - 1);
  if (scroll_row_ > max_row) scroll_row_ = max_row;
  if (scroll_row_ < 0) scroll_row_ = 0;
}

int EditorView::cursor_col() const {
  return utf8::byte_to_col(doc_->line(cursor_.line), cursor_.byte,
                           g_config.tab_width) + 1;
}

// ---------------------------------------------------------------------------
// Edicao
// ---------------------------------------------------------------------------

std::string EditorView::indent_unit() const {
  // O make so aceita TAB no inicio das regras, entao ali ignoramos o
  // use_spaces do ted.conf.
  if (hl_.lang() == Lang::Make || !g_config.use_spaces) return "\t";
  return std::string(static_cast<size_t>(g_config.tab_width), ' ');
}

void EditorView::insert_literal(const std::string& text) {
  if (text.empty()) return;
  bool had_sel = has_selection();
  doc_->begin_edit(had_sel ? EditKind::Other : EditKind::Typing, cursor_);
  if (had_sel) erase_selection_raw();
  cursor_ = doc_->insert(cursor_, text);
  desired_col_ = utf8::byte_to_col(doc_->line(cursor_.line), cursor_.byte,
                                   g_config.tab_width);
  ensure_visible();
}

std::string EditorView::indent_of(const std::string& line) const {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
  return line.substr(0, i);
}

void EditorView::insert_text(const std::string& text) {
  if (text.empty()) return;
  bool had_sel = has_selection();
  doc_->begin_edit(had_sel ? EditKind::Other : EditKind::Typing, cursor_);
  if (had_sel) erase_selection_raw();
  cursor_ = doc_->insert(cursor_, text);
  desired_col_ = utf8::byte_to_col(doc_->line(cursor_.line), cursor_.byte,
                                   g_config.tab_width);
  ensure_visible();
}

void EditorView::insert_newline() {
  bool had_sel = has_selection();
  doc_->begin_edit(EditKind::Other, cursor_);
  if (had_sel) erase_selection_raw();

  std::string ins = "\n";
  if (g_config.auto_indent) {
    const std::string& line = doc_->line(cursor_.line);
    std::string ind = indent_of(line);
    // Um nivel a mais depois de '{' ou ':' (C/C++/Python).
    std::string before = line.substr(0, cursor_.byte);
    while (!before.empty() && (before.back() == ' ' || before.back() == '\t'))
      before.pop_back();
    bool deeper = !before.empty() && (before.back() == '{' || before.back() == ':');
    ins += ind;
    if (deeper) ins += indent_unit();
    // Se o cursor esta entre '{' e '}', a chave de fechar desce mais uma linha.
    const std::string& after_src = doc_->line(cursor_.line);
    bool closing_ahead = cursor_.byte < after_src.size() &&
                         after_src[cursor_.byte] == '}';
    if (deeper && closing_ahead) {
      Pos p = doc_->insert(cursor_, ins + "\n" + ind);
      // Volta para o fim da linha indentada (a do meio).
      cursor_ = Pos{p.line - 1, doc_->line(p.line - 1).size()};
      desired_col_ = utf8::byte_to_col(doc_->line(cursor_.line), cursor_.byte,
                                       g_config.tab_width);
      ensure_visible();
      return;
    }
  }
  cursor_ = doc_->insert(cursor_, ins);
  desired_col_ = utf8::byte_to_col(doc_->line(cursor_.line), cursor_.byte,
                                   g_config.tab_width);
  ensure_visible();
}

void EditorView::backspace() {
  if (has_selection()) { delete_selection(); return; }
  doc_->begin_edit(EditKind::Deleting, cursor_);
  const std::string& line = doc_->line(cursor_.line);

  if (cursor_.byte == 0) {
    if (cursor_.line == 0) return;
    Pos prev_end{cursor_.line - 1, doc_->line(cursor_.line - 1).size()};
    doc_->erase(prev_end, cursor_);
    cursor_ = prev_end;
  } else {
    size_t start = utf8::prev(line, cursor_.byte);
    // Apaga o par vazio "()" de uma vez so.
    if (g_config.auto_close && cursor_.byte < line.size()) {
      const char* o = std::strchr(kOpen, line[start]);
      if (o && line[cursor_.byte] == kClose[o - kOpen]) {
        doc_->erase(Pos{cursor_.line, start}, Pos{cursor_.line, cursor_.byte + 1});
        cursor_.byte = start;
        ensure_visible();
        return;
      }
      if ((line[start] == '"' || line[start] == '\'') &&
          line[cursor_.byte] == line[start]) {
        doc_->erase(Pos{cursor_.line, start}, Pos{cursor_.line, cursor_.byte + 1});
        cursor_.byte = start;
        ensure_visible();
        return;
      }
    }
    // Dentro da indentacao, apaga um nivel inteiro de espacos.
    if (g_config.use_spaces && line[cursor_.byte - 1] == ' ' &&
        line.find_first_not_of(' ') >= cursor_.byte) {
      int col = static_cast<int>(cursor_.byte);
      int back = col % g_config.tab_width;
      if (back == 0) back = g_config.tab_width;
      if (back > col) back = col;
      start = cursor_.byte - back;
    }
    doc_->erase(Pos{cursor_.line, start}, cursor_);
    cursor_.byte = start;
  }
  desired_col_ = utf8::byte_to_col(doc_->line(cursor_.line), cursor_.byte,
                                   g_config.tab_width);
  ensure_visible();
}

void EditorView::delete_forward(bool by_word) {
  if (has_selection()) { delete_selection(); return; }
  doc_->begin_edit(EditKind::Deleting, cursor_);
  const std::string& line = doc_->line(cursor_.line);
  Pos end = cursor_;
  if (by_word) {
    Pos save = cursor_;
    bool save_sel = sel_active_;
    move_horizontal(+1, false, true);
    end = cursor_;
    cursor_ = save;
    sel_active_ = save_sel;
  } else if (cursor_.byte < line.size()) {
    end.byte = utf8::next(line, cursor_.byte);
  } else if (cursor_.line + 1 < doc_->line_count()) {
    end = Pos{cursor_.line + 1, 0};
  } else {
    return;
  }
  doc_->erase(cursor_, end);
  ensure_visible();
}

void EditorView::delete_line() {
  doc_->begin_edit(EditKind::Other, cursor_);
  sel_active_ = false;
  int l = cursor_.line;
  if (doc_->line_count() == 1) {
    doc_->erase(Pos{0, 0}, Pos{0, doc_->line(0).size()});
    cursor_ = Pos{0, 0};
  } else if (l + 1 < doc_->line_count()) {
    doc_->erase(Pos{l, 0}, Pos{l + 1, 0});
    cursor_ = doc_->clamp(Pos{l, 0});
  } else {
    doc_->erase(Pos{l - 1, doc_->line(l - 1).size()}, Pos{l, doc_->line(l).size()});
    cursor_ = doc_->clamp(Pos{l - 1, 0});
  }
  ensure_visible();
}

void EditorView::duplicate_line() {
  doc_->begin_edit(EditKind::Other, cursor_);
  const std::string text = doc_->line(cursor_.line);
  Pos eol{cursor_.line, text.size()};
  doc_->insert(eol, "\n" + text);
  cursor_.line++;
  sel_active_ = false;
  ensure_visible();
}

void EditorView::move_lines(int delta) {
  int first = has_selection() ? sel_begin().line : cursor_.line;
  int last = has_selection() ? sel_end().line : cursor_.line;
  if (delta < 0 && first == 0) return;
  if (delta > 0 && last + 1 >= doc_->line_count()) return;

  doc_->begin_edit(EditKind::Other, cursor_);
  // Estrategia simples: recorta as linhas e insere na nova posicao.
  std::vector<std::string> block;
  for (int l = first; l <= last; l++) block.push_back(doc_->line(l));

  Pos a{first, 0};
  Pos b = (last + 1 < doc_->line_count()) ? Pos{last + 1, 0}
                                          : Pos{last, doc_->line(last).size()};
  bool at_end = !(last + 1 < doc_->line_count());
  if (at_end) a = Pos{first - 1, doc_->line(first - 1).size()};
  doc_->erase(a, b);

  int new_first = first + delta;
  std::string payload;
  for (size_t i = 0; i < block.size(); i++) {
    payload += block[i];
    payload += "\n";
  }
  if (new_first >= doc_->line_count()) {
    Pos e = doc_->end_pos();
    payload.pop_back();
    doc_->insert(e, "\n" + payload);
  } else {
    doc_->insert(Pos{new_first, 0}, payload);
  }

  cursor_.line += delta;
  if (has_selection()) sel_anchor_.line += delta;
  cursor_ = doc_->clamp(cursor_);
  sel_anchor_ = doc_->clamp(sel_anchor_);
  ensure_visible();
}

void EditorView::indent_selection(bool remove) {
  int first = has_selection() ? sel_begin().line : cursor_.line;
  int last = has_selection() ? sel_end().line : cursor_.line;
  // Se a selecao termina na coluna 0, a ultima linha nao entra.
  if (has_selection() && sel_end().byte == 0 && last > first) last--;

  doc_->begin_edit(EditKind::Other, cursor_);
  std::string unit = indent_unit();
  for (int l = first; l <= last; l++) {
    const std::string& line = doc_->line(l);
    if (remove) {
      size_t n = 0;
      if (!line.empty() && line[0] == '\t') {
        n = 1;
      } else {
        while (n < line.size() && n < static_cast<size_t>(g_config.tab_width) &&
               line[n] == ' ')
          n++;
      }
      if (n == 0) continue;
      doc_->erase(Pos{l, 0}, Pos{l, n});
      if (cursor_.line == l) cursor_.byte -= std::min(cursor_.byte, n);
      if (sel_anchor_.line == l) sel_anchor_.byte -= std::min(sel_anchor_.byte, n);
    } else {
      if (line.empty() && first != last) continue;
      doc_->insert(Pos{l, 0}, unit);
      if (cursor_.line == l) cursor_.byte += unit.size();
      if (sel_anchor_.line == l) sel_anchor_.byte += unit.size();
    }
  }
  cursor_ = doc_->clamp(cursor_);
  sel_anchor_ = doc_->clamp(sel_anchor_);
  ensure_visible();
}

// Comenta/descomenta as linhas tocadas pela selecao. A regra e a mesma dos
// editores graficos: se *todas* as linhas ja estao comentadas, descomenta;
// senao, comenta todas - alinhando o marcador na menor indentacao do bloco,
// para o codigo nao ficar torto.
bool EditorView::toggle_comment() {
  const CommentSyntax cs = comment_syntax(hl_.lang());
  if (cs.empty()) return false;

  int first = has_selection() ? sel_begin().line : cursor_.line;
  int last = has_selection() ? sel_end().line : cursor_.line;
  if (has_selection() && sel_end().byte == 0 && last > first) last--;

  auto indent_end = [&](const std::string& s) {
    size_t i = s.find_first_not_of(" \t");
    return i == std::string::npos ? s.size() : i;
  };

  // Linhas em branco no meio do bloco nao entram (nem para comentar nem para
  // decidir se o bloco ja esta comentado).
  std::vector<int> lines;
  size_t indent = std::string::npos;
  for (int l = first; l <= last; l++) {
    const std::string& s = doc_->line(l);
    if (indent_end(s) == s.size()) continue;   // so espacos
    lines.push_back(l);
    indent = std::min(indent, indent_end(s));
  }
  if (lines.empty()) {          // selecao so com linhas vazias: usa a atual
    lines.push_back(cursor_.line);
    indent = indent_end(doc_->line(cursor_.line));
  }

  const bool use_line = !cs.line.empty();
  const std::string open = use_line ? cs.line : cs.block_open;

  // Ja esta tudo comentado?
  bool all_commented = true;
  for (int l : lines) {
    const std::string& s = doc_->line(l);
    size_t i = indent_end(s);
    if (s.compare(i, open.size(), open) != 0) { all_commented = false; break; }
    if (!use_line) {
      size_t e = s.find_last_not_of(" \t");
      if (e == std::string::npos ||
          e + 1 < cs.block_close.size() ||
          s.compare(e + 1 - cs.block_close.size(), cs.block_close.size(),
                    cs.block_close) != 0) {
        all_commented = false;
        break;
      }
    }
  }

  doc_->begin_edit(EditKind::Other, cursor_);

  // Move o cursor/ancora junto com o texto que entrou ou saiu antes deles.
  auto shift = [&](Pos* p, int line, size_t at, int delta) {
    if (p->line != line) return;
    if (delta > 0) {
      if (p->byte >= at) p->byte += static_cast<size_t>(delta);
    } else {
      size_t removed = static_cast<size_t>(-delta);
      if (p->byte > at) p->byte -= std::min(removed, p->byte - at);
    }
  };

  for (int l : lines) {
    const std::string& s = doc_->line(l);
    const size_t i = indent_end(s);

    if (all_commented) {
      // --- descomentar ---
      size_t n = open.size();
      if (s.size() > i + n && s[i + n] == ' ') n++;   // tira o espaco tambem
      doc_->erase(Pos{l, i}, Pos{l, i + n});
      shift(&cursor_, l, i, -static_cast<int>(n));
      shift(&sel_anchor_, l, i, -static_cast<int>(n));
      if (!use_line) {
        const std::string& s2 = doc_->line(l);
        size_t e = s2.find_last_not_of(" \t");
        if (e != std::string::npos && e + 1 >= cs.block_close.size()) {
          size_t start = e + 1 - cs.block_close.size();
          size_t from = (start > 0 && s2[start - 1] == ' ') ? start - 1 : start;
          doc_->erase(Pos{l, from}, Pos{l, e + 1});
          shift(&cursor_, l, from, -static_cast<int>(e + 1 - from));
          shift(&sel_anchor_, l, from, -static_cast<int>(e + 1 - from));
        }
      }
    } else {
      // --- comentar ---
      const size_t at = std::min(indent, i);
      const std::string text = open + " ";
      doc_->insert(Pos{l, at}, text);
      shift(&cursor_, l, at, static_cast<int>(text.size()));
      shift(&sel_anchor_, l, at, static_cast<int>(text.size()));
      if (!use_line) {
        const std::string& s2 = doc_->line(l);
        const std::string tail = " " + cs.block_close;
        doc_->insert(Pos{l, s2.size()}, tail);
      }
    }
  }

  cursor_ = doc_->clamp(cursor_);
  sel_anchor_ = doc_->clamp(sel_anchor_);
  desired_col_ = utf8::byte_to_col(doc_->line(cursor_.line), cursor_.byte,
                                   g_config.tab_width);
  ensure_visible();
  return true;
}

void EditorView::insert_tab() {
  if (has_selection() && sel_begin().line != sel_end().line) {
    indent_selection(false);
    return;
  }
  if (indent_unit() == "\t") { insert_text("\t"); return; }
  int col = utf8::byte_to_col(doc_->line(cursor_.line), cursor_.byte,
                              g_config.tab_width);
  int n = g_config.tab_width - (col % g_config.tab_width);
  insert_text(std::string(n, ' '));
}

// ---------------------------------------------------------------------------
// Busca
// ---------------------------------------------------------------------------

bool EditorView::find_next(bool forward, bool from_cursor) {
  if (search_.empty()) return false;
  Pos start = cursor_;
  if (from_cursor && forward && has_selection()) start = sel_end();
  else if (from_cursor && forward) start = cursor_;
  else if (!forward) start = sel_begin();

  Pos a, b;
  bool case_sensitive =
      std::any_of(search_.begin(), search_.end(),
                  [](unsigned char c) { return std::isupper(c); });
  if (!doc_->find(search_, start, forward, case_sensitive, &a, &b)) return false;
  sel_anchor_ = a;
  cursor_ = b;
  sel_active_ = true;
  desired_col_ = utf8::byte_to_col(doc_->line(cursor_.line), cursor_.byte,
                                   g_config.tab_width);
  // Centraliza se o resultado estiver fora da tela.
  if (area_.h > 0 && (a.line < scroll_row_ || a.line >= scroll_row_ + area_.h))
    scroll_row_ = std::max(0, a.line - area_.h / 2);
  ensure_visible();
  return true;
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

void EditorView::click(int screen_x, int screen_y, bool extend) {
  int row = screen_y - area_.y + scroll_row_;
  int gx = area_.x + gutter_width();
  int col = screen_x - gx + scroll_col_;
  if (col < 0) col = 0;
  Pos p;
  p.line = std::max(0, std::min(row, doc_->line_count() - 1));
  p.byte = utf8::col_to_byte(doc_->line(p.line), col, g_config.tab_width);
  set_cursor(p, extend);
}

void EditorView::select_word_at_cursor() {
  const std::string& line = doc_->line(cursor_.line);
  if (line.empty()) return;
  size_t b = cursor_.byte;
  if (b >= line.size()) b = utf8::prev(line, line.size());
  CharClass cls = classify(utf8::decode(line, b));
  if (cls == CharClass::Space) return;
  size_t start = b;
  while (start > 0) {
    size_t p = utf8::prev(line, start);
    if (classify(utf8::decode(line, p)) != cls) break;
    start = p;
  }
  size_t end = b;
  while (end < line.size() && classify(utf8::decode(line, end)) == cls)
    end = utf8::next(line, end);
  sel_anchor_ = Pos{cursor_.line, start};
  cursor_ = Pos{cursor_.line, end};
  sel_active_ = true;
}

// ---------------------------------------------------------------------------
// Teclado
// ---------------------------------------------------------------------------

bool EditorView::handle_key(const ui::KeyEvent& ev) {
  using namespace ui;

  if (ev.is_code) {
    switch (static_cast<int>(ev.ch)) {
      case KEY_UP:            move_vertical(-1, false); return true;
      case KEY_DOWN:          move_vertical(+1, false); return true;
      case KEY_LEFT:          move_horizontal(-1, false, false); return true;
      case KEY_RIGHT:         move_horizontal(+1, false, false); return true;
      case K_SHIFT_UP:        move_vertical(-1, true); return true;
      case K_SHIFT_DOWN:      move_vertical(+1, true); return true;
      case K_SHIFT_LEFT:      move_horizontal(-1, true, false); return true;
      case K_SHIFT_RIGHT:     move_horizontal(+1, true, false); return true;
      case K_CTRL_LEFT:       move_horizontal(-1, false, true); return true;
      case K_CTRL_RIGHT:      move_horizontal(+1, false, true); return true;
      case K_CTRL_SHIFT_LEFT: move_horizontal(-1, true, true); return true;
      case K_CTRL_SHIFT_RIGHT:move_horizontal(+1, true, true); return true;
      case K_CTRL_UP:         scroll_by(-1); return true;
      case K_CTRL_DOWN:       scroll_by(+1); return true;
      case K_ALT_SHIFT_UP:    move_lines(-1); return true;
      case K_ALT_SHIFT_DOWN:  move_lines(+1); return true;

      case KEY_HOME:          move_home(false); return true;
      case KEY_END:           move_end(false); return true;
      case K_SHIFT_HOME:      move_home(true); return true;
      case K_SHIFT_END:       move_end(true); return true;
      case K_CTRL_HOME:       set_cursor(doc_->begin_pos(), false); return true;
      case K_CTRL_END:        set_cursor(doc_->end_pos(), false); return true;
      case K_CTRL_SHIFT_HOME: set_cursor(doc_->begin_pos(), true); return true;
      case K_CTRL_SHIFT_END:  set_cursor(doc_->end_pos(), true); return true;

      case KEY_PPAGE:         move_vertical(-std::max(1, area_.h - 1), false); return true;
      case KEY_NPAGE:         move_vertical(+std::max(1, area_.h - 1), false); return true;
      case K_SHIFT_PGUP:      move_vertical(-std::max(1, area_.h - 1), true); return true;
      case K_SHIFT_PGDN:      move_vertical(+std::max(1, area_.h - 1), true); return true;

      case KEY_BACKSPACE:     backspace(); return true;
      case KEY_DC:            delete_forward(false); return true;
      case K_CTRL_DEL:        delete_forward(true); return true;
      case KEY_IC:            toggle_overwrite(); return true;
      case KEY_ENTER:         insert_newline(); return true;
      case K_SHIFT_TAB:       indent_selection(true); return true;
      case KEY_BTAB:          indent_selection(true); return true;
      default: return false;
    }
  }

  // Caracteres "crus".
  switch (ev.ch) {
    case '\r': case '\n': insert_newline(); return true;
    case '\t': insert_tab(); return true;
    case 8: case 127: backspace(); return true;
  }
  if (ev.alt) return false;               // Alt+letra e atalho do app
  if (ev.ch < 32) return false;           // Ctrl+letra e atalho do app

  std::string s = utf8::encode(static_cast<uint32_t>(ev.ch));

  // Fecha automaticamente parenteses/aspas.
  if (g_config.auto_close && !has_selection() && s.size() == 1) {
    char c = s[0];
    const std::string& line = doc_->line(cursor_.line);
    char nextc = cursor_.byte < line.size() ? line[cursor_.byte] : '\0';
    const char* close_at = std::strchr(kClose, c);
    if (close_at && nextc == c) {  // ja tem o par: so anda por cima
      cursor_.byte = utf8::next(line, cursor_.byte);
      ensure_visible();
      return true;
    }
    const char* open_at = std::strchr(kOpen, c);
    bool at_word = std::isalnum(static_cast<unsigned char>(nextc)) || nextc == '_';
    if (open_at && !at_word) {
      insert_text(std::string(1, c) + std::string(1, kClose[open_at - kOpen]));
      cursor_.byte = utf8::prev(doc_->line(cursor_.line), cursor_.byte);
      ensure_visible();
      return true;
    }
    if ((c == '"' || c == '\'')) {
      if (nextc == c) {
        cursor_.byte = utf8::next(line, cursor_.byte);
        ensure_visible();
        return true;
      }
      char prevc = cursor_.byte > 0 ? line[cursor_.byte - 1] : '\0';
      bool after_word = std::isalnum(static_cast<unsigned char>(prevc)) ||
                        prevc == '_' || prevc == c || prevc == '\\';
      if (!at_word && !after_word) {
        insert_text(std::string(2, c));
        cursor_.byte = utf8::prev(doc_->line(cursor_.line), cursor_.byte);
        ensure_visible();
        return true;
      }
    }
  }

  if (overwrite_ && !has_selection()) {
    const std::string& line = doc_->line(cursor_.line);
    if (cursor_.byte < line.size()) {
      doc_->begin_edit(EditKind::Typing, cursor_);
      doc_->erase(cursor_, Pos{cursor_.line, utf8::next(line, cursor_.byte)});
    }
  }
  insert_text(s);
  return true;
}

// ---------------------------------------------------------------------------
// Desenho
// ---------------------------------------------------------------------------

int EditorView::gutter_width() const {
  if (!g_config.show_line_numbers) return 1;
  return digits(std::max(1, doc_->line_count())) + 2;
}

void EditorView::update_highlight_states() {
  if (hl_version_ != doc_->version()) {
    hl_version_ = doc_->version();
    hl_states_.clear();
  }
  if (hl_states_.empty()) hl_states_.push_back(Highlighter::kNormal);
  int need = std::min(doc_->line_count(), scroll_row_ + area_.h) + 1;
  std::vector<int> tmp;
  while (static_cast<int>(hl_states_.size()) < need) {
    int idx = static_cast<int>(hl_states_.size()) - 1;
    int st = hl_.highlight(doc_->line(idx), hl_states_[idx], &tmp);
    hl_states_.push_back(st);
  }
}

bool EditorView::cursor_screen(int* x, int* y) const {
  if (cursor_x_ < 0) return false;
  *x = cursor_x_;
  *y = cursor_y_;
  return true;
}

// O mesmo Document pode estar aberto em mais de um painel (divisao da tela).
// Quando *outro* painel edita o texto, o cursor, a ancora da selecao e a
// rolagem deste aqui podem ter ficado apontando alem do fim do arquivo - sem
// isso o painel aparece em branco e a barra de status mostra uma linha que nao
// existe mais. Prender tudo a faixa valida antes de desenhar resolve; para o
// painel que fez a edicao a operacao nao muda nada.
void EditorView::sync_to_doc() {
  cursor_ = doc_->clamp(cursor_);
  sel_anchor_ = doc_->clamp(sel_anchor_);
  const int max_row = std::max(0, doc_->line_count() - 1);
  scroll_row_ = std::max(0, std::min(scroll_row_, max_row));
}

void EditorView::draw(const Rect& area, bool focused) {
  area_ = area;
  sync_to_doc();
  ensure_visible();
  update_highlight_states();
  cursor_x_ = cursor_y_ = -1;

  const int gw = gutter_width();
  const int text_w = std::max(0, area.w - gw);
  const bool sel = has_selection();
  const Pos sb = sel_begin(), se = sel_end();
  std::vector<int> colors;

  for (int row = 0; row < area.h; row++) {
    const int y = area.y + row;
    const int l = scroll_row_ + row;

    attrset(COLOR_PAIR(ui::kNormal));
    ui::fill(y, area.x, area.w);

    if (l >= doc_->line_count()) continue;
    const std::string& line = doc_->line(l);

    // --- numero da linha ---
    if (g_config.show_line_numbers) {
      bool cur = (l == cursor_.line);
      attrset(COLOR_PAIR(cur ? ui::kLineNoCur : ui::kLineNo) |
              (cur ? A_BOLD : 0));
      std::string num = std::to_string(l + 1);
      int pad = gw - 1 - static_cast<int>(num.size());
      if (pad < 0) pad = 0;
      ui::put(y, area.x + pad, gw, num);
    }

    // --- estado do realce nesta linha ---
    int st = (l < static_cast<int>(hl_states_.size())) ? hl_states_[l]
                                                       : Highlighter::kNormal;
    hl_.highlight(line, st, &colors);

    // --- ocorrencias da busca ---
    std::vector<std::pair<size_t, size_t>> hits;
    if (!search_.empty() && search_.size() <= line.size()) {
      bool cs = std::any_of(search_.begin(), search_.end(),
                            [](unsigned char c) { return std::isupper(c); });
      std::string hay = line, pat = search_;
      if (!cs) {
        for (char& c : hay) c = static_cast<char>(std::tolower((unsigned char)c));
        for (char& c : pat) c = static_cast<char>(std::tolower((unsigned char)c));
      }
      size_t p = hay.find(pat);
      while (p != std::string::npos && hits.size() < 200) {
        hits.emplace_back(p, p + pat.size());
        p = hay.find(pat, p + pat.size());
      }
    }

    // --- caracteres da linha ---
    int col = 0;  // coluna logica (antes da rolagem horizontal)
    size_t i = 0;
    while (i <= line.size()) {
      // Posicao do cursor (inclusive no fim da linha).
      if (l == cursor_.line && i == cursor_.byte) {
        int sx = area.x + gw + (col - scroll_col_);
        if (col >= scroll_col_ && sx < area.x + area.w) {
          cursor_x_ = sx;
          cursor_y_ = y;
        }
      }
      if (i >= line.size()) break;

      uint32_t cp = utf8::decode(line, i);
      size_t clen = utf8::char_len(line, i);
      int w = (cp == '\t') ? g_config.tab_width - (col % g_config.tab_width)
                           : utf8::cp_width(cp);
      if (cp < 32 && cp != '\t') w = 1;

      bool in_sel = false;
      if (sel) {
        Pos here{l, i};
        in_sel = (sb <= here) && (here < se);
      }
      bool in_hit = false;
      for (auto& h : hits)
        if (i >= h.first && i < h.second) { in_hit = true; break; }

      int pair = colors.empty() || i >= colors.size() ? 0 : colors[i];
      int attr;
      if (in_sel) attr = COLOR_PAIR(ui::kSelection);
      else if (in_hit) attr = COLOR_PAIR(ui::kSearchHit);
      else attr = COLOR_PAIR(pair ? pair : ui::kNormal);

      int sx = area.x + gw + (col - scroll_col_);
      if (col + w > scroll_col_ && col - scroll_col_ < text_w) {
        attrset(attr);
        if (cp == '\t') {
          int start = std::max(sx, area.x + gw);
          int end = std::min(sx + w, area.x + gw + text_w);
          if (end > start) ui::fill(y, start, end - start);
        } else if (cp < 32) {
          if (sx >= area.x + gw) mvaddch(y, sx, '?');
        } else if (sx >= area.x + gw && sx + w <= area.x + gw + text_w) {
          std::string ch = line.substr(i, clen);
          mvaddstr(y, sx, ch.c_str());
        } else if (sx >= area.x + gw) {
          mvaddch(y, sx, ' ');  // caractere largo cortado na borda
        }
      }
      col += w;
      i += clen;
      if (col - scroll_col_ > text_w && l != cursor_.line) break;
    }

    // Marca o fim da linha selecionada (a quebra de linha aparece destacada).
    if (sel && l >= sb.line && l < se.line) {
      int sx = area.x + gw + (col - scroll_col_);
      if (sx >= area.x + gw && sx < area.x + area.w) {
        attrset(COLOR_PAIR(ui::kSelection));
        mvaddch(y, sx, ' ');
      }
    }
  }
  attrset(COLOR_PAIR(ui::kNormal));
  (void)focused;
}
