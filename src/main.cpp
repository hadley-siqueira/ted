// main.cpp - ponto de entrada do ted (Text EDitor).
//
//   ted                 abre a pasta atual
//   ted arquivo.c       abre o arquivo (e usa a pasta dele como projeto)
//   ted pasta/          abre a pasta como projeto
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "app.hpp"
#include "config.hpp"
#include "ui.hpp"

namespace {

void print_usage(const char* prog) {
  std::printf(
      "ted - editor de texto simples para o terminal\n"
      "\n"
      "uso: %s [opcoes] [arquivo|pasta ...]\n"
      "\n"
      "opcoes:\n"
      "  -h, --help       mostra esta ajuda\n"
      "  -v, --version    mostra a versao\n"
      "\n"
      "dentro do editor, F1 mostra todos os atalhos.\n",
      prog);
}

bool is_dir(const std::string& p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string dir_of(const std::string& p) {
  size_t slash = p.find_last_of('/');
  if (slash == std::string::npos) return ".";
  if (slash == 0) return "/";
  return p.substr(0, slash);
}

// Transforma o argumento em caminho absoluto a partir da pasta atual. O
// arquivo pode nao existir ainda (ted anotacoes.txt cria um arquivo novo),
// entao resolvemos so a pasta que o contem.
std::string absolute_path(const std::string& p) {
  if (!p.empty() && p[0] == '/') return p;
  if (!p.empty() && p[0] == '~') {
    const char* home = getenv("HOME");
    if (home) return std::string(home) + p.substr(1);
  }
  std::string dir = dir_of(p);
  std::string name = p.substr(dir == "." ? 0 : dir.size() + 1);
  char buf[4096];
  if (!realpath(dir.c_str(), buf)) return p;
  std::string abs = buf;
  if (abs == "/") return "/" + name;
  return abs + "/" + name;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> files;
  std::string root;

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") { print_usage(argv[0]); return 0; }
    if (a == "-v" || a == "--version") { std::printf("ted 1.0\n"); return 0; }
    if (!a.empty() && a[0] == '-') {
      std::fprintf(stderr, "opcao desconhecida: %s\n", a.c_str());
      return 2;
    }
    if (is_dir(a)) {
      if (root.empty()) root = a;
    } else {
      std::string abs = absolute_path(a);
      files.push_back(abs);
      if (root.empty()) {
        // Se o arquivo esta dentro da pasta atual, o "projeto" e a pasta
        // atual (o caso comum: cd projeto && ted main.c). Se nao, usamos a
        // pasta do proprio arquivo.
        char cwd[4096];
        std::string here = getcwd(cwd, sizeof(cwd)) ? cwd : ".";
        root = (abs.size() > here.size() &&
                abs.compare(0, here.size(), here) == 0 && abs[here.size()] == '/')
                   ? here
                   : dir_of(abs);
      }
    }
  }
  if (root.empty()) root = ".";

  load_config();

  if (!ui::init()) {
    std::fprintf(stderr, "nao foi possivel iniciar o terminal (ncurses).\n");
    return 1;
  }

  int rc = 0;
  {
    App app(root, files);
    rc = app.run();
  }
  ui::shutdown();
  return rc;
}
