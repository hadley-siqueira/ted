# Notas de desenvolvimento do ted

Anotações para quem for mexer no código depois (inclusive eu mesmo, em outra
sessão). O `README.md` documenta o editor para quem **usa**; este arquivo
guarda o que ficou na cabeça de quem **escreveu**: decisões, armadilhas, bugs
já resolvidos e o que fazer em seguida.

Estado: ~7.000 linhas de C++17 em `src/` (25 arquivos), compila com `g++` e
`ncursesw`, sem nenhuma outra dependência. `make` gera `./ted`.

## Para que serve (leia antes de priorizar)

**O uso principal é desenvolvimento web: Node.js, Express, React.** Não é uma
turma de C. Isso não dá para deduzir do código, e o `README.md` induz ao erro:
ele abre com `main.c` e `gcc main.c && ./a.out`, e dedica um bloco à regra do
TAB no Makefile. Uma sessão já foi priorizada errada por causa disso.

O que a stack implica na prática:

- **Vários processos ao mesmo tempo**: `npm run dev` (Vite), `node server.js`
  ou `nodemon`, mais um shell livre para `npm install`/`git`. Um terminal só
  não serve — foi o que motivou as abas de terminal (§8.1).
- **O erro chega em tempo de execução, não ao compilar.** A stack trace do Node
  aparece toda vez que se salva um arquivo quebrado, não só quando se manda
  compilar. Isso torna "pular para o erro" (§5) mais valioso, não menos.
- **Já está coberto**: `node_modules` é ignorado na varredura do `Ctrl+P`
  (`is_noise()` em `filetree.cpp`, e ali o filtro é incondicional); `.ts`,
  `.tsx`, `.vue` e `.svelte` já caem no realce de JavaScript.
- **Pendência de documentação**: o `README.md` precisa de uma passada para
  parar de se apresentar como editor de C.

---

## 1. Decisões de projeto (e por quê)

- **Texto = `vector<string>`, uma linha por posição.** Toda edição passa por
  `Document::insert`/`erase`. Se você mexer no vetor por fora, o desfazer e o
  marcador de "modificado" saem de sincronia.
- **Desfazer por snapshot**: `begin_edit()` copia o vetor de linhas inteiro,
  agrupando digitação por tipo e por 600 ms. Simples e correto, mas é O(arquivo)
  por grupo — é a razão do limite de 16 MB para abrir (veja §3).
- **Realce por linha, com estado que atravessa linhas.** `highlight()` recebe o
  estado em que a linha começa e devolve o estado em que termina. Isso permite
  desenhar só as linhas visíveis e manter um cache incremental
  (`EditorView::hl_states_`, invalidado por `Document::version()`).
  Não há parser nem regex: são heurísticas na ordem em que aparecem no laço.
- **Terminal embutido é um emulador VT de verdade** (`terminal.cpp`): `forkpty`
  + parser de sequências ANSI + matriz de células com cor e atributos. Suporta
  tela alternativa (vim/less), região de rolagem, 256 cores e histórico.
- **Mouse decodificado à mão** (`ui::read_key`). O ncurses desta distribuição
  *pede* eventos em formato SGR ao terminal (`\033[?1006h`) mas só sabe ler o
  formato X10 antigo — então cliques simplesmente não chegavam. Aceitamos os
  dois caminhos: `KEY_MOUSE` do ncurses e o parse manual de `ESC [ < b;x;y M`.
  **Não troque isso por `getmouse()` puro.**
- **Temas em RGB convertidos para as 256 cores do terminal** (`theme.cpp`).
  Redefinir a paleta do terminal com `init_color()` daria fidelidade perfeita,
  mas quebraria as cores do terminal embutido e não funciona em tmux/PuTTY.
- **Indentação por linguagem**: `EditorView::indent_unit()` decide TAB ou
  espaços. Makefile é sempre TAB (o `make` recusa espaços).

---

## 2. Invariantes que não podem ser quebradas

Cada uma dessas linhas existe porque um bug real aconteceu:

1. **`highlight.cpp`: todo caractere aceito por `ident_start()` precisa ser
   aceito por `ident_char()`.** Senão a regra de identificador não consome nada
   e o scanner trava. Há uma rede no laço (`if (i == last_i) { i++; }`) — não a
   remova. Pela mesma razão, os sufixos de nome adicionados por linguagem
   (`empty?` e `save!` no Ruby, `foo'` em Haskell e OCaml) só ampliam o fim do
   identificador, nunca o começo: `?`, `!` e `'` continuam fora de
   `ident_start()`.
2. **`terminal.cpp`: toda escrita na tela passa por `row_at(y)`**, que prende o
   índice ao intervalo válido. E todo ajuste de `cx_`/`cy_` precisa de piso 0,
   não só de teto.
3. **Índices de byte ≠ colunas de tela.** `Pos::byte` é byte dentro da linha;
   a conversão para coluna é `utf8::byte_to_col` (e o inverso, `col_to_byte`).
   Nunca faça `line[cursor.col]`.
4. **A ajuda do `F1` tem que caber em 80×24**: no máximo ~21 linhas de conteúdo
   e 76 colunas por linha. Ao acrescentar um atalho, compacte outro.
5. **No terminal, só F1–F10 e Alt+… são do editor**; todo o resto vai para o
   shell (inclusive Ctrl+C). Não capture Ctrl+letra com foco no terminal.
6. **`Ctrl+Shift+X` não existe no terminal** — chega igual a `Ctrl+X`. Por isso
   "salvar como" é `Alt+S` e "comentar" é `Ctrl+/` (byte 31) ou `Alt+C`.
7. **Um arquivo, um `Document`.** Duas views do mesmo caminho com buffers
   separados fazem o segundo `Ctrl+S` apagar o trabalho do primeiro, sem aviso.
   Guardam isso: `open_file()` (procura em **todos** os painéis com
   `find_open_doc()` antes de carregar) e `do_save()` (recusa "salvar como" por
   cima de um caminho já aberto em outra aba). Veja a §3.
8. **`EditorView::draw()` tem que chamar `sync_to_doc()` antes de desenhar.**
   Com a tela dividida, o mesmo `Document` aparece em duas views; quando uma
   edita, o cursor, a âncora e a rolagem da outra podem apontar além do fim do
   arquivo. Não trava (`Document` é defensivo em toda parte), mas o painel fica
   em branco e a barra de status mostra uma linha que não existe mais.

---

## 3. Bugs já corrigidos (não reintroduzir)

| Sintoma | Causa | Onde |
|---|---|---|
| Trava ao abrir Makefile (100% CPU) | `$` iniciava identificador mas não fazia parte dele → laço parado | `highlight.cpp` |
| Segfault ao encolher o terminal (Alt+↓) | `cy_ -= src_start` ficava negativo; clamp só tinha teto | `terminal.cpp` |
| Mouse não funcionava | ncurses pede SGR e lê X10 | `ui.cpp` |
| `ted src/main.c` abria arquivo vazio errado | argumento resolvido contra a raiz do projeto, não contra o diretório atual | `main.cpp` |
| Arquivo novo salvo como `.c` ficava sem realce | linguagem detectada só no construtor | `editorview.cpp` / `app.cpp` |
| Barra lateral sumia no Rosé Pine | dois fundos próximos caíam no mesmo índice de 256 cores | `ui.cpp` (`shade_apart`) |
| `Ctrl+P` andava em círculo | link simbólico para pasta acima | `filetree.cpp` |
| Editor comia a memória em arquivo grande | undo por snapshot | limite de 16 MB em `app.cpp` |
| Abrir o mesmo arquivo em outro painel criava **dois buffers**; salvar os dois perdia texto | `open_file()` procurava o caminho só no painel focado | `find_open_doc()` em `app.cpp` |
| "Salvar como" por cima de um arquivo já aberto criava **dois buffers** do mesmo caminho (bug antigo, anterior ao split) | `do_save()` não checava colisão de caminho | `do_save()` em `app.cpp` |
| `Ctrl+W` perguntava "salvar alterações?" mesmo com o arquivo aberto em outro painel | não contava quantas views mostram o documento | `views_of_doc()` em `app.cpp` |
| "Salvar como" `.txt`→`.c` só trocava o realce no painel que salvou | `refresh_language()` só na view ativa | `refresh_language_of()` em `app.cpp` |
| Painel estreito escondia a aba ativa (mostrava a aba 0 e `>`) | `draw_tabbar()` desenhava sempre a partir da primeira aba | `draw_tabbar()` rola até a ativa, com `<`/`>` |
| Com o foco no terminal, a aba de terminal ativa ficava igual às outras | `kTabActive` e `kPaneTitleActive` são **o mesmo par** (`accent_fg`/`accent`): a aba ativa sumia dentro do fundo da barra | `draw_terminal_tabs()` em `app.cpp` |
| Shell encerrado (`exit`/`Ctrl+D`) continuava na barra de terminais | ninguém removia o `Terminal` parado da lista | `reap_terminals()` em `app.cpp` |

---

## 4. Como testar (o que funcionou aqui)

Não dá para testar TUI "no olho" — tudo foi verificado com **tmux**:

```sh
tmux new-session -d -s t -x 100 -y 30 "./ted ."
tmux send-keys -t t C-s                 # teclas normais
tmux send-keys -t t -H 1b 5b 31 3b 35 46  # bytes crus: Ctrl+End
tmux capture-pane -t t -p               # tela como texto
tmux capture-pane -t t -p -e            # com as cores (para conferir realce)
```

Sequências que precisam de `-H` (o tmux não tem nome para elas):

| Tecla | bytes |
|---|---|
| Ctrl+End / Ctrl+Home | `1b 5b 31 3b 35 46` / `... 48` |
| Alt+↓ / Alt+↑ | `1b 5b 31 3b 33 42` / `... 41` |
| Alt+Shift+↑/↓ (mover linha) | `1b 5b 31 3b 34 41` / `42` |
| Shift+Tab | `1b 5b 5a` |
| Ctrl+/ | `1f` |
| `;` literal | `3b` (o tmux trata `;` como separador!) |
| Clique do mouse (SGR) | `1b 5b 3c 30 3b <col> 3b <lin> 4d` (solta: `6d`) |

**Detectar travamento sem olhar a tela** — comparar tempo de CPU:

```sh
PID=$(pgrep -x ted); T0=$(awk '{print $14+$15}' /proc/$PID/stat)
sleep 1.5; T1=$(awk '{print $14+$15}' /proc/$PID/stat)
# > 50 ticks em 1,5 s = laço infinito; ocioso normal = 0
```

**Fuzz do realce** (achou o bug do `$` e valida qualquer mudança em
`highlight.cpp`): `tools_fuzz_hl.cpp` na raiz. Roda `highlight()` linha a linha
em **todas** as linguagens do enum sobre arquivos reais, usa `alarm(5)` +
`SIGALRM` para abortar em laço infinito, e ainda confere duas invariantes: o
vetor de cores tem um elemento por byte, e o estado devolvido cabe nos 16 bits
do empacotamento.

```sh
make
g++ -std=c++17 -O2 -Isrc -o /tmp/fuzz_hl tools_fuzz_hl.cpp \
    build/highlight.o build/config.o build/ui.o build/theme.o build/utf8.o \
    $(pkg-config --libs ncursesw)
/tmp/fuzz_hl src/*.cpp src/*.hpp Makefile README.md NOTAS.md
```

Última execução: 167 mil linhas (19 linguagens × 32 arquivos) sem nenhum
travamento. **Rode sempre que mexer em `highlight.cpp`** — e com amostras da
linguagem nova, porque os fontes do próprio projeto não exercitam sintaxe de
Ruby, Haskell, OCaml, Verilog nem VHDL.

Para crash, `gdb` em modo batch dentro do tmux funciona bem:
`gdb -q -batch -ex 'set logging file /tmp/g.log' -ex 'set logging enabled on'
-ex run -ex bt --args ./ted .`

---

## 5. Backlog recomendado (em ordem)

Reordenado depois que a stack alvo ficou clara (veja "Para que serve", no topo).

1. **Pular para o erro** (~150 linhas + um acessor novo no `Terminal`). `F8` e
   clique sobre `arquivo:linha:coluna` na saída do terminal abrem o arquivo na
   posição. Reaproveitar `open_file()` + `EditorView::select_range()`.
   **`Terminal` hoje não expõe o texto da tela nem o histórico — só `draw()`.**
   Precisa de algo como `std::string line_text(int row) const`.
   O parser tem que cobrir os formatos do Node, não só `arquivo:linha:coluna`:
   ```
   at Object.<anonymous> (/home/aluno/app/index.js:3:15)   ← entre parênteses
   /src/App.jsx:12:5                                        ← relativo (Vite)
   ```
   **Pré-requisito já pago**: sem a invariante §2.7 ("um arquivo, um
   `Document`"), pular para um erro num arquivo já aberto em outro painel
   criaria um segundo buffer e o próximo `Ctrl+S` apagaria o trabalho.
2. **Redimensionar os painéis divididos** (§7.5, ~30 linhas). Subiu de
   prioridade por causa do `Alt+H`: repartir 12 linhas em 6/6 dói mais do que
   repartir 54 colunas.
3. **Busca em todo o projeto** (~60 linhas, era ~100). Ficou mais barata: o
   encanamento do `Ctrl+T` (`pane_order()`, `PickerItem::pane`, dedup por
   `Document`) é o que uma busca global precisa para o resultado aterrissar no
   painel certo, e `FileTree::list_all_files()` já varre o projeto.
4. **Área de transferência do sistema** (~80 linhas). `wl-copy`/`xclip` quando
   existirem e OSC 52 como reserva — **só OSC 52 não basta**, o GNOME Terminal
   (padrão do Ubuntu) não suporta. Baixou de prioridade: é trabalho de
   compatibilidade que varia por máquina, e o `F9` já dá uma saída aceitável.
5. **Restaurar sessão** ao reabrir o projeto. O escopo cresceu com os painéis
   (agora teria que salvar o layout da grade, não só as abas) e o valor numa
   aula de uma hora é baixo.

**Saiu do backlog**: "salvar ao trocar o foco para o terminal". Ela existia
para o erro do iniciante que compila sem salvar; com hot reload (`nodemon`,
Vite) o aluno salva o tempo todo por reflexo, e o problema quase não acontece.

**Evitar por ora**: múltiplos cursores, LSP/autocomplete e git integrado. São
de usuário avançado e aumentam a superfície de confusão para quem nunca usou
editor.

**Quebra automática de linha: decidido que NÃO.** Reafirmado pelo autor em
agosto/2026. Registrando a tentação para não relitigar: JSX indenta fundo e um
painel dividido fica com ~21 colunas de código, então a rolagem horizontal
incomoda de verdade nessa stack. Mesmo assim o custo continua o que sempre foi
— contamina cursor, seleção, rolagem e desenho da `EditorView` inteira, porque
hoje **linha do arquivo = linha da tela**.

---

## 6. Miudezas úteis

- **Atalhos Ctrl livres**: `E`, `L`, `U`. (`H`, `I` e `M` são backspace, tab e
  enter no terminal — não use.) Alt livre: quase tudo, exceto `S`, `C`, `V`,
  `W` e `1`/`2`/`3`. Teclas de função livres: `F7` foi usada pela divisão de
  painéis; `F8` está reservada para o "pular para o erro do compilador" (§5).
- **Tetos**: 20.000 arquivos na varredura do `Ctrl+P`, 300 resultados na lista,
  500 na busca de texto, 3.000 linhas de histórico no terminal, 500 níveis de
  desfazer, 16 MB por arquivo.
- **Desempenho medido**: arquivo de 50 mil linhas → ~3 ms por tecla; picker com
  20 mil arquivos → ~8 ms por tecla; varredura de 20 mil arquivos → ~50 ms;
  ocioso → 0% de CPU (o loop bloqueia em `get_wch` com `timeout(20)` e só
  redesenha quando `needs_redraw_`).
- **Idioma**: comentários, mensagens e documentação em português, sem acentos
  no código-fonte (para não depender da codificação do compilador). Nomes de
  identificadores em inglês.
- **`teste.cpp`, `teste.js`, `teste.txt`** na raiz são arquivos de teste do
  autor, não fazem parte do editor.

---

## 7. Plano: divisão de painéis (split vertical e horizontal)

### 7.1 Por que dá para fazer

O trabalho difícil já estava feito sem querer:

- `EditorView::draw(const Rect& area, bool focused)` é **totalmente
  parametrizado por retângulo** e recorta o próprio desenho (`ui::put` com
  `max_w`; `mvaddstr` limitado por `area.x + gw + text_w`). Não assume posição
  na tela em lugar nenhum.
- `click()`, `cursor_screen()`, `ensure_visible()` e `scroll_by()` já trabalham
  em cima de `area_`.
- `EditorView` guarda `shared_ptr<Document>` — o comentário no topo de
  `editorview.hpp` já antecipava duas views do mesmo documento.
- O cache de realce (`hl_states_`) é **por view** e invalida sozinho por
  `Document::version()`: dois painéis do mesmo arquivo se mantêm corretos de
  graça.
- `Document::line()`, `clamp()`, `insert()` e `erase()` são defensivos contra
  índice fora da faixa — compartilhar documento não vira segfault.

Toda a fricção está em `app.cpp`: as ~45 referências a `tabs_`/`active_tab_`
estão nele (43) e no `app.hpp` (2). `document`, `highlight`, `filetree`,
`terminal`, `theme` e `utf8` não mudam.

**Os seis pontos de atrito**:

| # | Onde | Problema |
|---|---|---|
| 1 | `app.hpp` | `tabs_` é vetor plano com um único `active_tab_` |
| 2 | `compute_layout()` | produz **um** `editor_` e **um** `tabbar_` |
| 3 | `handle_mouse()` | testa `tabbar_`/`editor_` como retângulos únicos |
| 4 | `enum class Focus` | `Editor` precisa virar "o painel atual" |
| 5 | `Alt+setas` | já é redimensionar sidebar/terminal — conflito |
| 6 | `editorview.cpp` | `(void)focused;` — o parâmetro é **ignorado**; com split é obrigatório marcar qual painel tem o foco |

### 7.2 Modelo escolhido: grade de colunas × linhas (profundidade 2)

Três modelos foram considerados:

| Modelo | Cobre | Custo |
|---|---|---|
| Lista plana + 1 orientação | só lado a lado **ou** só empilhado | ~200 linhas, não mistura as duas |
| **Grade `vector<PaneColumn>` de `vector<Pane>`** | **esquerda \| direita-cima / direita-baixo** | **~300 linhas, sem recursão, sem ponteiros** |
| Árvore binária (vim/tmux) | aninhamento arbitrário | ~450 linhas, `unique_ptr` recursivo, troca de nó pai |

**Escolhida a grade**, que é também o que o VS Code faz (editor groups são uma
grade plana). Cobre todo caso real de sala de aula e o código fica em dois
laços aninhados — legível em aula, que é o critério do projeto. A árvore só
ganha em aninhamento de 3+ níveis, que ninguém usa, e paga com a máquina de
C++ mais pesada de todo o repositório.

```cpp
struct Pane {
  std::vector<std::unique_ptr<EditorView>> tabs;
  int active_tab = 0;
  Rect tabbar, area;    // preenchidos por compute_layout()
  int weight = 100;     // proporcao de altura dentro da coluna
};
struct PaneColumn {
  std::vector<Pane> rows;
  int weight = 100;     // proporcao de largura
};
std::vector<PaneColumn> cols_;    // no lugar de tabs_
int cur_col_ = 0, cur_row_ = 0;   // no lugar de active_tab_
```

`Alt+V` cria coluna nova, `Alt+H` cria linha nova na coluna atual. Navegar é
`cur_col_ ± 1` / `cur_row_ ± 1` — sem travessia de árvore. Fechar é `erase()`
num vetor.

**Cada painel ganha a própria barra de abas** (não é opcional: sem isso não dá
para saber que arquivo está em qual painel). A barra do painel focado usa
`kPaneTitleActive`, as demais `kTabBar` — resolve o ponto 6 sem cor nova, o
`theme.cpp` não muda.

### 7.3 O espaço cabe? (contas para 80×24)

- `body_h` = 23; terminal aberto (10 + 1 de título) → região do editor = **12
  linhas**.
- Split horizontal em 2: 6 linhas por célula → 1 de aba + **5 de código**.
  Apertado, mas usável.
- Sidebar 26 em 80 colunas → `rw` = 54; split vertical: 53 úteis → ~26 colunas
  por painel, menos ~5 de gutter = **21 colunas de código**.

Daí: **mínimo de 4 linhas e 24 colunas por célula** (`kMinPaneW`/`kMinPaneH` em
`app.cpp`). Abaixo disso o split é **recusado** com mensagem na barra de status.
Recusar é muito melhor que dividir e desenhar torto.

O guarda vale só na hora de **criar** o painel. Encolher o terminal depois pode
deixar três painéis com 17 colunas cada — testado, degrada bem (o gutter e o
texto continuam certos, nada estoura), e não vale a pena fechar painéis por
conta própria só porque o usuário arrastou a janela.

### 7.4 Fases

**Estado: todas as fases feitas.** Falta só o redimensionamento da §7.5,
adiado de propósito.

Duas coisas foram puxadas da frente para manter a Fase 1 utilizável sozinha:

- **`F7`** (navegar entre painéis) era da Fase 2, mas sem ele a divisão só
  funcionaria com o mouse.
- **O item 9** (cursor obsoleto) era da Fase 3. Foi verificado que sem ele o
  painel de trás fica em branco e a barra de status mente — visível já na Fase
  1. Virou a invariante §2.7.

**Fase 0 — refatorar sem mudar comportamento** (~120 linhas, `app.*`) — FEITA

Introduzir `Pane`/`PaneColumn` com **exatamente um painel**; trocar as ~45
referências a `tabs_`/`active_tab_` por `pane().tabs`/`pane().active_tab`;
`compute_layout()` passa a preencher `pane.tabbar`/`pane.area` via
`layout_panes()`. Ao fim o editor tem que se comportar *exatamente* como antes.
É a fase de maior risco e a única puramente mecânica — commit separado,
validado no tmux antes de seguir.

**Fase 1 — split vertical (colunas)** (~200 linhas) — FEITA

1. `layout_panes()` reparte a largura por `weight`, com 1 coluna de separador
   `│` entre colunas (`PaneColumn::sep_x`, desenhado no `draw()`).
2. `split_vertical()` (`Alt+V`): coluna nova à direita, com uma view **nova do
   mesmo `shared_ptr<Document>`**. Recusa se `(w - ncols) / (ncols+1)` ficar
   abaixo de `kMinPaneW` — é a largura exata com pesos iguais, o caso normal.
3. `close_pane()` (`Alt+W`): remove a célula; a coluna some se ficar vazia;
   nunca fecha a última. **Recusa** se alguma aba tiver alteração não salva que
   não esteja aberta em outro painel — não perguntar é de propósito: encadear
   `ask_yes_no` para N documentos ficaria ilegível, e recusar nunca perde texto.
4. `draw()`: laço sobre os painéis → barra de abas + `draw(p.area, focado)`.
5. `handle_mouse()`: `pane_at(x, y)` em vez dos retângulos únicos; a roda age no
   painel sob o ponteiro.
6. Par de cor novo `kTabActiveDim` (`ui.cpp`): a aba ativa de um painel *sem*
   foco fica com a cor de destaque na **letra**, não no fundo. Deriva de
   `accent`/`bg_alt`, então `theme.cpp` e a struct `Theme` não mudaram.

**Fase 2 — split horizontal (linhas)** (~55 linhas) — FEITA

7. ✔ `split_horizontal()` (`Alt+H`): linha nova dentro de `cols_[cur_col_]`. A
   barra de abas do painel de baixo já serve de separador visual — nenhuma
   linha extra. O guarda é `editor_region_.h / (nrows+1) < kMinPaneH`; a dica
   da mensagem sugere `Ctrl+J` (esconder o terminal), porque na horizontal o
   recurso escasso é a altura, não a largura.
8. ✔ `next_pane()` já percorria a grade em ordem de leitura, então o `F7`
   passou a cobrir a grade 2×2 sem uma linha de mudança. Falta só o
   redimensionamento (§7.5).
9. ✔ `close_tab()`: fechar a última aba de um painel que não é o único fecha o
   painel. `remove_current_pane()` foi extraída para `close_pane()` e
   `drop_active_tab()` usarem a mesma remoção.

   **Cuidado ao mexer**: `drop_active_tab()` segura uma `Pane&` e
   `remove_current_pane()` apaga do vetor — a referência morre ali. Por isso o
   `remove_current_pane()` é a última coisa da função.

**Fase 3 — arestas do "mesmo arquivo em dois painéis"** — quase toda feita
junto com a Fase 0, porque as funções já estavam sendo reescritas:

10. ✔ **`open_file()`** procura o caminho só **no painel focado** e abre ali.
    Assim `Ctrl+P` com dois painéis abre onde você está olhando, e dá para pôr
    o mesmo arquivo lado a lado de propósito — metade da razão de existir o
    split.
11. ✔ **`Ctrl+T`** varre todos os painéis. `PickerItem` ganhou `int pane`, que
    é a posição na **ordem de leitura** (`pane_order()`, extraída do
    `next_pane()`); `focus_pane_index()` faz o caminho de volta.

    A busca **deduplica por `Document*`, não por aba**: depois de um `Alt+V` o
    mesmo documento está em dois painéis, e listar os resultados dele duas
    vezes seria só barulho. O resultado aponta para o primeiro painel que o
    mostra. A contagem do rótulo ("procurar em N arquivos abertos") usa a
    mesma regra, senão diria 2 para um arquivo só.
12. ✔ **`quit_request()`** varre a grade inteira.
13. ✔ `undo`/`redo` ficaram corretos de graça — o snapshot é do `Document`, os
    dois painéis veem a mesma reversão (semântica do vim). Testado.

**Fase 4 — documentação** — FEITA para o que existe hoje

14. ✔ `draw_help()` — a invariante §2.4 aguentou: o bloco `PAINEIS` foi
    compactado de 6 linhas para 6 linhas com 3 atalhos a mais, e a caixa
    fechou em 76×24 numa tela de 80×24 (conferido na tela, não na conta).
15. ✔ `README.md`: seção "Dividir a tela" e a limitação conhecida.
16. ✔ A invariante do cursor obsoleto subiu para a §2.7.

### 7.5 Atalhos (decididos, menos o redimensionamento)

`Alt+setas` já é redimensionar sidebar/terminal (documentado no README) e a
§2.6 proíbe `Ctrl+Shift`. Livres, pela §6: `Ctrl+E/L/U` e quase todo `Alt`.

| Tecla | Ação |
|---|---|
| `Alt+V` | dividir na vertical (lado a lado) — convenção do `:vsplit` do vim |
| `Alt+H` | dividir na horizontal (um sobre o outro) |
| `Alt+W` | fechar o painel atual |
| `F7` | alternar entre os painéis (irmão do `F6`, que alterna as três regiões) |

Para **redimensionar** há duas saídas:

- **(a)** `Alt+setas` por contexto: com foco no editor e ≥2 painéis redimensiona
  o painel, senão sidebar/terminal como hoje. Não gasta tecla, mas a mesma
  tecla faz coisas diferentes — o tipo de sutileza que confunde iniciante.
- **(b)** `Alt+setas` fica como está e o redimensionamento do painel usa
  `Ctrl+L` + setas, **ou fica de fora da v1**.

Decisão: **(b), sem redimensionamento na v1** — 50/50 resolve o caso de uso e
adia o gasto de atalho até alguém pedir.

### 7.6 Como testar

Metodologia da §4. Sequências novas para o `tmux send-keys -H`:

| Tecla | bytes |
|---|---|
| Alt+V / Alt+H / Alt+W | `1b 76` / `1b 68` / `1b 77` |
| F7 | `1b 5b 31 38 7e` |

O caso crítico é o item 9: mesmo arquivo em dois painéis, digitar em um,
`Ctrl+Z` no outro, e comparar tempo de CPU (§4) para garantir que ninguém
entrou em laço.

### 7.7 CORRIGIDO: dois `Document` para o mesmo arquivo (perdia texto)

Regressão introduzida pelo item 10 (`open_file()` procurava o caminho só no
painel focado). Antes da divisão de painéis ela procurava em **todas** as abas,
então isto não podia acontecer. **Corrigido**; virou a invariante §2.7.

**Como reproduzir** (confirmado):

1. `ted b.txt`, `Alt+V` — os dois painéis mostram `b.txt`.
2. `F7` para a esquerda, `Ctrl+O d.txt` — só a esquerda tem `d.txt`.
3. `F7` para a direita, `Ctrl+O d.txt` — a direita **não** tem `d.txt`, então
   `open_file()` carrega o arquivo de novo e cria um **segundo `Document`**.
4. Digitar na direita não muda a esquerda (são buffers separados).
5. `Ctrl+S` na direita, depois `Ctrl+S` na esquerda: o segundo salvamento
   sobrescreve o primeiro. O texto digitado na direita **some sem aviso**.

**Correção**: `find_open_doc()` procura o caminho em todos os painéis; se
achar, `add_view()` cria ali uma `EditorView` nova compartilhando aquele
`shared_ptr<Document>` em vez de carregar o arquivo de novo. Mantém a intenção
do item 10 (abre no painel onde você está olhando) e garante **um arquivo, um
`Document`**.

Caçando a mesma classe de bug pela outra porta, apareceu um irmão **anterior
ao split**: `Alt+S` ("salvar como") por cima de um arquivo já aberto em outra
aba criava dois `Document` com o mesmo caminho, e o próximo `Ctrl+S` de
qualquer um apagava o outro. `do_save()` agora recusa esse caso (salvar por
cima do próprio caminho, que é o `Ctrl+S` normal, continua passando).

Mais três encontrados na mesma varredura, todos na tabela da §3: o `Ctrl+W`
que mentia, o realce que só mudava num painel, e a barra de abas que escondia
a aba ativa em painel estreito.

---

## 8. Plano: terminais múltiplos, auto-close e par de chaves

Três funcionalidades pedidas depois que a stack alvo ficou clara.
**Estado: as três feitas.**

### 8.1 Abas no painel do terminal — FEITA

**Problema**: `App` tem um `Terminal term_`. Com `npm run dev` e `node
server.js` rodando, não sobra shell para `npm install` ou `git` — e os dois
primeiros não terminam nunca. É o único item da lista que hoje *impede* o
fluxo de trabalho em vez de só incomodar.

**Escolhido: abas no painel do terminal** (opção "a"), e não terminais como
painéis da grade (opção "b"). A (a) resolve o problema reaproveitando a linha
de título que já existe e a lógica de barra de abas que os painéis já usam;
a (b) exigiria `Pane` virar "ou editor ou terminal", contaminando `pane_at()`,
`close_pane()` e `next_pane()`. A (b) continua possível depois.

- `Terminal term_` vira `std::vector<std::unique_ptr<Terminal>> terms_` +
  `cur_term_`. `unique_ptr` porque `Terminal` tem `fd` e `pid` — não é copiável.
- A linha `term_title_` passa a desenhar as abas. **Com um terminal só ela
  desenha exatamente o que desenha hoje** ("TERMINAL" + o texto da direita),
  para a suíte de regressão continuar válida.
- `Alt+T` cria; `F4` foca e, se já estiver focado, passa para o próximo.
  Com um terminal só, o segundo `F4` não muda nada — igual a hoje.
- `Alt+W` com foco no terminal fecha o terminal (se houver mais de um); senão
  cai no `close_pane()` de sempre. É a única tecla com significado
  dependente de foco, e vale porque "fecha o que está em foco" é o que se
  espera.

**A armadilha**: `poll_output()` tem que rodar em **todos** os terminais a cada
volta do laço, não só no visível. Se o `npm run dev` escrever enquanto você
está em outro terminal e ninguém ler o pty, o buffer enche e **o processo
trava**. Redesenhar, esse sim, só quando o terminal visível muda.

Teto: 6 terminais.

**Ciclo de vida**: `reap_terminals()` roda a cada volta do laço, logo depois da
leitura da saída, e tira da lista todo shell que encerrou. O **último**
terminal é a exceção — ele fica com o aviso "encerrado - Enter reinicia", senão
o painel ficaria vazio e sem como voltar. Por isso a barra de abas nunca
mostra um terminal morto, e o marcador `!` que existia no rótulo foi removido:
era inalcançável.

**Cuidado com as cores**: `kTabActive` e `kPaneTitleActive` são o mesmo par
(`accent_fg` sobre `accent`, veja `ui.cpp`). A barra de terminais usa o esquema
das abas do editor — fundo fixo `kTabBar`, e quem sinaliza o foco do painel é o
*destaque da aba ativa* (`kTabActive` com foco, `kTabActiveDim` sem). Pintar o
fundo da barra com `kPaneTitleActive` faz a aba ativa desaparecer dentro dele.
Os três estados resultantes, conferidos na tela: foco+ativa = fundo accent
(`48;5;24`); sem foco+ativa = texto accent (`38;5;24`); inativas = `48;5;234`.

**Medido**: um terminal escondido despejando ~200 KB drenou e terminou em ~1 s,
e um servidor Node rodando escondido respondeu a um `curl` feito de outro
terminal — que é exatamente o fluxo Express + cliente.

### 8.2 Auto-close por linguagem, incluindo tag JSX — FEITA

Hoje `auto_close` fecha `()`, `[]`, `{}`, `""` e `''` — igual em todo arquivo,
com dois guardas já corretos que **não podem ser perdidos**: `at_word` (não
fecha antes de uma letra) e `after_word` (não vira `don''t`). O `backspace`
também apaga o par vazio de uma vez; qualquer par novo precisa entrar lá também.

Vira uma tabela por linguagem, ao lado de `comment_syntax()` em `highlight.*`,
que é onde o dado por linguagem já mora:

| Linguagem | `"` | `'` | `` ` `` | tag |
|---|---|---|---|---|
| C, C++, Python, SQL, CSS, Make | sim | sim | não | não |
| JavaScript (js/ts/jsx/tsx/vue/svelte) | sim | sim | **sim** | **sim** |
| Shell | sim | sim | sim | não |
| HTML | sim | sim | não | **sim** |
| JSON | sim | não | não | não |
| Markdown, texto puro | **não** | **não** | sim | não |

Os pares `([{` valem em toda linguagem. Markdown e texto puro **não** fecham
aspas: ali `'` é apóstrofo de prosa, não delimitador.

**Tag**: ao digitar `>`, se o texto antes fecha uma tag de abertura, insere
`</nome>`. Não fecha quando: é `</fechamento`, termina em `/` (`<br/>`), é
comentário ou `<!DOCTYPE`, ou o nome é elemento vazio de HTML (`br`, `img`,
`input`, `hr`, `meta`, `link`, …). `<>` vira `</>` (fragmento JSX).

**A armadilha que quase passou**: sem cuidado, o genérico `Array<string>` do
TypeScript viraria `Array<string></string>`. O guarda é o caractere **antes**
do `<`: se for letra, `_`, `)` ou `]`, não é tag — é a mesma regra que o realce
já usa para decidir se `<` abre tag em JSX. E o nome tem que começar colado no
`<`, o que descarta `a < b`. Os dois casos estão testados.

### 8.3 Realce do par de chaves — FEITA

Vale mais em JS do que em C: uma cadeia de callbacks termina numa pilha de
`});` e é ali que o aluno se perde.

- **Sem par de cor novo**: os dois delimitadores ganham `A_BOLD | A_UNDERLINE`
  por cima da cor de sintaxe que já têm. Funciona em qualquer tema e não
  arrisca colidir com a seleção.
- Casa quando o cursor está **em cima** do delimitador ou **logo depois** dele
  (o caso de acabar de digitar).
- **Ignora delimitador dentro de string e de comentário** usando o vetor de
  cores que `Highlighter::highlight()` já devolve por byte — é o que evita
  casar o `{` de `"a { b"`.
- Precisa do estado do realce em linhas fora da faixa visível:
  `update_highlight_states()` vira `hl_state_at(linha)`, que estende o cache
  sob demanda. Varredura limitada a 2.000 linhas **e** 200 KB para cada lado —
  um `{` sem fechamento num arquivo de 60 mil linhas não pode varrer tudo a
  cada redesenho (medido: 1 tick de CPU ocioso nesse caso).
- Opção `show_bracket_match` no `ted.conf`, ligada por padrão.

---

## 9. Realce: as oito linguagens acrescentadas

Ruby, ERB, C#, Haskell, OCaml, Verilog e VHDL entraram de uma vez; **Bash já
existia** como `Lang::Shell` (`.sh`/`.bash`/`.zsh`) e só ganhou reforço —
`$VAR`, `${...}`, `$1` e mais palavras reservadas.

Quase tudo reaproveita o `scan_code()`, que já era um scanner parametrizado por
flags. O que **não** cabia nas flags existentes:

### 9.1 O apóstrofo não é só aspa

Em Haskell, OCaml, VHDL e Verilog o `'` tem outros usos, e tratá-lo como
delimitador de string quebra o arquivo inteiro a partir dali:

| Linguagem | Uso | Exemplo |
|---|---|---|
| Haskell / OCaml | parte do nome | `nome'`, `'a` (variável de tipo) |
| VHDL | atributo | `clk'event`, `x'length` |
| Verilog | base numérica | `8'hFF`, `4'b1010`, `'d0` |

`is_char_literal()` resolve: nessas linguagens o `'` só abre caractere na forma
curta `'x'` ou `'\n'`. O Verilog ainda tem uma regra própria antes, para a base
numérica. Os dois casos estão nas amostras de teste — **se alguém "simplificar"
isso, um arquivo `.hs` com `foo'` fica todo colorido de string.**

### 9.2 ERB são duas passadas

`scan_erb()` desenha o HTML da linha inteira e **depois** repinta por cima os
trechos `<% %>` como Ruby. As duas passadas escrevem no mesmo vetor por índice,
então a segunda vence. Sai muito mais simples do que interromper o scanner de
HTML no meio.

O estado normal é o do próprio HTML (16 bits, com o sub-estado de
`<script>`/`<style>`). Só quando um `<%` fica aberto no fim da linha é que
trocamos para `kErbTag`, guardando o estado *principal* do HTML nos bits de
cima — nesse caso o sub-estado se perde, o que só importa para um `<%` aberto
dentro de um `<script>`, que não acontece na prática.

### 9.3 Maiúscula tem significado (em três delas)

Em Ruby, Haskell e OCaml uma palavra que começa com maiúscula é constante,
classe, tipo, construtor ou módulo — e é destacada como tipo mesmo sem estar em
nenhuma lista. **Em C# essa regra não vale**: lá PascalCase é a convenção de
tudo, inclusive métodos, e destacar tudo viraria ruído.

### 9.4 Estados novos que atravessam linhas

`kRubyComment` (`=begin`/`=end`, que precisam estar na coluna 0), `kHsComment`
(`{- -}`), `kMlComment` (`(* *)`), `kCsVerbatim` (`@"..."` do C#, onde a aspa
se escapa dobrando) e `kErbTag`. Comentário aninhado de Haskell e OCaml **não**
é tratado: `{- {- -} -}` fecha no primeiro `-}`.

---

## 10. Teste de carga: 40 alunos num VPS de 8 GB

Cenário simulado: 40 instâncias simultâneas, cada uma com 10 arquivos de um
projeto web abertos, edições e 100 operações de desfazer/refazer. Medido com
**PSS** (`/proc/PID/smaps_rollup`), não RSS: o RSS conta o binário e a
`ncursesw` uma vez por processo e superestima em ~3×.

### 10.1 Resultado do cenário pedido

| Métrica | 1 aluno | 40 alunos |
|---|---|---|
| `ted` RSS | 7 MB | 291 MB |
| `ted` **PSS** | 3 MB | **91 MB** |
| shells do terminal embutido (PSS) | 4 MB | 172 MB |
| **total real** | ~7 MB | **~263 MB** |

**Cabe com folga enorme**: 263 MB de 8 GB, ~3%. As 40 continuaram vivas e
respondendo (`F1` testado), todas com 7 MB de RSS, distribuição uniforme.

A memória subiu de 43 para 91 MB durante as edições e ficou **plana** durante
os 100 desfazer/refazer — desfazer não aloca, só move snapshot entre as pilhas.

**CPU ocioso**: 40 instâncias paradas consomem **8,6% de um núcleo** (0,22%
cada), do laço que acorda a cada 20 ms (`timeout(20)`). A §6 diz "ocioso → 0%
de CPU", o que vale para uma instância arredondando; com 40 o custo agregado
aparece.

### 10.2 O teto do desfazer é o risco real

O desfazer guarda **uma cópia do vetor de linhas por grupo de edição** (§1),
com teto de 500 níveis (`kMaxUndo`). O custo por snapshot é ~2,1× o tamanho do
arquivo, então o teto de memória por documento é ~1.000× o arquivo:

| Arquivo | Snapshot | Teto (500 níveis) |
|---|---|---|
| `.jsx` de aula (241 linhas, 12 KB) | ~25 KB | ~25 MB |
| `package-lock.json` (20.005 linhas, 740 KB) | ~1,5 MB | **778 MB** |

Medições (confirmadas na máquina, não extrapoladas):

| Cenário | Por aluno | × 40 |
|---|---|---|
| Pedido (10 arquivos, 100 undo/redo) | 2,3 MB | **263 MB** ✔ |
| Pesado (10 arquivos, 500 grupos de edição em cada) | **211 MB** | **8,4 GB** ✘ |
| Um `package-lock.json` muito editado | **778 MB** | 31 GB ✘ |

O teto **segura** (700 edições dão a mesma memória que 500 — não é vazamento),
mas ele é alto demais. E 500 grupos de edição por arquivo numa aula longa é
plausível: a digitação abre um grupo novo a cada 600 ms de pausa.

### 10.3 Correção — FEITA

**Dois tetos, os dois inteiros e configuráveis no `ted.conf`**, valendo o que
estourar primeiro (`Document::trim_undo()`):

| Opção | Padrão | O que limita |
|---|---|---|
| `undo_levels` | 500 | quantos passos o `Ctrl+Z` volta — o conceito que o aluno enxerga |
| `undo_memory_mb` | 8 | RAM das duas pilhas (undo + redo) por arquivo — a rede de segurança |

O de memória é o que resolve o problema do VPS: o de níveis sozinho não serve,
porque o custo de um nível depende do tamanho do arquivo (25 KB num `.jsx` de
aula, 1,5 MB num `package-lock.json`).

**Detalhes que não podem se perder:**

- Cada `Snapshot` guarda o próprio custo em `bytes`, medido na criação, e
  `undo_bytes_`/`redo_bytes_` mantêm as somas. Sem isso, aparar a pilha seria
  O(histórico) a cada tecla.
- **`load()` limpa as pilhas e tem que zerar os contadores junto.** Esquecer
  isso deixa o teto disparando cedo demais no arquivo seguinte.
- **Sempre sobra pelo menos um nível de desfazer**, mesmo que ele estoure o
  teto sozinho — senão o `Ctrl+Z` deixaria de funcionar justamente nos arquivos
  grandes, que é onde ele mais importa.
- A pilha de **refazer é sacrificada primeiro**: perder o que já foi desfeito
  incomoda menos que perder o histórico do que ainda está na tela.

**Efeito medido** (mesmos cenários da §10.2, agora com o teto):

| Cenário | Antes | Depois | × 40 alunos |
|---|---|---|---|
| Pedido (10 arquivos, 100 undo/redo) | 2,3 MB | 2,3 MB | 263 MB ✔ |
| Pesado (10 arquivos, 500 grupos em cada) | 211 MB | **96 MB** | **3,8 GB** ✔ |
| `package-lock.json` muito editado | 778 MB | **16 MB** | 640 MB ✔ |

O `undo_memory_mb` escala linear e previsível: 1 → 10 MB, 8 → 16 MB, 64 → 80 MB,
1024 → 466 MB no mesmo teste. Para apertar mais num VPS, `undo_memory_mb = 4`
leva o pior caso a ~50 MB por aluno.

O limite de 16 MB por arquivo (`kMaxFileSize`) deixou de ser enganoso: um
arquivo de 16 MB muito editado agora custa 16 MB de arquivo + 8 MB de desfazer,
e não os ~16 GB de antes.
