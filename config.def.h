/* See LICENSE file for copyright and license details. */
#include <X11/XF86keysym.h>

/* appearance */
static const unsigned int borderpx = 1;
static const unsigned int snap = 32;
static const int showbar = 0;
static const int topbar = 0;
static const char *fonts[] = {
	"Hack Nerd Font Mono:size=11",
	"Noto Color Emoji:size=11"
};
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
	"wm-launcher", "-m", dmenumon, NULL
};
static const char *termcmd[] = { "jumpapp", "alacritty", NULL };
static const char *browsercmd[] = { "jumpapp", "brave", NULL };
static const char *chromiumcmd[] = { "jumpapp", "chromium", NULL };
static const char *filescmd[] = { "jumpapp", "dolphin", NULL };
static const char *musiccmd[] = { "jumpapp", "spotify", NULL };
static const char *clipboardcmd[] = { "copyq", "show", NULL };
static const char *playcmd[] = { "playerctl", "play-pause", NULL };
static const char *pausecmd[] = { "playerctl", "pause", NULL };
static const char *stopcmd[] = { "playerctl", "stop", NULL };
static const char *prevcmd[] = { "playerctl", "previous", NULL };
static const char *nextcmd[] = { "playerctl", "next", NULL };
static const char *volupcmd[] = {
	"wpctl", "set-volume", "--limit", "1.0", "@DEFAULT_AUDIO_SINK@", "5%+", NULL
};
static const char *voldowncmd[] = {
	"wpctl", "set-volume", "--limit", "1.0", "@DEFAULT_AUDIO_SINK@", "5%-", NULL
};
static const char *mutecmd[] = {
	"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle", NULL
};
static const char *brightupcmd[] = {
	"brightnessctl", "--class=backlight", "--min-value=1", "set", "+5%", NULL
};
static const char *brightdowncmd[] = {
	"brightnessctl", "--class=backlight", "--min-value=1", "set", "5%-", NULL
};
static const char *capturecmd[] = { "flameshot", "gui", NULL };
static const char *lockcmd[] = { "wm-launcher", "--action", "Bloquear", NULL };

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
	{ MODKEY,            XK_Home,      spawn,            {.v = playcmd } },
	{ MODKEY,            XK_Prior,     spawn,            {.v = volupcmd } },
	{ MODKEY,            XK_Next,      spawn,            {.v = voldowncmd } },
	{ MODKEY,            XK_End,       spawn,            {.v = mutecmd } },
	{ MODKEY|ShiftMask,  XK_Prior,     spawn,            {.v = prevcmd } },
	{ MODKEY|ShiftMask,  XK_Next,      spawn,            {.v = nextcmd } },
	{ MODKEY,            XK_l,         spawn,            {.v = lockcmd } },
	{ 0, XF86XK_AudioPlay,            spawn,            {.v = playcmd } },
	{ 0, XF86XK_AudioPause,           spawn,            {.v = pausecmd } },
	{ 0, XF86XK_AudioStop,            spawn,            {.v = stopcmd } },
	{ 0, XF86XK_AudioPrev,            spawn,            {.v = prevcmd } },
	{ 0, XF86XK_AudioNext,            spawn,            {.v = nextcmd } },
	{ 0, XF86XK_AudioRaiseVolume,     spawn,            {.v = volupcmd } },
	{ 0, XF86XK_AudioLowerVolume,     spawn,            {.v = voldowncmd } },
	{ 0, XF86XK_AudioMute,            spawn,            {.v = mutecmd } },
	{ 0, XF86XK_MonBrightnessUp,      spawn,            {.v = brightupcmd } },
	{ 0, XF86XK_MonBrightnessDown,    spawn,            {.v = brightdowncmd } },
	{ 0, XK_Print,                   spawn,            {.v = capturecmd } },
};

static const Button buttons[] = {
	/* click         event mask  button   function     argument */
	{ ClkClientWin,  MODKEY,     Button1, movemouse,   {0} },
	{ ClkClientWin,  MODKEY,     Button3, resizemouse, {0} },
};
