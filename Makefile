# Makefile do ted - editor de texto para o terminal.
#
#   make            compila (binario em ./ted)
#   make run        compila e abre o editor na pasta atual
#   make debug      compila com simbolos e sem otimizacao
#   make install    instala em ~/.local/bin (ou PREFIX=/usr/local sudo make install)
#   make clean      apaga os arquivos gerados

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -D_XOPEN_SOURCE=600 -D_DEFAULT_SOURCE
LDLIBS   := $(shell pkg-config --libs ncursesw 2>/dev/null || echo -lncursesw -ltinfo) -lutil
CPPFLAGS += $(shell pkg-config --cflags ncursesw 2>/dev/null)

PREFIX ?= $(HOME)/.local
BINDIR := $(PREFIX)/bin

SRCDIR := src
OBJDIR := build
SRCS   := $(wildcard $(SRCDIR)/*.cpp)
OBJS   := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SRCS))
DEPS   := $(OBJS:.o=.d)
TARGET := ted

.PHONY: all run debug install uninstall clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c -o $@ $<

$(OBJDIR):
	@mkdir -p $(OBJDIR)

run: $(TARGET)
	./$(TARGET) .

debug: CXXFLAGS := -std=c++17 -O0 -g3 -Wall -Wextra -Wno-unused-parameter
debug: clean $(TARGET)

install: $(TARGET)
	@mkdir -p $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)
	@echo "instalado em $(BINDIR)/$(TARGET)"

uninstall:
	rm -f $(BINDIR)/$(TARGET)

clean:
	rm -rf $(OBJDIR) $(TARGET)

-include $(DEPS)
