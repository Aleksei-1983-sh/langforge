CC = gcc
CFLAGS = -O2 -Wall -Wextra -I./src -I./src/libs -I./src/db -I/usr/include -I/usr/include/postgresql -DDEBUG_REQUEST=1
LDFLAGS = -lpq -lcrypto
SRCDIR = src
BINDIR = ./bin
TARGET = $(BINDIR)/englearn

# Собираем все .c в src и поддиректориях
SOURCES := $(shell find $(SRCDIR) -name '*.c')
OBJECTS := $(patsubst $(SRCDIR)/%.c,$(SRCDIR)/%.o,$(SOURCES))

.PHONY: all clean test deps

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR):
	mkdir -p $(BINDIR)

clean:
	# Удаляем все объектные файлы (включая в поддиректориях) и саму папку bin
	@if [ -n "$(OBJECTS)" ]; then rm -f $(OBJECTS); fi
	rm -rf $(BINDIR)

# Запуск тестов: по умолчанию вызывает скрипт в tests/
test:
	@if [ -x ./tests/run_tests.sh ]; then ./tests/run_tests.sh; else echo "No tests/run_tests.sh found"; fi



