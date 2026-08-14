#include "utf8.hpp"

#include <wchar.h>

namespace utf8 {

size_t char_len(const std::string& s, size_t i) {
  if (i >= s.size()) return 0;
  unsigned char c = static_cast<unsigned char>(s[i]);
  size_t len = 1;
  if ((c & 0x80) == 0x00) len = 1;
  else if ((c & 0xE0) == 0xC0) len = 2;
  else if ((c & 0xF0) == 0xE0) len = 3;
  else if ((c & 0xF8) == 0xF0) len = 4;
  else len = 1;  // byte invalido: trata como 1 para nunca travar
  if (i + len > s.size()) len = 1;
  return len;
}

size_t next(const std::string& s, size_t i) {
  if (i >= s.size()) return s.size();
  return i + char_len(s, i);
}

size_t prev(const std::string& s, size_t i) {
  if (i == 0) return 0;
  size_t j = i - 1;
  while (j > 0 && is_continuation(static_cast<unsigned char>(s[j]))) j--;
  return j;
}

uint32_t decode(const std::string& s, size_t i) {
  if (i >= s.size()) return 0;
  unsigned char c = static_cast<unsigned char>(s[i]);
  size_t len = char_len(s, i);
  if (len == 1) return c;
  uint32_t cp = 0;
  if (len == 2) cp = c & 0x1F;
  else if (len == 3) cp = c & 0x0F;
  else cp = c & 0x07;
  for (size_t k = 1; k < len; k++) {
    unsigned char cc = static_cast<unsigned char>(s[i + k]);
    if (!is_continuation(cc)) return c;  // sequencia quebrada
    cp = (cp << 6) | (cc & 0x3F);
  }
  return cp;
}

std::string encode(uint32_t cp) {
  std::string out;
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
  return out;
}

int cp_width(uint32_t cp) {
  if (cp == '\t') return 1;  // tratado a parte por width()
  if (cp < 32 || cp == 127) return 1;  // mostrado como caractere de controle
  int w = ::wcwidth(static_cast<wchar_t>(cp));
  if (w < 0) return 1;
  return w;
}

int width(const std::string& s, size_t from, size_t to, int tab_width,
          int start_col) {
  if (tab_width <= 0) tab_width = 4;
  int col = start_col;
  size_t i = from;
  if (to > s.size()) to = s.size();
  while (i < to) {
    uint32_t cp = decode(s, i);
    if (cp == '\t') {
      col += tab_width - (col % tab_width);
    } else {
      col += cp_width(cp);
    }
    i = next(s, i);
  }
  return col - start_col;
}

size_t col_to_byte(const std::string& s, int col, int tab_width) {
  if (tab_width <= 0) tab_width = 4;
  if (col <= 0) return 0;
  int cur = 0;
  size_t i = 0;
  while (i < s.size()) {
    uint32_t cp = decode(s, i);
    int w = (cp == '\t') ? tab_width - (cur % tab_width) : cp_width(cp);
    if (cur + w > col) return i;
    cur += w;
    i = next(s, i);
    if (cur == col) return i;
  }
  return s.size();
}

size_t count(const std::string& s) {
  size_t n = 0;
  for (size_t i = 0; i < s.size(); i = next(s, i)) n++;
  return n;
}

}  // namespace utf8
