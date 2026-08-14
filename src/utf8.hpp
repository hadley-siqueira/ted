// utf8.hpp - utilitarios para lidar com texto UTF-8.
//
// O editor guarda cada linha como um std::string de *bytes*. Um caractere
// acentuado ("ç", "á") ocupa mais de um byte, entao nunca podemos andar pelo
// texto de byte em byte: usamos as funcoes abaixo para pular um caractere
// inteiro de cada vez e para descobrir quantas *colunas da tela* ele ocupa.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace utf8 {

// Quantos bytes tem o caractere que comeca em s[i].
size_t char_len(const std::string& s, size_t i);

// Indice do byte que inicia o proximo/anterior caractere.
size_t next(const std::string& s, size_t i);
size_t prev(const std::string& s, size_t i);

// Decodifica o caractere que comeca em i (retorna o code point Unicode).
uint32_t decode(const std::string& s, size_t i);

// Converte um code point para bytes UTF-8.
std::string encode(uint32_t cp);

// Largura em colunas de tela de um code point (0, 1 ou 2 para CJK/emoji).
int cp_width(uint32_t cp);

// Largura em colunas do trecho s[from, to), expandindo TAB ate o proximo
// multiplo de tab_width. start_col e a coluna onde o trecho comeca na tela.
int width(const std::string& s, size_t from, size_t to, int tab_width,
          int start_col = 0);

// Largura total da linha.
inline int width(const std::string& s, int tab_width) {
  return width(s, 0, s.size(), tab_width, 0);
}

// Converte coluna de tela -> indice de byte (aproxima para o caractere mais
// proximo que nao ultrapassa a coluna pedida).
size_t col_to_byte(const std::string& s, int col, int tab_width);

// Converte indice de byte -> coluna de tela.
inline int byte_to_col(const std::string& s, size_t byte, int tab_width) {
  return width(s, 0, byte, tab_width, 0);
}

// Numero de caracteres (nao bytes) da string.
size_t count(const std::string& s);

// True se o byte e continuacao (10xxxxxx) de um caractere multibyte.
inline bool is_continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

}  // namespace utf8
