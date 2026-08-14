// fuzzy.hpp - casamento "fuzzy" de nomes de arquivo.
//
// A ideia: "edvw" deve encontrar "src/editorview.cpp". Cada letra da consulta
// precisa aparecer no texto, na mesma ordem, mas nao necessariamente juntas.
// Entre os arquivos que casam, damos nota para os mais provaveis aparecerem
// primeiro (letras coladas, inicio de palavra, casar no nome e nao na pasta).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Devolve false se a consulta nao casa. Quando casa, preenche *score (maior =
// melhor) e *positions com os indices de byte casados, para poder destaca-los.
// Nao diferencia maiusculas de minusculas. Consulta vazia casa com tudo.
bool fuzzy_match(const std::string& text, const std::string& query, int* score,
                 std::vector<size_t>* positions);
