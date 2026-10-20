/* See LICENSE file for copyright and license details. */

/* appearance */
static const unsigned int borderpx = 1;
static const unsigned int snap = 32;
static const int showbar = 0;
static const int topbar = 0;
static const char *fonts[] = {
	"Hack Nerd Font Mono:size=11",
	"Noto Color Emoji:size=11"
};
static const char dmenufont[] = "Hack Nerd Font Mono:size=11";
static const char col_gray1[] = "#2d2d2d";
static const char col_gray2[] = "#999999";
static const char col_gray3[] = "#cccccc";
static const char col_gray4[] = "#ffffff";
static const char col_blue[] = "#6699cc";
static const char *colors[][3] = {
	/*               fg         bg         border   */
	[SchemeNorm] = { col_gray3, col_gray1, col_gray2 },
	[SchemeSel]  = { col_gray4, col_gray1, col_blue  },
};

/* tagging */
static const char *tags[] = { "1" };
static const Rule rules[] = {
	/* class  instance  title  tags mask  isfloating  monitor */
	{ NULL,   NULL,     NULL,  0,         1,          -1 },
};

/* layouts: floating is the default; placement is per window */
static const float mfact = 0.55;
static const int nmaster = 1;
static const int resizehints = 1;
static const int lockfullscreen = 0;
static const int refreshrate = 120;
static const Layout layouts[] = {
	/* symbol  arrange function */
	{ "><>",   NULL },
	{ "[]=",   tile },
	{ "[M]",   monocle },
};

/* commands */
#define MODKEY Mod4Mask
static char dmenumon[8];
static const char *dmenucmd[] = {
	"dmenu_run", "-m", dmenumon, "-fn", dmenufont, NULL
};
static const char *termcmd[] = { "jumpapp", "st", NULL };
static const char *browsercmd[] = { "jumpapp", "brave", NULL };
static const char *chromiumcmd[] = { "jumpapp", "chromium", NULL };
static const char *filescmd[] = { "jumpapp", "dolphin", NULL };
static const char *musiccmd[] = { "jumpapp", "spotify", NULL };
static const char *clipboardcmd[] = { "copyq", "show", NULL };

static const Key keys[] = {
	/* modifier          key           function          argument */
	{ MODKEY,            XK_space,     spawn,            {.v = dmenucmd } },
	{ MODKEY,            XK_Return,    spawn,            {.v = termcmd } },
	{ MODKEY,            XK_w,         spawn,            {.v = browsercmd } },
	{ MODKEY|ShiftMask,  XK_w,         spawn,            {.v = chromiumcmd } },
	{ MODKEY,            XK_e,         spawn,            {.v = filescmd } },
	{ MODKEY|ShiftMask,  XK_m,         spawn,            {.v = musiccmd } },
	{ MODKEY|ShiftMask,  XK_c,         spawn,            {.v = clipboardcmd } },
	{ MODKEY,            XK_Left,      snapclient,       {.ui = PlaceLeft } },
	{ MODKEY,            XK_Right,     snapclient,       {.ui = PlaceRight } },
	{ MODKEY,            XK_Up,        snapclient,       {.ui = PlaceTop } },
	{ MODKEY,            XK_Down,      snapclient,       {.ui = PlaceBottom } },
	{ MODKEY,            XK_m,         togglemaximize,   {0} },
	{ MODKEY,            XK_f,         togglefullscreen, {0} },
	{ MODKEY,            XK_ntilde,    centerclient,     {0} },
	{ MODKEY|ShiftMask,  XK_Left,      tagmon,           {.i = -1 } },
	{ MODKEY|ShiftMask,  XK_Right,     tagmon,           {.i = +1 } },
	{ Mod1Mask,          XK_Tab,       focusstack,       {.i = +1 } },
	{ Mod1Mask|ShiftMask,XK_Tab,       focusstack,       {.i = -1 } },
	{ ControlMask,       XK_q,         killclient,       {0} },
	{ Mod1Mask,          XK_F4,        killclient,       {0} },
};

static const Button buttons[] = {
	/* click         event mask  button   function     argument */
	{ ClkClientWin,  MODKEY,     Button1, movemouse,   {0} },
	{ ClkClientWin,  MODKEY,     Button3, resizemouse, {0} },
};
