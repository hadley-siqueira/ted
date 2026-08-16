# Notas de desenvolvimento do ted

Anotações para quem for mexer no código depois (inclusive eu mesmo, em outra
sessão). O `README.md` documenta o editor para quem **usa**; este arquivo
guarda o que ficou na cabeça de quem **escreveu**: decisões, armadilhas, bugs
já resolvidos e o que fazer em seguida.

Estado: ~6.500 linhas de C++17 em `src/` (25 arquivos), compila com `g++` e
`ncursesw`, sem nenhuma outra dependência. `make` gera `./ted`.

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
   remova.
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
`highlight.cpp`): um `main()` que lê arquivos reais, roda `highlight()` linha a
linha em **todas** as linguagens do enum e usa `alarm(5)` + `SIGALRM` para
abortar em loop infinito. Rodar contra `src/*`, `Makefile`, `README.md` e o
próprio binário cobre bem (~90 mil linhas em segundos).

Para crash, `gdb` em modo batch dentro do tmux funciona bem:
`gdb -q -batch -ex 'set logging file /tmp/g.log' -ex 'set logging enabled on'
-ex run -ex bt --args ./ted .`

---

## 5. Backlog recomendado (em ordem)

1. **Pular para o erro do compilador** (~150 linhas). `F8` e clique sobre
   `arquivo:linha:coluna` na saída do terminal embutido abrem o arquivo na
   posição. Fecha o ciclo compilar → erro → corrigir, que é o dia a dia da
   turma. O `Terminal` já tem a matriz de células e o histórico; reaproveitar
   `open_file()` + `EditorView::select_range()`.
2. **Salvar ao trocar o foco para o terminal** (~10 linhas, dentro de
   `set_focus()`, com opção `save_on_focus_change` no `ted.conf`). Elimina o
   erro nº 1 do iniciante: editar, compilar e não entender por que nada mudou.
3. **Área de transferência do sistema** (~80 linhas). Hoje `Ctrl+C` só copia
   dentro do editor. Precisa de duas vias: `wl-copy`/`xclip` quando existirem e
   OSC 52 como reserva — **só OSC 52 não basta**, o GNOME Terminal (padrão do
   Ubuntu) não suporta.
4. **Restaurar sessão** ao reabrir o projeto (~80 linhas): mesmas abas, mesma
   linha.
5. **Busca em todo o projeto** — é a fase 3 do `Ctrl+T`, e a varredura de
   arquivos do `Ctrl+P` já existe.
6. **Divisão de painéis** (split vertical e horizontal, ~350 linhas). Plano
   detalhado na §7.

**Evitar por ora**: quebra automática de linha (contamina cursor, seleção,
rolagem e desenho da `EditorView` inteira — hoje linha do arquivo = linha da
tela), múltiplos cursores, LSP/autocomplete e git integrado. São de usuário
avançado e aumentam a superfície de confusão para quem nunca usou editor.

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
