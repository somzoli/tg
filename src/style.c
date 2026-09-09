/*
    tg
    Copyright (C) 2015 Marcello Mamino

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

/* Look and feel of the application: a dark, flat theme matching the
   colours used by the cairo drawing areas (see initialize_palette). */

#include "tg.h"

static const char tg_css[] =
"@define-color tg_bg       #15171d;"
"@define-color tg_panel    #1c1f27;"
"@define-color tg_raised   #232733;"
"@define-color tg_border   #2c313d;"
"@define-color tg_fg       #e7eaf0;"
"@define-color tg_dim      #99a2b3;"
"@define-color tg_accent   #4d8dff;"

"window, .background {"
"	background-color: @tg_bg;"
"	color: @tg_fg;"
"}"

/* Title bar */
"headerbar {"
"	background-image: none;"
"	background-color: @tg_panel;"
"	border-bottom: 1px solid @tg_border;"
"	box-shadow: none;"
"	padding: 0 8px;"
"}"
"headerbar .title { font-weight: bold; }"
"headerbar .subtitle { color: @tg_dim; font-size: 11px; }"

/* The strip of measurement controls under the title bar */
".tg-toolbar {"
"	background-color: @tg_panel;"
"	border: 1px solid @tg_border;"
"	border-radius: 12px;"
"	padding: 10px 14px;"
"}"
".tg-caption {"
"	color: @tg_dim;"
"	font-size: 11px;"
"	font-weight: bold;"
"}"

/* Controls */
"button {"
"	background-image: none;"
"	background-color: @tg_raised;"
"	color: @tg_fg;"
"	border: 1px solid @tg_border;"
"	border-radius: 8px;"
"	padding: 4px 12px;"
"	transition: background-color 120ms ease, border-color 120ms ease;"
"}"
"button:hover { background-color: shade(@tg_raised, 1.25); }"
"button:active, button:checked {"
"	background-color: @tg_accent;"
"	border-color: @tg_accent;"
"	color: #ffffff;"
"}"
"button:disabled {"
"	background-color: alpha(@tg_raised, 0.4);"
"	color: alpha(@tg_dim, 0.5);"
"}"
"button.suggested-action {"
"	background-color: @tg_accent;"
"	border-color: @tg_accent;"
"	color: #ffffff;"
"}"
"button.suggested-action:hover { background-color: shade(@tg_accent, 1.15); }"
"button.flat, button.titlebutton {"
"	background-color: transparent;"
"	border-color: transparent;"
"}"
"button.flat:hover, button.titlebutton:hover { background-color: @tg_raised; }"

/* Button groups keep square inner corners */
".linked > button { border-radius: 0; }"
".linked > button:first-child { border-radius: 8px 0 0 8px; }"
".linked > button:last-child { border-radius: 0 8px 8px 0; }"
".linked > button:only-child { border-radius: 8px; }"

"entry, spinbutton, combobox entry, combobox box entry {"
"	background-image: none;"
"	background-color: #11131a;"
"	color: @tg_fg;"
"	border: 1px solid @tg_border;"
"	border-radius: 8px;"
"	padding: 4px 8px;"
"}"
"entry:focus, spinbutton:focus, combobox entry:focus { border-color: @tg_accent; }"
"spinbutton button { border: none; background-color: transparent; padding: 0 6px; }"
"spinbutton button:hover { background-color: @tg_raised; }"

/* Snapshot tabs */
"notebook > header { background-color: transparent; border: none; }"
"notebook > header > tabs > tab {"
"	color: @tg_dim;"
"	border: none;"
"	border-bottom: 2px solid transparent;"
"	padding: 6px 10px;"
"}"
"notebook > header > tabs > tab:checked {"
"	color: @tg_fg;"
"	border-bottom-color: @tg_accent;"
"}"
/* Same rules for gtk+ older than 3.20 */
".notebook tab { color: @tg_dim; border: none; padding: 6px 10px; }"
".notebook tab:active { color: @tg_fg; }"

"menu, .menu, popover {"
"	background-color: @tg_panel;"
"	border: 1px solid @tg_border;"
"	border-radius: 10px;"
"}"
"menuitem { padding: 6px 10px; }"
"menuitem:hover { background-color: @tg_accent; color: #ffffff; }"

"tooltip {"
"	background-color: @tg_panel;"
"	border: 1px solid @tg_border;"
"	border-radius: 8px;"
"	color: @tg_fg;"
"}";

void tg_style_init(void)
{
	GtkSettings *settings = gtk_settings_get_default();
	if(settings)
		g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);

	GdkScreen *screen = gdk_screen_get_default();
	if(!screen) return;

	GtkCssProvider *provider = gtk_css_provider_new();
	if(!gtk_css_provider_load_from_data(provider, tg_css, -1, NULL))
		debug("Failed to load the application stylesheet\n");
	gtk_style_context_add_provider_for_screen(screen,
			GTK_STYLE_PROVIDER(provider),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);
}

void tg_add_class(GtkWidget *widget, const char *class_name)
{
	gtk_style_context_add_class(gtk_widget_get_style_context(widget), class_name);
}

/* A button showing a symbolic icon, falling back to a text label where
   symbolic icons are not available (old gtk+ bundles, e.g. Windows XP). */
GtkWidget *tg_icon_button(const char *icon_name, const char *fallback, const char *tooltip)
{
	GtkWidget *button;
#if defined(WIN_XP) || !GTK_CHECK_VERSION(3,10,0)
	UNUSED(icon_name);
	button = gtk_button_new_with_label(fallback);
#else
	UNUSED(fallback);
	button = gtk_button_new_from_icon_name(icon_name, GTK_ICON_SIZE_BUTTON);
#endif
	if(tooltip) gtk_widget_set_tooltip_text(button, tooltip);
	return button;
}

/* A control preceded by a small caption, as used in the toolbar. */
GtkWidget *tg_labelled_control(const char *caption, GtkWidget *control)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	GtkWidget *label = gtk_label_new(caption);

	gtk_widget_set_halign(label, GTK_ALIGN_START);
	tg_add_class(label, "tg-caption");
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), control, FALSE, FALSE, 0);

	return box;
}
