#include "config.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

Config g_config;

std::string config_path() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  const char* home = std::getenv("HOME");
  std::string base;
  if (xdg && *xdg) base = xdg;
  else if (home && *home) base = std::string(home) + "/.config";
  else return "";
  return base + "/ted/ted.conf";
}

// Formato bem simples: uma opcao por linha, "chave = valor".
// Linhas vazias e comecadas por '#' sao ignoradas.
void load_config() {
  std::string path = config_path();
  if (path.empty()) return;
  std::ifstream in(path);
  if (!in) return;
  std::string line;
  while (std::getline(in, line)) {
    size_t hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    auto trim = [](std::string s) {
      size_t a = s.find_first_not_of(" \t\r\n");
      size_t b = s.find_last_not_of(" \t\r\n");
      if (a == std::string::npos) return std::string();
      return s.substr(a, b - a + 1);
    };
    std::string key = trim(line.substr(0, eq));
    std::string val = trim(line.substr(eq + 1));
    if (key.empty() || val.empty()) continue;
    auto as_bool = [&]() {
      return val == "1" || val == "true" || val == "sim" || val == "yes";
    };
    auto as_int = [&]() { return std::atoi(val.c_str()); };

    if (key == "theme" || key == "tema") g_config.theme = val;
    else if (key == "tab_width") g_config.tab_width = as_int();
    else if (key == "use_spaces") g_config.use_spaces = as_bool();
    else if (key == "auto_indent") g_config.auto_indent = as_bool();
    else if (key == "line_numbers") g_config.show_line_numbers = as_bool();
    else if (key == "auto_close") g_config.auto_close = as_bool();
    else if (key == "show_bracket_match") g_config.show_bracket_match = as_bool();
    else if (key == "mouse") g_config.mouse = as_bool();
    else if (key == "sidebar_width") g_config.sidebar_width = as_int();
    else if (key == "terminal_height") g_config.terminal_height = as_int();
    else if (key == "show_terminal") g_config.show_terminal = as_bool();
    else if (key == "show_sidebar") g_config.show_sidebar = as_bool();
  }
  if (g_config.tab_width < 1 || g_config.tab_width > 16) g_config.tab_width = 4;
  if (g_config.sidebar_width < 10) g_config.sidebar_width = 10;
  if (g_config.terminal_height < 3) g_config.terminal_height = 3;
}
