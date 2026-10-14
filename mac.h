/* macOS behaviour, declared ahead of config.h so the bindings can use it */

enum { MinNone, MinMinimized, MinHidden, MinDesktop };
enum { TileNone, TileLeft, TileRight, TileTop, TileBottom, TileFill,
	TileCenter, TileTL, TileTR, TileBL, TileBR, TileRestore };

static void macmissioncontrol(const Arg *arg);
static void macappexpose(const Arg *arg);
static void macshowdesktop(const Arg *arg);
static void macswitch(const Arg *arg);
static void macappnext(const Arg *arg);
static void machideapp(const Arg *arg);
static void macquitapp(const Arg *arg);
static void macminimizeaction(const Arg *arg);
static void maczoomaction(const Arg *arg);
static void macspaceadd(const Arg *arg);
static void macspacedel(const Arg *arg);
static void mactile(const Arg *arg);
