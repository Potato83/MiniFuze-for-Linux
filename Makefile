# SPDX-License-Identifier: MIT
CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -g
WARN = -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -ffp-contract=off
ENGINE_SRC = $(wildcard native/app/*.c)
DESKTOP_SRC = $(wildcard native/desktop/*.c)
ENGINE_DEPS = json-c libmicrohttpd openssl
DESKTOP_DEPS = gtk4 libsoup-3.0 json-c

.PHONY: all clean check kernel
all: build/minifuze2-engine build/minifuze2

build/generated/web_assets.h: web/index.html web/app.js web/style.css
	@mkdir -p build/generated
	@{ for asset in index.html app.js style.css; do \
	  symbol=$$(echo "$$asset" | tr . _); \
	  printf 'static const unsigned char web_%s[] = {\n' "$$symbol"; \
	  od -An -v -tu1 "web/$$asset" | awk '{for(i=1;i<=NF;i++)printf "%s,",$$i;printf "\n"}'; \
	  printf '0};\n'; done; } > "$@.tmp"
	@mv "$@.tmp" "$@"

build/generated/desktop_assets.h: native/desktop/style.css native/desktop/dark.css
	@mkdir -p build/generated
	@{ for spec in 'style:desktop_css' 'dark:desktop_dark_css'; do \
	 name=$${spec%%:*}; symbol=$${spec##*:}; \
	 printf 'static const unsigned char %s[] = {\n' "$$symbol"; \
	 od -An -v -tu1 "native/desktop/$$name.css" | awk '{for(i=1;i<=NF;i++)printf "%s,",$$i;printf "\n"}'; \
	 printf '0};\n'; done; } > "$@.tmp"
	@mv "$@.tmp" "$@"

build/minifuze2-engine: $(ENGINE_SRC) native/app/mf.h kernel/minifuze2_uapi.h build/generated/web_assets.h
	$(CC) $(WARN) $(CFLAGS) -Ibuild/generated $(ENGINE_SRC) -o "$@.tmp" $$($(PKG_CONFIG) --cflags --libs $(ENGINE_DEPS)) -lm -pthread
	mv "$@.tmp" "$@"

build/minifuze2: $(DESKTOP_SRC) build/generated/desktop_assets.h
	@$(PKG_CONFIG) --print-errors --exists 'gtk4 >= 4.12' 'libsoup-3.0 >= 3.0' json-c
	$(CC) $(WARN) $(CFLAGS) -Ibuild/generated $(DESKTOP_SRC) -o "$@.tmp" $$($(PKG_CONFIG) --cflags --libs $(DESKTOP_DEPS)) -lm
	mv "$@.tmp" "$@"

check: all
	python3 -m unittest discover -s tests -v

build/desktop-smoke: tests/desktop_smoke.c $(DESKTOP_SRC) build/generated/desktop_assets.h
	$(CC) $(WARN) $(CFLAGS) -Ibuild/generated tests/desktop_smoke.c -o "$@.tmp" $$($(PKG_CONFIG) --cflags --libs $(DESKTOP_DEPS)) -lm
	mv "$@.tmp" "$@"

build/desktop-smoke-asan: tests/desktop_smoke.c $(DESKTOP_SRC) build/generated/desktop_assets.h
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Ibuild/generated tests/desktop_smoke.c -o "$@.tmp" $$($(PKG_CONFIG) --cflags --libs $(DESKTOP_DEPS)) -lm
	mv "$@.tmp" "$@"

kernel:
	$(MAKE) -C kernel

clean:
	rm -rf build
	$(MAKE) -C kernel clean
