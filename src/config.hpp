// config.hpp - opcoes globais do editor (valores pensados para iniciantes).
#pragma once

#include <string>

struct Config {
  int tab_width = 4;          // largura visual do TAB
  bool use_spaces = true;     // TAB insere espacos (mais previsivel p/ alunos)
  bool auto_indent = true;    // mantem a indentacao ao apertar Enter
  bool show_line_numbers = true;
  bool auto_close = true;     // fecha (), [], {}, "" e '' automaticamente
  bool mouse = true;          // clique/rolagem do mouse
  int sidebar_width = 26;     // largura do painel de arquivos
  int terminal_height = 10;   // altura do painel do terminal
  bool show_terminal = true;
  bool show_sidebar = true;
};

extern Config g_config;

// Caminho do arquivo de configuracao (~/.config/ted/ted.conf) e leitura dele.
std::string config_path();
void load_config();
