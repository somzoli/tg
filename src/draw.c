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

/* Drawing primitives shared by the output panel: cards, grids and text.
   Text goes through pango rather than cairo's "toy" API, which gives us real
   font selection, weights and consistent metrics. */

#include "tg.h"
#include <pango/pangocairo.h>

/* The palette. Status colours (red/green/yellow) carry meaning and are used
   only where they do; everything structural is a shade of the background. */
cairo_pattern_t *black,*white,*red,*green,*blue,*blueish,*yellow;
cairo_pattern_t *window_bg,*card_bg,*card_border,*text_dim,*text_faint;
cairo_pattern_t *grid_minor,*grid_major,*highlight,*trace_glow,*band,*band_line;

static void define_color(cairo_pattern_t **gc,double r,double g,double b)
{
	*gc = cairo_pattern_create_rgb(r,g,b);
}

static void define_color_a(cairo_pattern_t **gc,double r,double g,double b,double a)
{
	*gc = cairo_pattern_create_rgba(r,g,b,a);
}

void initialize_palette()
{
	define_color(&black,      .055,.063,.086);	// plot background
	define_color(&white,      .910,.925,.957);	// traces and primary text
	define_color(&red,        .973,.443,.443);	// failure
	define_color(&green,      .204,.827,.600);	// success
	define_color(&blue,       .302,.553,1.00);	// rate line, pulse marker
	define_color(&blueish,    .114,.149,.259);	// tic/toc bands
	define_color(&yellow,     .984,.749,.141);	// stale data

	define_color(&window_bg,  .055,.063,.086);	// behind the cards
	define_color(&card_bg,    .082,.096,.133);	// card face
	define_color(&card_border,.137,.165,.220);	// card outline
	define_color(&text_dim,   .604,.647,.722);	// secondary text
	define_color(&text_faint, .420,.463,.541);	// card captions
	define_color(&grid_minor, .106,.129,.188);	// minor grid lines
	define_color(&grid_major, .165,.204,.275);	// labelled grid lines
	define_color(&highlight,  .302,.553,1.00);	// strip borders

	define_color_a(&trace_glow, .910,.925,.957, .25);
	define_color_a(&band,       .302,.553,1.00, .10);
	define_color_a(&band_line,  .302,.553,1.00, .38);
}

void tg_rounded_rect(cairo_t *c, double x, double y, double w, double h, double r)
{
	if(r > w/2) r = w/2;
	if(r > h/2) r = h/2;
	cairo_new_sub_path(c);
	cairo_arc(c, x + w - r, y + r,     r, -M_PI/2,      0);
	cairo_arc(c, x + w - r, y + h - r, r,       0,  M_PI/2);
	cairo_arc(c, x + r,     y + h - r, r,  M_PI/2,  M_PI);
	cairo_arc(c, x + r,     y + r,     r,  M_PI,  3*M_PI/2);
	cairo_close_path(c);
}

/* The card every drawing area sits in. Returns the area left for content. */
struct tg_rect tg_draw_card(cairo_t *c, double w, double h, const char *title)
{
	struct tg_rect r;

	cairo_set_source(c, window_bg);
	cairo_paint(c);

	tg_rounded_rect(c, .5, .5, w-1, h-1, CARD_RADIUS);
	cairo_set_source(c, card_bg);
	cairo_fill_preserve(c);
	cairo_set_line_width(c, 1);
	cairo_set_source(c, card_border);
	cairo_stroke(c);

	double top = CARD_PAD;
	if(title) {
		tg_text(c, CARD_PAD + 2, CARD_PAD - 2, TG_FONT_LABEL, TG_TEXT_LABEL,
			text_faint, title, NULL);
		top = CARD_PAD + TG_FONT_LABEL + 6;
	}

	r.x = CARD_PAD;
	r.y = top;
	r.width = w - 2*CARD_PAD;
	r.height = h - top - CARD_PAD;
	if(r.width < 1) r.width = 1;
	if(r.height < 1) r.height = 1;
	return r;
}

/* Draw text at (x,y). x is the left, right or centre of the box according to
   the alignment; y is always the top. Returns the width drawn. */
double tg_text(cairo_t *c, double x, double y, int size, int style,
	       cairo_pattern_t *color, const char *text, double *height)
{
	PangoLayout *layout = pango_cairo_create_layout(c);
	PangoFontDescription *desc = pango_font_description_new();
	PangoAttrList *attrs = NULL;

	pango_font_description_set_family(desc, "-apple-system,SF Pro Text,Helvetica Neue,Cantarell,Segoe UI,sans-serif");
	pango_font_description_set_absolute_size(desc, size * PANGO_SCALE);

	switch(style) {
	case TG_TEXT_VALUE:
		pango_font_description_set_weight(desc, PANGO_WEIGHT_SEMIBOLD);
		/* Tabular figures: digits must not jitter as the value changes */
		attrs = pango_attr_list_new();
		pango_attr_list_insert(attrs,
			pango_attr_font_features_new("tnum=1,lnum=1"));
		break;
	case TG_TEXT_LABEL:
		pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
		break;
	default:
		pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
		break;
	}

	pango_layout_set_font_description(layout, desc);
	if(attrs) pango_layout_set_attributes(layout, attrs);
	pango_layout_set_text(layout, text, -1);

	int tw, th;
	pango_layout_get_pixel_size(layout, &tw, &th);
	if(height) *height = th;

	cairo_set_source(c, color);
	cairo_move_to(c, x, y);
	pango_cairo_show_layout(c, layout);

	if(attrs) pango_attr_list_unref(attrs);
	pango_font_description_free(desc);
	g_object_unref(layout);

	return tw;
}

double tg_text_width(cairo_t *c, int size, int style, const char *text)
{
	PangoLayout *layout = pango_cairo_create_layout(c);
	PangoFontDescription *desc = pango_font_description_new();

	pango_font_description_set_family(desc, "-apple-system,SF Pro Text,Helvetica Neue,Cantarell,Segoe UI,sans-serif");
	pango_font_description_set_absolute_size(desc, size * PANGO_SCALE);
	pango_font_description_set_weight(desc,
		style == TG_TEXT_VALUE ? PANGO_WEIGHT_SEMIBOLD :
		style == TG_TEXT_LABEL ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
	pango_layout_set_font_description(layout, desc);
	pango_layout_set_text(layout, text, -1);

	int tw, th;
	pango_layout_get_pixel_size(layout, &tw, &th);

	pango_font_description_free(desc);
	g_object_unref(layout);
	return tw;
}
