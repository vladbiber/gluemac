/* The genie: a minimised window pours into its dock icon. It is drawn as
 * thin horizontal strips cloned from the window's buffers, each one pinched
 * towards the icon the closer it gets to it, the way KWin's Magic Lamp
 * bends its mesh. Restoring plays it backwards. */

typedef struct {
	struct wlr_scene_buffer *scene;
	float u0, u1, v0, v1; /* where the strip sits in the window, 0..1 */
	float r0, r1, r2, r3; /* corner radii of the source, scaled per strip */
} GenieStrip;

typedef struct {
	struct wl_list link;
	struct wlr_scene_tree *tree;
	Monitor *mon;
	Client *c;            /* set while restoring: shown again at the end */
	struct wlr_box from, to;
	GenieStrip *strips;
	int n, cap, reverse;
	float t;
} Genie;

static struct wl_list genies = {&genies, &genies};

static float
geniestep(float a, float b, float x)
{
	x = (x - a) / (b - a);
	x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
	return x * x * (3.0f - 2.0f * x);
}

static float
genieease(float x)
{
	x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
	return x < 0.5f ? 4.0f * x * x * x : 1.0f - powf(-2.0f * x + 2.0f, 3.0f) / 2.0f;
}

static void
geniestrips(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
	Genie *g = data;
	struct wlr_fbox src = buffer->src_box;
	int width, height, n, k;

	if (!buffer->buffer)
		return;
	if (src.width <= 0 || src.height <= 0)
		src = (struct wlr_fbox){0, 0, buffer->buffer->width, buffer->buffer->height};
	width = buffer->dst_width > 0 ? buffer->dst_width : (int)round(src.width);
	height = buffer->dst_height > 0 ? buffer->dst_height : (int)round(src.height);
	if (width <= 0 || height <= 0 || g->from.width <= 0 || g->from.height <= 0)
		return;
	n = MAX(1, MIN(240, height / 4));
	for (k = 0; k < n; k++) {
		GenieStrip *s;
		struct wlr_fbox box = src;
		float y0 = (float)height * k / n, y1 = (float)height * (k + 1) / n;

		if (g->n == g->cap) {
			g->cap = g->cap ? g->cap * 2 : 128;
			g->strips = realloc(g->strips, g->cap * sizeof(*g->strips));
			if (!g->strips)
				die("genie: out of memory");
		}
		s = &g->strips[g->n++];
		box.y = src.y + src.height * k / n;
		box.height = src.height / n;
		s->scene = wlr_scene_buffer_create(g->tree, buffer->buffer);
		s->scene->point_accepts_input = closeaniminput;
		wlr_scene_buffer_set_source_box(s->scene, &box);
		wlr_scene_buffer_set_transform(s->scene, buffer->transform);
		wlr_scene_buffer_set_filter_mode(s->scene, WLR_SCALE_FILTER_BILINEAR);
		s->u0 = (float)(sx - g->from.x) / g->from.width;
		s->u1 = (float)(sx + width - g->from.x) / g->from.width;
		s->v0 = (sy + y0 - g->from.y) / g->from.height;
		s->v1 = (sy + y1 - g->from.y) / g->from.height;
		s->r0 = k == 0 ? buffer->corners.top_left : 0;
		s->r1 = k == 0 ? buffer->corners.top_right : 0;
		s->r2 = k == n - 1 ? buffer->corners.bottom_right : 0;
		s->r3 = k == n - 1 ? buffer->corners.bottom_left : 0;
	}
}

/* the icon the dock drew for this app, from the file the shell keeps:
 * one "app-id x y width height" line per icon, in layout coordinates */
static int
geniedockicon(Client *c, struct wlr_box *box)
{
	char path[600], line[512], id[256];
	const char *app = macappid(c);
	struct wlr_box b;
	int found = 0;
	FILE *f;

	if (!statefile(path, sizeof path, "dock") || !(f = fopen(path, "r")))
		return 0;
	while (!found && fgets(line, sizeof line, f))
		if (sscanf(line, "%255s %d %d %d %d", id, &b.x, &b.y, &b.width, &b.height) == 5
				&& !strcasecmp(id, app) && b.width > 0 && b.height > 0)
			found = 1;
	fclose(f);
	if (found)
		*box = b;
	return found;
}

static void
genietarget(Client *c, Monitor *m, struct wlr_box *to)
{
	if (geniedockicon(c, to))
		return;
	/* no icon known: the middle of the dock */
	*to = (struct wlr_box){m->m.x + m->m.width / 2 - 24,
			m->m.y + m->m.height - 64, 48, 48};
}

static void
geniedestroy(Genie *g)
{
	wl_list_remove(&g->link);
	if (g->c) {
		g->c->genie = 0;
		if (g->c->mon)
			arrange(g->c->mon);
	}
	wlr_scene_node_destroy(&g->tree->node);
	free(g->strips);
	free(g);
}

static void
geniedraw(Genie *g)
{
	float p = g->reverse ? 1.0f - g->t : g->t;
	float eb = genieease(p / 0.55f), et = genieease((p - 0.25f) / 0.75f);
	float pin = genieease(p / 0.45f);
	float top = g->from.y + (g->to.y - g->from.y) * et;
	float bot = g->from.y + g->from.height
			+ (g->to.y + g->to.height - g->from.y - g->from.height) * eb;
	float span = MAX(1.0f, (float)(g->to.y + g->to.height) - top);
	float alpha = 1.0f - geniestep(0.82f, 1.0f, p);
	int i;

	if (bot < top + 1.0f)
		bot = top + 1.0f;
	for (i = 0; i < g->n; i++) {
		GenieStrip *s = &g->strips[i];
		float y0 = top + (bot - top) * s->v0, y1 = top + (bot - top) * s->v1;
		float k = geniestep(0.0f, 1.0f, ((y0 + y1) / 2.0f - top) / span);
		float q = MIN(1.0f, pin * k + et);
		float l = g->from.x + (g->to.x - g->from.x) * q;
		float r = g->from.x + g->from.width
				+ (g->to.x + g->to.width - g->from.x - g->from.width) * q;
		float x0 = l + (r - l) * s->u0, x1 = l + (r - l) * s->u1;
		float sc = (x1 - x0) / MAX(1.0f, (s->u1 - s->u0) * g->from.width);
		int x = (int)floorf(x0), y = (int)floorf(y0);
		int w = MAX(1, (int)ceilf(x1) - x), h = MAX(1, (int)ceilf(y1) - y);

		wlr_scene_node_set_position(&s->scene->node, x, y);
		wlr_scene_buffer_set_dest_size(s->scene, w, h);
		wlr_scene_buffer_set_opacity(s->scene, alpha);
		wlr_scene_buffer_set_corner_radii(s->scene, corner_radii_new(
				(int)roundf(s->r0 * sc), (int)roundf(s->r1 * sc),
				(int)roundf(s->r2 * sc), (int)roundf(s->r3 * sc)));
	}
}

static int
geniestart(Client *c, int reverse)
{
	Monitor *m = c->mon;
	Genie *g;

	if (!mac_genie || !animations || !m || !c->scene || overview_visible
			|| !overviewvalid(m) || c->ws != m->ws)
		return 0;
	g = ecalloc(1, sizeof(*g));
	g->tree = wlr_scene_tree_create(layers[LyrFloat]);
	wlr_scene_node_raise_to_top(&g->tree->node);
	g->mon = m;
	g->reverse = reverse;
	g->from = c->geom;
	genietarget(c, m, &g->to);
	clientforeachbuffer(c, geniestrips, g);
	if (!g->n) {
		wlr_scene_node_destroy(&g->tree->node);
		free(g);
		return 0;
	}
	if (reverse) {
		g->c = c;
		c->genie = 1;
	}
	wl_list_insert(&genies, &g->link);
	geniedraw(g);
	animkick(m);
	return 1;
}

static int
genieadvance(Monitor *m, float dt)
{
	Genie *g, *tmp;
	int pending = 0;

	wl_list_for_each_safe(g, tmp, &genies, link) {
		if (g->mon != m)
			continue;
		g->t += dt / (float)MAX(60, mac_genie_duration);
		if (g->t >= 1.0f) {
			geniedestroy(g);
			continue;
		}
		geniedraw(g);
		pending = 1;
	}
	return pending;
}

/* a window going away mid-restore, or a monitor: nothing may point at it */
static void
genieclear(Monitor *m, Client *c)
{
	Genie *g, *tmp;

	wl_list_for_each_safe(g, tmp, &genies, link) {
		if ((m && g->mon == m) || (c && g->c == c) || (!m && !c)) {
			if (c && g->c == c)
				g->c = NULL;
			geniedestroy(g);
		}
	}
}
