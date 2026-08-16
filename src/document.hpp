// document.hpp - o buffer de texto de um arquivo aberto.
//
// O texto e guardado como um vetor de linhas (sem o '\n' no fim). Uma posicao
// no texto e um par (linha, byte dentro da linha). Todas as edicoes passam
// por insert()/erase(), que sao as unicas funcoes que mexem no vetor - assim
// fica facil manter o undo e o "arquivo modificado" corretos.
#pragma once

#include <chrono>
#include <string>
#include <vector>

struct Pos {
  int line = 0;
  size_t byte = 0;

  bool operator==(const Pos& o) const {
    return line == o.line && byte == o.byte;
  }
  bool operator!=(const Pos& o) const { return !(*this == o); }
  bool operator<(const Pos& o) const {
    if (line != o.line) return line < o.line;
    return byte < o.byte;
  }
  bool operator<=(const Pos& o) const { return *this < o || *this == o; }
};

// Tipo da edicao, usado para agrupar varias digitacoes em um unico "desfazer".
enum class EditKind { Typing, Deleting, Other };

class Document {
 public:
  Document();

  // ---- arquivo -----------------------------------------------------------
  bool load(const std::string& path, std::string* error);
  bool save(std::string* error);
  bool save_as(const std::string& path, std::string* error);

  const std::string& path() const { return path_; }
  // Define o nome do arquivo sem gravar nada (usado para arquivos novos).
  void set_path(const std::string& path) { path_ = path; }
  bool has_path() const { return !path_.empty(); }
  std::string display_name() const;   // so o nome do arquivo
  bool modified() const { return modified_; }
  bool crlf() const { return crlf_; }

  // ---- acesso ao texto ---------------------------------------------------
  int line_count() const { return static_cast<int>(lines_.size()); }
  const std::string& line(int i) const;
  const std::vector<std::string>& lines() const { return lines_; }
  size_t version() const { return version_; }  // muda a cada edicao

  Pos clamp(Pos p) const;
  Pos begin_pos() const { return Pos{0, 0}; }
  Pos end_pos() const;
  std::string text_range(Pos a, Pos b) const;
  std::string text() const;

  // ---- edicao ------------------------------------------------------------
  // Marca o inicio de uma edicao do usuario (guarda estado para o undo).
  void begin_edit(EditKind kind, Pos cursor);
  Pos insert(Pos at, const std::string& text);
  std::string erase(Pos a, Pos b);

  // ---- desfazer/refazer --------------------------------------------------
  bool undo(Pos* cursor);
  bool redo(Pos* cursor);
  bool can_undo() const { return !undo_stack_.empty(); }
  bool can_redo() const { return !redo_stack_.empty(); }

  // ---- busca -------------------------------------------------------------
  // Procura a partir de 'from' (inclusive), dando a volta no arquivo.
  bool find(const std::string& needle, Pos from, bool forward,
            bool case_sensitive, Pos* match_begin, Pos* match_end) const;

 private:
  struct Snapshot {
    std::vector<std::string> lines;
    Pos cursor;
    bool modified;
    size_t bytes = 0;   // custo aproximado deste snapshot, medido ao criar
  };

  void push_undo(Pos cursor);
  // Descarta os snapshots mais antigos ate caber nos dois tetos do ted.conf.
  void trim_undo();
  void touch();

  std::vector<std::string> lines_;
  std::string path_;
  bool modified_ = false;
  bool crlf_ = false;              // arquivo usava CRLF (Windows)
  bool final_newline_ = true;      // arquivo terminava com '\n'
  size_t version_ = 1;

  std::vector<Snapshot> undo_stack_;
  std::vector<Snapshot> redo_stack_;
  // Soma de 'bytes' de cada pilha, mantida na mao para o teto de memoria nao
  // ter que percorrer os snapshots a cada edicao.
  size_t undo_bytes_ = 0;
  size_t redo_bytes_ = 0;
  EditKind last_kind_ = EditKind::Other;
  std::chrono::steady_clock::time_point last_edit_time_{};
};
