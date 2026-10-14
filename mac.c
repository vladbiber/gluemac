/* macOS behaviour: stacking windows, dynamic spaces, app-wide actions */
#include <strings.h>

static int mac_stacking = 1;
static int mac_cmd_as_ctrl = 1;
static int mac_super_tap = 0;
static int mac_spaces = 1;
static int mac_edge_tiling = 1;
static int mac_genie = 1;
static int mac_genie_duration = 520;
static char mac_terminals[512] = "Alacritty alacritty foot footclient kitty "
		"org.wezfurlong.wezterm com.mitchellh.ghostty st-glue xterm "
		"org.gnome.Console org.gnome.Ptyxis konsole";
static struct {
	void (*func)(const Arg *);
	Arg arg;
	int armed;
} mac_corner[4] = {{0}, {0}, {0}, {0}};

static int mac_busy;
static uint32_t mac_edges;
static struct wlr_box mac_grab0;
static double mac_cx0, mac_cy0;
static int mac_tile_pending;
static Monitor *mac_tile_mon;
static struct wlr_scene_rect *mac_tile_preview;
static struct {
	Monitor *m;
	unsigned int ws;
} mac_fsgone;
static struct {
	uint32_t code, sent;
} mac_keys[8];

static int mc_appexpose;
static char mc_app[128];
static struct {
	int active, idx, n;
	char app[24][128];
	struct wlr_scene_tree *tree;
} macsw;

static int cfgaction(const char *act, void (**func)(const Arg *), Arg *arg);
static void macswitchhide(void);
static void macswitchdraw(void);

static const char *
macappid(Client *c)
{
	const char *id = c ? client_get_appid(c) : NULL;
	return id && *id ? id : "?";
}

static int
macsameapp(Client *a, Client *b)
{
	return a && b && !strcmp(macappid(a), macappid(b));
}

static int
macmanaged(Client *c)
{
	return c && c->mon && !client_is_unmanaged(c) && client_surface(c)->mapped;
}

static unsigned int
macspaces(Monitor *m)
{
	Client *c;
	unsigned int n;

	if (!m)
		return 1;
	if (!m->nspaces)
		m->nspaces = (unsigned int)MAX(1, MIN(NUMWS, mac_spaces));
	n = MAX(m->nspaces, m->ws + 1);
	wl_list_for_each(c, &clients, link)
		if (c->mon == m && c->ws + 1 > n)
			n = c->ws + 1;
	return m->nspaces = MIN(n, NUMWS);
}

static int
macfsspace(Monitor *m, unsigned int ws)
{
	Client *c;

	wl_list_for_each(c, &clients, link)
		if (c->mon == m && c->ws == ws && c->fsspace)
			return 1;
	return 0;
}

static void
macmovespace(Client *c, unsigned int ws)
{
	if (c->ws == ws || !c->mon)
		return;
	detachclient(c);
	attachclient(c->mon, ws, c);
}

static int
macspaceinsert(Monitor *m, unsigned int at)
{
	unsigned int n = macspaces(m), ws;
	Client *c;

	if (n >= NUMWS || at > n)
		return 0;
	for (ws = n; ws-- > at;)
		wl_list_for_each(c, &clients, link)
			if (c->mon == m && c->ws == ws)
				macmovespace(c, ws + 1);
	m->nspaces = n + 1;
	if (m->ws >= at)
		m->ws++;
	return 1;
}

static void
macspaceremove(Monitor *m, unsigned int i)
{
	unsigned int n = macspaces(m), ws, to;
	Client *c;

	if (n <= 1 || i >= n)
		return;
	wl_list_for_each(c, &clients, link) {
		if (c->mon == m && c->ws == i && c->fsspace) {
			/* leaving fullscreen takes its space away by itself */
			setfullscreen(c, 0);
			return;
		}
	}
	to = i ? i - 1 : 1;
	wl_list_for_each(c, &clients, link)
		if (c->mon == m && c->ws == i)
			macmovespace(c, to);
	for (ws = i + 1; ws < n; ws++)
		wl_list_for_each(c, &clients, link)
			if (c->mon == m && c->ws == ws)
				macmovespace(c, ws - 1);
	m->nspaces = n - 1;
	if (m->ws > i || (m->ws == i && i > 0))
		m->ws--;
	arrange(m);
	if (m == selmon && !overview_visible)
		focusclient(focustop(m), 1);
	ipcnotifyall();
}

/* the nearest space to the left that is a desktop, not a fullscreen app */
static unsigned int
macdesktopspace(Monitor *m, unsigned int ws)
{
	unsigned int i;

	for (i = ws + 1; i-- > 0;)
		if (!macfsspace(m, i))
			return i;
	for (i = ws + 1; i < macspaces(m); i++)
		if (!macfsspace(m, i))
			return i;
	return ws;
}

static void
macview(Monitor *m, unsigned int ws)
{
	Arg a = {.ui = ws};

	if (!m || m->ws == ws)
		return;
	selmon = m;
	viewws(&a);
}

static void
macfullscreen(Client *c, int fs)
{
	Monitor *m = c->mon;
	unsigned int at, old;

	if (!mac_stacking || mac_busy || !macmanaged(c) || client_get_parent(c))
		return;
	mac_busy = 1;
	if (fs && !c->fsspace) {
		at = c->ws + 1;
		if (macspaceinsert(m, at)) {
			macmovespace(c, at);
			c->fsspace = 1;
			macview(m, at);
		}
	} else if (!fs && c->fsspace) {
		old = c->ws;
		c->fsspace = 0;
		if (old > 0) {
			macmovespace(c, old - 1);
			macview(m, old - 1);
			macspaceremove(m, old);
		}
	}
	mac_busy = 0;
	ipcnotifyall();
}

static void
macgeomclamp(Monitor *m, struct wlr_box *g)
{
	struct wlr_box a = m->w;

	g->width = MIN(g->width, a.width);
	g->height = MIN(g->height, a.height);
	g->x = MAX(a.x, MIN(g->x, a.x + a.width - g->width));
	g->y = MAX(a.y, MIN(g->y, a.y + a.height - g->height));
}

static void
macplace(Client *c, Client *p)
{
	Monitor *m = c->mon;
	struct wlr_box a, g = c->geom;
	Client *o;
	int tries, clash;

	if (!m || c->isfullscreen)
		return;
	a = m->w;
	if (p && p->mon == m) {
		g.x = p->geom.x + (p->geom.width - g.width) / 2;
		g.y = p->geom.y + MAX(0, (p->geom.height - g.height) / 3);
	} else if (client_is_x11(c) && (g.x - m->m.x >= 48 || g.y - m->m.y >= 48)) {
		/* an X11 window that picked a spot keeps it */
	} else {
		if (g.width < 120 || g.height < 80) {
			g.width = a.width * 3 / 5;
			g.height = a.height * 2 / 3;
		}
		g.width = MIN(g.width, a.width);
		g.height = MIN(g.height, a.height);
		g.x = a.x + (a.width - g.width) / 2;
		g.y = a.y + (a.height - g.height) / 3;
		for (tries = 0; tries < 16; tries++) {
			clash = 0;
			wl_list_for_each(o, &clients, link)
				if (o != c && VISIBLEON(o, m) && abs(o->geom.x - g.x) < 8
						&& abs(o->geom.y - g.y) < 8)
					clash = 1;
			if (!clash)
				break;
			g.x += 26;
			g.y += 26;
			if (g.x + g.width > a.x + a.width || g.y + g.height > a.y + a.height) {
				g.x = a.x + 26 * (tries % 5);
				g.y = a.y + 26 * (tries % 5);
			}
		}
	}
	macgeomclamp(m, &g);
	resize(c, g, 0);
}

/* mapnotify: every window floats, lands where macOS would put it, and a
 * new app window never opens on top of a fullscreen app */
static void
macmap(Client *c, Client *p)
{
	Monitor *m = c->mon;
	unsigned int to;

	if (!mac_stacking || !m)
		return;
	client_set_tiled(c, 0);
	if (!p && macfsspace(m, c->ws)) {
		to = macdesktopspace(m, c->ws);
		macmovespace(c, to);
		if (m->ws != to)
			macview(m, to);
	}
	macplace(c, p);
	if (macsw.active)
		macswitchhide();
}

static void
macunmap(Client *c)
{
	genieclear(NULL, c);
	mac_fsgone.m = NULL;
	if (mac_stacking && c->fsspace && c->mon) {
		mac_fsgone.m = c->mon;
		mac_fsgone.ws = c->ws;
		c->fsspace = 0;
	}
	if (macsw.active)
		macswitchhide();
}

static void
macunmapped(void)
{
	Monitor *m = mac_fsgone.m;

	mac_fsgone.m = NULL;
	if (!m || mac_fsgone.ws == 0)
		return;
	if (m->ws == mac_fsgone.ws)
		macview(m, mac_fsgone.ws - 1);
	macspaceremove(m, mac_fsgone.ws);
}

static void
macsetmaximized(Client *c, int on)
{
	if (c->type == XDGShell && c->surface.xdg->initialized)
		wlr_xdg_toplevel_set_maximized(c->surface.xdg->toplevel, on);
}

static struct wlr_box
mactilebox(Monitor *m, Client *c, int t)
{
	struct wlr_box a = m->w, b = a;
	int g = MAX(0, gappx), hw = a.width / 2, hh = a.height / 2;

	switch (t) {
	case TileLeft: b.width = hw; break;
	case TileRight: b.x += hw; b.width -= hw; break;
	case TileTop: b.height = hh; break;
	case TileBottom: b.y += hh; b.height -= hh; break;
	case TileTL: b.width = hw; b.height = hh; break;
	case TileTR: b.x += hw; b.width -= hw; b.height = hh; break;
	case TileBL: b.width = hw; b.y += hh; b.height -= hh; break;
	case TileBR: b.x += hw; b.width -= hw; b.y += hh; b.height -= hh; break;
	case TileCenter:
		b = c->geom;
		b.x = a.x + (a.width - b.width) / 2;
		b.y = a.y + (a.height - b.height) / 2;
		macgeomclamp(m, &b);
		return b;
	default: break;
	}
	b.x += g / 2;
	b.y += g / 2;
	b.width = MAX(1, b.width - g);
	b.height = MAX(1, b.height - g);
	return b;
}

static void
macrestore(Client *c)
{
	struct wlr_box b = c->restore;

	if (!c->tiled)
		return;
	c->tiled = 0;
	if (c->zoomed) {
		c->zoomed = 0;
		macsetmaximized(c, 0);
	}
	if (b.width > 0 && b.height > 0)
		resize(c, b, 0);
}

static void
mactileclient(Client *c, int t)
{
	if (!macmanaged(c) || c->isfullscreen)
		return;
	if (t == TileRestore) {
		macrestore(c);
		return;
	}
	if (!c->tiled)
		c->restore = c->geom;
	if (t != TileCenter)
		c->tiled = 1;
	if (t == TileFill && !c->zoomed) {
		c->zoomed = 1;
		macsetmaximized(c, 1);
	} else if (t != TileFill && c->zoomed) {
		c->zoomed = 0;
		macsetmaximized(c, 0);
	}
	resize(c, mactilebox(c->mon, c, t), 0);
}

static void
mactile(const Arg *arg)
{
	mactileclient(focustop(selmon), arg->i);
}

/* the green button and a double click on the title bar */
static void
maczoom(Client *c)
{
	if (!macmanaged(c) || c->isfullscreen)
		return;
	if (c->zoomed)
		macrestore(c);
	else
		mactileclient(c, TileFill);
	if (c->type == XDGShell && c->surface.xdg->initialized)
		wlr_xdg_surface_schedule_configure(c->surface.xdg);
}

static void
maczoomaction(const Arg *arg)
{
	maczoom(focustop(selmon));
}

/* a tiled window dragged away goes back to its own size under the cursor */
static void
macuntile(Client *c)
{
	struct wlr_box b = c->restore;
	double fx;

	if (!c->tiled || b.width <= 0 || b.height <= 0)
		return;
	fx = (cursor->x - c->geom.x) / MAX(1, c->geom.width);
	b.x = (int)round(cursor->x - fx * b.width);
	b.y = c->geom.y;
	c->tiled = 0;
	if (c->zoomed) {
		c->zoomed = 0;
		macsetmaximized(c, 0);
	}
	resize(c, b, 1);
}

static const char *
macresizecursor(uint32_t e)
{
	if ((e & WLR_EDGE_TOP) && (e & WLR_EDGE_LEFT))
		return "nw-resize";
	if ((e & WLR_EDGE_TOP) && (e & WLR_EDGE_RIGHT))
		return "ne-resize";
	if ((e & WLR_EDGE_BOTTOM) && (e & WLR_EDGE_LEFT))
		return "sw-resize";
	if ((e & WLR_EDGE_BOTTOM) && (e & WLR_EDGE_RIGHT))
		return "se-resize";
	if (e & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM))
		return "ns-resize";
	return "ew-resize";
}

static void
macgrab(Client *c, unsigned int mode, uint32_t edges)
{
	if (!macmanaged(c) || c->isfullscreen)
		return;
	focusclient(c, 1);
	grabc = c;
	if (mode == CurMove) {
		macuntile(c);
		grabcx = (int)round(cursor->x) - c->geom.x;
		grabcy = (int)round(cursor->y) - c->geom.y;
		cursor_mode = CurMove;
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
	} else {
		if (c->tiled) {
			c->tiled = 0;
			if (c->zoomed) {
				c->zoomed = 0;
				macsetmaximized(c, 0);
			}
		}
		mac_edges = edges ? edges : WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT;
		cursor_mode = CurMacResize;
		wlr_cursor_set_xcursor(cursor, cursor_mgr, macresizecursor(mac_edges));
	}
	mac_grab0 = c->geom;
	mac_cx0 = cursor->x;
	mac_cy0 = cursor->y;
	wlr_seat_pointer_notify_clear_focus(seat);
}

/* Super+drag: the edges nearest the click are the ones that move */
static int
macmoveresize(Client *c, unsigned int mode)
{
	uint32_t e = 0;

	if (!mac_stacking)
		return 0;
	if (mode == CurResize) {
		e |= cursor->x < c->geom.x + c->geom.width / 2.0 ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT;
		e |= cursor->y < c->geom.y + c->geom.height / 2.0 ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM;
		macgrab(c, CurMacResize, e);
	} else {
		macgrab(c, CurMove, 0);
	}
	return 1;
}

static void
macrequestmove(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, req_move);

	if (mac_stacking && (cursor_mode == CurNormal || cursor_mode == CurPressed))
		macgrab(c, CurMove, 0);
}

static void
macrequestresize(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_resize_event *ev = data;
	Client *c = wl_container_of(listener, c, req_resize);

	if (mac_stacking && (cursor_mode == CurNormal || cursor_mode == CurPressed))
		macgrab(c, CurMacResize, ev->edges);
}

static void
macresizemotion(void)
{
	struct wlr_box g = mac_grab0;
	int dx = (int)round(cursor->x - mac_cx0), dy = (int)round(cursor->y - mac_cy0);
	const int minw = 120, minh = 80;

	if (!grabc)
		return;
	if (mac_edges & WLR_EDGE_LEFT) {
		dx = MIN(dx, g.width - minw);
		g.x += dx;
		g.width -= dx;
	} else if (mac_edges & WLR_EDGE_RIGHT) {
		g.width = MAX(minw, g.width + dx);
	}
	if (mac_edges & WLR_EDGE_TOP) {
		dy = MIN(dy, g.height - minh);
		g.y += dy;
		g.height -= dy;
	} else if (mac_edges & WLR_EDGE_BOTTOM) {
		g.height = MAX(minh, g.height + dy);
	}
	resize(grabc, g, 1);
}

static int
macedgetile(Monitor *m, double x, double y)
{
	int l = x <= m->m.x + 1, r = x >= m->m.x + m->m.width - 2;
	int t = y <= m->m.y + 1, b = y >= m->m.y + m->m.height - 2;
	int band = MAX(40, m->m.height / 12);

	if (l)
		return y < m->m.y + band ? TileTL : y > m->m.y + m->m.height - band
				? TileBL : TileLeft;
	if (r)
		return y < m->m.y + band ? TileTR : y > m->m.y + m->m.height - band
				? TileBR : TileRight;
	if (t)
		return TileFill;
	(void)b;
	return TileNone;
}

static void
macpreviewclear(void)
{
	if (mac_tile_preview)
		wlr_scene_node_destroy(&mac_tile_preview->node);
	mac_tile_preview = NULL;
	mac_tile_pending = TileNone;
	mac_tile_mon = NULL;
}

/* while a window is dragged, touching a screen edge previews where it tiles */
static void
macdragmotion(void)
{
	static const float color[] = {0.55f, 0.6f, 0.68f, 0.28f};
	Monitor *m;
	struct wlr_box b;
	int t;

	if (!mac_stacking || !mac_edge_tiling || !grabc)
		return;
	m = xytomon(cursor->x, cursor->y);
	t = m ? macedgetile(m, cursor->x, cursor->y) : TileNone;
	if (t == mac_tile_pending && m == mac_tile_mon)
		return;
	macpreviewclear();
	if (t == TileNone)
		return;
	b = mactilebox(m, grabc, t);
	mac_tile_preview = wlr_scene_rect_create(layers[LyrTile], b.width, b.height, color);
	wlr_scene_rect_set_corner_radii(mac_tile_preview, corner_radii_all(MAX(8, corner_radius)));
	wlr_scene_node_set_position(&mac_tile_preview->node, b.x, b.y);
	mac_tile_pending = t;
	mac_tile_mon = m;
}

static void
macdragend(Client *c)
{
	int t = mac_tile_pending;

	macpreviewclear();
	if (!c || t == TileNone || !c->mon)
		return;
	c->restore = (struct wlr_box){c->geom.x, c->geom.y,
			mac_grab0.width, mac_grab0.height};
	mactileclient(c, t);
}

static void
macminimize(Client *c, int kind)
{
	if (!macmanaged(c) || c->minimized || c->isfullscreen)
		return;
	if (kind == MinMinimized && animations && !geniestart(c, 0))
		clientcloseanim(c);
	c->minimized = kind;
	if (c->ftl && kind == MinMinimized)
		wlr_foreign_toplevel_handle_v1_set_minimized(c->ftl, 1);
	arrange(c->mon);
	if (c->mon == selmon && !overview_visible)
		focusclient(focustop(selmon), 1);
}

static void
macunminimize(Client *c)
{
	if (!c->minimized)
		return;
	if (c->ftl && c->minimized == MinMinimized)
		wlr_foreign_toplevel_handle_v1_set_minimized(c->ftl, 0);
	if (c->minimized == MinMinimized)
		geniestart(c, 1);
	c->minimized = MinNone;
	if (c->mon)
		arrange(c->mon);
}

static void
macdesktopoff(void)
{
	Client *c;

	wl_list_for_each(c, &clients, link)
		if (c->minimized == MinDesktop)
			macunminimize(c);
}

static void
macdropshellfocus(void)
{
	struct wlr_surface *k = seat->keyboard_state.focused_surface;
	LayerSurface *l = NULL;
	Client *x = NULL;

	if (k && toplevel_from_wlr_surface(k, &x, &l) == LayerShell && l
			&& l->layer_surface->current.keyboard_interactive
				!= ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE)
		wlr_seat_keyboard_notify_clear_focus(seat);
}

/* bring an app forward: its hidden windows come back, the space it is on
 * slides in, and its windows there rise together, this one on top */
static void
macactivate(Client *c, int allwindows)
{
	Monitor *m;
	Client *w;

	if (!macmanaged(c))
		return;
	m = c->mon;
	overviewset(0);
	macdesktopoff();
	wl_list_for_each(w, &clients, link)
		if (w->minimized == MinHidden && macsameapp(w, c))
			macunminimize(w);
	macunminimize(c);
	macview(m, c->ws);
	if (allwindows)
		wl_list_for_each_reverse(w, &fstack, flink)
			if (w != c && macsameapp(w, c) && VISIBLEON(w, m))
				wlr_scene_node_raise_to_top(&w->scene->node);
	/* the dock or launcher that asked keeps the keyboard otherwise, and
	 * focusclient leaves a focused overlay alone */
	macdropshellfocus();
	focusclient(c, 1);
}

static Client *
macappwindow(const char *app)
{
	Client *c, *min = NULL;

	wl_list_for_each(c, &fstack, flink) {
		if (!macmanaged(c) || strcmp(macappid(c), app))
			continue;
		if (c->minimized != MinMinimized)
			return c;
		if (!min)
			min = c;
	}
	return min;
}

static void
macftlactivate(Client *c)
{
	macactivate(c, 1);
}

static void
macftlminimize(struct wl_listener *listener, void *data)
{
	struct wlr_foreign_toplevel_handle_v1_minimized_event *ev = data;
	Client *c = wl_container_of(listener, c, ftl_minimize);

	if (ev->minimized)
		macminimize(c, MinMinimized);
	else
		macactivate(c, 0);
}

static void
macrequestminimize(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, req_minimize);

	if (mac_stacking)
		macminimize(c, MinMinimized);
}

static void
macminimizeaction(const Arg *arg)
{
	macminimize(focustop(selmon), MinMinimized);
}

static void
machideapp(const Arg *arg)
{
	Client *sel = focustop(selmon), *c;

	if (!sel)
		return;
	wl_list_for_each(c, &clients, link)
		if (macmanaged(c) && !c->isfullscreen
				&& (arg->i ? !macsameapp(c, sel) : macsameapp(c, sel)))
			macminimize(c, MinHidden);
}

static void
macquitapp(const Arg *arg)
{
	Client *sel = focustop(selmon), *c;
	char app[128];

	if (!sel)
		return;
	snprintf(app, sizeof app, "%s", macappid(sel));
	wl_list_for_each(c, &clients, link) {
		if (macmanaged(c) && !strcmp(macappid(c), app)) {
			if (VISIBLEON(c, c->mon))
				clientcloseanim(c);
			client_send_close(c);
		}
	}
}

/* Cmd+`: the next window of the same app on this space */
static void
macappnext(const Arg *arg)
{
	Client *sel = focustop(selmon), *c, *last = NULL;

	if (!sel)
		return;
	wl_list_for_each(c, &fstack, flink)
		if (c != sel && macsameapp(c, sel) && VISIBLEON(c, selmon))
			last = c;
	if (last)
		focusclient(last, 1);
}

static void
macshowdesktop(const Arg *arg)
{
	Client *c;
	int any = 0;

	wl_list_for_each(c, &clients, link)
		any |= c->minimized == MinDesktop;
	if (any) {
		macdesktopoff();
		focusclient(focustop(selmon), 1);
		return;
	}
	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, selmon) && macmanaged(c) && !c->isfullscreen)
			macminimize(c, MinDesktop);
}

static void
macspaceadd(const Arg *arg)
{
	if (!selmon || !macspaceinsert(selmon, macspaces(selmon)))
		return;
	ipcnotifyall();
	if (overview_visible)
		overviewbuild();
}

static void
macspacedel(const Arg *arg)
{
	if (selmon)
		macspaceremove(selmon, selmon->ws);
	if (overview_visible)
		overviewbuild();
}

static void
macmissioncontrol(const Arg *arg)
{
	if (overview_active && !mc_appexpose) {
		overviewset(0);
		return;
	}
	overviewset(0);
	mc_appexpose = 0;
	overviewset(1);
}

static void
macappexpose(const Arg *arg)
{
	Client *sel = focustop(selmon);

	if (overview_active && mc_appexpose) {
		overviewset(0);
		return;
	}
	if (!sel)
		return;
	overviewset(0);
	snprintf(mc_app, sizeof mc_app, "%s", macappid(sel));
	mc_appexpose = 1;
	overviewset(1);
}

static int
macisterm(Client *c)
{
	char buf[sizeof mac_terminals], *t, *save = NULL;
	const char *id = c ? client_get_appid(c) : NULL;

	if (!id)
		return 0;
	snprintf(buf, sizeof buf, "%s", mac_terminals);
	for (t = strtok_r(buf, " ,", &save); t; t = strtok_r(NULL, " ,", &save))
		if (!strcasecmp(t, id))
			return 1;
	return 0;
}

static Client *
macfocused(void)
{
	Client *c = NULL;

	toplevel_from_wlr_surface(seat->keyboard_state.focused_surface, &c, NULL);
	return c;
}

/* apps see Cmd as Ctrl (Ctrl+Shift in a terminal), the way their shortcuts
 * are written on Linux */
static void
macsendmods(struct wlr_keyboard *kb)
{
	struct wlr_keyboard_modifiers mods;

	if (!kb)
		return;
	mods = kb->modifiers;
	if (mac_stacking && mac_cmd_as_ctrl && (mods.depressed & WLR_MODIFIER_LOGO))
		mods.depressed = (mods.depressed & ~WLR_MODIFIER_LOGO) | WLR_MODIFIER_CTRL
				| (macisterm(macfocused()) ? WLR_MODIFIER_SHIFT : 0);
	wlr_seat_keyboard_notify_modifiers(seat, &mods);
}

/* Cmd+arrows jump to line and document ends, Option+arrows by word */
static int
macsendkey(struct wlr_keyboard *kb, uint32_t time, uint32_t code, uint32_t state)
{
	struct wlr_keyboard_modifiers mods;
	uint32_t m, keep, sent = 0, newmods = 0;
	size_t i;

	if (!mac_stacking || !mac_cmd_as_ctrl || !kb)
		return 0;
	if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		for (i = 0; i < LENGTH(mac_keys); i++) {
			if (mac_keys[i].code == code + 1) {
				wlr_seat_keyboard_notify_key(seat, time, mac_keys[i].sent,
						state);
				mac_keys[i].code = 0;
				macsendmods(kb);
				return 1;
			}
		}
		return 0;
	}
	m = kb->modifiers.depressed;
	keep = m & WLR_MODIFIER_SHIFT;
	if ((m & WLR_MODIFIER_LOGO) && !(m & (WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL))) {
		switch (code) {
		case KEY_LEFT: sent = KEY_HOME; newmods = keep; break;
		case KEY_RIGHT: sent = KEY_END; newmods = keep; break;
		case KEY_UP: sent = KEY_HOME; newmods = keep | WLR_MODIFIER_CTRL; break;
		case KEY_DOWN: sent = KEY_END; newmods = keep | WLR_MODIFIER_CTRL; break;
		}
	} else if ((m & WLR_MODIFIER_ALT) && !(m & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL))) {
		switch (code) {
		case KEY_LEFT: case KEY_RIGHT: case KEY_BACKSPACE: case KEY_DELETE:
			sent = code;
			newmods = keep | WLR_MODIFIER_CTRL;
			break;
		}
	}
	if (!sent || macisterm(macfocused()))
		return 0;
	for (i = 0; i < LENGTH(mac_keys) && mac_keys[i].code; i++);
	if (i == LENGTH(mac_keys))
		return 0;
	mac_keys[i].code = code + 1;
	mac_keys[i].sent = sent;
	mods = kb->modifiers;
	mods.depressed = newmods;
	wlr_seat_keyboard_notify_modifiers(seat, &mods);
	wlr_seat_keyboard_notify_key(seat, time, sent, state);
	return 1;
}

static void
machotcorner(void)
{
	Monitor *m;
	double x = cursor->x, y = cursor->y;
	int i, near, far;

	if (!mac_stacking || locked || cursor_mode != CurNormal
			|| !(m = xytomon(x, y)))
		return;
	for (i = 0; i < 4; i++) {
		double cx = m->m.x + (i & 1 ? m->m.width - 1 : 0);
		double cy = m->m.y + (i & 2 ? m->m.height - 1 : 0);
		near = fabs(x - cx) <= 1.0 && fabs(y - cy) <= 1.0;
		far = fabs(x - cx) > 24.0 || fabs(y - cy) > 24.0;
		if (near && mac_corner[i].armed && mac_corner[i].func) {
			mac_corner[i].armed = 0;
			mac_corner[i].func(&mac_corner[i].arg);
		} else if (far) {
			mac_corner[i].armed = 1;
		}
	}
}

/* one line per output for the shell: name, focused, current space, how
 * many there are, and which of them hold a fullscreen app */
static void
macsavestate(void)
{
	static char *last;
	char *text = NULL;
	size_t len = 0;
	unsigned int i, n;
	Monitor *m;
	FILE *f;

	if (!mac_stacking || !(f = open_memstream(&text, &len)))
		return;
	wl_list_for_each(m, &mons, link) {
		if (!m->wlr_output->enabled || m->mirroring)
			continue;
		n = macspaces(m);
		fprintf(f, "%s %d %u %u ", m->wlr_output->name, m == selmon, m->ws, n);
		for (i = 0; i < n; i++)
			fprintf(f, "%s%d", i ? "," : "", macfsspace(m, i));
		fputc('\n', f);
	}
	fclose(f);
	statewrite("spaces", &last, text, len);
}

static int
maccfg(const char *k, const char *v)
{
	static const char *corners[] = {"hot_corner_top_left", "hot_corner_top_right",
			"hot_corner_bottom_left", "hot_corner_bottom_right"};
	int on = !strcmp(v, "true") || !strcmp(v, "1"), i;

	if (!strcmp(k, "stacking"))
		mac_stacking = on;
	else if (!strcmp(k, "cmd_as_ctrl"))
		mac_cmd_as_ctrl = on;
	else if (!strcmp(k, "super_tap_overview"))
		mac_super_tap = on;
	else if (!strcmp(k, "spaces"))
		mac_spaces = MAX(1, MIN(NUMWS, atoi(v)));
	else if (!strcmp(k, "edge_tiling"))
		mac_edge_tiling = on;
	else if (!strcmp(k, "minimize_effect"))
		mac_genie = !strcmp(v, "genie");
	else if (!strcmp(k, "genie_duration"))
		mac_genie_duration = atoi(v);
	else if (!strcmp(k, "terminals"))
		snprintf(mac_terminals, sizeof mac_terminals, "%s", v);
	else {
		for (i = 0; i < 4; i++) {
			if (strcmp(k, corners[i]))
				continue;
			mac_corner[i].func = NULL;
			mac_corner[i].armed = 1;
			if (strcmp(v, "none") && !cfgaction(v, &mac_corner[i].func,
					&mac_corner[i].arg))
				cfgerror("bad hot corner action '%s'", v);
			return 1;
		}
		return 0;
	}
	return 1;
}

#include "macgenie.c"
#include "macmc.c"
