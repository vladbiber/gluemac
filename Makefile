.POSIX:
.SUFFIXES:

include config.mk

# flags for compiling
GLUEMACCPPFLAGS = -I. -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" $(XWAYLAND)
GLUEMACDEVCFLAGS = -g -Wpedantic -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-unused-parameter -Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion

# CFLAGS / LDFLAGS
PKGS      = wayland-server xkbcommon libinput pangocairo librsvg-2.0 libdrm $(XLIBS)
LIBINPUT3FG = `$(PKG_CONFIG) --atleast-version=1.28 libinput && echo -DLIBINPUT_HAVE_3FG_DRAG=1`
GLUEMACCFLAGS = $(LIBINPUT3FG) `$(PKG_CONFIG) --cflags $(PKGS)` $(SCENEFX_INCS) $(WLR_INCS) $(GLUEMACCPPFLAGS) $(GLUEMACDEVCFLAGS) $(CFLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(SCENEFX_LIBS) $(WLR_LIBS) -lm $(LIBS)
# gluemac-msg is a plain Wayland client
MSGCFLAGS = `$(PKG_CONFIG) --cflags wayland-client` -I. $(GLUEMACDEVCFLAGS) $(CFLAGS)
MSGLIBS   = `$(PKG_CONFIG) --libs wayland-client` $(LIBS)

all: gluemac gluemac-msg

check: gluemac
	sh -n install.sh gluemac-session gluemac-backlight tests/drm-test.sh
	$(MAKE) -C tests
$(SCENEFX_BUILD)/libscenefx-0.5.a: scenefx/render/fx_renderer/fx_pass.c \
	scenefx/render/fx_renderer/shaders/glass.frag scenefx/types/scene/wlr_scene.c
	[ -f $(SCENEFX_BUILD)/build.ninja ] || PKG_CONFIG_PATH="$${PKG_CONFIG_PATH:+$$PKG_CONFIG_PATH:}$(PREFIX)/lib/pkgconfig:$(PREFIX)/lib64/pkgconfig" \
		meson setup $(SCENEFX_BUILD) scenefx -Ddefault_library=static \
		-Dexamples=false -Dbuildtype=release -Db_ndebug=true
	ninja -C $(SCENEFX_BUILD)
gluemac: gluemac.o util.o dwl-ipc-unstable-v2-protocol.o $(SCENEFX_BUILD)/libscenefx-0.5.a
	$(CC) gluemac.o util.o dwl-ipc-unstable-v2-protocol.o $(GLUEMACCFLAGS) $(LDFLAGS) $(LDLIBS) -o $@
gluemac-msg: gluemac-msg.o dwl-ipc-unstable-v2-protocol.o wlr-output-power-management-unstable-v1-protocol.o
	$(CC) gluemac-msg.o dwl-ipc-unstable-v2-protocol.o wlr-output-power-management-unstable-v1-protocol.o $(MSGCFLAGS) $(LDFLAGS) $(MSGLIBS) -o $@
gluemac-msg.o: gluemac-msg.c dwl-ipc-unstable-v2-client-protocol.h \
	wlr-output-power-management-unstable-v1-client-protocol.h
	$(CC) $(CPPFLAGS) $(MSGCFLAGS) -o $@ -c gluemac-msg.c
gluemac.o: $(SCENEFX_BUILD)/libscenefx-0.5.a gluemac.c client.h config.h config.mk mac.h mac.c mactext.c mactitle.c macgenie.c macmc.c cursor-shape-v1-protocol.h \
	dwl-ipc-unstable-v2-protocol.h \
	pointer-constraints-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h \
	wlr-output-power-management-unstable-v1-protocol.h xdg-shell-protocol.h
util.o: util.c util.h
dwl-ipc-unstable-v2-protocol.o: dwl-ipc-unstable-v2-protocol.c dwl-ipc-unstable-v2-protocol.h
wlr-output-power-management-unstable-v1-protocol.o: wlr-output-power-management-unstable-v1-protocol.c
	$(CC) $(CPPFLAGS) $(MSGCFLAGS) -o $@ -c wlr-output-power-management-unstable-v1-protocol.c

# wayland-scanner is a tool which generates C headers and rigging for Wayland
# protocols, which are specified in XML. wlroots requires you to rig these up
# to your build system yourself and provide them in the include path.
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

cursor-shape-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
pointer-constraints-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-output-power-management-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-output-power-management-unstable-v1.xml $@
wlr-output-power-management-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		protocols/wlr-output-power-management-unstable-v1.xml $@
wlr-output-power-management-unstable-v1-client-protocol.h:
	$(WAYLAND_SCANNER) client-header \
		protocols/wlr-output-power-management-unstable-v1.xml $@
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
dwl-ipc-unstable-v2-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/dwl-ipc-unstable-v2.xml $@
dwl-ipc-unstable-v2-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		protocols/dwl-ipc-unstable-v2.xml $@
dwl-ipc-unstable-v2-client-protocol.h:
	$(WAYLAND_SCANNER) client-header \
		protocols/dwl-ipc-unstable-v2.xml $@

config.h:
	cp config.def.h $@
clean:
	rm -f gluemac gluemac-msg *.o *-protocol.h *-protocol.c
	rm -rf $(SCENEFX_BUILD)

dist: clean
	mkdir -p gluemac-$(VERSION)
	cp -R .github docs LICENSE* Makefile CHANGELOG.md CONTRIBUTING.md \
		README.md SECURITY.md install.sh flake.nix client.h config.def.h \
		config.def.conf config.mk protocols gluemac.1 gluemac.c mac.h mac.c mactext.c mactitle.c macgenie.c macmc.c gluemac-msg.c \
		util.c util.h scenefx gluemac.desktop gluemac-session gluemac-backlight gluemac-$(VERSION)
	tar -caf gluemac-$(VERSION).tar.gz gluemac-$(VERSION)
	rm -rf gluemac-$(VERSION)

install: gluemac gluemac-msg
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	rm -f $(DESTDIR)$(PREFIX)/bin/gluemac $(DESTDIR)$(PREFIX)/bin/gluemac-msg
	cp -f gluemac $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/gluemac
	cp -f gluemac-msg $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/gluemac-msg
	cp -f gluemac-session $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/gluemac-session
	cp -f gluemac-backlight $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/gluemac-backlight
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp -f gluemac.1 $(DESTDIR)$(MANDIR)/man1
	chmod 644 $(DESTDIR)$(MANDIR)/man1/gluemac.1
	mkdir -p $(DESTDIR)$(DATADIR)/gluemac
	cp -f config.def.conf $(DESTDIR)$(DATADIR)/gluemac/config.def.conf
	chmod 644 $(DESTDIR)$(DATADIR)/gluemac/config.def.conf
	mkdir -p $(DESTDIR)$(DATADIR)/doc/gluemac
	cp -f README.md CONTRIBUTING.md SECURITY.md $(DESTDIR)$(DATADIR)/doc/gluemac
	cp -R docs $(DESTDIR)$(DATADIR)/doc/gluemac
	mkdir -p $(DESTDIR)$(SESSIONDIR)
	cp -f gluemac.desktop $(DESTDIR)$(SESSIONDIR)/gluemac.desktop
	chmod 644 $(DESTDIR)$(SESSIONDIR)/gluemac.desktop
	sync
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/gluemac $(DESTDIR)$(PREFIX)/bin/gluemac-session \
		$(DESTDIR)$(PREFIX)/bin/gluemac-msg $(DESTDIR)$(MANDIR)/man1/gluemac.1 \
		$(DESTDIR)$(DATADIR)/gluemac/config.def.conf $(DESTDIR)$(DATADIR)/gluemac/gluemac-gtk.css \
		$(DESTDIR)$(SESSIONDIR)/gluemac.desktop
	rm -rf $(DESTDIR)$(DATADIR)/doc/gluemac

.PHONY: all check clean dist install uninstall

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(GLUEMACCFLAGS) -o $@ -c $<
