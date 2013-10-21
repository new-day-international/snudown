//
//  main.c
//  snudown
//
//  Created by Curtis Faith on 10/18/13.
//  Copyright (c) 2013 Lightnet Group, Inc. All rights reserved.
//

#include <stdio.h>
#include <string.h>

#include "markdown.h"
#include "html.h"
#include "autolink.h"

#define SNUDOWN_VERSION "1.1.5"

#define false 0
#define true 1

enum snudown_renderer_mode {
	RENDERER_USERTEXT = 0,
	RENDERER_WIKI,
	RENDERER_COUNT
};

struct snudown_renderopt {
	struct html_renderopt html;
	int nofollow;
	const char *target;
};

struct snudown_renderer {
	struct sd_markdown* main_renderer;
	struct sd_markdown* toc_renderer;
	struct module_state* state;
	struct module_state* toc_state;
};

struct module_state {
	struct sd_callbacks callbacks;
	struct snudown_renderopt options;
};

static struct snudown_renderer sundown[RENDERER_COUNT];

static char* html_element_whitelist[] = {"tr", "th", "td", "table", "tbody", "thead", "tfoot", "caption", NULL};
static char* html_attr_whitelist[] = {"colspan", "rowspan", "cellspacing", "cellpadding", "scope", NULL};

static struct module_state usertext_toc_state;
static struct module_state usertext_state;

static const unsigned int snudown_default_md_flags =
MKDEXT_NO_INTRA_EMPHASIS |
MKDEXT_SUPERSCRIPT |
MKDEXT_AUTOLINK |
MKDEXT_STRIKETHROUGH |
MKDEXT_TABLES;

static const unsigned int snudown_default_render_flags =
HTML_SKIP_HTML |
HTML_SKIP_IMAGES |
HTML_SAFELINK |
HTML_ESCAPE |
HTML_USE_XHTML |
HTML_HARD_WRAP;

static void
snudown_link_attr(struct buf *ob, const struct buf *link, void *opaque)
{
	struct snudown_renderopt *options = opaque;
	
	if (options->nofollow)
		BUFPUTSL(ob, " rel=\"nofollow\"");
	
	if (options->target != NULL) {
		BUFPUTSL(ob, " target=\"");
		bufputs(ob, options->target);
		bufputc(ob, '\"');
	}
}

static struct sd_markdown* make_custom_renderer(struct module_state* state,
												const unsigned int renderflags,
												const unsigned int markdownflags,
												int toc_renderer) {
	if(toc_renderer) {
		sdhtml_toc_renderer(&state->callbacks,
							(struct html_renderopt *)&state->options);
	} else {
		sdhtml_renderer(&state->callbacks,
						(struct html_renderopt *)&state->options,
						renderflags);
	}
	
	state->options.html.link_attributes = &snudown_link_attr;
	state->options.html.html_element_whitelist = html_element_whitelist;
	state->options.html.html_attr_whitelist = html_attr_whitelist;
	
	return sd_markdown_new(
						   markdownflags,
						   16,
						   &state->callbacks,
						   &state->options
						   );
}

void init_default_renderer() {
	
	sundown[RENDERER_USERTEXT].main_renderer = make_custom_renderer(&usertext_state, snudown_default_render_flags, snudown_default_md_flags, 0);
	sundown[RENDERER_USERTEXT].toc_renderer = make_custom_renderer(&usertext_toc_state, snudown_default_render_flags, snudown_default_md_flags, 1);
	sundown[RENDERER_USERTEXT].state = &usertext_state;
	sundown[RENDERER_USERTEXT].toc_state = &usertext_toc_state;
}


int main(int argc, const char * argv[])
{
	struct buf					ib, *ob;
	int							enable_toc = 0;
	struct snudown_renderer		renderer;
	int							nofollow = 0;
	char*						target = NULL;
	char*						toc_id_prefix = NULL;
	unsigned int				flags;
	FILE *						fp;
	
	init_default_renderer();

	memset(&ib, 0x0, sizeof(struct buf));
	
	nofollow = false;
	target = "_blank";
	toc_id_prefix = "";
	enable_toc = false;
	
	renderer = sundown[RENDERER_USERTEXT];
	
	struct snudown_renderopt *options = &(renderer.state->options);
	options->nofollow = nofollow;
	options->target = target;
	
	/* Set the input buffer */
	ib.data = (unsigned char*) "First line:\nSecond Line:\nThird Line";
	ib.size = strlen((const char*) ib.data);
	
	/* Output buffer */
	ob = bufnew(128);
	
	flags = options->html.flags;
	
	if (enable_toc) {
		renderer.toc_state->options.html.toc_id_prefix = toc_id_prefix;
		sd_markdown_render(ob, ib.data, ib.size, renderer.toc_renderer);
		renderer.toc_state->options.html.toc_id_prefix = NULL;
		
		options->html.flags |= HTML_TOC;
	}
	
	options->html.toc_id_prefix = toc_id_prefix;
	
	/* do the magic */
	sd_markdown_render(ob, ib.data, ib.size, renderer.main_renderer);
	
	options->html.toc_id_prefix = NULL;
	options->html.flags = flags;
	
	fp = fopen( "markdown.html", "w+" );

	fprintf( fp, "<HTML>\n<BODY>\n");
	fprintf( fp, "%s", (const char*) ob->data);
	fprintf( fp, "\n<HTML>\n<BODY>\n");
	
	fclose( fp );
	
	printf( "%s", (const char*) ob->data );
	
	/* Cleanup */
	bufrelease(ob);

	return 0;
}

