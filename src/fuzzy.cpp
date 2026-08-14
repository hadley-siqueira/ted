#include "fuzzy.hpp"

#include <cctype>

namespace {

char lower(char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool is_separator(char c) {
  return c == '/' || c == '_' || c == '-' || c == '.' || c == ' ';
}

// Uma posicao "comeca palavra" se e o primeiro caractere, vem depois de um
// separador, ou e uma maiuscula no meio de minusculas (camelCase).
bool starts_word(const std::string& text, size_t i) {
  if (i == 0) return true;
  if (is_separator(text[i - 1])) return true;
  unsigned char prev = static_cast<unsigned char>(text[i - 1]);
  unsigned char cur = static_cast<unsigned char>(text[i]);
  return std::islower(prev) && std::isupper(cur);
}

constexpr int kMatch = 16;         // por letra casada
constexpr int kConsecutive = 10;   // letra colada na anterior
constexpr int kWordStart = 14;     // letra no comeco de uma palavra
constexpr int kInName = 8;         // letra no nome do arquivo (depois da /)
constexpr int kNamePrefix = 30;    // o nome do arquivo *comeca* com a consulta
constexpr int kGapPenalty = 2;     // por letra pulada entre duas casadas

}  // namespace

bool fuzzy_match(const std::string& text, const std::string& query, int* score,
                 std::vector<size_t>* positions) {
  if (positions) positions->clear();
  if (score) *score = 0;
  if (query.empty()) return true;
  if (query.size() > text.size()) return false;

  // Passo 1: varredura da esquerda para a direita so para saber se casa.
  size_t qi = 0;
  for (size_t i = 0; i < text.size() && qi < query.size(); i++)
    if (lower(text[i]) == lower(query[qi])) qi++;
  if (qi < query.size()) return false;

  // Passo 2: agora de tras para frente, a partir do fim do texto. Isso junta
  // as letras o mais a direita possivel e evita o resultado esparso que a
  // varredura gulosa produz (em "editorview", "edv" casa "ed...v" e nao
  // "e...d...v").
  std::vector<size_t> pos(query.size());
  size_t qj = query.size();
  for (size_t i = text.size(); i-- > 0 && qj > 0;)
    if (lower(text[i]) == lower(query[qj - 1])) pos[--qj] = i;

  // Onde comeca o nome do arquivo (depois da ultima barra).
  size_t name_start = text.find_last_of('/');
  name_start = (name_start == std::string::npos) ? 0 : name_start + 1;

  int total = 0;
  for (size_t k = 0; k < pos.size(); k++) {
    total += kMatch;
    if (starts_word(text, pos[k])) total += kWordStart;
    if (pos[k] >= name_start) total += kInName;
    if (k > 0) {
      size_t gap = pos[k] - pos[k - 1] - 1;
      if (gap == 0) total += kConsecutive;
      else total -= static_cast<int>(gap) * kGapPenalty;
    }
  }
  // "stdio" tem que achar stdio.h antes de OpenEXR/ImfStdIO.h: quando o nome
  // do arquivo comeca com a consulta, isso vale mais do que qualquer soma de
  // bonus espalhados pelo caminho.
  if (pos[0] == name_start) total += kNamePrefix;

  // Casar cedo vale um pouquinho mais do que casar no fim de um caminho longo.
  total -= static_cast<int>(pos[0]) / 4;

  if (score) *score = total;
  if (positions) *positions = pos;
  return true;
}
