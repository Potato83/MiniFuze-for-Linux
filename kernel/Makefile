obj-m += minifuze2_ctl.o

KDIR ?= /usr/lib/modules/$(shell uname -r)/build
KERNEL_LLVM := $(shell grep -q '^CONFIG_CC_IS_CLANG=y' "$(KDIR)/include/config/auto.conf" 2>/dev/null && echo LLVM=1)

all:
	@set -eu; \
	build_dir=$$(mktemp -d /tmp/minifuze2-kbuild.XXXXXXXX); \
	trap 'rm -rf -- "$$build_dir"' EXIT; \
	cp Makefile minifuze2_ctl.c minifuze2_selection.h minifuze2_uapi.h "$$build_dir/"; \
	$(MAKE) -C "$(KDIR)" M="$$build_dir" $(KERNEL_LLVM) modules; \
	cp "$$build_dir/minifuze2_ctl.ko" "$(CURDIR)/minifuze2_ctl.ko"

clean:
	rm -f -- "$(CURDIR)/minifuze2_ctl.ko"
