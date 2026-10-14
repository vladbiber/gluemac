/* Mission Control, App Exposé and the Cmd+Tab switcher */

#define MC_MAXWIN 64

static struct wlr_scene_rect *overviewrect(struct wlr_scene_tree *tree,
		struct wlr_box box, int radius, const float color[static 4],
		const struct wlr_box *clip);
static void overviewshadow(struct wlr_scene_tree *tree, struct wlr_box box,
		int radius, float sigma, const float color[static 4],
		const struct wlr_box *clip);
static void overviewwindowdraw(struct wlr_scene_tree *tree, Client *c,
		const struct wlr_box *box, float opacity, int focused,
		const struct wlr_box *clip);
static void overviewpanel(Monitor *m, unsigned int ws,
		const struct wlr_box *box, float opacity);
static void overviewclear(void);
static void overviewdragclear(void);
static int overviewvalid(Monitor *m);

typedef struct {
	struct wlr_box strip[NUMWS], plus, area;
	unsigned int nspaces;
	int nwin;
	Client *win[MC_MAXWIN];
	struct wlr_box slot[MC_MAXWIN];
} McLayout;

static Client *mc_hover;
static int mc_hover_space = -1;
static int mc_press_space = -1;
static int mc_press_minus;

static int
mcwanted(Monitor *m, Client *c)
{
	if (c->mon != m || client_is_unmanaged(c) || !client_surface(c)->mapped
			|| c->minimized == MinDesktop)
		return 0;
	if (mc_appexpose)
		return !strcmp(macappid(c), mc_app) && c->minimized != MinHidden;
	return c->ws == m->ws && !c->minimized;
}

static void
mcorder(McLayout *l)
{
	int i, j;
	Client *t;

	/* top to bottom, then left to right, so the spread keeps the layout */
	for (i = 1; i < l->nwin; i++) {
		for (j = i; j > 0; j--) {
			Client *a = l->win[j - 1], *b = l->win[j];
			int ay = a->geom.y + a->geom.height / 2, by = b->geom.y + b->geom.height / 2;
			int ax = a->geom.x + a->geom.width / 2, bx = b->geom.x + b->geom.width / 2;
			if (ay < by - 40 || (abs(ay - by) <= 40 && ax <= bx))
				break;
			t = l->win[j - 1];
			l->win[j - 1] = l->win[j];
			l->win[j] = t;
		}
	}
}

static void
mcspread(McLayout *l)
{
	struct wlr_box a = l->area;
	int rows, best = 1, cols, r, i, k, n = l->nwin;
	double score, bestscore = -1.0;

	if (!n)
		return;
	for (rows = 1; rows <= n; rows++) {
		cols = (n + rows - 1) / rows;
		score = 0.0;
		for (i = 0; i < n; i++) {
			struct wlr_box g = l->win[i]->geom;
			double s = MIN(1.0, MIN(a.width / (double)cols * 0.88 / MAX(1, g.width),
					a.height / (double)rows * 0.86 / MAX(1, g.height)));
			score += s * s * g.width * g.height;
		}
		if (score > bestscore) {
			bestscore = score;
			best = rows;
		}
	}
	cols = (n + best - 1) / best;
	for (r = 0, k = 0; r < best; r++) {
		int inrow = MIN(cols, n - k), cw = a.width / cols, ch = a.height / best;
		int x0 = a.x + (a.width - inrow * cw) / 2;
		for (i = 0; i < inrow; i++, k++) {
			struct wlr_box g = l->win[k]->geom;
			double s = MIN(1.0, MIN(cw * 0.88 / MAX(1, g.width),
					ch * 0.86 / MAX(1, g.height)));
			int w = MAX(8, (int)(g.width * s)), h = MAX(8, (int)(g.height * s));
			l->slot[k] = (struct wlr_box){x0 + i * cw + (cw - w) / 2,
					a.y + r * ch + (ch - h) / 2, w, h};
		}
	}
}

static void
mclayout(Monitor *m, McLayout *l)
{
	unsigned int i;
	int th, tw, gap = 18, total, x, top;
	Client *c;

	memset(l, 0, sizeof *l);
	l->nspaces = macspaces(m);
	th = MAX(48, m->m.height / 9);
	tw = th * m->m.width / MAX(1, m->m.height);
	total = (int)l->nspaces * (tw + gap) + th * 2 / 3;
	if (total > m->w.width * 9 / 10) {
		double s = (m->w.width * 0.9) / total;
		th = (int)(th * s);
		tw = (int)(tw * s);
		gap = MAX(8, (int)(gap * s));
		total = (int)l->nspaces * (tw + gap) + th * 2 / 3;
	}
	top = m->w.y + 14;
	x = m->w.x + (m->w.width - total) / 2;
	for (i = 0; i < l->nspaces; i++, x += tw + gap)
		l->strip[i] = (struct wlr_box){x, top, tw, th};
	l->plus = (struct wlr_box){x, top + th / 6, th * 2 / 3, th * 2 / 3};
	if (mc_appexpose)
		top = m->w.y - th - 14;
	l->area = (struct wlr_box){m->w.x + 48, top + th + 40,
			m->w.width - 96, m->w.y + m->w.height - (top + th + 40) - 32};
	wl_list_for_each(c, &clients, link)
		if (l->nwin < MC_MAXWIN && mcwanted(m, c))
			l->win[l->nwin++] = c;
	mcorder(l);
	mcspread(l);
}

static struct wlr_box
mclerp(struct wlr_box a, struct wlr_box b, float t)
{
	return (struct wlr_box){a.x + (int)roundf((b.x - a.x) * t),
			a.y + (int)roundf((b.y - a.y) * t),
			MAX(1, a.width + (int)roundf((b.width - a.width) * t)),
			MAX(1, a.height + (int)roundf((b.height - a.height) * t))};
}

static void
mcplus(struct wlr_scene_tree *tree, struct wlr_box b, float t, const struct wlr_box *clip)
{
	float bg[] = {0.9f * t * 0.22f, 0.9f * t * 0.22f, 0.9f * t * 0.22f, 0.22f * t};
	float fg[] = {t, t, t, t};
	int s = b.width / 3, w = MAX(2, b.width / 14);

	overviewrect(tree, b, b.width / 4, bg, clip);
	overviewrect(tree, (struct wlr_box){b.x + (b.width - s) / 2,
			b.y + (b.height - w) / 2, s, w}, 1, fg, clip);
	overviewrect(tree, (struct wlr_box){b.x + (b.width - w) / 2,
			b.y + (b.height - s) / 2, w, s}, 1, fg, clip);
}

static struct wlr_box
mcminusbox(struct wlr_box b)
{
	int d = MAX(14, b.height / 4);

	return (struct wlr_box){b.x - d / 3, b.y - d / 3, d, d};
}

static void
mcbuildmon(Monitor *m)
{
	float t = m->overview_dim;
	float dim[] = {0.0f, 0.0f, 0.0f, 0.34f * t};
	float ring[] = {0.85f * t, 0.85f * t, 0.88f * t, 0.9f * t};
	float hover[] = {0.04f * t, 0.48f * t, 0.98f * t, t};
	float knob[] = {0.12f * t, 0.12f * t, 0.13f * t, 0.92f * t};
	float white[] = {t, t, t, t};
	struct wlr_scene_rect *r;
	McLayout l;
	unsigned int i;
	int k, lift;

	r = wlr_scene_rect_create(overview_dim_scene, m->m.width, m->m.height, dim);
	wlr_scene_node_set_position(&r->node, m->m.x, m->m.y);
	mclayout(m, &l);

	if (!mc_appexpose) {
		lift = (int)roundf((1.0f - t) * (l.strip[0].height + 40));
		for (i = 0; i < l.nspaces; i++) {
			struct wlr_box b = l.strip[i];
			b.y -= lift;
			if (i == m->ws || (int)i == mc_hover_space)
				overviewrect(overview_panels_scene, (struct wlr_box){b.x - 3,
						b.y - 3, b.width + 6, b.height + 6}, 9,
						(int)i == mc_hover_space && overview_dragging
						? hover : ring, &m->m);
			overviewpanel(m, i, &b, t);
			if ((int)i == mc_hover_space && !overview_dragging && l.nspaces > 1) {
				struct wlr_box d = mcminusbox(b);
				overviewrect(overview_panels_scene, d, d.width / 2, knob, &m->m);
				overviewrect(overview_panels_scene, (struct wlr_box){d.x + d.width / 4,
						d.y + d.height / 2 - 1, d.width / 2, 2}, 1, white, &m->m);
			}
		}
		l.plus.y -= lift;
		if (l.nspaces < NUMWS)
			mcplus(overview_panels_scene, l.plus, t, &m->m);
	}

	for (k = 0; k < l.nwin; k++) {
		Client *c = l.win[k];
		struct wlr_box from = c->geom, b;
		if (overview_dragging && c == overview_drag_client)
			continue;
		if (c->minimized || c->ws != m->ws)
			from = (struct wlr_box){l.slot[k].x, m->m.y + m->m.height,
					l.slot[k].width, l.slot[k].height};
		b = mclerp(from, l.slot[k], t);
		if (c == mc_hover && t > 0.99f)
			overviewrect(overview_panels_scene, (struct wlr_box){b.x - 4, b.y - 4,
					b.width + 8, b.height + 8}, MAX(8, corner_radius) + 4,
					hover, &m->m);
		overviewwindowdraw(overview_panels_scene, c, &b,
				c->ws == m->ws || t > 0.99f ? 1.0f : t, 0, &m->m);
	}
	wlr_output_schedule_frame(m->wlr_output);
}

static void
mcbuild(void)
{
	Monitor *m;

	overviewclear();
	wl_list_for_each(m, &mons, link)
		if (overviewvalid(m))
			mcbuildmon(m);
}

static Client *
mcclientat(Monitor *m, double x, double y, struct wlr_box *box)
{
	McLayout l;
	int k;

	mclayout(m, &l);
	for (k = l.nwin; k-- > 0;) {
		if (wlr_box_contains_point(&l.slot[k], x, y)) {
			if (box)
				*box = l.slot[k];
			return l.win[k];
		}
	}
	return NULL;
}

/* index of the space thumbnail at x,y, NUMWS for the + button, -1 for none */
static int
mcspaceat(Monitor *m, double x, double y, int *minus)
{
	McLayout l;
	unsigned int i;

	if (minus)
		*minus = 0;
	if (mc_appexpose)
		return -1;
	mclayout(m, &l);
	for (i = 0; i < l.nspaces; i++) {
		struct wlr_box d = mcminusbox(l.strip[i]);
		if (l.nspaces > 1 && wlr_box_contains_point(&d, x, y)) {
			if (minus)
				*minus = 1;
			return (int)i;
		}
		if (wlr_box_contains_point(&l.strip[i], x, y))
			return (int)i;
	}
	if (l.nspaces < NUMWS && wlr_box_contains_point(&l.plus, x, y))
		return NUMWS;
	return -1;
}

static void
mcbutton(struct wlr_pointer_button_event *ev)
{
	Monitor *m = xytomon(cursor->x, cursor->y);
	struct wlr_box box;
	Client *c;
	int sp, minus;

	if (ev->state == WL_POINTER_BUTTON_STATE_PRESSED) {
		overview_button_swallow = 1;
		if (!overview_active || ev->button != BTN_LEFT || !overviewvalid(m)
				|| m->overview_animating)
			return;
		selmon = m;
		mc_press_space = mcspaceat(m, cursor->x, cursor->y, &mc_press_minus);
		if (mc_press_space >= 0)
			return;
		if ((c = mcclientat(m, cursor->x, cursor->y, &box))) {
			overview_drag_client = c;
			overview_drag_box = box;
			overview_press_x = cursor->x;
			overview_press_y = cursor->y;
			overview_grab_x = (int)round(cursor->x) - box.x;
			overview_grab_y = (int)round(cursor->y) - box.y;
			return;
		}
		overviewset(0);
		return;
	}
	overview_button_swallow = 0;
	if (!overview_active || !overviewvalid(m))
		goto done;
	sp = mcspaceat(m, cursor->x, cursor->y, &minus);
	if ((c = overview_drag_client)) {
		if (overview_dragging) {
			if (sp == NUMWS && macspaceinsert(m, macspaces(m)))
				sp = (int)macspaces(m) - 1;
			if (sp >= 0 && sp < NUMWS) {
				if (c->mon != m)
					setmon(c, m, sp);
				else
					macmovespace(c, (unsigned int)sp);
				macunminimize(c);
				arrange(m);
			}
		} else {
			overview_focus_client = c;
			if (c->minimized)
				macunminimize(c);
			if (c->ws != m->ws)
				macview(m, c->ws);
			overview_drag_client = NULL;
			overviewset(0);
			return;
		}
	} else if (sp >= 0 && sp == mc_press_space) {
		if (sp == NUMWS) {
			macspaceadd(NULL);
		} else if (minus && mc_press_minus) {
			macspaceremove(m, (unsigned int)sp);
		} else {
			macview(m, (unsigned int)sp);
			overviewset(0);
			goto done;
		}
	}
done:
	overview_drag_client = NULL;
	overview_dragging = 0;
	mc_press_space = -1;
	overviewdragclear();
	if (overview_visible)
		mcbuild();
}

static int
mcmotion(void)
{
	Monitor *m;
	Client *hov = NULL;
	int sp = -1;

	if (!overview_visible)
		return 0;
	if (!overview_active)
		return 1;
	m = xytomon(cursor->x, cursor->y);
	if (overview_drag_client && !overview_dragging
			&& (fabs(cursor->x - overview_press_x) >= 7.0
				|| fabs(cursor->y - overview_press_y) >= 7.0)) {
		overview_dragging = 1;
		overviewdragclear();
		overview_drag_box.x = overview_drag_box.y = 0;
		overviewwindowdraw(overview_drag_scene, overview_drag_client,
				&overview_drag_box, 0.96f, 0,
				&(struct wlr_box){-(1 << 20), -(1 << 20), 1 << 21, 1 << 21});
	}
	if (overview_dragging)
		wlr_scene_node_set_position(&overview_drag_scene->node,
				(int)round(cursor->x) - overview_grab_x,
				(int)round(cursor->y) - overview_grab_y);
	if (overviewvalid(m) && !m->overview_animating) {
		sp = mcspaceat(m, cursor->x, cursor->y, NULL);
		if (sp == NUMWS)
			sp = -1;
		if (!overview_dragging)
			hov = mcclientat(m, cursor->x, cursor->y, NULL);
	}
	if (hov != mc_hover || sp != mc_hover_space) {
		mc_hover = hov;
		mc_hover_space = sp;
		mcbuild();
	}
	return 1;
}

static int
mckey(uint32_t mods, xkb_keysym_t sym)
{
	if (!CLEANMASK(mods)) {
		if (sym == XKB_KEY_Escape)
			overviewset(0);
		else if (sym == XKB_KEY_Return && mc_hover) {
			overview_focus_client = mc_hover;
			macunminimize(mc_hover);
			macview(mc_hover->mon, mc_hover->ws);
			overviewset(0);
		}
		return 1;
	}
	keybinding(mods, sym);
	if (overview_visible)
		mcbuild();
	return 1;
}

static void
macswitchhide(void)
{
	if (macsw.tree)
		wlr_scene_node_destroy(&macsw.tree->node);
	macsw.tree = NULL;
	macsw.active = 0;
}

static void
macswitchdraw(void)
{
	float panel[] = {0.11f, 0.11f, 0.12f, 0.86f};
	float ring[] = {0.42f, 0.42f, 0.44f, 0.48f};
	float shadow[] = {0.0f, 0.0f, 0.0f, 0.5f};
	struct wlr_box all, cell;
	Monitor *m = selmon;
	int i, cw, ch, pad = 18, n = macsw.n;

	if (macsw.tree)
		wlr_scene_node_destroy(&macsw.tree->node);
	macsw.tree = NULL;
	if (!m || !n)
		return;
	cw = MIN(170, (m->m.width - 2 * pad - 80) / n);
	ch = cw * 3 / 4;
	all = (struct wlr_box){0, 0, n * cw + 2 * pad, ch + 2 * pad};
	all.x = m->m.x + (m->m.width - all.width) / 2;
	all.y = m->m.y + (m->m.height - all.height) / 2;
	macsw.tree = wlr_scene_tree_create(layers[LyrOverlay]);
	overviewshadow(macsw.tree, all, 22, 20.0f, shadow, &m->m);
	overviewrect(macsw.tree, all, 22, panel, &m->m);
	for (i = 0; i < n; i++) {
		Client *c = macappwindow(macsw.app[i]);
		struct wlr_box b;
		double s;
		cell = (struct wlr_box){all.x + pad + i * cw, all.y + pad, cw, ch};
		if (i == macsw.idx)
			overviewrect(macsw.tree, (struct wlr_box){cell.x + 4, cell.y + 4,
					cell.width - 8, cell.height - 8}, 14, ring, &m->m);
		if (!c)
			continue;
		s = MIN((cw - 28.0) / MAX(1, c->geom.width), (ch - 28.0) / MAX(1, c->geom.height));
		b.width = MAX(8, (int)(c->geom.width * s));
		b.height = MAX(8, (int)(c->geom.height * s));
		b.x = cell.x + (cw - b.width) / 2;
		b.y = cell.y + (ch - b.height) / 2;
		overviewwindowdraw(macsw.tree, c, &b, 1.0f, 0, &m->m);
	}
}

/* Cmd+Tab walks the apps in the order they were used; letting go of Cmd
 * brings the chosen one forward */
static void
macswitch(const Arg *arg)
{
	Client *c;
	int i;

	if (!macsw.active) {
		macsw.n = 0;
		wl_list_for_each(c, &fstack, flink) {
			const char *id = macappid(c);
			if (!macmanaged(c) || macsw.n >= (int)LENGTH(macsw.app))
				continue;
			for (i = 0; i < macsw.n && strcmp(macsw.app[i], id); i++);
			if (i == macsw.n)
				snprintf(macsw.app[macsw.n++], sizeof macsw.app[0], "%s", id);
		}
		if (!macsw.n)
			return;
		macsw.active = 1;
		macsw.idx = 0;
	}
	macsw.idx = (macsw.idx + arg->i + macsw.n) % macsw.n;
	macswitchdraw();
}

/* Cmd went up: finish an app switch; 1 = the key release is used up */
static int
macsuperup(void)
{
	Client *c;

	if (!macsw.active)
		return 0;
	c = macappwindow(macsw.app[macsw.idx]);
	macswitchhide();
	if (c)
		macactivate(c, 1);
	return 1;
}
