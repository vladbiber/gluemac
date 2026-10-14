/* Taken from https://github.com/djpohly/dwl/issues/466 */
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }
/* appearance - the non-const values can be overridden at runtime from
 * ~/.config/gluemac/config.conf */
static const int sloppyfocus               = 1;  /* focus follows mouse */
static int warpcursor                      = 1;  /* warp cursor to focused window on keyboard focus, like nvwm */
static const int bypass_surface_visibility = 0;  /* 1 means idle inhibitors will disable idle tracking even if it's surface isn't visible  */
static unsigned int borderpx               = 0;  /* border pixel of windows */
static int gappx                           = 8;  /* gap around tiled windows */
static float rootcolor[]                   = COLOR(0x000000ff);
static float bordercolor[]                 = COLOR(0x2f3549ff);
static float focuscolor[]                  = COLOR(0x7aa2f7ff);
static float normalmodecolor[]             = COLOR(0xe0af68ff); /* focused border while in normal mode */
static int unfocused_borders                = 0;  /* 1 keeps the dim border on unfocused windows */
static int corner_radius                   = 10;  /* rounded corner radius, 0 disables */
static int blurenabled                     = 0;  /* backdrop blur behind transparent windows */
static int blur_passes                     = 3;
static int blur_radius                     = 5;
static float win_opacity                   = 1.0f; /* window opacity when transparency is on */
static int opacityenabled                  = 1;  /* toggled at runtime with wm:toggle_opacity */
static int animations                      = 1;  /* position animations (open/retile/workspace) */
static int animation_duration              = 200; /* ms, retile and workspace moves */
/* how a window appears and disappears: AnimZoom pops it out of its own centre,
 * AnimSlide moves it in from below, AnimFade only fades, AnimNone is instant */
static int animation_type_open             = AnimZoom;
static int animation_type_close            = AnimZoom;
static int animation_duration_open         = 300; /* ms */
static int animation_duration_close        = 250; /* ms */
static float zoom_initial_ratio            = 0.72f; /* size an opening window starts at */
static float zoom_end_ratio                = 0.76f; /* size a closing window ends at */
/* cubic-bezier control points, as in CSS: x1, y1, x2, y2 */
static float animation_curve_open[4]       = {0.16f, 1.0f, 0.3f, 1.0f};  /* ease out, snappy */
static float animation_curve_close[4]      = {0.42f, 0.0f, 0.6f, 1.0f};  /* ease in out */
static float scroll_colfrac                = 0.5f; /* default column width in the niri-style scroll layout */
static int default_layout                  = LtBSP; /* layout new monitors start in: LtBSP, LtScroll or LtDrift */
static int remember_layout                 = 0;     /* reopen in the layout the last session ended in */
static int start_in_overview               = 0;     /* open the session on the overview */
/* drift layout - driftwm-style infinite canvas */
static int drift_snap                      = 24;    /* edge snapping distance, in canvas pixels */
static int drift_nudge                     = 20;    /* pixels a window moves per keyboard nudge */
static float drift_zoom_min                = 0.2f;  /* how far the camera can zoom out */
static float drift_zoom_max                = 3.0f;  /* how far the camera can zoom in */
static float drift_zoom_step               = 1.12f; /* zoom factor per key press or wheel notch */
static float drift_pan_speed               = 1.0f;  /* multiplier for touchpad and scroll panning */
static int swipe_distance                  = 300;   /* touchpad travel for a whole workspace, 0 = step at once */
/* This conforms to the xdg-protocol. Set the alpha to zero to restore the old behavior */
static const float fullscreen_bg[]         = {0.0f, 0.0f, 0.0f, 1.0f}; /* You can also use glsl colors */

/* logging */
static int log_level = WLR_ERROR;

static const Rule rules[] = {
	/* app_id     title       ws (1-9, 0 = current)   isfloating   monitor */
	{ NULL,       NULL,       0,                       0,           -1 },
};

/* monitors */
/* (x=-1, y=-1) is reserved as an "autoconfigure" monitor position indicator
 * WARNING: negative values other than (-1, -1) cause problems with Xwayland clients
 * https://gitlab.freedesktop.org/xorg/xserver/-/issues/899 */
static const MonitorRule monrules[] = {
	/* name       scale rotate/reflect                x    y */
	/* example of a HiDPI laptop monitor:
	{ "eDP-1",    2,    WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 }, */
	/* defaults */
	{ NULL,       1,    WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 },
};

/* keyboard */
static struct xkb_rule_names xkb_rules = {
	/* can specify fields: rules, model, layout, variant, options */
	/* example:
	.options = "ctrl:nocaps",
	*/
	.options = NULL,
};

static int repeat_rate = 25;
static int repeat_delay = 600;

/* Trackpad */
static int tap_to_click = 1;
static const int tap_and_drag = 1;
static const int drag_lock = 1;
static int natural_scrolling = 1;
static int three_finger_drag = 0;
static const int disable_while_typing = 1;
static const int left_handed = 0;
static const int middle_button_emulation = 0;
/* You can choose between:
LIBINPUT_CONFIG_SCROLL_NO_SCROLL
LIBINPUT_CONFIG_SCROLL_2FG
LIBINPUT_CONFIG_SCROLL_EDGE
LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN
*/
static const enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;

/* You can choose between:
LIBINPUT_CONFIG_CLICK_METHOD_NONE
LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS
LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER
*/
static enum libinput_config_click_method click_method = LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER;

/* You can choose between:
LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE
*/
static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

/* You can choose between:
LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
*/
static const enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static double accel_speed = 0.0;

/* You can choose between:
LIBINPUT_CONFIG_TAP_MAP_LRM -- 1/2/3 finger tap maps to left/right/middle
LIBINPUT_CONFIG_TAP_MAP_LMR -- 1/2/3 finger tap maps to left/middle/right
*/
static const enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

/* mod = Super, like nvwm */
#define MODKEY WLR_MODIFIER_LOGO

#define WSKEYS(KEY,WS) \
	{ MODKEY,                    KEY, viewws,         {.ui = (WS)} }, \
	{ MODKEY|WLR_MODIFIER_CTRL,  KEY, movetowsfollow, {.ui = (WS)} }

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* Volume is driven by whichever helper is installed: wpctl comes with
 * WirePlumber and is there on any PipeWire system, pactl with the PulseAudio
 * tools. The backlight goes through gluemac-backlight, which wraps
 * brightnessctl or light and steps 5% down to 10%, then 1% down to 0. */
#define VOLCMD(wp, pa) SHCMD("if command -v wpctl >/dev/null 2>&1; then " \
	"wpctl " wp "; else pactl " pa "; fi")
#define VOLUP   VOLCMD("set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 5%+", "set-sink-volume @DEFAULT_SINK@ +5%")
#define VOLDOWN VOLCMD("set-volume @DEFAULT_AUDIO_SINK@ 5%-", "set-sink-volume @DEFAULT_SINK@ -5%")
#define VOLMUTE VOLCMD("set-mute @DEFAULT_AUDIO_SINK@ toggle", "set-sink-mute @DEFAULT_SINK@ toggle")
#define MICMUTE VOLCMD("set-mute @DEFAULT_AUDIO_SOURCE@ toggle", "set-source-mute @DEFAULT_SOURCE@ toggle")
#define BRTUP   SHCMD("gluemac-backlight up")
#define BRTDOWN SHCMD("gluemac-backlight down")

/* Screenshots. grim takes the picture, slurp draws the crop rectangle and
 * wl-copy puts it on the clipboard. slurp exits non-zero when the selection
 * is cancelled with Escape, which is what stops the shot from being taken. */
#define SHOTDIR "${XDG_PICTURES_DIR:-$HOME/Pictures}/Screenshots"
#define SHOTNAME SHOTDIR "/gluemac-$(date +%Y%m%d-%H%M%S).png"
#define SHOTFULL SHCMD("mkdir -p " SHOTDIR " && grim " SHOTNAME)
#define SHOTAREA SHCMD("box=$(slurp) || exit 0; mkdir -p " SHOTDIR \
	" && grim -g \"$box\" " SHOTNAME)
#define SHOTFULLCLIP SHCMD("grim - | wl-copy")
#define SHOTAREACLIP SHCMD("box=$(slurp) || exit 0; grim -g \"$box\" - | wl-copy")

/* commands */
static const char *termcmd[] = { "alacritty", NULL };
static const char *menucmd[] = { "rofi", "-show", "drun", NULL };

/* Cmd is Super. Apps get Cmd as Ctrl, so Cmd+C, Cmd+V, Cmd+W, Cmd+T and the
 * rest are theirs; only the keys below belong to the desktop, as on a Mac. */
#define CMD  MODKEY
#define CTRL WLR_MODIFIER_CTRL
#define OPT  WLR_MODIFIER_ALT
#define SHIFT WLR_MODIFIER_SHIFT
#define SPACEKEYS(KEY,WS) \
	{ CTRL,     KEY, viewws,         {.ui = (WS)} }, \
	{ CTRL|CMD, KEY, movetowsfollow, {.ui = (WS)} }
#define LOCKCMD SHCMD("command -v hyprlock >/dev/null && exec hyprlock; exec swaylock -f")
static const Key keys[] = {
	/* modifier         key                    function           argument */
	{ CMD,              XKB_KEY_space,         spawn,             {.v = menucmd} },
	{ CMD,              XKB_KEY_Tab,           macswitch,         {.i = +1} },
	{ CMD|SHIFT,        XKB_KEY_ISO_Left_Tab,  macswitch,         {.i = -1} },
	{ CMD|SHIFT,        XKB_KEY_Tab,           macswitch,         {.i = -1} },
	{ CMD,              XKB_KEY_grave,         macappnext,        {0} },
	{ CMD,              XKB_KEY_q,             macquitapp,        {0} },
	{ CMD,              XKB_KEY_h,             machideapp,        {.i = 0} },
	{ CMD|OPT,          XKB_KEY_h,             machideapp,        {.i = 1} },
	{ CMD,              XKB_KEY_m,             macminimizeaction, {0} },
	{ CTRL|CMD,         XKB_KEY_f,             togglefullscreen,  {0} },
	{ CTRL|CMD,         XKB_KEY_q,             spawn,             LOCKCMD },
	{ CMD|SHIFT,        XKB_KEY_Q,             quit,              {0} },
	{ CTRL,             XKB_KEY_Left,          wsstep,            {.i = -1} },
	{ CTRL,             XKB_KEY_Right,         wsstep,            {.i = +1} },
	{ CTRL,             XKB_KEY_Up,            macmissioncontrol, {0} },
	{ CTRL,             XKB_KEY_Down,          macappexpose,      {0} },
	{ 0,                XKB_KEY_XF86LaunchA,   macmissioncontrol, {0} },
	{ 0,                XKB_KEY_F3,            macmissioncontrol, {0} },
	{ CMD,              XKB_KEY_F3,            macshowdesktop,    {0} },
	{ 0,                XKB_KEY_F11,           macshowdesktop,    {0} },
	{ 0,                XKB_KEY_XF86LaunchB,   spawn,             {.v = menucmd} },
	/* window tiling, Fn+Ctrl+arrows on a Mac */
	{ CTRL|OPT,         XKB_KEY_Left,          mactile,           {.i = TileLeft} },
	{ CTRL|OPT,         XKB_KEY_Right,         mactile,           {.i = TileRight} },
	{ CTRL|OPT,         XKB_KEY_Up,            mactile,           {.i = TileTop} },
	{ CTRL|OPT,         XKB_KEY_Down,          mactile,           {.i = TileBottom} },
	{ CTRL|OPT,         XKB_KEY_Return,        mactile,           {.i = TileFill} },
	{ CTRL|OPT,         XKB_KEY_f,             mactile,           {.i = TileFill} },
	{ CTRL|OPT,         XKB_KEY_c,             mactile,           {.i = TileCenter} },
	{ CTRL|OPT,         XKB_KEY_r,             mactile,           {.i = TileRestore} },
	{ CTRL|OPT,         XKB_KEY_u,             mactile,           {.i = TileTL} },
	{ CTRL|OPT,         XKB_KEY_i,             mactile,           {.i = TileTR} },
	{ CTRL|OPT,         XKB_KEY_j,             mactile,           {.i = TileBL} },
	{ CTRL|OPT,         XKB_KEY_k,             mactile,           {.i = TileBR} },
	/* screenshots: Cmd+Shift+3 the screen, 4 an area, add Ctrl to copy */
	{ CMD|SHIFT,        XKB_KEY_numbersign,    spawn,             SHOTFULL },
	{ CMD|SHIFT,        XKB_KEY_dollar,        spawn,             SHOTAREA },
	{ CMD|SHIFT,        XKB_KEY_percent,       spawn,             SHOTAREA },
	{ CMD|SHIFT|CTRL,   XKB_KEY_numbersign,    spawn,             SHOTFULLCLIP },
	{ CMD|SHIFT|CTRL,   XKB_KEY_dollar,        spawn,             SHOTAREACLIP },
	{ 0,                XKB_KEY_Print,         spawn,             SHOTFULL },
	{ SHIFT,            XKB_KEY_Print,         spawn,             SHOTAREA },
	{ CTRL,             XKB_KEY_Print,         spawn,             SHOTFULLCLIP },
	{ CTRL|OPT|CMD,     XKB_KEY_r,             reloadconfig,      {0} },
	{ CTRL|OPT|CMD,     XKB_KEY_t,             spawn,             {.v = termcmd} },
	{ 0, XKB_KEY_XF86AudioRaiseVolume,  spawn, VOLUP },
	{ 0, XKB_KEY_XF86AudioLowerVolume,  spawn, VOLDOWN },
	{ 0, XKB_KEY_XF86AudioMute,         spawn, VOLMUTE },
	{ 0, XKB_KEY_XF86AudioMicMute,      spawn, MICMUTE },
	{ 0, XKB_KEY_XF86AudioPlay,         spawn, SHCMD("playerctl play-pause") },
	{ 0, XKB_KEY_XF86AudioPause,        spawn, SHCMD("playerctl play-pause") },
	{ 0, XKB_KEY_XF86AudioStop,         spawn, SHCMD("playerctl stop") },
	{ 0, XKB_KEY_XF86AudioNext,         spawn, SHCMD("playerctl next") },
	{ 0, XKB_KEY_XF86AudioPrev,         spawn, SHCMD("playerctl previous") },
	{ 0, XKB_KEY_XF86MonBrightnessUp,   spawn, BRTUP },
	{ 0, XKB_KEY_XF86MonBrightnessDown, spawn, BRTDOWN },
	SPACEKEYS(XKB_KEY_1, 0),
	SPACEKEYS(XKB_KEY_2, 1),
	SPACEKEYS(XKB_KEY_3, 2),
	SPACEKEYS(XKB_KEY_4, 3),
	SPACEKEYS(XKB_KEY_5, 4),
	SPACEKEYS(XKB_KEY_6, 5),
	SPACEKEYS(XKB_KEY_7, 6),
	SPACEKEYS(XKB_KEY_8, 7),
	SPACEKEYS(XKB_KEY_9, 8),

	/* Ctrl-Alt-Backspace and Ctrl-Alt-Fx used to be handled by X server */
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, quit, {0} },
	/* Ctrl-Alt-Fx is used to switch to another VT, if you don't know what a VT is
	 * do not remove them.
	 */
#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_XF86Switch_VT_##n, chvt, {.ui = (n)} }
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};

/* normal-mode keys, matched by keysym alone (like nvwm's grabbed keyboard) */
static const Key normalkeys[] = {
	{ 0, XKB_KEY_i,     entermode,   {.i = ModeInsert} },
	{ 0, XKB_KEY_Tab,   focusnext,   {0} },
	{ 0, XKB_KEY_Left,  focusdir,    {.i = DirLeft} },
	{ 0, XKB_KEY_Right, focusdir,    {.i = DirRight} },
	{ 0, XKB_KEY_Up,    focusdir,    {.i = DirUp} },
	{ 0, XKB_KEY_Down,  focusdir,    {.i = DirDown} },
	{ 0, XKB_KEY_k,     focusdir,    {.i = DirUp} },
	{ 0, XKB_KEY_j,     focusdir,    {.i = DirDown} },
	{ 0, XKB_KEY_h,     setratio,    {.f = -0.05f} },
	{ 0, XKB_KEY_l,     setratio,    {.f = +0.05f} },
	{ 0, XKB_KEY_H,     swapstack,   {.i = -1} },
	{ 0, XKB_KEY_L,     swapstack,   {.i = +1} },
	{ 0, XKB_KEY_s,     togglesplit, {0} },
	/* the function row without Mod, since normal mode already owns the
	 * keyboard. F1..F3 are the exception: the player keys stay on Mod
	 * everywhere, and keys[] is matched first, so Mod+F1..F3 reach them
	 * here too. */
	{ 0, XKB_KEY_F4,    spawn, VOLMUTE },
	{ 0, XKB_KEY_F5,    spawn, VOLDOWN },
	{ 0, XKB_KEY_F6,    spawn, VOLUP },
	{ 0, XKB_KEY_F7,    spawn, BRTDOWN },
	{ 0, XKB_KEY_F8,    spawn, BRTUP },
};

/* Ctrl+Cmd drags a window from anywhere in it, right button resizes;
 * a plain Cmd+click stays the app's (it is Ctrl+click to it) */
static const Button buttons[] = {
	{ MODKEY|WLR_MODIFIER_CTRL, BTN_LEFT,  moveresize, {.ui = CurMove} },
	{ MODKEY|WLR_MODIFIER_CTRL, BTN_RIGHT, moveresize, {.ui = CurResize} },
};
