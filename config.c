#include "shod.h"

struct Config config = {
	/*
	 * except for the foreground, colors fields are strings
	 * containing three elements delimited by colon:
	 * the body color, the color of the light 3D shadow,
	 * and the color of the dark 3D shadow.
	 */

	/* 0-or-1 flags */
	.floatdialog    = 0,            /* set to 1 to use floating dialog windows */
	.sloppyfocus    = 0,            /* set to 1 to use sloppy focus */
	.honorconfig    = 0,            /* set to 1 to honor configure requests */

	/* general configuration */
	.modifier       = Mod1Mask,     /* Modifier button */
	.snap           = 8,            /* proximity of container edges to perform snap attraction */
	.font = "monospace:pixelsize=11", /* font for titles in titlebars */
	.ndesktops      = 10,           /* number of desktops per monitor */

	/* dock configuration */
	.dockwidth      = 64,           /* width of the dock (or its height, if it is horizontal) */
	.dockspace      = 64,           /* size of each dockapp (64 for windowmaker dockapps) */
	.dockgravity    = "E",          /* placement of the dock */
	.dockcolors     = {"#121212", "#2E3436"},

	/* notification configuration */
	.notifgap       = 3,            /* gap, in pixels, between notifications */
	.notifgravity   = "NE",         /* placement of notifications */

	/* title bar */
	.titlewidth = 17,
	.foreground = "#FFFFFF",

	/* border */
	.borderwidth = 6,
	.bordercolors = {
		[FOCUSED]   = {"#3465A4", "#729FCF", "#204A87"},
		[UNFOCUSED] = {"#555753", "#888A85", "#2E3436"},
		[URGENT]    = {"#CC0000", "#EF2929", "#A40000"},
	},

	/* size of 3D shadow effect, must be less than borderwidth */
	.shadowthickness = 2,

	/* the following are hardcoded rules; use X Resources to set rules without recompiling */
	.rules = (struct Rule []){
		/* class        instance      role                    type              state bitmask */ 

		/* Although Firefox's PictureInPicture is technically a utility (sub)window, make it a normal one */
		{ NULL,         NULL,         "PictureInPicture",     TYPE_NORMAL,      ABOVE },

		/* Last rule must be all NULL! */
		{ NULL,         NULL,         NULL,                   TYPE_NORMAL,      0 },
	}
};
