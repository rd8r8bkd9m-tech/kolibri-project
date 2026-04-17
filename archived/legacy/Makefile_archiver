# Kolibri OS - Makefile

.PHONY: all build archiver test clean help

help:
	@echo "╔════════════════════════════════════════════════════════╗"
	@echo "║          KOLIBRI OS - TARGETS                          ║"
	@echo "╚════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "make build       - Компилировать всё"
	@echo "make archiver    - Компилировать архиватор"
	@echo "make test        - Тестировать архиватор"
	@echo "make clean       - Удалить binaries"
	@echo "make help        - Эта справка"
	@echo ""

all: build archiver test

build:
	@echo "🔨 Компилирую основные тесты..."
	@mkdir -p build-test
	@cd tests && for f in test_*.c; do \
		echo "  Компилирую $$f..."; \
		gcc -O2 -I../backend/include -o ../build-test/$${f%.c} $$f -lm -Wl,-rpath,@loader_path/../lib 2>/dev/null || true; \
	done
	@echo "✅ Готово"

archiver:
	@echo "🎯 Компилирую архиватор..."
	@mkdir -p tools
	@gcc -O2 -o tools/kolibri-archive tools/kolibri_archiver.c
	@echo "✅ tools/kolibri-archive готов"
	@ls -lh tools/kolibri-archive

test: archiver
	@echo "🧪 Тестирую архиватор..."
	@bash test_archiver.sh

clean:
	@echo "🧹 Очистка..."
	@rm -rf build-test/test_* build-test/*.dSYM
	@rm -f tools/kolibri-archive
	@rm -f /tmp/test*.kolibri /tmp/large.txt /tmp/readme_restored.md
	@echo "✅ Очищено"
