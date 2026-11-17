CC ?= cc
AR ?= ar
PKG_CONFIG ?= pkg-config
WAYLAND_SCANNER ?= wayland-scanner
WAYLAND_PROTOCOLS ?= $(shell $(PKG_CONFIG) --variable=pkgdatadir wayland-protocols)
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror -fPIC -MMD -MP
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags libsystemd) -Igenerated
LDLIBS += $(shell $(PKG_CONFIG) --libs libsystemd) -ldl -lpthread
OBJECTS = tee.o protocol.o sensor.o dsp.o touch.o display.o engine.o platform.o gatekeeper.o authorization.o
PROTOCOLS = layer-shell viewporter idle-inhibit xdg-shell
GENERATED = $(addprefix generated/,$(addsuffix .c,$(PROTOCOLS)))

.PHONY: all check install clean
all: unicorn-fingerprint-authorize unicorn-fingerprint-display-broker unicorn-fingerprint-runtime unicorn-fingerprint-select unicorn-fingerprint-hint libunicorn-fingerprint.so libunicorn-fingerprint.a libarm_proxy_skel.so

unicorn-fingerprint-authorize: authorize.o authorization-state.o login.o libunicorn-fingerprint.so
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-rpath,'$$ORIGIN/../lib:$$ORIGIN' -o $@ authorize.o authorization-state.o login.o -L. -lunicorn-fingerprint $(LDLIBS) $(shell $(PKG_CONFIG) --libs libcrypto) -lpam

libunicorn-fingerprint.a: $(OBJECTS)
	$(AR) rcs $@ $^
libunicorn-fingerprint.so: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -Wl,-soname,libunicorn-fingerprint.so -o $@ $^ $(LDLIBS)
libarm_proxy_skel.so: arm-proxy.o libunicorn-fingerprint.so
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -Wl,-rpath,'$$ORIGIN' -o $@ arm-proxy.o -L. -lunicorn-fingerprint
unicorn-fingerprint-runtime: main.o libunicorn-fingerprint.so
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-rpath,'$$ORIGIN/../lib:$$ORIGIN' -o $@ main.o -L. -lunicorn-fingerprint $(LDLIBS)
unicorn-fingerprint-select: select.o selection.o libunicorn-fingerprint.so
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-rpath,'$$ORIGIN/../lib:$$ORIGIN' -o $@ select.o selection.o -L. -lunicorn-fingerprint $(LDLIBS)
unicorn-fingerprint-display-broker: hint-broker.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
unicorn-fingerprint-hint: display-hint.c $(GENERATED)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(shell $(PKG_CONFIG) --cflags wayland-client cairo) -o $@ $^ $(LDLIBS) $(shell $(PKG_CONFIG) --libs wayland-client cairo)

generated:
	mkdir -p $@
generated/layer-shell.c: wayland/wlr-layer-shell-unstable-v1.xml | generated
	$(WAYLAND_SCANNER) private-code $< $@
	$(WAYLAND_SCANNER) client-header $< generated/layer-shell.h
generated/viewporter.c: $(WAYLAND_PROTOCOLS)/stable/viewporter/viewporter.xml | generated
	$(WAYLAND_SCANNER) private-code $< $@
	$(WAYLAND_SCANNER) client-header $< generated/viewporter.h
generated/idle-inhibit.c: $(WAYLAND_PROTOCOLS)/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml | generated
	$(WAYLAND_SCANNER) private-code $< $@
	$(WAYLAND_SCANNER) client-header $< generated/idle-inhibit.h
generated/xdg-shell.c: $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml | generated
	$(WAYLAND_SCANNER) private-code $< $@

# Tests exercise the native codec and syscall/error paths without hardware.
test-transport: tests/transport.c tee.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -Wl,--wrap=ioctl -o $@ $^
test-protocol: tests/protocol.c protocol.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -o $@ $^
test-gatekeeper: tests/gatekeeper.c gatekeeper.c protocol.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -o $@ $^
test-login: tests/login.c login.c protocol.c tee.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -o $@ $^
test-authorization-state: tests/authorization-state.c authorization-state.c protocol.c tee.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -o $@ $^ $(shell $(PKG_CONFIG) --libs libcrypto)
test-authorization: tests/authorization.c authorization.c protocol.c tee.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -Wl,--wrap=fstat,--wrap=getsockopt -o $@ tests/authorization.c protocol.c tee.c
test-provision: tests/provision.c authorize.c authorization-state.o login.o libunicorn-fingerprint.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -Wl,-rpath,'$$ORIGIN' -Wl,--wrap=uf_gatekeeper_open,--wrap=uf_gatekeeper_close,--wrap=uf_gatekeeper_enroll,--wrap=uf_credential_create -o $@ tests/provision.c authorization-state.o login.o -L. -lunicorn-fingerprint $(LDLIBS) $(shell $(PKG_CONFIG) --libs libcrypto) -lpam
test-sensor: tests/sensor.c sensor.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -o $@ $^
test-lifecycle: tests/lifecycle.c tee.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. \
	  -Wl,--wrap=glob,--wrap=globfree,--wrap=open,--wrap=close,--wrap=fcntl \
	  -Wl,--wrap=ioctl,--wrap=mmap,--wrap=munmap,--wrap=madvise -o $@ $^
test-actions: tests/actions.c protocol.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -o $@ $^
test-dsp: tests/dsp.c dsp.c protocol.c arm-proxy.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -Wl,--wrap=dlopen,--wrap=dlsym,--wrap=dlclose -o $@ $^ -ldl -lpthread
test-engine: tests/engine.c engine.c protocol.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -Wl,--wrap=poll -o $@ tests/engine.c protocol.c $(shell $(PKG_CONFIG) --libs libsystemd)
test-touch: tests/touch.c touch.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -Wl,--wrap=ioctl -o $@ $^
test-broker: hint-broker.c tests/broker-peer.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -Wl,--wrap=getsockopt -o $@ $^ $(shell $(PKG_CONFIG) --libs libsystemd)
test-platform: tests/platform.c platform.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -o $@ tests/platform.c
test-selection: tests/selection.c selection.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -o $@ $^
test-init: tests/init.c engine.c dsp.c protocol.c arm-proxy.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -I. -Wl,--wrap=sd_id128_get_machine_app_specific,--wrap=ioctl,--wrap=dlopen,--wrap=dlsym,--wrap=dlclose -o $@ tests/init.c dsp.c protocol.c arm-proxy.c $(LDLIBS)
check: test-provision test-login test-authorization-state test-authorization test-gatekeeper test-init test-selection test-platform test-broker test-touch test-transport test-protocol test-sensor test-lifecycle test-actions test-dsp test-engine
	./test-provision
	./test-login
	./test-authorization-state
	./test-authorization
	./test-gatekeeper
	./test-init
	./test-selection
	./test-platform
	./test-transport
	./test-protocol
	./test-sensor
	./test-lifecycle
	./test-actions
	./test-dsp
	./test-engine
	./test-touch
	python3 tests/broker.py ./test-broker
install: all
	install -d $(out)/bin $(out)/lib $(out)/include/unicorn-fingerprint
	install -m0755 unicorn-fingerprint-authorize unicorn-fingerprint-display-broker unicorn-fingerprint-runtime unicorn-fingerprint-select unicorn-fingerprint-hint $(out)/bin/
	install -m0755 libunicorn-fingerprint.so libarm_proxy_skel.so $(out)/lib/
	install -m0644 libunicorn-fingerprint.a $(out)/lib/
	install -m0644 tee.h protocol.h sensor.h dsp.h touch.h display.h ipc.h gatekeeper.h authorization.h $(out)/include/unicorn-fingerprint/
clean:
	$(RM) test-provision test-login test-authorization-state test-authorization
	$(RM) *.o *.d *.a *.so unicorn-fingerprint-authorize unicorn-fingerprint-display-broker unicorn-fingerprint-runtime unicorn-fingerprint-select unicorn-fingerprint-hint test-gatekeeper test-init test-selection test-platform test-broker test-touch test-transport test-protocol test-sensor test-lifecycle test-actions test-dsp test-engine

-include $(wildcard *.d)
