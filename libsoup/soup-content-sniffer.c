/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * soup-content-sniffer.c
 *
 * Copyright (C) 2009 Gustavo Noronha Silva.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gio/gio.h>

#include "soup-content-sniffer.h"
#include "soup-enum-types.h"
#include "soup-message.h"
#include "soup-message-private.h"
#include "soup-session-feature.h"
#include "soup-uri.h"

/**
 * SECTION:soup-content-sniffer
 * @short_description: Content sniffing for #SoupSession
 *
 * A #SoupContentSniffer tries to detect the actual content type of
 * the files that are being downloaded by looking at some of the data
 * before the #SoupMessage emits its #SoupMessage::got-headers signal.
 * #SoupContentSniffer implements #SoupSessionFeature, so you can add
 * content sniffing to a session with soup_session_add_feature() or
 * soup_session_add_feature_by_type().
 *
 * Since: 2.27.3
 **/

static char *sniff (SoupContentSniffer *sniffer, SoupMessage *msg, SoupBuffer *buffer, GHashTable **params);
static gsize get_buffer_size (SoupContentSniffer *sniffer);

static void soup_content_sniffer_session_feature_init (SoupSessionFeatureInterface *feature_interface, gpointer interface_data);

static void request_queued (SoupSessionFeature *feature, SoupSession *session, SoupMessage *msg);
static void request_unqueued (SoupSessionFeature *feature, SoupSession *session, SoupMessage *msg);

G_DEFINE_TYPE_WITH_CODE (SoupContentSniffer, soup_content_sniffer, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (SOUP_TYPE_SESSION_FEATURE,
						soup_content_sniffer_session_feature_init))

static void
soup_content_sniffer_init (SoupContentSniffer *content_sniffer)
{
}

static void
soup_content_sniffer_class_init (SoupContentSnifferClass *content_sniffer_class)
{
	content_sniffer_class->sniff = sniff;
	content_sniffer_class->get_buffer_size = get_buffer_size;
}

static void
soup_content_sniffer_session_feature_init (SoupSessionFeatureInterface *feature_interface,
					   gpointer interface_data)
{
	feature_interface->request_queued = request_queued;
	feature_interface->request_unqueued = request_unqueued;
}

/**
 * soup_content_sniffer_new:
 *
 * Creates a new #SoupContentSniffer.
 *
 * Returns: a new #SoupContentSniffer
 *
 * Since: 2.27.3
 **/
SoupContentSniffer *
soup_content_sniffer_new ()
{
	return g_object_new (SOUP_TYPE_CONTENT_SNIFFER, NULL);
}

char *
soup_content_sniffer_sniff (SoupContentSniffer *sniffer,
			    SoupMessage *msg, SoupBuffer *buffer,
			    GHashTable **params)
{
	g_return_val_if_fail (SOUP_IS_CONTENT_SNIFFER (sniffer), NULL);
	g_return_val_if_fail (SOUP_IS_MESSAGE (msg), NULL);
	g_return_val_if_fail (buffer != NULL, NULL);

	return SOUP_CONTENT_SNIFFER_GET_CLASS (sniffer)->sniff (sniffer, msg, buffer, params);
}

/* This table is based on the HTML5 spec;
 * See 2.7.4 Content-Type sniffing: unknown type
 */
typedef struct {
	/* @has_ws is TRUE if @pattern contains "generic" whitespace */
	gboolean    has_ws;
	const char *mask;
	const char *pattern;
	guint       pattern_length;
	const char *sniffed_type;
	gboolean    scriptable;
} SoupContentSnifferPattern;

static SoupContentSnifferPattern types_table[] = {
	{ FALSE,
	  "\xFF\xFF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xFF\xDF\xDF\xDF\xDF",
	  "\x3C\x21\x44\x4F\x43\x54\x59\x50\x45\x20\x48\x54\x4D\x4C",
	  14,
	  "text/html",
	  TRUE },

	{ TRUE,
	  "\xFF\xFF\xDF\xDF\xDF\xDF",
	  " \x3C\x48\x54\x4D\x4C",
	  5,
	  "text/html",
	  TRUE },

	{ TRUE,
	  "\xFF\xFF\xDF\xDF\xDF\xDF",
	  " \x3C\x48\x45\x41\x44",
	  5,
	  "text/html",
	  TRUE },

	{ TRUE,
	  "\xFF\xFF\xDF\xDF\xDF\xDF\xDF\xDF",
	  " \x3C\x53\x43\x52\x49\x50\x54",
	  7,
	  "text/html",
	  TRUE },

	{ FALSE,
	  "\xFF\xFF\xFF\xFF\xFF",
	  "\x25\x50\x44\x46\x2D",
	  5,
	  "application/pdf",
	  TRUE },

	{ FALSE,
	  "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF",
	  "\x25\x21\x50\x53\x2D\x41\x64\x6F\x62\x65\x2D",
	  11,
	  "application/postscript",
	  FALSE },

	{ FALSE,
	  "\xFF\xFF\x00\x00",
	  "\xFE\xFF\x00\x00",
	  4,
	  "text/plain",
	  FALSE },

	{ FALSE,
	  "\xFF\xFF\x00\x00",
	  "\xFF\xFF\x00\x00",
	  4,
	  "text/plain",
	  FALSE },

	{ FALSE,
	  "\xFF\xFF\xFF\x00",
	  "\xEF\xBB\xBF\x00",
	  4,
	  "text/plain",
	  FALSE },

	{ FALSE,
	  "\xFF\xFF\xFF\xFF\xFF\xFF",
	  "\x47\x49\x46\x38\x37\x61",
	  6,
	  "image/gif",
	  FALSE },

	{ FALSE,
	  "\xFF\xFF\xFF\xFF\xFF\xFF",
	  "\x47\x49\x46\x38\x39\x61",
	  6,
	  "image/gif",
	  FALSE },

	{ FALSE,
	  "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF",
	  "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A",
	  8,
	  "image/png",
	  FALSE },

	{ FALSE,
	  "\xFF\xFF\xFF",
	  "\xFF\xD8\xFF",
	  3,
	  "image/jpeg",
	  FALSE },

	{ FALSE,
	  "\xFF\xFF",
	  "\x42\x4D",
	  2,
	  "image/bmp",
	  FALSE },

	{ FALSE,
	  "\xFF\xFF\xFF\xFF",
	  "\x00\x00\x01\x00",
	  4,
	  "image/vnd.microsoft.icon",
	  FALSE }
};

/* Whether a given byte looks like it might be part of binary content.
 * Source: HTML5 spec; borrowed from the Chromium mime sniffer code,
 * which is BSD-licensed
 */
static char byte_looks_binary[] = {
	1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1, 1,  /* 0x00 - 0x0F */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1,  /* 0x10 - 0x1F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x20 - 0x2F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x30 - 0x3F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x40 - 0x4F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x50 - 0x5F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x60 - 0x6F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x70 - 0x7F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x80 - 0x8F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x90 - 0x9F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xA0 - 0xAF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xB0 - 0xBF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xC0 - 0xCF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xD0 - 0xDF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xE0 - 0xEF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0xF0 - 0xFF */
};

static char *
sniff_gio (SoupContentSniffer *sniffer, SoupMessage *msg, SoupBuffer *buffer)
{
	SoupURI *uri;
	char *uri_path;
	char *content_type;
	char *mime_type;
	gboolean uncertain;

	uri = soup_message_get_uri (msg);
	uri_path = soup_uri_to_string (uri, TRUE);

	content_type= g_content_type_guess (uri_path, (const guchar*)buffer->data, buffer->length, &uncertain);
	mime_type = g_content_type_get_mime_type (content_type);

	g_free (uri_path);
	g_free (content_type);

	return mime_type;
}

/* HTML5: 2.7.4 Content-Type sniffing: unknown type */
static char*
sniff_unknown (SoupContentSniffer *sniffer, SoupMessage *msg,
	       SoupBuffer *buffer, gboolean for_text_or_binary)
{
	const char *resource = buffer->data;
	int resource_length = MIN (512, buffer->length);
	char *gio_guess;
	int i;

	for (i = 0; i < G_N_ELEMENTS (types_table); i++) {
		SoupContentSnifferPattern *type_row = &(types_table[i]);

		/* The scriptable types should be skiped for the text
		 * or binary path, but considered for other paths */
		if (for_text_or_binary && type_row->scriptable)
			continue;

		if (type_row->has_ws) {
			int index_stream = 0;
			int index_pattern = 0;
			gboolean skip_row = FALSE;

			while (index_stream < resource_length) {
				/* Skip insignificant white space ("WS" in the spec) */
				if (type_row->pattern[index_pattern] == ' ') {
					if (resource[index_stream] == '\x09' ||
					    resource[index_stream] == '\x0a' ||
					    resource[index_stream] == '\x0c' ||
					    resource[index_stream] == '\x0d' ||
					    resource[index_stream] == '\x20')
						index_stream++;
					else
						index_pattern++;
				} else {
					if ((type_row->mask[index_pattern] & resource[index_stream]) != type_row->pattern[index_pattern]) {
						skip_row = TRUE;
						break;
					}
					index_pattern++;
					index_stream++;
				}
			}

			if (skip_row)
				continue;

			if (index_pattern > type_row->pattern_length)
				return g_strdup (type_row->sniffed_type);
		} else {
			int j;

			if (resource_length < type_row->pattern_length)
				continue;

			for (j = 0; j < type_row->pattern_length; j++) {
				if ((type_row->mask[j] & resource[j]) != type_row->pattern[j])
					break;
			}

			/* This means our comparison above matched completely */
			if (j == type_row->pattern_length)
				return g_strdup (type_row->sniffed_type);
		}
	}

	/* The spec allows us to use platform sniffing to find out
	 * about other types that are not covered, but we need to be
	 * careful to not escalate privileges, if on text or binary.
	 */
	gio_guess = sniff_gio (sniffer, msg, buffer);

	if (for_text_or_binary) {
		for (i = 0; i < G_N_ELEMENTS (types_table); i++) {
			SoupContentSnifferPattern *type_row = &(types_table[i]);

			if (!g_ascii_strcasecmp (type_row->sniffed_type, gio_guess) &&
			    type_row->scriptable) {
				g_free (gio_guess);
				gio_guess = NULL;
				break;
			}
		}
	}

	if (gio_guess)
		return gio_guess;

	return g_strdup ("application/octet-stream");
}

/* HTML5: 2.7.3 Content-Type sniffing: text or binary */
static char*
sniff_text_or_binary (SoupContentSniffer *sniffer, SoupMessage *msg,
		      SoupBuffer *buffer)
{
	const char *resource = buffer->data;
	int resource_length = MIN (512, buffer->length);
	gboolean looks_binary = FALSE;
	int i;

	/* Detecting UTF-16BE, UTF-16LE, or UTF-8 BOMs means it's text/plain */
	if (resource_length >= 4) {
		if ((resource[0] == 0xFE && resource[1] == 0xFF) ||
		    (resource[0] == 0xFF && resource[1] == 0xFE) ||
		    (resource[0] == 0xEF && resource[1] == 0xBB && resource[2] == 0xBF))
			return g_strdup ("text/plain");
	}

	/* Look to see if any of the first n bytes looks binary */
	for (i = 0; i < resource_length; i++) {
		if (byte_looks_binary[(unsigned char)resource[i]]) {
			looks_binary = TRUE;
			break;
		}
	}

	if (!looks_binary)
		return g_strdup ("text/plain");

	return sniff_unknown (sniffer, msg, buffer, TRUE);
}

static char*
sniff_images (SoupContentSniffer *sniffer, SoupMessage *msg,
	      SoupBuffer *buffer, const char *content_type)
{
	const char *resource = buffer->data;
	int resource_length = MIN (512, buffer->length);
	int i;

	for (i = 0; i < G_N_ELEMENTS (types_table); i++) {
		SoupContentSnifferPattern *type_row = &(types_table[i]);

		if (resource_length < type_row->pattern_length)
			continue;

		if (!g_str_has_prefix (type_row->sniffed_type, "image/"))
			continue;

		/* All of the image types use all-\xFF for the mask,
		 * so we can just memcmp.
		 */
		if (memcmp (type_row->pattern, resource, type_row->pattern_length) == 0)
			return g_strdup (type_row->sniffed_type);
	}

	return g_strdup (content_type);
}

static char*
sniff_feed_or_html (SoupContentSniffer *sniffer, SoupMessage *msg, SoupBuffer *buffer)
{
	const char *resource = buffer->data;
	int resource_length = MIN (512, buffer->length);
	int pos = 0;

	/* Skip a leading UTF-8 BOM */
	if (resource[0] == 0xEF && resource[1] == 0xBB && resource[2] == 0xBF)
		pos = 3;

 look_for_tag:
	/* Skip insignificant white space */
	while ((resource[pos] == '\x09') ||
	       (resource[pos] == '\x20') ||
	       (resource[pos] == '\x0A') ||
	       (resource[pos] == '\x0D'))
		pos++;

	/* != < */
	if (resource[pos] != '\x3C')
		return g_strdup ("text/html");

	pos++;

	/* Skipping comments */
	if ((resource[pos] == '\x2D') ||
	    (resource[pos+1] == '\x2D') ||
	    (resource[pos+2] == '\x3E')) {
		pos = pos + 3;

		while ((resource[pos] != '\x2D') &&
		       (resource[pos+1] != '\x2D') &&
		       (resource[pos+2] != '\x3E'))
			pos++;

		goto look_for_tag;
	}

	/* == ! */
	if (resource[pos] == '\x21') {
		do {
			pos++;
		} while (resource[pos] != '\x3E');

		pos++;

		goto look_for_tag;
	} else if (resource[pos] == '\x3F') { /* ? */
		do {
			pos++;
		} while ((resource[pos] != '\x3F') &&
			 (resource[pos+1] != '\x3E'));

		pos = pos + 2;

		goto look_for_tag;
	}

	if ((resource[pos] == '\x72') &&
	    (resource[pos+1] == '\x73') &&
	    (resource[pos+2] == '\x73'))
		return g_strdup ("application/rss+xml");

	if ((resource[pos] == '\x66') &&
	    (resource[pos+1] == '\x65') &&
	    (resource[pos+2] == '\x65') &&
	    (resource[pos+3] == '\x64'))
		return g_strdup ("application/atom+xml");

	return g_strdup ("text/html");
}

static char*
sniff (SoupContentSniffer *sniffer, SoupMessage *msg, SoupBuffer *buffer, GHashTable **params)
{
	const char *content_type_with_params;
	const char *content_type;

	content_type = soup_message_headers_get_content_type (msg->response_headers, params);
	content_type_with_params = soup_message_headers_get_one (msg->response_headers, "Content-Type");


	/* These comparisons are done in an ASCII-case-insensitive
	 * manner because the spec requires it */
	if ((content_type == NULL) ||
	    !g_ascii_strcasecmp (content_type, "unknown/unknown") ||
	    !g_ascii_strcasecmp (content_type, "application/unknown") ||
	    !g_ascii_strcasecmp (content_type, "*/*"))
		return sniff_unknown (sniffer, msg, buffer, FALSE);

	if (g_str_has_suffix (content_type, "+xml") ||
	    !g_ascii_strcasecmp (content_type, "text/xml") ||
	    !g_ascii_strcasecmp (content_type, "application/xml"))
		return g_strdup (content_type);

	/* 2.7.5 Content-Type sniffing: image
	 * The spec says:
	 *
	 *   If the resource's official type is "image/svg+xml", then
	 *   the sniffed type of the resource is its official type (an
	 *   XML type)
	 *
	 * The XML case is handled by the if above; if you refactor
	 * this code, keep this in mind.
	 */
	if (!g_ascii_strncasecmp (content_type, "image/", 6))
		return sniff_images (sniffer, msg, buffer, content_type);

	/* If we got text/plain, use text_or_binary */
	if (g_str_equal (content_type_with_params, "text/plain") ||
	    g_str_equal (content_type_with_params, "text/plain; charset=ISO-8859-1") ||
	    g_str_equal (content_type_with_params, "text/plain; charset=iso-8859-1") ||
	    g_str_equal (content_type_with_params, "text/plain; charset=UTF-8")) {
		return sniff_text_or_binary (sniffer, msg, buffer);
	}

	if (!g_ascii_strcasecmp (content_type, "text/html"))
		return sniff_feed_or_html (sniffer, msg, buffer);

	return g_strdup (content_type);
}

static gsize
get_buffer_size (SoupContentSniffer *sniffer)
{
	return 512;
}

static void
soup_content_sniffer_got_headers_cb (SoupMessage *msg, SoupContentSniffer *sniffer)
{
	SoupMessagePrivate *priv = SOUP_MESSAGE_GET_PRIVATE (msg);
	SoupContentSnifferClass *content_sniffer_class = SOUP_CONTENT_SNIFFER_GET_CLASS (sniffer);

	priv->bytes_for_sniffing = content_sniffer_class->get_buffer_size (sniffer);
}

static void
request_queued (SoupSessionFeature *feature, SoupSession *session,
		SoupMessage *msg)
{
	SoupMessagePrivate *priv = SOUP_MESSAGE_GET_PRIVATE (msg);

	priv->sniffer = g_object_ref (feature);
	g_signal_connect (msg, "got-headers",
			  G_CALLBACK (soup_content_sniffer_got_headers_cb),
			  feature);
}

static void
request_unqueued (SoupSessionFeature *feature, SoupSession *session,
		  SoupMessage *msg)
{
	SoupMessagePrivate *priv = SOUP_MESSAGE_GET_PRIVATE (msg);

	g_object_unref (priv->sniffer);
	priv->sniffer = NULL;

	g_signal_handlers_disconnect_by_func (msg, soup_content_sniffer_got_headers_cb, feature);
}
