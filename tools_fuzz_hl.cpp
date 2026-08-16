// Fuzz do realce (NOTAS.md secao 4): roda highlight() linha a linha em TODAS as
// linguagens do enum, sobre arquivos reais, e aborta com SIGALRM se alguma
// regra ficar parada no mesmo byte (o bug do '$' no Makefile foi assim).
//
// Verifica tambem duas invariantes:
//   - o vetor de cores tem exatamente um elemento por byte da linha;
//   - o estado devolvido cabe nos 16 bits do empacotamento.
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "highlight.hpp"

static const char* g_arquivo = "?";
static int g_linha = 0;
static int g_lang = -1;

extern "C" void on_alarm(int) {
  std::fprintf(stderr,
               "\nTRAVOU: lang=%d arquivo=%s linha=%d\n", g_lang, g_arquivo,
               g_linha);
  std::_Exit(2);
}

int main(int argc, char** argv) {
  std::signal(SIGALRM, on_alarm);

  const int kNumLangs = 19;   // ate Lang::Vhdl
  long total_linhas = 0;

  for (int a = 1; a < argc; a++) {
    std::ifstream in(argv[a]);
    if (!in) continue;
    std::vector<std::string> linhas;
    std::string l;
    while (std::getline(in, l)) linhas.push_back(l);
    g_arquivo = argv[a];

    for (int lang = 0; lang < kNumLangs; lang++) {
      g_lang = lang;
      Highlighter hl(static_cast<Lang>(lang));
      int state = Highlighter::kNormal;
      std::vector<int> cores;
      for (size_t i = 0; i < linhas.size(); i++) {
        g_linha = static_cast<int>(i) + 1;
        alarm(5);
        state = hl.highlight(linhas[i], state, &cores);
        alarm(0);
        if (cores.size() != linhas[i].size()) {
          std::fprintf(stderr,
                       "\nCORES ERRADAS: lang=%d %s:%d  %zu cores para %zu "
                       "bytes\n",
                       lang, argv[a], g_linha, cores.size(), linhas[i].size());
          return 3;
        }
        if (state < 0 || state > 0xFFFF) {
          std::fprintf(stderr, "\nESTADO FORA DA FAIXA: lang=%d %s:%d = %d\n",
                       lang, argv[a], g_linha, state);
          return 4;
        }
        total_linhas++;
      }
    }
  }
  std::printf("ok: %ld linhas realcadas (%d linguagens x %d arquivos)\n",
              total_linhas, kNumLangs, argc - 1);
  return 0;
}
