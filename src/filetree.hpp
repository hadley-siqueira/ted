// filetree.hpp - painel lateral com as pastas e arquivos do projeto.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ui.hpp"

class FileTree {
 public:
  struct Node {
    std::string path;
    std::string name;
    bool is_dir = false;
    bool expanded = false;
    bool loaded = false;
    int depth = 0;
    std::vector<std::unique_ptr<Node>> children;
  };

  explicit FileTree(const std::string& root_path);

  void draw(const Rect& area, bool focused);

  // Trata uma tecla. Se o usuario abriu um arquivo, devolve o caminho em
  // *open_path (senao ele fica vazio).
  bool handle_key(const ui::KeyEvent& ev, std::string* open_path);

  // Clique do mouse: devolve o arquivo aberto, se houver.
  bool click(int screen_x, int screen_y, std::string* open_path);
  void scroll_by(int lines);

  void refresh();
  void reveal(const std::string& path);   // expande ate o arquivo e seleciona
  const std::string& root() const { return root_; }
  bool show_hidden() const { return show_hidden_; }

 private:
  void load_children(Node* n);
  void rebuild_visible();
  void ensure_visible();
  bool activate(std::string* open_path);

  std::string root_;
  std::unique_ptr<Node> root_node_;
  std::vector<Node*> visible_;
  int selected_ = 0;
  int scroll_ = 0;
  bool show_hidden_ = false;
  Rect area_;
};
