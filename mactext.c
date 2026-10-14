/* text and app icons drawn by the compositor: Mission Control labels, the
 * Cmd+Tab switcher */
#include <cairo.h>
#include <pango/pangocairo.h>
#include <librsvg/rsvg.h>
#include <drm_fourcc.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <dirent.h>
#include <ctype.h>

static int
maccasecontains(const char *hay, const char *needle)
{
	size_t n = strlen(needle);

	for (; *hay; hay++)
		if (!strncasecmp(hay, needle, n))
			return 1;
	return 0;
}

typedef struct {
	struct wlr_buffer base;
	cairo_surface_t *surf;
} MacBuf;

static void
macbufdestroy(struct wlr_buffer *b)
{
	MacBuf *mb = wl_container_of(b, mb, base);

	cairo_surface_destroy(mb->surf);
	free(mb);
}

static bool
macbufbegin(struct wlr_buffer *b, uint32_t flags, void **data, uint32_t *format,
		size_t *stride)
{
	MacBuf *mb = wl_container_of(b, mb, base);

	if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
		return false;
	*data = cairo_image_surface_get_data(mb->surf);
	*format = DRM_FORMAT_ARGB8888;
	*stride = (size_t)cairo_image_surface_get_stride(mb->surf);
	return true;
}

static void
macbufend(struct wlr_buffer *b)
{
}

static const struct wlr_buffer_impl macbuf_impl = {
	.destroy = macbufdestroy,
	.begin_data_ptr_access = macbufbegin,
	.end_data_ptr_access = macbufend,
};

static struct wlr_buffer *
macbufwrap(cairo_surface_t *s)
{
	MacBuf *mb = ecalloc(1, sizeof *mb);

	cairo_surface_flush(s);
	mb->surf = s;
	wlr_buffer_init(&mb->base, &macbuf_impl, cairo_image_surface_get_width(s),
			cairo_image_surface_get_height(s));
	return &mb->base;
}

/* a small cache, Mission Control redraws every frame while it moves */
#define MACCACHE 96
static struct {
	char key[256];
	struct wlr_buffer *buf;
	int w, h;
	uint32_t used;
} maccache[MACCACHE];
static uint32_t maccacheclock;

static struct wlr_buffer *
maccacheget(const char *key, int *w, int *h)
{
	int i;

	for (i = 0; i < MACCACHE; i++) {
		if (maccache[i].buf && !strcmp(maccache[i].key, key)) {
			maccache[i].used = ++maccacheclock;
			*w = maccache[i].w;
			*h = maccache[i].h;
			return maccache[i].buf;
		}
	}
	return NULL;
}

static void
maccacheput(const char *key, struct wlr_buffer *b, int w, int h)
{
	int i, old = 0;

	for (i = 0; i < MACCACHE; i++) {
		if (!maccache[i].buf) {
			old = i;
			break;
		}
		if (maccache[i].used < maccache[old].used)
			old = i;
	}
	if (maccache[old].buf)
		wlr_buffer_drop(maccache[old].buf);
	snprintf(maccache[old].key, sizeof maccache[old].key, "%s", key);
	maccache[old].buf = b;
	maccache[old].w = w;
	maccache[old].h = h;
	maccache[old].used = ++maccacheclock;
}

/* text in the system font, white, in a dark pill when pill is set; px is
 * the size in logical pixels and the buffer is drawn at the output scale */
static struct wlr_scene_buffer *
mactext(struct wlr_scene_tree *tree, const char *text, int px, int bold,
		int maxw, int pill, float scale, int *outw, int *outh)
{
	char key[256];
	struct wlr_buffer *b;
	struct wlr_scene_buffer *sb;
	cairo_surface_t *s;
	cairo_t *cr;
	PangoLayout *pl;
	PangoFontDescription *fd;
	int w, h, tw, th, padx, pady;
	char font[160];

	*outw = *outh = 0;
	if (!text || !*text)
		return NULL;
	scale = scale > 0.0f ? scale : 1.0f;
	snprintf(key, sizeof key, "t%d|%d|%d|%d|%.2f|%s", px, bold, maxw, pill, scale, text);
	if (!(b = maccacheget(key, &w, &h))) {
		padx = pill ? (int)lroundf(px * 0.75f * scale) : 0;
		pady = pill ? (int)lroundf(px * 0.3f * scale) : 0;
		s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
		cr = cairo_create(s);
		pl = pango_cairo_create_layout(cr);
		snprintf(font, sizeof font, "%s %s %dpx", mac_font, bold ? "Bold" : "",
				(int)lroundf(px * scale));
		fd = pango_font_description_from_string(font);
		pango_layout_set_font_description(pl, fd);
		pango_font_description_free(fd);
		pango_layout_set_text(pl, text, -1);
		pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_END);
		if (maxw > 0)
			pango_layout_set_width(pl, (int)(maxw * scale - 2 * padx) * PANGO_SCALE);
		pango_layout_get_pixel_size(pl, &tw, &th);
		cairo_destroy(cr);
		cairo_surface_destroy(s);
		w = tw + 2 * padx;
		h = th + 2 * pady;
		s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, MAX(1, w), MAX(1, h));
		cr = cairo_create(s);
		if (pill) {
			double r = h / 2.0;
			cairo_new_sub_path(cr);
			cairo_arc(cr, w - r, r, r, -G_PI / 2, G_PI / 2);
			cairo_arc(cr, r, r, r, G_PI / 2, 3 * G_PI / 2);
			cairo_close_path(cr);
			cairo_set_source_rgba(cr, 0.12, 0.12, 0.13, 0.82);
			cairo_fill(cr);
		}
		cairo_move_to(cr, padx, pady);
		pango_cairo_update_layout(cr, pl);
		if (!pill) {
			/* a soft shadow keeps white text readable on any wallpaper */
			cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
			cairo_move_to(cr, padx, pady + scale);
			pango_cairo_show_layout(cr, pl);
			cairo_move_to(cr, padx, pady);
		}
		cairo_set_source_rgba(cr, 1, 1, 1, 0.96);
		pango_cairo_show_layout(cr, pl);
		g_object_unref(pl);
		cairo_destroy(cr);
		b = macbufwrap(s);
		maccacheput(key, b, w, h);
	}
	*outw = (int)lroundf(w / scale);
	*outh = (int)lroundf(h / scale);
	if (!tree)
		return NULL;
	sb = wlr_scene_buffer_create(tree, b);
	wlr_scene_buffer_set_dest_size(sb, *outw, *outh);
	return sb;
}

/* app icon lookup: appid -> desktop entry -> Icon= -> file in the theme */
static int
macdesktopicon(const char *path, const char *appid, char *icon, size_t n)
{
	FILE *f = fopen(path, "r");
	char line[512], *v;
	int match = !appid, indesk = 0;

	if (!f)
		return 0;
	icon[0] = '\0';
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = '\0';
		if (line[0] == '[') {
			indesk = !strcmp(line, "[Desktop Entry]");
			continue;
		}
		if (!indesk || !(v = strchr(line, '=')))
			continue;
		*v++ = '\0';
		if (!strcmp(line, "Icon"))
			snprintf(icon, n, "%s", v);
		else if (appid && !strcmp(line, "StartupWMClass") && !strcasecmp(v, appid))
			match = 1;
	}
	fclose(f);
	return match && icon[0];
}

static char macentrypath[1300];

static int
macfindentry(const char *appid, char *icon, size_t n)
{
	const char *dirs = getenv("XDG_DATA_DIRS"), *home = getenv("XDG_DATA_HOME");
	char all[2048], *d, *save = NULL, path[1024], low[128];
	struct dirent *e;
	DIR *dir;
	size_t i;

	snprintf(low, sizeof low, "%s", appid);
	for (i = 0; low[i]; i++)
		low[i] = (char)tolower((unsigned char)low[i]);
	if (home)
		snprintf(all, sizeof all, "%s:%s", home,
				dirs && *dirs ? dirs : "/usr/local/share:/usr/share");
	else
		snprintf(all, sizeof all, "%s/.local/share:%s", getenv("HOME") ? getenv("HOME") : "",
				dirs && *dirs ? dirs : "/usr/local/share:/usr/share");
	/* exact file names first, then any entry naming the class */
	for (d = strtok_r(all, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
		snprintf(path, sizeof path, "%s/applications/%s.desktop", d, appid);
		if (macdesktopicon(path, NULL, icon, n))
			return snprintf(macentrypath, sizeof macentrypath, "%s", path), 1;
		snprintf(path, sizeof path, "%s/applications/%s.desktop", d, low);
		if (macdesktopicon(path, NULL, icon, n))
			return snprintf(macentrypath, sizeof macentrypath, "%s", path), 1;
		if (!(dir = opendir((snprintf(path, sizeof path, "%s/applications", d), path))))
			continue;
		while ((e = readdir(dir))) {
			char full[1300];
			const char *dot = strrchr(e->d_name, '.');
			if (!dot || strcmp(dot, ".desktop"))
				continue;
			snprintf(full, sizeof full, "%s/%s", path, e->d_name);
			/* org.foo.App.desktop for appid App */
			if ((maccasecontains(e->d_name, appid) && macdesktopicon(full, NULL, icon, n))
					|| macdesktopicon(full, appid, icon, n)) {
				closedir(dir);
				snprintf(macentrypath, sizeof macentrypath, "%s", full);
				return 1;
			}
		}
		closedir(dir);
	}
	return 0;
}

static int
macfindiconfile(const char *name, char *out, size_t n)
{
	static const char *sizes[] = {"512x512", "256x256", "scalable", "192x192",
			"128x128", "96x96", "64x64", "48x48"};
	const char *themes[] = {mac_icon_theme, "hicolor", "Adwaita"};
	const char *bases[] = {"/usr/share/icons", "/usr/local/share/icons", NULL};
	char path[1024], homeicons[512];
	size_t t, s, b, x;

	if (name[0] == '/') {
		snprintf(out, n, "%s", name);
		return access(out, R_OK) == 0;
	}
	snprintf(homeicons, sizeof homeicons, "%s/.local/share/icons",
			getenv("HOME") ? getenv("HOME") : "");
	bases[2] = homeicons;
	for (t = 0; t < LENGTH(themes); t++) {
		if (!themes[t][0])
			continue;
		for (b = 0; b < LENGTH(bases); b++) {
			for (s = 0; s < LENGTH(sizes); s++) {
				for (x = 0; x < 2; x++) {
					snprintf(path, sizeof path, "%s/%s/%s/apps/%s.%s", bases[b],
							themes[t], sizes[s], name, x ? "svg" : "png");
					if (!access(path, R_OK)) {
						snprintf(out, n, "%s", path);
						return 1;
					}
				}
			}
		}
	}
	for (x = 0; x < 2; x++) {
		snprintf(path, sizeof path, "/usr/share/pixmaps/%s.%s", name, x ? "svg" : "png");
		if (!access(path, R_OK)) {
			snprintf(out, n, "%s", path);
			return 1;
		}
	}
	return 0;
}

static cairo_surface_t *
macloadicon(const char *file, int px)
{
	cairo_surface_t *s = NULL, *png;
	cairo_t *cr;
	const char *dot = strrchr(file, '.');

	if (dot && !strcmp(dot, ".svg")) {
		RsvgHandle *h = rsvg_handle_new_from_file(file, NULL);
		RsvgRectangle vp = {0, 0, px, px};
		if (!h)
			return NULL;
		s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
		cr = cairo_create(s);
		rsvg_handle_render_document(h, cr, &vp, NULL);
		cairo_destroy(cr);
		g_object_unref(h);
		return s;
	}
	png = cairo_image_surface_create_from_png(file);
	if (cairo_surface_status(png) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(png);
		return NULL;
	}
	s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, px, px);
	cr = cairo_create(s);
	cairo_scale(cr, (double)px / cairo_image_surface_get_width(png),
			(double)px / cairo_image_surface_get_height(png));
	cairo_set_source_surface(cr, png, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(png);
	return s;
}

/* the app's icon at size px (logical), or NULL when none is found */
static struct wlr_scene_buffer *
macicon(struct wlr_scene_tree *tree, const char *appid, int px, float scale)
{
	char key[256], name[256], file[1024];
	struct wlr_buffer *b;
	struct wlr_scene_buffer *sb;
	cairo_surface_t *s = NULL;
	int w, h, dev;

	if (!appid || !*appid)
		return NULL;
	scale = scale > 0.0f ? scale : 1.0f;
	dev = (int)lroundf(px * scale);
	snprintf(key, sizeof key, "i%d|%s", dev, appid);
	if (!(b = maccacheget(key, &w, &h))) {
		if ((macfindentry(appid, name, sizeof name) && macfindiconfile(name, file, sizeof file))
				|| macfindiconfile(appid, file, sizeof file)
				|| macfindiconfile("application-x-executable", file, sizeof file))
			s = macloadicon(file, dev);
		if (!s)
			return NULL;
		b = macbufwrap(s);
		w = h = dev;
		maccacheput(key, b, w, h);
	}
	sb = wlr_scene_buffer_create(tree, b);
	wlr_scene_buffer_set_dest_size(sb, px, px);
	return sb;
}

/* the name an app goes by: Name= of its desktop entry, else its id */
static const char *
macappname(const char *appid)
{
	static struct { char id[128], name[128]; } cache[32];
	static int next;
	char icon[256], line[512], *v;
	const char *dot;
	FILE *f;
	int i;

	for (i = 0; i < (int)LENGTH(cache); i++)
		if (cache[i].id[0] && !strcmp(cache[i].id, appid))
			return cache[i].name;
	i = next++ % (int)LENGTH(cache);
	snprintf(cache[i].id, sizeof cache[i].id, "%s", appid);
	dot = strrchr(appid, '.');
	snprintf(cache[i].name, sizeof cache[i].name, "%s", dot && dot[1] ? dot + 1 : appid);
	cache[i].name[0] = (char)toupper((unsigned char)cache[i].name[0]);
	if (macfindentry(appid, icon, sizeof icon) && (f = fopen(macentrypath, "r"))) {
		int indesk = 0;
		while (fgets(line, sizeof line, f)) {
			line[strcspn(line, "\n")] = '\0';
			if (line[0] == '[')
				indesk = !strcmp(line, "[Desktop Entry]");
			else if (indesk && (v = strchr(line, '=')) && !strncmp(line, "Name=", 5)) {
				snprintf(cache[i].name, sizeof cache[i].name, "%s", v + 1);
				break;
			}
		}
		fclose(f);
	}
	return cache[i].name;
}
