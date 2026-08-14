#include "document.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {
constexpr size_t kMaxUndo = 500;
constexpr auto kCoalesceWindow = std::chrono::milliseconds(600);
}  // namespace

Document::Document() { lines_.push_back(std::string()); }

const std::string& Document::line(int i) const {
  static const std::string empty;
  if (i < 0 || i >= static_cast<int>(lines_.size())) return empty;
  return lines_[i];
}

std::string Document::display_name() const {
  if (path_.empty()) return "[sem nome]";
  size_t slash = path_.find_last_of('/');
  return slash == std::string::npos ? path_ : path_.substr(slash + 1);
}

Pos Document::clamp(Pos p) const {
  if (p.line < 0) p.line = 0;
  if (p.line >= line_count()) p.line = line_count() - 1;
  if (p.byte > lines_[p.line].size()) p.byte = lines_[p.line].size();
  return p;
}

Pos Document::end_pos() const {
  Pos p;
  p.line = line_count() - 1;
  p.byte = lines_[p.line].size();
  return p;
}

bool Document::load(const std::string& path, std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error) *error = std::string("nao foi possivel abrir: ") + strerror(errno);
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string data = ss.str();

  lines_.clear();
  crlf_ = data.find("\r\n") != std::string::npos;
  std::string cur;
  for (size_t i = 0; i < data.size(); i++) {
    char c = data[i];
    if (c == '\n') {
      if (!cur.empty() && cur.back() == '\r') cur.pop_back();
      lines_.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  final_newline_ = cur.empty() && !data.empty();
  if (!cur.empty()) lines_.push_back(cur);
  if (lines_.empty()) lines_.push_back(std::string());

  path_ = path;
  modified_ = false;
  undo_stack_.clear();
  redo_stack_.clear();
  version_++;
  return true;
}

bool Document::save(std::string* error) {
  if (path_.empty()) {
    if (error) *error = "arquivo sem nome";
    return false;
  }
  return save_as(path_, error);
}

bool Document::save_as(const std::string& path, std::string* error) {
  // Grava em arquivo temporario e renomeia: se faltar energia no meio, o
  // arquivo antigo continua intacto.
  std::string tmp = path + ".ted-tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      if (error) *error = std::string("nao foi possivel gravar: ") + strerror(errno);
      return false;
    }
    const char* eol = crlf_ ? "\r\n" : "\n";
    for (size_t i = 0; i < lines_.size(); i++) {
      out << lines_[i];
      bool last = (i + 1 == lines_.size());
      if (!last || final_newline_) out << eol;
    }
    out.flush();
    if (!out) {
      if (error) *error = "erro de escrita";
      std::remove(tmp.c_str());
      return false;
    }
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    if (error) *error = std::string("nao foi possivel salvar: ") + strerror(errno);
    std::remove(tmp.c_str());
    return false;
  }
  path_ = path;
  modified_ = false;
  return true;
}

std::string Document::text_range(Pos a, Pos b) const {
  a = clamp(a);
  b = clamp(b);
  if (b < a) std::swap(a, b);
  if (a.line == b.line) return lines_[a.line].substr(a.byte, b.byte - a.byte);
  std::string out = lines_[a.line].substr(a.byte);
  for (int l = a.line + 1; l < b.line; l++) {
    out += '\n';
    out += lines_[l];
  }
  out += '\n';
  out += lines_[b.line].substr(0, b.byte);
  return out;
}

std::string Document::text() const { return text_range(begin_pos(), end_pos()); }

void Document::touch() {
  modified_ = true;
  version_++;
}

void Document::push_undo(Pos cursor) {
  undo_stack_.push_back(Snapshot{lines_, cursor, modified_});
  if (undo_stack_.size() > kMaxUndo)
    undo_stack_.erase(undo_stack_.begin());
  redo_stack_.clear();
}

void Document::begin_edit(EditKind kind, Pos cursor) {
  auto now = std::chrono::steady_clock::now();
  bool same_run = (kind == last_kind_) && kind != EditKind::Other &&
                  !undo_stack_.empty() &&
                  (now - last_edit_time_) < kCoalesceWindow;
  if (!same_run) push_undo(cursor);
  last_kind_ = kind;
  last_edit_time_ = now;
}

Pos Document::insert(Pos at, const std::string& text) {
  at = clamp(at);
  if (text.empty()) return at;

  // Quebra o texto inserido em linhas.
  std::vector<std::string> parts;
  std::string cur;
  for (char c : text) {
    if (c == '\n') {
      parts.push_back(cur);
      cur.clear();
    } else if (c != '\r') {
      cur += c;
    }
  }
  parts.push_back(cur);

  std::string& target = lines_[at.line];
  std::string tail = target.substr(at.byte);
  target = target.substr(0, at.byte);

  if (parts.size() == 1) {
    target += parts[0];
    Pos end{at.line, target.size()};
    target += tail;
    touch();
    return end;
  }

  target += parts[0];
  std::vector<std::string> rest(parts.begin() + 1, parts.end());
  std::string& last = rest.back();
  Pos end{at.line + static_cast<int>(rest.size()), last.size()};
  last += tail;
  lines_.insert(lines_.begin() + at.line + 1, rest.begin(), rest.end());
  touch();
  return end;
}

std::string Document::erase(Pos a, Pos b) {
  a = clamp(a);
  b = clamp(b);
  if (b < a) std::swap(a, b);
  if (a == b) return std::string();
  std::string removed = text_range(a, b);

  if (a.line == b.line) {
    lines_[a.line].erase(a.byte, b.byte - a.byte);
  } else {
    lines_[a.line] = lines_[a.line].substr(0, a.byte) +
                     lines_[b.line].substr(b.byte);
    lines_.erase(lines_.begin() + a.line + 1, lines_.begin() + b.line + 1);
  }
  if (lines_.empty()) lines_.push_back(std::string());
  touch();
  return removed;
}

bool Document::undo(Pos* cursor) {
  if (undo_stack_.empty()) return false;
  Snapshot s = undo_stack_.back();
  undo_stack_.pop_back();
  redo_stack_.push_back(Snapshot{lines_, cursor ? *cursor : Pos{}, modified_});
  lines_ = s.lines;
  modified_ = s.modified;
  if (cursor) *cursor = clamp(s.cursor);
  version_++;
  last_kind_ = EditKind::Other;
  return true;
}

bool Document::redo(Pos* cursor) {
  if (redo_stack_.empty()) return false;
  Snapshot s = redo_stack_.back();
  redo_stack_.pop_back();
  undo_stack_.push_back(Snapshot{lines_, cursor ? *cursor : Pos{}, modified_});
  lines_ = s.lines;
  modified_ = s.modified;
  if (cursor) *cursor = clamp(s.cursor);
  version_++;
  last_kind_ = EditKind::Other;
  return true;
}

bool Document::find(const std::string& needle, Pos from, bool forward,
                    bool case_sensitive, Pos* match_begin,
                    Pos* match_end) const {
  if (needle.empty()) return false;
  auto lower = [](std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
    return s;
  };
  std::string pat = case_sensitive ? needle : lower(needle);
  int n = line_count();
  from = clamp(from);

  for (int step = 0; step <= n; step++) {
    int l = forward ? (from.line + step) % n
                    : ((from.line - step) % n + n) % n;
    std::string hay = case_sensitive ? lines_[l] : lower(lines_[l]);
    if (forward) {
      size_t start = (step == 0) ? from.byte : 0;
      if (start > hay.size()) continue;
      size_t p = hay.find(pat, start);
      if (p != std::string::npos) {
        *match_begin = Pos{l, p};
        *match_end = Pos{l, p + pat.size()};
        return true;
      }
    } else {
      size_t limit = (step == 0) ? from.byte : hay.size();
      if (limit == 0 && step == 0) continue;
      size_t p = hay.rfind(pat, limit == 0 ? 0 : limit - 1);
      if (p != std::string::npos && (step != 0 || p + pat.size() <= from.byte ||
                                     p < from.byte)) {
        *match_begin = Pos{l, p};
        *match_end = Pos{l, p + pat.size()};
        return true;
      }
    }
  }
  return false;
}
