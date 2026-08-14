# ted — editor de texto para o terminal

Um editor de texto **sem modos** (está sempre pronto para digitar), feito em
C++17 do zero, com a cara dos editores gráficos que os alunos vão encontrar
depois — barra lateral com os arquivos do projeto, painel de código e um
terminal embutido —, mas inteiramente em modo texto.

```
┌─────────────────┬────────────────────────────────────────────┐
│ ARQUIVOS: proj  │ main.c  teste.py •            ← abas       │
│ ▾ src           │  1 #include <stdio.h>                      │
│     main.c      │  2                                         │
│   teste.py      │  3 int main(void) {                        │
│                 ├────────────────────────────────────────────┤
│                 │ TERMINAL                                   │
│                 │ $ gcc main.c && ./a.out                    │
├─────────────────┴────────────────────────────────────────────┤
│ src/main.c *                       Ln 3, Col 1  C  Espacos:4 │
└──────────────────────────────────────────────────────────────┘
```

## Compilar e instalar

Precisa de `g++` (C++17) e da biblioteca **ncursesw**:

```sh
sudo apt install build-essential libncursesw5-dev   # Debian/Ubuntu
make                    # gera ./ted
make install            # copia para ~/.local/bin/ted
```

Para instalar para a turma toda: `sudo make install PREFIX=/usr/local`.

## Usar

```sh
ted                 # abre a pasta atual
ted main.c          # abre o arquivo (a pasta atual vira o "projeto")
ted ~/exercicios    # abre uma pasta
ted --theme nord    # abre com outra paleta de cores
```

Dentro do editor, **F1 mostra todos os atalhos**.

## Atalhos

### Arquivo
| Tecla | O que faz |
|---|---|
| `Ctrl+S` | salvar |
| `Alt+S` | salvar como |
| `Ctrl+O` | abrir arquivo |
| `Ctrl+N` | novo arquivo (nova aba) |
| `Ctrl+W` | fechar a aba |
| `Ctrl+PgUp` / `Ctrl+PgDn` | trocar de aba |
| `Ctrl+Q` ou `F10` | sair |

### Edição
| Tecla | O que faz |
|---|---|
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | copiar / recortar / colar (sem seleção, agem na linha inteira) |
| `Ctrl+A` | selecionar tudo |
| `Ctrl+Z` / `Ctrl+Y` | desfazer / refazer |
| `Ctrl+D` | duplicar a linha |
| `Ctrl+/` (ou `Alt+C`) | comentar/descomentar a linha ou a seleção |
| `Tab` / `Shift+Tab` | indentar / desindentar (funciona na seleção) |
| `Alt+Shift+↑/↓` | mover a linha para cima/baixo |
| `Insert` | alterna inserir/sobrescrever |

O `Ctrl+/` usa o comentário certo para cada linguagem (`//`, `#`, `--`, `/* */`
ou `<!-- -->`), alinha o marcador na indentação do bloco e pula linhas em
branco. Se todas as linhas selecionadas já estiverem comentadas, ele
descomenta. Alguns terminais não enviam nada com `Ctrl+/` — nesses, use
`Alt+C`.

### Busca e navegação
| Tecla | O que faz |
|---|---|
| `Ctrl+P` | abrir arquivo digitando parte do nome (busca *fuzzy*) |
| `Ctrl+T` | procurar um texto em todos os arquivos abertos |
| `Ctrl+F` | buscar (`F3` = próxima, `Shift+F3` = anterior) |
| `Ctrl+R` | substituir tudo |
| `Ctrl+G` | ir para uma linha |
| `Shift+setas` | selecionar |
| `Ctrl+←/→` | andar de palavra em palavra |
| `Home` / `End` / `Ctrl+Home` / `Ctrl+End` | início/fim da linha e do arquivo |

### Painéis
| Tecla | O que faz |
|---|---|
| `F2` / `F3` / `F4` | focar arquivos / editor / terminal |
| `F6` | alternar entre os painéis |
| `Ctrl+B` | mostrar/esconder a barra de arquivos |
| `Ctrl+J` | mostrar/esconder o terminal |
| `Alt+setas` | redimensionar os painéis |
| `F5` | recarregar a lista de arquivos |
| `F9` | ligar/desligar o mouse |

Na barra de arquivos: `↑↓` navega, `→`/`Enter` abre a pasta ou o arquivo,
`←` fecha a pasta, `.` mostra/esconde arquivos ocultos.

### Achar arquivos e trechos rapidamente

`Ctrl+P` abre uma caixa onde você digita **pedaços** do nome: `edvw` acha
`src/editorview.cpp`, `apph` acha `src/app.hpp`. As letras precisam aparecer na
ordem, mas não coladas — quem casa melhor (letras juntas, começo de palavra,
nome do arquivo em vez da pasta) aparece primeiro. Arquivos gerados e binários
(`.o`, `.png`, `.zip`…) ficam de fora da lista.

`Ctrl+T` faz o contrário: procura um **texto** dentro dos arquivos que você já
abriu e lista `arquivo:linha` com o trecho. Aqui a busca é literal, igual à do
`Ctrl+F` — só diferencia maiúsculas se você digitar alguma. `Enter` pula para a
linha com o trecho já selecionado.

Nas duas caixas: `↑↓` navega, `PgUp`/`PgDn` pula de tela, `Enter` confirma,
`Esc` fecha, `Ctrl+U` limpa o que você digitou e o mouse também funciona.

O **mouse** funciona: clicar posiciona o cursor, arrastar seleciona, a roda
rola, clicar em um arquivo o abre, clicar numa aba a ativa e um clique duplo
seleciona a palavra. Se você preferir usar a seleção do seu próprio terminal
para copiar (`Ctrl+Shift+C`), aperte `F9` para desligar o mouse do editor.

### Terminal embutido
Ele roda o seu `$SHELL` de verdade na pasta do projeto. Com o foco nele
(`F4`), **todas as teclas vão para o shell** — inclusive `Ctrl+C`, `Ctrl+D` e
`Ctrl+Z`. As exceções são as teclas de função (`F1`–`F10`) e `Alt+…`, que
continuam sendo do editor, para você conseguir sair de lá.

- `Shift+PgUp` / `Shift+PgDn`: rolar o histórico.
- `Ctrl+V`: colar o que você copiou no editor.
- Se o shell fechar (`exit`), o painel mostra "encerrado"; `Enter` reinicia.

## Cores

Vem com seis paletas. Escolha no `ted.conf` (`theme = nord`) ou experimente na
hora com `ted --theme nord`; `ted --themes` lista todas.

| Nome | Como é |
|---|---|
| `default` | cores padrão do terminal, fundo transparente |
| `rose-pine` | escuro, tons de vinho e lilás |
| `rose-pine-dawn` | a versão clara do Rosé Pine — boa em projetor |
| `dracula` | escuro, roxo e rosa |
| `gruvbox` | escuro, quente, com ar retrô |
| `nord` | escuro, azul acinzentado e frio |

As paletas são escritas em RGB (`src/theme.cpp`) e convertidas para a cor mais
próxima entre as 256 do terminal, então funcionam em qualquer emulador com 256
cores, sem precisar reconfigurá-lo. Em terminais de 8 cores o editor cai num
esquema simples e legível. Para criar a sua, copie um bloco em `src/theme.cpp`,
troque os valores e recompile — o nome novo já aparece em `--themes`.

## Configuração

Tudo é opcional: sem nenhum arquivo, o `ted` já abre com padrões pensados para
quem está começando. A configuração serve para mudar esses padrões.

### Onde fica o arquivo

Um único arquivo, procurado nesta ordem:

1. `$XDG_CONFIG_HOME/ted/ted.conf` — se a variável estiver definida;
2. `~/.config/ted/ted.conf` — o caso normal.

Ele **não é criado sozinho**: se não existir, o editor simplesmente usa os
padrões. Não há arquivo de configuração por projeto nem um arquivo global do
sistema — é um por usuário, e só.

Para começar a partir do modelo que vem no repositório:

```sh
mkdir -p ~/.config/ted
cp ted.conf.exemplo ~/.config/ted/ted.conf
```

O `ted.conf.exemplo` já traz todas as opções com um comentário explicando cada
uma, e a lista completa de paletas comentada para você descomentar a que
quiser.

### Formato

Uma opção por linha, `chave = valor`. Os espaços em volta são ignorados, `#`
começa um comentário (em qualquer ponto da linha) e linhas sem `=` são
ignoradas:

```ini
theme = rose-pine      # isto aqui é comentário
tab_width = 2
```

Chaves que o editor não conhece são ignoradas em silêncio — inclusive quando
você erra o nome. Se uma opção não fez efeito, confira a grafia na tabela
abaixo.

### Opções

| Chave | Valores | Padrão | O que faz |
|---|---|---|---|
| `theme` | nome de paleta | `default` | cores do editor — veja [Cores](#cores) |
| `tab_width` | 1 a 16 | `4` | de quantas colunas é um nível de indentação |
| `use_spaces` | `true`/`false` | `true` | `Tab` insere espaços em vez do caractere TAB |
| `auto_indent` | `true`/`false` | `true` | `Enter` mantém a indentação da linha atual |
| `auto_close` | `true`/`false` | `true` | fecha `()`, `[]`, `{}`, `""` e `''` sozinho |
| `line_numbers` | `true`/`false` | `true` | mostra a coluna com os números de linha |
| `mouse` | `true`/`false` | `true` | clique, arrasto e roda do mouse dentro do editor |
| `sidebar_width` | ≥ 10 | `26` | largura inicial da barra de arquivos |
| `terminal_height` | ≥ 3 | `10` | altura inicial do terminal embutido |
| `show_sidebar` | `true`/`false` | `true` | começa com a barra de arquivos visível |
| `show_terminal` | `true`/`false` | `true` | começa com o terminal embutido visível |

Valem como verdadeiro: `true`, `1`, `sim` e `yes`. **Qualquer outra coisa é
lida como falso**, então um `tru` mal digitado desliga a opção sem avisar.

Números fora da faixa voltam ao padrão (`tab_width`) ou são presos no mínimo
(`sidebar_width`, `terminal_height`). Um nome de paleta que não existe faz o
editor abrir com a paleta padrão e avisar na barra de status.

### Quando vale o quê

- O arquivo é lido **uma vez, ao abrir o editor**. Editar o `ted.conf` com o
  próprio `ted` funciona, mas o efeito só aparece na próxima vez que você abrir.
- `ted --theme NOME` tem prioridade sobre o `theme` do arquivo — bom para
  experimentar sem editar nada.
- O que você muda com o teclado durante o uso (`Ctrl+B`, `Ctrl+J`, `F9`,
  `Alt+setas`) vale só para aquela sessão; para tornar permanente, escreva a
  opção equivalente no `ted.conf`.

### Para uma turma inteira

Não existe configuração de sistema, então a forma direta é cada aluno copiar o
modelo (as duas linhas de `cp` acima) no primeiro dia. Se as máquinas forem
compartilhadas ou você quiser uma configuração igual para todos sem mexer no
`$HOME` de cada um, aponte `XDG_CONFIG_HOME` para uma pasta comum no script que
abre o editor:

```sh
XDG_CONFIG_HOME=/opt/aula/config ted    # lê /opt/aula/config/ted/ted.conf
```

## Como o código está organizado

Cada arquivo cuida de uma coisa só — dá para ler um por vez com a turma:

| Arquivo | Responsabilidade |
|---|---|
| `src/utf8.*` | bytes ↔ caracteres ↔ colunas da tela (acentuação) |
| `src/document.*` | o texto de um arquivo: linhas, edição, desfazer, salvar |
| `src/editorview.*` | o painel de código: cursor, seleção, rolagem, desenho |
| `src/filetree.*` | a barra lateral de pastas e arquivos |
| `src/picker.*` | a caixa flutuante de busca com lista (Ctrl+P e Ctrl+T) |
| `src/fuzzy.*` | o casamento e a pontuação da busca por nome |
| `src/terminal.*` | o terminal embutido: PTY + emulador VT100/xterm |
| `src/highlight.*` | realce de sintaxe (C, C++, Python, JS/JSX, HTML, CSS, SQL, Shell, Markdown) |
| `src/ui.*` | ncurses: cores, teclas com modificadores, mouse |
| `src/app.*` | layout dos painéis, loop de eventos e atalhos |
| `src/theme.*` | paletas de cores e conversão RGB → 256 cores |
| `src/config.*` | opções do `ted.conf` |

Detalhes que valem uma aula: o texto é um `vector<string>` (uma linha por
posição) e **toda** edição passa por `Document::insert`/`erase`, o que torna o
"desfazer" e o "arquivo modificado" fáceis de manter corretos; o terminal
embutido é um emulador VT de verdade (`terminal.cpp`), que lê os bytes do
`forkpty` e monta uma matriz de células com cor e atributos.

## Limitações conhecidas

- Um cursor só (sem multi-cursor) e sem quebra automática de linha longa.
- O realce de sintaxe é por expressões simples, não um parser de verdade. Em
  particular: no JSX, `<` só vira tag quando aparece onde um valor pode
  começar (para não colorir `a < b`); e no CSS uma declaração que se espalhe
  por várias linhas perde o contexto de "valor" na linha seguinte.
- O emulador de terminal cobre o essencial (cores, tela alternativa, regiões
  de rolagem). Programas muito exóticos podem desenhar torto.
- A área de transferência é interna ao editor; para trocar texto com outros
  programas, use a colagem do seu terminal (`Ctrl+Shift+V`), que o editor
  entende, ou desligue o mouse com `F9` e use a seleção do terminal.
