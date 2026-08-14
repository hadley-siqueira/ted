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
| `Tab` / `Shift+Tab` | indentar / desindentar (funciona na seleção) |
| `Alt+Shift+↑/↓` | mover a linha para cima/baixo |
| `Insert` | alterna inserir/sobrescrever |

### Busca e navegação
| Tecla | O que faz |
|---|---|
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

## Configuração (opcional)

Crie `~/.config/ted/ted.conf` com uma opção por linha:

```ini
tab_width = 4          # largura do TAB
use_spaces = true      # TAB insere espaços
auto_indent = true     # mantém a indentação no Enter
auto_close = true      # fecha (), [], {}, "" e '' sozinho
line_numbers = true
mouse = true
sidebar_width = 26
terminal_height = 10
show_terminal = true
show_sidebar = true
```

## Como o código está organizado

Cada arquivo cuida de uma coisa só — dá para ler um por vez com a turma:

| Arquivo | Responsabilidade |
|---|---|
| `src/utf8.*` | bytes ↔ caracteres ↔ colunas da tela (acentuação) |
| `src/document.*` | o texto de um arquivo: linhas, edição, desfazer, salvar |
| `src/editorview.*` | o painel de código: cursor, seleção, rolagem, desenho |
| `src/filetree.*` | a barra lateral de pastas e arquivos |
| `src/terminal.*` | o terminal embutido: PTY + emulador VT100/xterm |
| `src/highlight.*` | realce de sintaxe (C, C++, Python, JS, Shell, Markdown) |
| `src/ui.*` | ncurses: cores, teclas com modificadores, mouse |
| `src/app.*` | layout dos painéis, loop de eventos e atalhos |
| `src/config.*` | opções do `ted.conf` |

Detalhes que valem uma aula: o texto é um `vector<string>` (uma linha por
posição) e **toda** edição passa por `Document::insert`/`erase`, o que torna o
"desfazer" e o "arquivo modificado" fáceis de manter corretos; o terminal
embutido é um emulador VT de verdade (`terminal.cpp`), que lê os bytes do
`forkpty` e monta uma matriz de células com cor e atributos.

## Limitações conhecidas

- Um cursor só (sem multi-cursor) e sem quebra automática de linha longa.
- O realce de sintaxe é por expressões simples, não um parser de verdade.
- O emulador de terminal cobre o essencial (cores, tela alternativa, regiões
  de rolagem). Programas muito exóticos podem desenhar torto.
- A área de transferência é interna ao editor; para trocar texto com outros
  programas, use a colagem do seu terminal (`Ctrl+Shift+V`), que o editor
  entende, ou desligue o mouse com `F9` e use a seleção do terminal.
