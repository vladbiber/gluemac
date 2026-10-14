/* the title bar gluemac draws above every window: traffic lights and title */

#define MACTB_D 12      /* button diameter */
#define MACTB_X0 20     /* centre of the first button */
#define MACTB_GAP 20    /* centre to centre */

static Client *mactb_hover;
static int mactb_hoverbtn = -1;
static Client *mactb_press;
static int mactb_pressbtn = -1;
static Client *mactb_lastc;
static uint32_t mactb_lastt;

static int
mactbwanted(Client *c)
{
	return mac_stacking && mac_titlebar && !decorhidden && c->scene
			&& !client_is_unmanaged(c) && !c->isfullscreen && !c->isfakefull;
}

/* where the bar is inside the window's tree: under the border normally, and
 * over the top of a full screen window, below the menu bar, while the
 * pointer has called it down */
static int
mactbbox(Client *c, struct wlr_box *b)
{
	if (c->tb) {
		*b = (struct wlr_box){c->bw, c->bw, c->geom.width - 2 * (int)c->bw, c->tb};
		return b->width > 0;
	}
	if (c->isfullscreen && c->tbreveal && mac_stacking && mac_titlebar) {
		*b = (struct wlr_box){0, mac_menubar_h, c->geom.width, mac_titlebar_h};
		return b->width > 0;
	}
	return 0;
}

/* 0..2 for a button, 3 for the rest of the button group, -1 elsewhere;
 * lx/ly are relative to the bar */
static int
mactbhit(Client *c, double lx, double ly)
{
	struct wlr_box b;
	int i;
	double dx, dy;

	if (!mactbbox(c, &b) || ly < 0 || ly >= b.height || lx < 0 || lx >= b.width)
		return -1;
	for (i = 0; i < 3; i++) {
		dx = lx - (MACTB_X0 + i * MACTB_GAP);
		dy = ly - b.height / 2.0;
		if (dx * dx + dy * dy <= (MACTB_D / 2.0 + 2) * (MACTB_D / 2.0 + 2))
			return i;
	}
	return lx < MACTB_X0 + 2 * MACTB_GAP + MACTB_D ? 3 : -1;
}

static void
mactbglyph(cairo_t *cr, int i, double cx, double cy, double s)
{
	double r = MACTB_D * s * 0.22;

	cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
	cairo_set_line_width(cr, 1.1 * s);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	if (i == 0) {
		cairo_move_to(cr, cx - r, cy - r);
		cairo_line_to(cr, cx + r, cy + r);
		cairo_move_to(cr, cx + r, cy - r);
		cairo_line_to(cr, cx - r, cy + r);
		cairo_stroke(cr);
	} else if (i == 1) {
		cairo_move_to(cr, cx - r * 1.2, cy);
		cairo_line_to(cr, cx + r * 1.2, cy);
		cairo_stroke(cr);
	} else {
		/* two arrowheads pointing apart, the full screen sign */
		r *= 1.15;
		cairo_move_to(cr, cx - r, cy - r);
		cairo_line_to(cr, cx + r * 0.35, cy - r);
		cairo_line_to(cr, cx - r, cy + r * 0.35);
		cairo_close_path(cr);
		cairo_move_to(cr, cx + r, cy + r);
		cairo_line_to(cr, cx - r * 0.35, cy + r);
		cairo_line_to(cr, cx + r, cy - r * 0.35);
		cairo_close_path(cr);
		cairo_fill(cr);
	}
}

static struct wlr_buffer *
mactbdraw(Client *c, int w, int h, float s, int focused, int hover, int press)
{
	static const double fill[3][3] = {{1.0, 0.373, 0.341},
			{0.996, 0.737, 0.180}, {0.157, 0.784, 0.251}};
	static const double edge[3][3] = {{0.878, 0.267, 0.243},
			{0.871, 0.631, 0.137}, {0.102, 0.671, 0.161}};
	const char *title = client_get_title(c);
	int pw = MAX(1, (int)lroundf(w * s)), ph = MAX(1, (int)lroundf(h * s));
	int i, tw, th, maxw;
	double cx, cy, rr = MACTB_D * s / 2.0, k;
	cairo_surface_t *surf;
	cairo_t *cr;
	PangoLayout *pl;
	PangoFontDescription *fd;
	char font[160];

	surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
	cr = cairo_create(surf);
	if (mac_titlebar_dark)
		k = focused ? 0.200 : 0.165;
	else
		k = focused ? 0.910 : 0.965;
	cairo_set_source_rgb(cr, k, k, k + (mac_titlebar_dark ? 0.008 : 0));
	cairo_paint(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, mac_titlebar_dark ? 0.55 : 0.12);
	cairo_rectangle(cr, 0, ph - MAX(1, (int)s), pw, MAX(1, (int)s));
	cairo_fill(cr);

	for (i = 0; i < 3; i++) {
		cx = (MACTB_X0 + i * MACTB_GAP) * s;
		cy = ph / 2.0;
		cairo_arc(cr, cx, cy, rr, 0, 2 * G_PI);
		if (focused || hover >= 0) {
			k = press == i ? 0.82 : 1.0;
			cairo_set_source_rgb(cr, fill[i][0] * k, fill[i][1] * k, fill[i][2] * k);
			cairo_fill_preserve(cr);
			cairo_set_source_rgb(cr, edge[i][0], edge[i][1], edge[i][2]);
		} else {
			k = mac_titlebar_dark ? 0.32 : 0.80;
			cairo_set_source_rgb(cr, k, k, k);
			cairo_fill_preserve(cr);
			cairo_set_source_rgba(cr, 0, 0, 0, 0.10);
		}
		cairo_set_line_width(cr, 0.6 * s);
		cairo_stroke(cr);
		if (hover >= 0)
			mactbglyph(cr, i, cx, cy, s);
	}

	maxw = w - 2 * (MACTB_X0 + 2 * MACTB_GAP + MACTB_D + 8);
	if (title && *title && maxw > 20) {
		pl = pango_cairo_create_layout(cr);
		snprintf(font, sizeof font, "%s Semi-Bold %dpx", mac_font,
				(int)lroundf(13 * s));
		fd = pango_font_description_from_string(font);
		pango_layout_set_font_description(pl, fd);
		pango_font_description_free(fd);
		pango_layout_set_text(pl, title, -1);
		pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_END);
		pango_layout_set_width(pl, (int)(maxw * s) * PANGO_SCALE);
		pango_layout_set_single_paragraph_mode(pl, TRUE);
		pango_layout_get_pixel_size(pl, &tw, &th);
		if (mac_titlebar_dark)
			k = focused ? 0.86 : 0.50;
		else
			k = focused ? 0.30 : 0.66;
		cairo_set_source_rgb(cr, k, k, k);
		cairo_move_to(cr, (pw - tw) / 2.0, (ph - th) / 2.0);
		pango_cairo_show_layout(cr, pl);
		g_object_unref(pl);
	}
	cairo_destroy(cr);
	return macbufwrap(surf);
}

static uint32_t
mactbhash(const char *s, uint32_t h)
{
	for (; s && *s; s++)
		h = (h ^ (unsigned char)*s) * 16777619u;
	return h;
}

/* (re)draws the bar when anything it shows changed; cheap otherwise */
static void
mactbupdate(Client *c)
{
	struct wlr_buffer *b;
	struct wlr_box box;
	int w, focused, hover, press, r;
	float s;
	uint32_t h;

	if (!c || !c->scene)
		return;
	if (!c->isfullscreen)
		c->tbreveal = 0;
	if (c->offscreen || !c->mon || !client_surface(c)->mapped || !mactbbox(c, &box)) {
		if (c->titlebar)
			wlr_scene_node_set_enabled(&c->titlebar->node, 0);
		return;
	}
	if (!c->titlebar) {
		c->titlebar = wlr_scene_buffer_create(c->scene, NULL);
		c->titlebar->node.data = c;
	}
	/* a full screen window's bar lies over its content */
	if (c->tb)
		wlr_scene_node_place_below(&c->titlebar->node, &c->scene_surface->node);
	else
		wlr_scene_node_raise_to_top(&c->titlebar->node);
	w = box.width;
	s = c->mon->wlr_output->scale;
	focused = client_surface(c) == seat->keyboard_state.focused_surface;
	hover = mactb_hover == c ? mactb_hoverbtn : -1;
	press = mactb_press == c ? mactb_pressbtn : -1;
	h = mactbhash(client_get_title(c), 2166136261u);
	h = (h ^ (uint32_t)w) * 16777619u;
	h = (h ^ (uint32_t)(s * 100)) * 16777619u;
	h = (h ^ (uint32_t)(focused | (hover + 1) << 1 | (press + 1) << 4
			| mac_titlebar_dark << 7 | box.height << 8 | c->tb << 15)) * 16777619u;
	wlr_scene_node_set_enabled(&c->titlebar->node, 1);
	wlr_scene_node_set_position(&c->titlebar->node, box.x, box.y);
	wlr_scene_buffer_set_dest_size(c->titlebar, w, box.height);
	if (h == c->tbhash && c->titlebar->buffer)
		return;
	c->tbhash = h;
	b = mactbdraw(c, w, box.height, s, focused, hover, press);
	wlr_scene_buffer_set_buffer(c->titlebar, b);
	wlr_buffer_drop(b);
	r = c->tb ? MAX(0, MIN(corner_radius, w / 2) - (int)c->bw) : 0;
	wlr_scene_buffer_set_corner_radii(c->titlebar, corner_radii_new(r, r, 0, 0));
}

/* the bar follows the window while the open animation scales it */
static void
mactbscale(Client *c, float z, int bw)
{
	int w;

	if (!c->titlebar || !c->tb)
		return;
	w = MAX(1, (int)roundf((c->geom.width - 2 * (int)c->bw) * z));
	wlr_scene_node_set_position(&c->titlebar->node, bw, bw);
	wlr_scene_buffer_set_dest_size(c->titlebar, w, MAX(1, (int)roundf(c->tb * z)));
}

static void
mactbforget(Client *c)
{
	if (mactb_hover == c)
		mactb_hover = NULL;
	if (mactb_press == c)
		mactb_press = NULL;
	if (mactb_lastc == c)
		mactb_lastc = NULL;
}

/* over a full screen window the bar comes down when the pointer touches
 * the top edge and goes once it leaves the menu bar and the title bar */
static void
mactbreveal(void)
{
	Monitor *m = xytomon(cursor->x, cursor->y);
	Client *f = m ? focustop(m) : NULL;
	double ry;
	int want;

	if (!f || !f->isfullscreen || !mac_stacking || !mac_titlebar)
		return;
	ry = cursor->y - m->m.y;
	want = f->tbreveal ? ry < mac_menubar_h + mac_titlebar_h + 6 : ry <= 1;
	if (want == f->tbreveal || cursor_mode != CurNormal)
		return;
	f->tbreveal = want;
	mactbupdate(f);
	ipcnotifyall();
}

static void
mactbmotion(Client *c)
{
	Client *old = mactb_hover;
	struct wlr_box b;
	int btn = -1;

	mactbreveal();
	if (c && mactbbox(c, &b) && cursor_mode != CurMove && cursor_mode != CurMacResize)
		btn = mactbhit(c, cursor->x - c->geom.x - b.x, cursor->y - c->geom.y - b.y);
	if (btn < 0)
		c = NULL;
	if (c == old && btn == mactb_hoverbtn)
		return;
	mactb_hover = c;
	mactb_hoverbtn = btn;
	if (old && old != c)
		mactbupdate(old);
	if (c)
		mactbupdate(c);
}

static void
mactbaction(Client *c, int btn)
{
	switch (btn) {
	case 0:
		clientcloseanim(c);
		client_send_close(c);
		break;
	case 1:
		macminimize(c, MinMinimized);
		break;
	case 2:
		c->tbreveal = 0;
		setfullscreen(c, !c->isfullscreen);
		focusclient(c, 1);
		break;
	}
}

/* a left press on the bar: a button arms, the rest moves the window and a
 * double click zooms it, as the title bar of a Mac window does */
static int
mactbbutton(Client *c, struct wlr_pointer_button_event *ev)
{
	struct wlr_box b;
	double lx, ly;
	int btn;

	if (!c || ev->button != BTN_LEFT || !mactbbox(c, &b))
		return 0;
	lx = cursor->x - c->geom.x - b.x;
	ly = cursor->y - c->geom.y - b.y;
	if (ly < 0 || ly >= b.height || lx < 0 || lx >= b.width)
		return 0;
	btn = mactbhit(c, lx, ly);
	if (btn >= 0 && btn < 3) {
		mactb_press = c;
		mactb_pressbtn = btn;
		mactbupdate(c);
		return 1;
	}
	if (!c->tb)
		return 1;
	if (mactb_lastc == c && ev->time_msec - mactb_lastt < 400) {
		mactb_lastc = NULL;
		maczoom(c);
		return 1;
	}
	mactb_lastc = c;
	mactb_lastt = ev->time_msec;
	macgrab(c, CurMove, 0);
	return 1;
}

static int
mactbrelease(void)
{
	Client *c = mactb_press, *under = NULL;
	struct wlr_box b;
	int btn = mactb_pressbtn;

	if (!c)
		return 0;
	mactb_press = NULL;
	mactb_pressbtn = -1;
	cursor_mode = CurNormal;
	mactbupdate(c);
	xytonode(cursor->x, cursor->y, NULL, &under, NULL, NULL, NULL);
	if (under == c && mactbbox(c, &b) && mactbhit(c, cursor->x - c->geom.x - b.x,
			cursor->y - c->geom.y - b.y) == btn)
		mactbaction(c, btn);
	return 1;
}
