// config.hpp - opcoes globais do editor (valores pensados para iniciantes).
#pragma once

#include <string>

struct Config {
  std::string theme = "default";   // paleta de cores (veja theme.cpp)
  int tab_width = 4;          // largura visual do TAB
  bool use_spaces = true;     // TAB insere espacos (mais previsivel p/ alunos)
  bool auto_indent = true;    // mantem a indentacao ao apertar Enter
  bool show_line_numbers = true;
  bool auto_close = true;     // fecha (), [], {}, "" e '' automaticamente
  bool show_bracket_match = true;  // destaca o par de chaves sob o cursor
  bool mouse = true;          // clique/rolagem do mouse
  // Tetos do desfazer/refazer. Valem os dois: o que estourar primeiro corta.
  // 'undo_levels' e o conceito que o aluno enxerga (quantos passos da para
  // voltar); 'undo_memory_mb' e a rede de seguranca, porque cada nivel guarda
  // uma copia do arquivo inteiro (~2x o tamanho dele). Sem o teto de memoria,
  // 500 niveis de um package-lock.json passam de 700 MB - veja a secao 10 do
  // NOTAS.md.
  int undo_levels = 500;      // 1 a 10000
  int undo_memory_mb = 8;     // 1 a 1024, somando as pilhas de undo e redo

  int sidebar_width = 26;     // largura do painel de arquivos
  int terminal_height = 10;   // altura do painel do terminal
  bool show_terminal = true;
  bool show_sidebar = true;
};

extern Config g_config;

// Caminho do arquivo de configuracao (~/.config/ted/ted.conf) e leitura dele.
std::string config_path();
void load_config();
