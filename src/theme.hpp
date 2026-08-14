// theme.hpp - paletas de cores do editor.
//
// Cada cor e escrita em RGB (0xRRGGBB), do jeito que os autores das paletas
// publicam. Como o terminal so aceita as 256 cores da paleta xterm, na hora
// de montar os pares de cor convertemos cada RGB para a cor mais parecida
// (veja rgb_to_256). Fica bem proximo do original e funciona em qualquer
// terminal com 256 cores, sem precisar redefinir a paleta dele.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

using Color = int32_t;
constexpr Color kDefaultColor = -1;   // "a cor padrao do terminal"

struct Theme {
  const char* name;

  // Cores de base da interface.
  Color bg;         // fundo do editor
  Color fg;         // texto comum
  Color bg_alt;     // fundo da barra lateral, das abas e dos titulos
  Color bg_sel;     // fundo do texto selecionado
  Color fg_dim;     // numeros de linha, comentarios, texto apagado
  Color accent;     // barra de status, aba ativa, painel em foco
  Color accent_fg;  // texto escrito por cima de 'accent'
  Color accent2;    // pastas na barra lateral

  // Cores do realce de sintaxe.
  Color keyword;
  Color type;
  Color string;
  Color comment;
  Color number;
  Color preproc;
  Color punct;      // pontuacao de tags (HTML/JSX)

  // Avisos e busca.
  Color modified;   // arquivo com alteracoes nao salvas
  Color search_bg;
  Color search_fg;
};

// Devolve o tema pelo nome (aceita "rose-pine", "rose_pine" ou "Rose Pine").
// Se o nome nao existir, devolve o tema "default".
const Theme& theme_by_name(const std::string& name);
bool theme_exists(const std::string& name);
std::vector<std::string> theme_names();

// Converte uma cor RGB para o indice mais proximo da paleta de 256 cores.
int rgb_to_256(int r, int g, int b);
inline int rgb_to_256(Color rgb) {
  return rgb_to_256((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// Brilho aproximado de uma cor (0..255).
inline int luminance(Color rgb) {
  int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  return (r * 30 + g * 59 + b * 11) / 100;
}

// Mistura duas cores ('percent' e quanto de 'b' entra em 'a').
Color blend(Color a, Color b, int percent);

// Duas cores parecidas podem cair no mesmo indice da paleta de 256 - e ai o
// fundo da barra lateral some dentro do fundo do editor. Esta funcao devolve o
// indice de 'want' garantindo que seja diferente de 'avoid': se coincidir, vai
// aproximando 'want' de 'toward' (o que preserva o tom da cor) ate mudar.
int shade_apart(Color want, Color toward, int avoid);
