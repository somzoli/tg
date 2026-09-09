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

#include "tg.h"

static void draw_graph(double a, double b, cairo_t *c, struct processing_buffers *p,
			struct tg_rect r)
{
	int width = r.width;
	int height = r.height;
	int n;

	int first = 1;
	for(n=0; n<2*width; n++) {
		int i = n < width ? n : 2*width - 1 - n;
		double x = fmod(a + i * (b-a) / width, p->period);
		if(x < 0) x += p->period;
		int j = floor(x);
		double y;

		if(p->waveform[j] <= 0 || p->waveform_max <= 0) y = 0;
		else y = p->waveform[j] * 0.4 / p->waveform_max;

		int k = round(y*height);
		if(n < width) k = -k;

		if(first) {
			cairo_move_to(c, r.x+i+.5, r.y+height/2+k+.5);
			first = 0;
		} else
			cairo_line_to(c, r.x+i+.5, r.y+height/2+k+.5);
	}
}

/* Fill the shape left on the path by draw_graph, with a soft vertical fade. */
static void fill_trace(cairo_t *c, struct tg_rect r, int old)
{
	cairo_pattern_t *grad = cairo_pattern_create_linear(0, r.y, 0, r.y + r.height);
	if(old) {
		cairo_pattern_add_color_stop_rgba(grad, 0, .984,.749,.141, .95);
		cairo_pattern_add_color_stop_rgba(grad, 1, .984,.749,.141, .45);
	} else {
		cairo_pattern_add_color_stop_rgba(grad, 0, .910,.925,.957, .95);
		cairo_pattern_add_color_stop_rgba(grad, 1, .604,.760,1.00, .55);
	}
	cairo_set_line_width(c, 1);
	cairo_set_source(c, grad);
	cairo_fill_preserve(c);
	cairo_stroke(c);
	cairo_pattern_destroy(grad);
}

#ifdef DEBUG
static void draw_debug_graph(double a, double b, cairo_t *c, struct processing_buffers *p, struct tg_rect r)
{
	if(!p->debug) return;

	int width = r.width;
	int height = r.height;

	int i;
	float max = 0;

	int ai = round(a);
	int bi = 1+round(b);
	if(ai < 0) ai = 0;
	if(bi > p->sample_count) bi = p->sample_count;
	for(i=ai; i<bi; i++)
		if(p->debug[i] > max)
			max = p->debug[i];

	int first = 1;
	for(i=0; i<width; i++) {
		if( round(a + i*(b-a)/width) != round(a + (i+1)*(b-a)/width) ) {
			int j = round(a + i*(b-a)/width);
			if(j < 0) j = 0;
			if(j >= p->sample_count) j = p->sample_count-1;

			int k = round((0.1+p->debug[j]/max)*0.8*height);

			if(first) {
				cairo_move_to(c,r.x+i+.5,r.y+height-k-.5);
				first = 0;
			} else
				cairo_line_to(c,r.x+i+.5,r.y+height-k-.5);
		}
	}
}
#endif

static double amplitude_to_time(double lift_angle, double amp)
{
	return asin(lift_angle / (2 * amp)) / M_PI;
}

/* Status badge: a watch outline, a signal-strength meter, and the mode. */
static double draw_status(cairo_t *c, struct tg_rect r, int signal, int happy, int light)
{
	double cx = r.x + 17;
	double cy = r.y + r.height/2 - 4;
	double rad = 15;

	cairo_set_line_width(c, 2);
	cairo_set_source(c, happy ? green : red);
	cairo_arc(c, cx, cy, rad, 0, 2*M_PI);
	cairo_stroke(c);

	/* hands */
	cairo_set_line_width(c, 2);
	cairo_move_to(c, cx, cy);
	cairo_line_to(c, cx + rad*0.52, cy - rad*(happy ? 0.36 : 0.02));
	cairo_move_to(c, cx, cy);
	cairo_line_to(c, cx - rad*0.30, cy - rad*(happy ? 0.60 : 0.34));
	cairo_stroke(c);

	/* signal strength, four rising bars */
	double bx = cx + rad + 9;
	int i;
	for(i = 0; i < NSTEPS; i++) {
		double bh = 5 + i*3.4;
		double by = cy + 9 - bh;
		tg_rounded_rect(c, bx + i*6, by, 4, bh, 1.5);
		if(i < signal) cairo_set_source(c, happy ? green : yellow);
		else cairo_set_source(c, grid_major);
		cairo_fill(c);
	}

	if(light)
		tg_text(c, r.x + 2, r.y + r.height - TG_FONT_LABEL - 2,
			TG_FONT_LABEL, TG_TEXT_LABEL, text_faint, "LIGHT", NULL);

	return bx + NSTEPS*6 + 10;
}

/* One reading: small caption, large value, small unit. */
static void draw_metric(cairo_t *c, struct tg_rect r, const char *caption,
			const char *value, const char *unit, cairo_pattern_t *color)
{
	tg_text(c, r.x, r.y, TG_FONT_LABEL, TG_TEXT_LABEL, text_faint, caption, NULL);

	double vy = r.y + TG_FONT_LABEL + 7;
	double vw = tg_text(c, r.x, vy, METRIC_FONT, TG_TEXT_VALUE, color, value, NULL);
	if(unit)
		tg_text(c, r.x + vw + 6, vy + METRIC_FONT - TG_FONT_LABEL - 5,
			TG_FONT_LABEL + 2, TG_TEXT_BODY, text_dim, unit, NULL);
}

static gboolean output_draw_event(GtkWidget *widget, cairo_t *c, struct output_panel *op)
{
	UNUSED(widget);
	GtkAllocation a;
	gtk_widget_get_allocation(op->output_drawing_area, &a);

	struct tg_rect r = tg_draw_card(c, a.width, a.height, NULL);
	cairo_set_line_width(c, 1);

	struct snapshot *snst = op->snst;
	struct processing_buffers *p = snst->pb;
	int old = snst->is_old;

	double x = draw_status(c, r, snst->signal,
			snst->calibrate ? snst->signal == NSTEPS : snst->signal,
			snst->is_light);

	/* Separator between the badge and the readings */
	cairo_set_source(c, card_border);
	cairo_move_to(c, x - 5.5, r.y + 4);
	cairo_line_to(c, x - 5.5, r.y + r.height - 4);
	cairo_stroke(c);

	struct tg_rect cell = r;
	cell.x = x + 6;
	cell.width = r.x + r.width - cell.x;

	if(snst->calibrate) {
		char s[32];
		const char *state, *detail = NULL;
		cairo_pattern_t *color;

		switch(snst->cal_state) {
		case 1:
			state = "DONE"; color = green;
			sprintf(s, "%s%d.%d", snst->cal_result < 0 ? "-" : "+",
					abs(snst->cal_result) / 10, abs(snst->cal_result) % 10);
			detail = s;
			break;
		case -1:
			state = "FAILED"; color = red;
			break;
		default:
			state = snst->signal == NSTEPS ? "ACQUIRING" : "WAITING";
			color = snst->signal == NSTEPS ? white : yellow;
			sprintf(s, "%d", snst->cal_percent);
			detail = s;
			break;
		}

		const char *unit = snst->cal_state == 1 ? "s/d" :
				   snst->cal_state == 0 ? "%" : NULL;
		if(!detail) detail = "—";

		struct tg_rect m = cell;
		double vw = tg_text_width(c, METRIC_FONT, TG_TEXT_VALUE, detail);
		if(unit) vw += 6 + tg_text_width(c, TG_FONT_LABEL+2, TG_TEXT_BODY, unit);
		double cw = tg_text_width(c, TG_FONT_LABEL, TG_TEXT_LABEL, "CALIBRATION");
		m.width = (vw > cw ? vw : cw) + METRIC_GAP;
		draw_metric(c, m, "CALIBRATION", detail, unit, color);

		m.x += m.width;
		cairo_set_source(c, card_border);
		cairo_move_to(c, m.x - METRIC_GAP/2 + .5, r.y + 6);
		cairo_line_to(c, m.x - METRIC_GAP/2 + .5, r.y + r.height - 6);
		cairo_stroke(c);

		tg_text(c, m.x, m.y, TG_FONT_LABEL, TG_TEXT_LABEL, text_faint, "STATE", NULL);
		tg_text(c, m.x, m.y + TG_FONT_LABEL + 10, METRIC_FONT*3/5, TG_TEXT_VALUE,
			color, state, NULL);
	} else {
		char rate[16], be[16], amp[16], bph[16];
		cairo_pattern_t *color = p && old ? yellow : white;
		cairo_pattern_t *stale = p ? color : text_dim;

		if(p) {
			int r_i = round(snst->rate);
			sprintf(rate, "%s%d", r_i > 0 ? "+" : r_i < 0 ? "−" : "", abs(r_i));
			sprintf(be, "%.1f", snst->be);
			if(snst->amp > 0) sprintf(amp, "%.0f°", snst->amp);
			else strcpy(amp, "—");
		} else {
			strcpy(rate, "—");
			strcpy(be, "—");
			strcpy(amp, "—");
		}
		sprintf(bph, "%d", snst->guessed_bph);

		const char *caps[4] = { "RATE", "BEAT ERROR", "AMPLITUDE", "FREQUENCY" };
		const char *vals[4] = { rate, be, amp, bph };
		const char *units[4] = { "s/d", "ms", NULL, "bph" };
		int i;

		/* Lay the readings out on their own width, so they stay together
		   instead of drifting apart on a wide window. */
		double w[4], total = 0;
		for(i = 0; i < 4; i++) {
			double vw = tg_text_width(c, METRIC_FONT, TG_TEXT_VALUE, vals[i]);
			if(units[i]) vw += 6 + tg_text_width(c, TG_FONT_LABEL+2, TG_TEXT_BODY, units[i]);
			double cw = tg_text_width(c, TG_FONT_LABEL, TG_TEXT_LABEL, caps[i]);
			w[i] = (vw > cw ? vw : cw) + METRIC_GAP;
			total += w[i];
		}

		/* Spread any slack evenly, up to a limit, and keep the group left */
		double slack = cell.width - total;
		if(slack > 0) {
			double extra = slack / 4;
			if(extra > METRIC_GAP) extra = METRIC_GAP;
			for(i = 0; i < 4; i++) w[i] += extra;
		}

		double mx = cell.x;
		for(i = 0; i < 4; i++) {
			struct tg_rect m = cell;
			m.x = mx;
			m.width = w[i];
			draw_metric(c, m, caps[i], vals[i], units[i],
					i == 3 ? (p ? white : text_dim) : stale);
			if(i) {
				cairo_set_source(c, card_border);
				cairo_move_to(c, mx - METRIC_GAP/2 + .5, r.y + 6);
				cairo_line_to(c, mx - METRIC_GAP/2 + .5, r.y + r.height - 6);
				cairo_stroke(c);
			}
			mx += w[i];
		}
	}

#ifdef DEBUG
	{
		static GTimer *timer = NULL;
		if(!timer) timer = g_timer_new();
		else {
			char s[32];
			sprintf(s, "%.0f fps", 1./g_timer_elapsed(timer, NULL));
			tg_text(c, a.width - CARD_PAD - tg_text_width(c, TG_FONT_LABEL, TG_TEXT_BODY, s),
				r.y, TG_FONT_LABEL, TG_TEXT_BODY, text_faint, s, NULL);
			g_timer_reset(timer);
		}
	}
#endif

	return FALSE;
}

static void expose_waveform(
			struct output_panel *op,
			GtkWidget *da,
			cairo_t *c,
			const char *title,
			int (*get_offset)(struct processing_buffers*),
			double (*get_pulse)(struct processing_buffers*))
{
	GtkAllocation alloc;
	gtk_widget_get_allocation(da, &alloc);
	struct tg_rect r = tg_draw_card(c, alloc.width, alloc.height, title);

	int width = r.width;
	int height = r.height;
	int i;

	cairo_save(c);
	tg_rounded_rect(c, r.x, r.y, width, height, 4);
	cairo_clip(c);
	cairo_set_line_width(c, 1);

	/* Time grid, on the lower half */
	for(i = 1-NEGATIVE_SPAN; i < POSITIVE_SPAN; i++) {
		int x = r.x + (NEGATIVE_SPAN + i) * width / (POSITIVE_SPAN + NEGATIVE_SPAN);
		cairo_move_to(c, x + .5, r.y + height / 2 + .5);
		cairo_line_to(c, x + .5, r.y + height - .5);
		cairo_set_source(c, i%5 ? grid_minor : grid_major);
		cairo_stroke(c);
	}
	for(i = 1-NEGATIVE_SPAN; i < POSITIVE_SPAN; i++) {
		if(!(i%5)) {
			int x = r.x + (NEGATIVE_SPAN + i) * width / (POSITIVE_SPAN + NEGATIVE_SPAN);
			char s[10];
			sprintf(s,"%d",i);
			tg_text(c, x + 4, r.y + height - AXIS_FONT - 3, AXIS_FONT,
				TG_TEXT_BODY, text_faint, s, NULL);
		}
	}
	tg_text(c, r.x + width - tg_text_width(c, AXIS_FONT, TG_TEXT_LABEL, "ms") - 2,
		r.y + height - AXIS_FONT - 3, AXIS_FONT, TG_TEXT_LABEL, text_dim, "ms", NULL);

	struct snapshot *snst = op->snst;
	struct processing_buffers *p = snst->pb;
	int old = snst->is_old;
	double period = p ? p->period / snst->sample_rate : 7200. / snst->guessed_bph;

	/* Amplitude grid, on the upper half */
	for(i = 10; i < 360; i+=10) {
		if(2*i < snst->la) continue;
		double t = period*amplitude_to_time(snst->la,i);
		if(t > .001 * NEGATIVE_SPAN) continue;
		int x = r.x + round(width * (NEGATIVE_SPAN - 1000*t) / (NEGATIVE_SPAN + POSITIVE_SPAN));
		cairo_move_to(c, x+.5, r.y + .5);
		cairo_line_to(c, x+.5, r.y + height / 2 + .5);
		cairo_set_source(c, i % 50 ? grid_minor : grid_major);
		cairo_stroke(c);
	}

	double last_x = 0;
	for(i = 50; i < 360; i+=50) {
		double t = period*amplitude_to_time(snst->la,i);
		if(t > .001 * NEGATIVE_SPAN) continue;
		int x = r.x + round(width * (NEGATIVE_SPAN - 1000*t) / (NEGATIVE_SPAN + POSITIVE_SPAN));
		if(x > last_x) {
			char s[10];
			sprintf(s,"%d",abs(i));
			last_x = x + 4 + tg_text(c, x + 4, r.y + 2, AXIS_FONT,
					TG_TEXT_BODY, text_faint, s, NULL);
		}
	}
	tg_text(c, r.x + width - tg_text_width(c, AXIS_FONT, TG_TEXT_LABEL, "deg") - 2,
		r.y + 2, AXIS_FONT, TG_TEXT_LABEL, text_dim, "deg", NULL);

	if(p) {
		double span = 0.001 * snst->sample_rate;
		int offset = get_offset(p);

		double a = offset - span * NEGATIVE_SPAN;
		double b = offset + span * POSITIVE_SPAN;

		draw_graph(a,b,c,p,r);
		fill_trace(c, r, old);

		double pulse = get_pulse(p);
		if(pulse > 0) {
			int x = r.x + round((NEGATIVE_SPAN - pulse / span) * width / (POSITIVE_SPAN + NEGATIVE_SPAN));
			cairo_move_to(c, x + .5, r.y + 1);
			cairo_line_to(c, x + .5, r.y + height - 1);
			cairo_set_source(c,blue);
			cairo_set_line_width(c,2);
			cairo_stroke(c);
			cairo_set_line_width(c,1);
		}
	} else {
		/* No signal: a flat line where the trace would be */
		cairo_move_to(c, r.x + .5, r.y + height / 2 + .5);
		cairo_line_to(c, r.x + width - .5, r.y + height / 2 + .5);
		cairo_set_source(c,yellow);
		cairo_stroke(c);
	}
	cairo_restore(c);
}

static int get_tic(struct processing_buffers *p)
{
	return p->tic;
}

static int get_toc(struct processing_buffers *p)
{
	return p->toc;
}

static double get_tic_pulse(struct processing_buffers *p)
{
	return p->tic_pulse;
}

static double get_toc_pulse(struct processing_buffers *p)
{
	return p->toc_pulse;
}

static gboolean tic_draw_event(GtkWidget *widget, cairo_t *c, struct output_panel *op)
{
	UNUSED(widget);
	expose_waveform(op, op->tic_drawing_area, c, "TIC", get_tic, get_tic_pulse);
	return FALSE;
}

static gboolean toc_draw_event(GtkWidget *widget, cairo_t *c, struct output_panel *op)
{
	UNUSED(widget);
	expose_waveform(op, op->toc_drawing_area, c, "TOC", get_toc, get_toc_pulse);
	return FALSE;
}

static gboolean period_draw_event(GtkWidget *widget, cairo_t *c, struct output_panel *op)
{
	UNUSED(widget);
	GtkAllocation alloc;
	gtk_widget_get_allocation(op->period_drawing_area, &alloc);
	struct tg_rect r = tg_draw_card(c, alloc.width, alloc.height, "PERIOD");

	int width = r.width;
	int height = r.height;

	struct snapshot *snst = op->snst;
	struct processing_buffers *p = snst->pb;
	int old = snst->is_old;

	double toc,a=0,b=0;
	cairo_save(c);
	tg_rounded_rect(c, r.x, r.y, width, height, 4);
	cairo_clip(c);
	cairo_set_line_width(c, 1);

	if(p) {
		/* The windows shown enlarged in the tic and toc views */
		toc = p->tic < p->toc ? p->toc : p->toc + p->period;
		a = ((double)p->tic + toc)/2 - p->period/2;
		b = ((double)p->tic + toc)/2 + p->period/2;

		int k;
		for(k = 0; k < 2; k++) {
			double centre = k ? toc : p->tic;
			double x0 = r.x + (centre - a - NEGATIVE_SPAN*.001*snst->sample_rate) * width/p->period;
			double x1 = r.x + (centre - a + POSITIVE_SPAN*.001*snst->sample_rate) * width/p->period;
			tg_rounded_rect(c, x0, r.y, x1-x0, height, 4);
			cairo_set_source(c, band);
			cairo_fill(c);

			tg_text(c, x0 + 5, r.y + 2, AXIS_FONT, TG_TEXT_LABEL, text_faint,
				k ? "TOC" : "TIC", NULL);
		}
	}

	int i;
	for(i = 1; i < 16; i++) {
		int x = r.x + i * width / 16;
		cairo_move_to(c, x+.5, r.y + .5);
		cairo_line_to(c, x+.5, r.y + height - .5);
		cairo_set_source(c, i % 4 ? grid_minor : grid_major);
		cairo_stroke(c);
	}

	if(p) {
		draw_graph(a,b,c,p,r);
		fill_trace(c, r, old);
	} else {
		cairo_move_to(c, r.x + .5, r.y + height / 2 + .5);
		cairo_line_to(c, r.x + width - .5, r.y + height / 2 + .5);
		cairo_set_source(c,yellow);
		cairo_stroke(c);
	}
	cairo_restore(c);

	return FALSE;
}

static gboolean paperstrip_draw_event(GtkWidget *widget, cairo_t *c, struct output_panel *op)
{
	UNUSED(widget);
	int i;
	struct snapshot *snst = op->snst;
	uint64_t time = snst->timestamp ? snst->timestamp : get_timestamp(snst->is_light);
	double sweep;
	int zoom_factor;
	double slope = 1000; // detected rate: 1000 -> do not display
	if(snst->calibrate) {
		sweep = snst->nominal_sr;
		zoom_factor = PAPERSTRIP_ZOOM_CAL;
		slope = (double) snst->cal * zoom_factor / (10 * 3600 * 24);
	} else {
		sweep = snst->sample_rate * 3600. / snst->guessed_bph;
		zoom_factor = PAPERSTRIP_ZOOM;
		if(snst->events_count && snst->events[snst->events_wp])
			slope = - snst->rate * zoom_factor / (3600. * 24.);
	}

	GtkAllocation alloc;
	gtk_widget_get_allocation(op->paperstrip_drawing_area, &alloc);
	struct tg_rect r = tg_draw_card(c, alloc.width, alloc.height, "TRACE");

	int width = r.width;
	int height = r.height;

	int stopped = 0;
	if( snst->events_count &&
	    snst->events[snst->events_wp] &&
	    time > 5 * snst->nominal_sr + snst->events[snst->events_wp]) {
		time = 5 * snst->nominal_sr + snst->events[snst->events_wp];
		stopped = 1;
	}

	int strip_width = round(width / (1 + PAPERSTRIP_MARGIN));
	int left_margin = (width - strip_width) / 2;
	int right_margin = (width + strip_width) / 2;

	/* Clip everything to the card, so the trace cannot spill over the edge */
	cairo_save(c);
	tg_rounded_rect(c, r.x, r.y, width, height, 4);
	cairo_clip(c);

	/* Time rules, labelled once a minute */
	double now = sweep*ceil(time/sweep);
	double ten_s = snst->sample_rate * 10 / sweep;
	double last_line = fmod(now/sweep, ten_s);
	int last_tenth = floor(now/(sweep*ten_s));
	cairo_set_line_width(c, 1);
	for(i=0;;i++) {
		double y = r.y + 0.5 + round(last_line + i*ten_s);
		if(y > r.y + height) break;
		int major = !((last_tenth-i)%6);
		cairo_move_to(c, r.x + .5, y);
		cairo_line_to(c, r.x + width - .5, y);
		cairo_set_source(c, major ? grid_major : grid_minor);
		cairo_stroke(c);
	}

	/* The strip the trace is folded into */
	cairo_set_source(c, grid_major);
	cairo_move_to(c, r.x + left_margin + .5, r.y + .5);
	cairo_line_to(c, r.x + left_margin + .5, r.y + height - .5);
	cairo_move_to(c, r.x + right_margin + .5, r.y + .5);
	cairo_line_to(c, r.x + right_margin + .5, r.y + height - .5);
	cairo_stroke(c);

	/* Lines of constant rate, so a drifting trace can be read off */
	slope *= strip_width;
	if(slope <= 2 && slope >= -2) {
		cairo_set_line_width(c, 1);
		for(i=0; i<4; i++) {
			double y = 0;
			cairo_move_to(c, r.x + (double)width * (i+.5) / 4, r.y);
			for(;;) {
				double x = y * slope + (double)width * (i+.5) / 4;
				x = fmod(x, width);
				if(x < 0) x += width;
				double nx = x + slope * (height - y);
				if(nx >= 0 && nx <= width) {
					cairo_line_to(c, r.x + nx, r.y + height);
					break;
				} else {
					double d = slope > 0 ? width - x : x;
					y += d / fabs(slope);
					cairo_line_to(c, slope > 0 ? r.x + width : r.x, r.y + y);
					y += 1;
					if(y > height) break;
					cairo_move_to(c, slope > 0 ? r.x : r.x + width, r.y + y);
				}
			}
		}
		cairo_set_source(c, band_line);
		cairo_stroke(c);
		cairo_set_line_width(c, 1);
	}

	/* The beats themselves */
	cairo_set_source(c, stopped ? yellow : white);
	for(i = snst->events_wp;;) {
		if(!snst->events_count || !snst->events[i]) break;
		double event = now - snst->events[i] + snst->trace_centering + sweep * PAPERSTRIP_MARGIN / (2 * zoom_factor);
		int column = floor(fmod(event, (sweep / zoom_factor)) * strip_width / (sweep / zoom_factor));
		int row = floor(event / sweep);
		if(row >= height) break;
		cairo_rectangle(c, r.x + column - .5, r.y + row - .5, BEAT_DOT, BEAT_DOT);
		cairo_fill(c);
		if(column < width - strip_width && row > 0) {
			cairo_rectangle(c, r.x + column + strip_width - .5,
					r.y + row - 1.5, BEAT_DOT, BEAT_DOT);
			cairo_fill(c);
		}
		if(--i < 0) i = snst->events_count - 1;
		if(i == snst->events_wp) break;
	}

	/* Elapsed time, drawn last so the beats do not run through the digits */
	for(i=1;;i++) {
		double y = r.y + 0.5 + round(last_line + i*ten_s);
		if(y > r.y + height - AXIS_FONT) break;
		if((last_tenth-i)%6) continue;
		char s[16];
		sprintf(s, "%ds", i*10);
		double tw = tg_text_width(c, AXIS_FONT, TG_TEXT_BODY, s);
		tg_rounded_rect(c, r.x + 1, y + 1, tw + 9, AXIS_FONT + 5, 3);
		cairo_set_source(c, card_bg);
		cairo_fill(c);
		tg_text(c, r.x + 5, y + 2, AXIS_FONT, TG_TEXT_BODY, text_dim, s, NULL);
	}
	cairo_restore(c);

	/* Scale bar across the strip */
	char s[64];
	sprintf(s, "%.1f ms", snst->calibrate ?
				1000. / zoom_factor :
				3600000. / (snst->guessed_bph * zoom_factor));
	double tw = tg_text_width(c, AXIS_FONT, TG_TEXT_BODY, s);
	double sy = r.y + height - 16.5;
	double mid = r.x + width/2.0;

	cairo_set_source(c, text_dim);
	cairo_set_line_width(c, 1);
	cairo_move_to(c, r.x + left_margin + 4, sy);
	cairo_line_to(c, mid - tw/2 - 6, sy);
	cairo_move_to(c, mid + tw/2 + 6, sy);
	cairo_line_to(c, r.x + right_margin - 4, sy);
	cairo_stroke(c);

	cairo_move_to(c, r.x + left_margin + .5, sy);
	cairo_line_to(c, r.x + left_margin + 5.5, sy - 3.5);
	cairo_line_to(c, r.x + left_margin + 5.5, sy + 3.5);
	cairo_close_path(c);
	cairo_move_to(c, r.x + right_margin + .5, sy);
	cairo_line_to(c, r.x + right_margin - 4.5, sy - 3.5);
	cairo_line_to(c, r.x + right_margin - 4.5, sy + 3.5);
	cairo_close_path(c);
	cairo_fill(c);

	tg_text(c, mid - tw/2, sy - AXIS_FONT/2 - 2, AXIS_FONT, TG_TEXT_BODY, text_dim, s, NULL);

	return FALSE;
}

#ifdef DEBUG
static gboolean debug_draw_event(GtkWidget *widget, cairo_t *c, struct output_panel *op)
{
	UNUSED(widget);
	GtkAllocation alloc;
	gtk_widget_get_allocation(op->debug_drawing_area, &alloc);
	struct tg_rect r = tg_draw_card(c, alloc.width, alloc.height, "DEBUG");

	struct snapshot *snst = op->snst;
	struct processing_buffers *p;
	if(snst->calibrate)
		p = &op->computer->pdata->buffers[0];
	else
		p = snst->pb;

	if(p) {
		double a = snst->nominal_sr / 10;
		double b = snst->nominal_sr * 2;

		draw_debug_graph(a,b,c,p,r);

		cairo_set_source(c,snst->is_old?yellow:white);
		cairo_stroke(c);
	}

	return FALSE;
}
#endif

static void handle_clear_trace(GtkButton *b, struct output_panel *op)
{
	UNUSED(b);
	if(op->computer) {
		lock_computer(op->computer);
		if(!op->snst->calibrate) {
			memset(op->snst->events,0,op->snst->events_count*sizeof(uint64_t));
			op->computer->clear_trace = 1;
		}
		unlock_computer(op->computer);
		gtk_widget_queue_draw(op->paperstrip_drawing_area);
	}
}

static void handle_center_trace(GtkButton *b, struct output_panel *op)
{
	UNUSED(b);
	struct snapshot *snst = op->snst;
	if(!snst || !snst->events)
		return;
	uint64_t last_ev = snst->events[snst->events_wp];
	double new_centering;
	if(last_ev) {
		double sweep;
		if(snst->calibrate)
			sweep = (double) snst->nominal_sr / PAPERSTRIP_ZOOM_CAL;
		else
			sweep = snst->sample_rate * 3600. / (PAPERSTRIP_ZOOM * snst->guessed_bph);
		new_centering = fmod(last_ev + .5*sweep , sweep);
	} else 
		new_centering = 0;
	snst->trace_centering = new_centering;
	gtk_widget_queue_draw(op->paperstrip_drawing_area);
}

static void shift_trace(struct output_panel *op, double direction)
{
	struct snapshot *snst = op->snst;
	double sweep;
	if(snst->calibrate)
		sweep = (double) snst->nominal_sr / PAPERSTRIP_ZOOM_CAL;
	else
		sweep = snst->sample_rate * 3600. / (PAPERSTRIP_ZOOM * snst->guessed_bph);
	snst->trace_centering = fmod(snst->trace_centering + sweep * (1.+.1*direction), sweep);
	gtk_widget_queue_draw(op->paperstrip_drawing_area);
}

static void handle_left(GtkButton *b, struct output_panel *op)
{
	UNUSED(b);
	shift_trace(op,-1);
}

static void handle_right(GtkButton *b, struct output_panel *op)
{
	UNUSED(b);
	shift_trace(op,1);
}

/* Invalidate the drawing areas only: queueing a redraw on the whole notebook
   also repaints the tab strip and the borders, ten times a second. */
void redraw_op(struct output_panel *op)
{
	gtk_widget_queue_draw(op->output_drawing_area);
	gtk_widget_queue_draw(op->paperstrip_drawing_area);
	gtk_widget_queue_draw(op->tic_drawing_area);
	gtk_widget_queue_draw(op->toc_drawing_area);
	gtk_widget_queue_draw(op->period_drawing_area);
#ifdef DEBUG
	gtk_widget_queue_draw(op->debug_drawing_area);
#endif
}

void op_set_snapshot(struct output_panel *op, struct snapshot *snst)
{
	op->snst = snst;
	gtk_widget_set_sensitive(op->clear_button, !snst->calibrate);
}

void op_set_border(struct output_panel *op, int i)
{
	gtk_container_set_border_width(GTK_CONTAINER(op->panel), i);
}

void op_destroy(struct output_panel *op)
{
	snapshot_destroy(op->snst);
	free(op);
}

struct output_panel *init_output_panel(struct computer *comp, struct snapshot *snst, int border)
{
	struct output_panel *op = malloc(sizeof(struct output_panel));

	op->computer = comp;
	op->snst = snst;

	op->panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_set_border_width(GTK_CONTAINER(op->panel), border);

	// Info area on top
	op->output_drawing_area = gtk_drawing_area_new();
	gtk_widget_set_size_request(op->output_drawing_area, 0, OUTPUT_WINDOW_HEIGHT);
	gtk_box_pack_start(GTK_BOX(op->panel),op->output_drawing_area, FALSE, TRUE, 0);
	g_signal_connect (op->output_drawing_area, "draw", G_CALLBACK(output_draw_event), op);
	gtk_widget_set_events(op->output_drawing_area, GDK_EXPOSURE_MASK);

	GtkWidget *hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
	gtk_box_pack_start(GTK_BOX(op->panel), hbox2, TRUE, TRUE, 0);

	GtkWidget *vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_box_pack_start(GTK_BOX(hbox2), vbox2, FALSE, TRUE, 0);

	// Paperstrip
	op->paperstrip_drawing_area = gtk_drawing_area_new();
	gtk_widget_set_size_request(op->paperstrip_drawing_area, 300, 0);
	gtk_box_pack_start(GTK_BOX(vbox2), op->paperstrip_drawing_area, TRUE, TRUE, 0);
	g_signal_connect (op->paperstrip_drawing_area, "draw", G_CALLBACK(paperstrip_draw_event), op);
	gtk_widget_set_events(op->paperstrip_drawing_area, GDK_EXPOSURE_MASK);

	// Paperstrip controls, shown as a single group of linked buttons
	GtkWidget *hbox3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	tg_add_class(hbox3, GTK_STYLE_CLASS_LINKED);
	gtk_box_pack_start(GTK_BOX(vbox2), hbox3, FALSE, TRUE, 0);

	// < button
	GtkWidget *left_button = tg_icon_button("pan-start-symbolic", "<", "Shift the trace left");
	gtk_box_pack_start(GTK_BOX(hbox3), left_button, TRUE, TRUE, 0);
	g_signal_connect (left_button, "clicked", G_CALLBACK(handle_left), op);

	// CLEAR button
	if(comp) {
		op->clear_button = gtk_button_new_with_label("Clear");
		gtk_widget_set_tooltip_text(op->clear_button, "Discard the recorded beats");
		gtk_box_pack_start(GTK_BOX(hbox3), op->clear_button, TRUE, TRUE, 0);
		g_signal_connect (op->clear_button, "clicked", G_CALLBACK(handle_clear_trace), op);
		gtk_widget_set_sensitive(op->clear_button, !snst->calibrate);
	}

	// CENTER button
	GtkWidget *center_button = gtk_button_new_with_label("Center");
	gtk_widget_set_tooltip_text(center_button, "Centre the trace on the strip");
	gtk_box_pack_start(GTK_BOX(hbox3), center_button, TRUE, TRUE, 0);
	g_signal_connect (center_button, "clicked", G_CALLBACK(handle_center_trace), op);

	// > button
	GtkWidget *right_button = tg_icon_button("pan-end-symbolic", ">", "Shift the trace right");
	gtk_box_pack_start(GTK_BOX(hbox3), right_button, TRUE, TRUE, 0);
	g_signal_connect (right_button, "clicked", G_CALLBACK(handle_right), op);

	GtkWidget *vbox3 = gtk_box_new(GTK_ORIENTATION_VERTICAL,10);
	gtk_box_pack_start(GTK_BOX(hbox2), vbox3, TRUE, TRUE, 0);

	// Tic waveform area
	op->tic_drawing_area = gtk_drawing_area_new();
	gtk_box_pack_start(GTK_BOX(vbox3), op->tic_drawing_area, TRUE, TRUE, 0);
	g_signal_connect (op->tic_drawing_area, "draw", G_CALLBACK(tic_draw_event), op);
	gtk_widget_set_events(op->tic_drawing_area, GDK_EXPOSURE_MASK);

	// Toc waveform area
	op->toc_drawing_area = gtk_drawing_area_new();
	gtk_box_pack_start(GTK_BOX(vbox3), op->toc_drawing_area, TRUE, TRUE, 0);
	g_signal_connect (op->toc_drawing_area, "draw", G_CALLBACK(toc_draw_event), op);
	gtk_widget_set_events(op->toc_drawing_area, GDK_EXPOSURE_MASK);

	// Period waveform area
	op->period_drawing_area = gtk_drawing_area_new();
	gtk_box_pack_start(GTK_BOX(vbox3), op->period_drawing_area, TRUE, TRUE, 0);
	g_signal_connect (op->period_drawing_area, "draw", G_CALLBACK(period_draw_event), op);
	gtk_widget_set_events(op->period_drawing_area, GDK_EXPOSURE_MASK);

#ifdef DEBUG
	op->debug_drawing_area = gtk_drawing_area_new();
	gtk_box_pack_start(GTK_BOX(vbox3), op->debug_drawing_area, TRUE, TRUE, 0);
	g_signal_connect (op->debug_drawing_area, "draw", G_CALLBACK(debug_draw_event), op);
	gtk_widget_set_events(op->debug_drawing_area, GDK_EXPOSURE_MASK);
#endif

	return op;
}
