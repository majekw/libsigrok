/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "config.h" /* Needed for PACKAGE_STRING and others. */
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "output/csv"

struct context {
	unsigned int num_enabled_channels;
	uint64_t samplerate;
	GString *header;
	char separator;
	int *channel_index;
};

/*
 * TODO:
 *  - Option to specify delimiter character and/or string.
 *  - Option to (not) print metadata as comments.
 *  - Option to specify the comment character(s), e.g. # or ; or C/C++-style.
 *  - Option to (not) print samplenumber / time as extra column.
 *  - Option to "compress" output (only print changed samples, VCD-like).
 *  - Option to print comma-separated bits, or whole bytes/words (for 8/16
 *    channel LAs) as ASCII/hex etc. etc.
 *  - Trigger support.
 */

static int init(struct sr_output *o)
{
	struct context *ctx;
	struct sr_channel *ch;
	GSList *l;
	GVariant *gvar;
	int num_channels, i, j;
	time_t t;

	if (!o)
		return SR_ERR_ARG;

	if (!o->sdi)
		return SR_ERR_ARG;

	ctx = g_malloc0(sizeof(struct context));
	o->internal = ctx;

	/* Get the number of channels, and the unitsize. */
	for (l = o->sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_LOGIC)
			continue;
		if (!ch->enabled)
			continue;
		ctx->num_enabled_channels++;
	}
	ctx->channel_index = g_malloc(sizeof(int) * ctx->num_enabled_channels);

	num_channels = g_slist_length(o->sdi->channels);

	if (sr_config_get(o->sdi->driver, o->sdi, NULL, SR_CONF_SAMPLERATE,
			&gvar) == SR_OK) {
		ctx->samplerate = g_variant_get_uint64(gvar);
		g_variant_unref(gvar);
	} else
		ctx->samplerate = 0;

	ctx->separator = ',';
	ctx->header = g_string_sized_new(512);

	t = time(NULL);

	/* Some metadata */
	g_string_append_printf(ctx->header, "; CSV, generated by %s on %s",
			       PACKAGE_STRING, ctime(&t));
	g_string_append_printf(ctx->header, "; Samplerate: %"PRIu64"\n",
			       ctx->samplerate);

	/* Columns / channels */
	g_string_append_printf(ctx->header, "; Channels (%d/%d):",
			       ctx->num_enabled_channels, num_channels);
	for (i = 0, j = 0, l = o->sdi->channels; l; l = l->next, i++) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_LOGIC)
			continue;
		if (!ch->enabled)
			continue;
		g_string_append_printf(ctx->header, " %s,", ch->name);
		/* Remember the enabled channel's index while we're at it. */
		ctx->channel_index[j++] = i;
	}
	if (o->sdi->channels)
		/* Drop last separator. */
		g_string_truncate(ctx->header, ctx->header->len - 1);
	g_string_append_printf(ctx->header, "\n");

	return SR_OK;
}

static int receive(struct sr_output *o, const struct sr_datafeed_packet *packet,
		GString **out)
{
	const struct sr_datafeed_logic *logic;
	struct context *ctx;
	int idx;
	uint64_t i, j;
	gchar *p, c;

	*out = NULL;
	if (!o || !o->sdi)
		return SR_ERR_ARG;
	if (!(ctx = o->internal))
		return SR_ERR_ARG;

	switch (packet->type) {
	case SR_DF_LOGIC:
		logic = packet->payload;
		if (ctx->header) {
			/*
			 * First data packet: prime the output with the
			 * previously prepared header.
			 */
			*out = ctx->header;
			ctx->header = NULL;
		} else {
			*out = g_string_sized_new(512);
		}

		for (i = 0; i <= logic->length - logic->unitsize; i += logic->unitsize) {
			for (j = 0; j < ctx->num_enabled_channels; j++) {
				idx = ctx->channel_index[j];
				p = logic->data + i + idx / 8;
				c = *p & (1 << (idx % 8));
				g_string_append_c(*out, c ? '1' : '0');
				g_string_append_c(*out, ctx->separator);
			}
			if (j) {
				/* Drop last separator. */
				g_string_truncate(*out, (*out)->len - 1);
			}
			g_string_append_printf(*out, "\n");
		}
		break;
	}

	return SR_OK;
}

static int cleanup(struct sr_output *o)
{
	struct context *ctx;

	if (!o || !o->sdi)
		return SR_ERR_ARG;

	if (o->internal) {
		ctx = o->internal;
		if (ctx->header)
			g_string_free(ctx->header, TRUE);
		g_free(ctx->channel_index);
		g_free(o->internal);
		o->internal = NULL;
	}

	return SR_OK;
}

SR_PRIV struct sr_output_format output_csv = {
	.id = "csv",
	.description = "Comma-separated values (CSV)",
	.init = init,
	.receive = receive,
	.cleanup = cleanup,
};
