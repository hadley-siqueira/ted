#include "filetree.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <functional>

#include "utf8.hpp"

namespace {

bool is_dir_path(const std::string& p) {
  struct stat st;
  if (stat(p.c_str(), &st) != 0) return false;
  return S_ISDIR(st.st_mode);
}

std::string base_name(const std::string& p) {
  if (p == "/") return "/";
  std::string q = p;
  while (q.size() > 1 && q.back() == '/') q.pop_back();
  size_t slash = q.find_last_of('/');
  return slash == std::string::npos ? q : q.substr(slash + 1);
}

// Arquivos gerados ou binarios: aparecem no painel (porque existem), mas nao
// entram na lista do Ctrl+P - o editor recusaria abri-los de qualquer jeito.
bool is_binary_name(const std::string& name) {
  static const char* kExts[] = {
      "o", "d", "a", "so", "obj", "lib", "exe", "dll", "class", "pyc", "pyo",
      "png", "jpg", "jpeg", "gif", "bmp", "ico", "svgz", "pdf", "zip", "gz",
      "xz", "bz2", "7z", "tar", "mp3", "mp4", "wav", "ogg", "ttf", "otf",
      "woff", "woff2", "bin", "iso", "jar"};
  size_t dot = name.find_last_of('.');
  if (dot == std::string::npos || dot + 1 >= name.size()) return false;
  std::string ext = name.substr(dot + 1);
  for (char& c : ext) c = static_cast<char>(tolower((unsigned char)c));
  for (const char* e : kExts)
    if (ext == e) return true;
  return false;
}

// Pastas que quase nunca interessam ao aluno.
bool is_noise(const std::string& name) {
  static const char* kNoise[] = {".git", "node_modules", "__pycache__",
                                 ".cache", ".venv", "venv"};
  for (const char* n : kNoise)
    if (name == n) return true;
  return false;
}

}  // namespace

FileTree::FileTree(const std::string& root_path) {
  char buf[4096];
  std::string p = root_path;
  if (p.empty()) p = ".";
  if (realpath(p.c_str(), buf)) p = buf;
  root_ = p;

  root_node_ = std::make_unique<Node>();
  root_node_->path = root_;
  root_node_->name = base_name(root_);
  root_node_->is_dir = true;
  root_node_->expanded = true;
  root_node_->depth = 0;
  load_children(root_node_.get());
  rebuild_visible();
}

void FileTree::load_children(Node* n) {
  n->children.clear();
  n->loaded = true;
  DIR* d = opendir(n->path.c_str());
  if (!d) return;
  std::vector<std::unique_ptr<Node>> dirs, files;
  while (dirent* e = readdir(d)) {
    std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    if (!show_hidden_ && !name.empty() && name[0] == '.') continue;
    if (!show_hidden_ && is_noise(name)) continue;

    auto child = std::make_unique<Node>();
    child->path = n->path == "/" ? "/" + name : n->path + "/" + name;
    child->name = name;
    child->depth = n->depth + 1;
    // d_type pode nao ser confiavel em alguns sistemas de arquivos.
    if (e->d_type == DT_DIR) child->is_dir = true;
    else if (e->d_type == DT_UNKNOWN || e->d_type == DT_LNK)
      child->is_dir = is_dir_path(child->path);
    (child->is_dir ? dirs : files).push_back(std::move(child));
  }
  closedir(d);

  auto by_name = [](const std::unique_ptr<Node>& a,
                    const std::unique_ptr<Node>& b) {
    return strcasecmp(a->name.c_str(), b->name.c_str()) < 0;
  };
  std::sort(dirs.begin(), dirs.end(), by_name);
  std::sort(files.begin(), files.end(), by_name);
  for (auto& x : dirs) n->children.push_back(std::move(x));
  for (auto& x : files) n->children.push_back(std::move(x));
}

void FileTree::rebuild_visible() {
  visible_.clear();
  // A raiz aparece como cabecalho; os filhos comecam com recuo 1.
  std::vector<Node*> stack;
  std::function<void(Node*)> walk = [&](Node* n) {
    for (auto& c : n->children) {
      visible_.push_back(c.get());
      if (c->is_dir && c->expanded) walk(c.get());
    }
  };
  walk(root_node_.get());
  if (selected_ >= static_cast<int>(visible_.size()))
    selected_ = static_cast<int>(visible_.size()) - 1;
  if (selected_ < 0) selected_ = 0;
}

void FileTree::refresh() {
  // Guarda quais pastas estavam abertas para reabrir depois.
  std::vector<std::string> expanded;
  std::function<void(Node*)> collect = [&](Node* n) {
    for (auto& c : n->children) {
      if (c->is_dir && c->expanded) {
        expanded.push_back(c->path);
        collect(c.get());
      }
    }
  };
  collect(root_node_.get());
  std::string sel_path =
      (selected_ >= 0 && selected_ < static_cast<int>(visible_.size()))
          ? visible_[selected_]->path
          : std::string();

  load_children(root_node_.get());
  std::function<void(Node*)> reopen = [&](Node* n) {
    for (auto& c : n->children) {
      if (c->is_dir && std::find(expanded.begin(), expanded.end(), c->path) !=
                           expanded.end()) {
        c->expanded = true;
        load_children(c.get());
        reopen(c.get());
      }
    }
  };
  reopen(root_node_.get());
  rebuild_visible();

  if (!sel_path.empty()) {
    for (size_t i = 0; i < visible_.size(); i++)
      if (visible_[i]->path == sel_path) { selected_ = static_cast<int>(i); break; }
  }
}

std::vector<std::string> FileTree::list_all_files(size_t limit,
                                                  bool* truncated) const {
  std::vector<std::string> out;
  if (truncated) *truncated = false;

  // Percorre em largura: os arquivos do topo do projeto aparecem primeiro.
  std::vector<std::string> queue{std::string()};   // caminhos relativos
  for (size_t q = 0; q < queue.size(); q++) {
    const std::string& rel_dir = queue[q];
    std::string abs = rel_dir.empty() ? root_ : root_ + "/" + rel_dir;
    DIR* d = opendir(abs.c_str());
    if (!d) continue;
    std::vector<std::string> files, dirs;
    while (dirent* e = readdir(d)) {
      std::string name = e->d_name;
      if (name == "." || name == "..") continue;
      if (!show_hidden_ && !name.empty() && name[0] == '.') continue;
      if (is_noise(name)) continue;
      std::string rel = rel_dir.empty() ? name : rel_dir + "/" + name;
      if (is_binary_name(name)) continue;
      // Nao descemos em link simbolico: um link para uma pasta acima ("ln -s
      // .. atalho") faria a varredura andar em circulo para sempre.
      if (e->d_type == DT_LNK) {
        if (!is_dir_path(abs + "/" + name)) files.push_back(rel);
        continue;
      }
      bool dir = (e->d_type == DT_DIR);
      if (e->d_type == DT_UNKNOWN) dir = is_dir_path(abs + "/" + name);
      (dir ? dirs : files).push_back(rel);
    }
    closedir(d);
    std::sort(files.begin(), files.end());
    std::sort(dirs.begin(), dirs.end());
    for (auto& f : files) {
      if (out.size() >= limit) {
        if (truncated) *truncated = true;
        return out;
      }
      out.push_back(f);
    }
    for (auto& s : dirs) queue.push_back(s);
  }
  return out;
}

void FileTree::reveal(const std::string& path) {
  if (path.compare(0, root_.size(), root_) != 0) return;
  std::string rel = path.substr(root_.size());
  if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);

  Node* n = root_node_.get();
  size_t start = 0;
  while (start < rel.size()) {
    size_t slash = rel.find('/', start);
    std::string part = rel.substr(start, slash == std::string::npos
                                             ? std::string::npos
                                             : slash - start);
    if (!n->loaded) load_children(n);
    Node* found = nullptr;
    for (auto& c : n->children)
      if (c->name == part) { found = c.get(); break; }
    if (!found) return;
    if (slash == std::string::npos) {
      rebuild_visible();
      for (size_t i = 0; i < visible_.size(); i++)
        if (visible_[i] == found) {
          selected_ = static_cast<int>(i);
          ensure_visible();
          return;
        }
      return;
    }
    found->expanded = true;
    if (!found->loaded) load_children(found);
    n = found;
    start = slash + 1;
  }
}

void FileTree::ensure_visible() {
  int h = std::max(1, area_.h);
  if (selected_ < scroll_) scroll_ = selected_;
  if (selected_ >= scroll_ + h) scroll_ = selected_ - h + 1;
  if (scroll_ < 0) scroll_ = 0;
}

bool FileTree::activate(std::string* open_path) {
  if (selected_ < 0 || selected_ >= static_cast<int>(visible_.size())) return false;
  Node* n = visible_[selected_];
  if (n->is_dir) {
    n->expanded = !n->expanded;
    if (n->expanded && !n->loaded) load_children(n);
    rebuild_visible();
    ensure_visible();
    return true;
  }
  *open_path = n->path;
  return true;
}

bool FileTree::handle_key(const ui::KeyEvent& ev, std::string* open_path) {
  open_path->clear();
  int count = static_cast<int>(visible_.size());

  if (ev.is_code) {
    switch (static_cast<int>(ev.ch)) {
      case KEY_UP:
        if (selected_ > 0) selected_--;
        ensure_visible();
        return true;
      case KEY_DOWN:
        if (selected_ + 1 < count) selected_++;
        ensure_visible();
        return true;
      case KEY_PPAGE:
        selected_ = std::max(0, selected_ - std::max(1, area_.h - 1));
        ensure_visible();
        return true;
      case KEY_NPAGE:
        selected_ = std::min(count - 1, selected_ + std::max(1, area_.h - 1));
        ensure_visible();
        return true;
      case KEY_HOME: selected_ = 0; ensure_visible(); return true;
      case KEY_END: selected_ = count - 1; ensure_visible(); return true;
      case KEY_RIGHT: {
        if (selected_ < 0 || selected_ >= count) return true;
        Node* n = visible_[selected_];
        if (n->is_dir && !n->expanded) {
          n->expanded = true;
          if (!n->loaded) load_children(n);
          rebuild_visible();
        } else if (n->is_dir && selected_ + 1 < count) {
          selected_++;
        }
        ensure_visible();
        return true;
      }
      case KEY_LEFT: {
        if (selected_ < 0 || selected_ >= count) return true;
        Node* n = visible_[selected_];
        if (n->is_dir && n->expanded) {
          n->expanded = false;
          rebuild_visible();
        } else {
          // Sobe para a pasta que contem este item.
          int d = n->depth;
          for (int i = selected_ - 1; i >= 0; i--)
            if (visible_[i]->depth < d) { selected_ = i; break; }
        }
        ensure_visible();
        return true;
      }
      case KEY_ENTER: return activate(open_path);
      default: return false;
    }
  }

  switch (ev.ch) {
    case '\r': case '\n': case ' ': return activate(open_path);
    case '.':
      show_hidden_ = !show_hidden_;
      refresh();
      return true;
    default: return false;
  }
}

bool FileTree::click(int screen_x, int screen_y, std::string* open_path) {
  open_path->clear();
  if (!area_.contains(screen_x, screen_y)) return false;
  int idx = scroll_ + (screen_y - area_.y);
  if (idx < 0 || idx >= static_cast<int>(visible_.size())) return false;
  bool same = (idx == selected_);
  selected_ = idx;
  ensure_visible();
  if (same || visible_[idx]->is_dir) return activate(open_path);
  // Primeiro clique em um arquivo ja abre (como no VS Code).
  return activate(open_path);
}

void FileTree::scroll_by(int lines) {
  scroll_ += lines;
  int max_scroll = std::max(0, static_cast<int>(visible_.size()) - 1);
  if (scroll_ > max_scroll) scroll_ = max_scroll;
  if (scroll_ < 0) scroll_ = 0;
}

void FileTree::draw(const Rect& area, bool focused) {
  area_ = area;
  ensure_visible();

  for (int row = 0; row < area.h; row++) {
    int y = area.y + row;
    int idx = scroll_ + row;
    attrset(COLOR_PAIR(ui::kSidebar));
    ui::fill(y, area.x, area.w);
    if (idx >= static_cast<int>(visible_.size())) continue;

    Node* n = visible_[idx];
    bool is_sel = (idx == selected_);
    int pair = is_sel ? (focused ? ui::kSidebarSel : ui::kSidebarSelInactive)
                      : (n->is_dir ? ui::kSidebarDir : ui::kSidebar);
    attrset(COLOR_PAIR(pair) | (n->is_dir && !is_sel ? A_BOLD : 0));
    ui::fill(y, area.x, area.w);

    std::string label;
    for (int d = 1; d < n->depth; d++) label += "  ";
    if (n->is_dir) label += n->expanded ? "▾ " : "▸ ";
    else label += "  ";
    label += n->name;
    ui::put(y, area.x + 1, area.w - 1, label);
  }
  attrset(COLOR_PAIR(ui::kNormal));
}
