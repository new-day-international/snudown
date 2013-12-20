#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdbool.h>

#include "markdown.h"
#include "html.h"
#include "autolink.h"

#define SNUDOWN_VERSION "1.1.10"

enum snudown_renderer_mode {
	RENDERER_USERTEXT = 0,
	RENDERER_WIKI,
	RENDERER_COUNT
};

struct snudown_renderopt {
	struct html_renderopt   html;
	int                     nofollow;
	const char*             target;
	const char*             domain;
    PyObject*               username_exists;
    PyObject*               username_to_display_name;
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

static char* html_element_whitelist[] = {"tr", "th", "td", "table", "tbody", "thead", "tfoot", "caption", "div", NULL};
static char* html_attr_whitelist[] = {"colspan", "rowspan", "cellspacing", "cellpadding", "scope", "class", "style", NULL};

static struct module_state usertext_toc_state;
static struct module_state wiki_toc_state;
static struct module_state usertext_state;
static struct module_state wiki_state;

/* The module doc strings */
PyDoc_STRVAR(snudown_module__doc__, "When does the narwhal bacon? At Sundown.");
PyDoc_STRVAR(snudown_md__doc__, "Render a Markdown document");
PyDoc_STRVAR(snudown_set_username_callbacks__doc__, "Set the callbacks for @notification username lookups.");

static const unsigned int snudown_default_md_flags =
	MKDEXT_NO_INTRA_EMPHASIS |
	MKDEXT_SUPERSCRIPT |
	MKDEXT_AUTOLINK |
	MKDEXT_STRIKETHROUGH |
	MKDEXT_TABLES;

static const unsigned int snudown_default_render_flags =
	HTML_SKIP_HTML |
	HTML_SAFELINK |
	HTML_ESCAPE |
	HTML_USE_XHTML |
	HTML_HARD_WRAP |
	HTML_ALLOW_ELEMENT_WHITELIST;

static void
snudown_link_attr(struct buf *ob, const struct buf *link, void *opaque)
{
	struct snudown_renderopt *options = opaque;

	if (options->nofollow)
		BUFPUTSL(ob, " rel=\"nofollow\"");

	/* If we have a target, if it is "_blank" which means to open a 
	   new tab, then we should check to make sure the item is not
	   on the lightnet domain before outputting the target. We don't
	   want to open new windows for links within the lightnet.is domain. */
	if (options->target != NULL &&
		(strcmp(options->target, "_blank") != 0 ||
		strstr((const char*) link->data, options->domain) == 0) ) {

		BUFPUTSL(ob, " target=\"");
		bufputs(ob, options->target);
		bufputc(ob, '\"');
	}
}

static int
snudown_user_exists(const struct buf *username, void *opaque)
{
    struct snudown_renderopt*   options = opaque;
    PyObject*                   argument_list;
    PyObject*                   result;
    const char*                 username_from_callback;
    int                         user_exists = false;

    /* Use the callback if there is one */
    if (options->username_exists) {

        /* Build the argument list and call the python function. */
        argument_list = Py_BuildValue("(s#)", username->data, username->size);
        result = PyObject_CallObject(options->username_exists, argument_list);
        Py_DECREF(argument_list);

        /* If we got an error fallback to false */
        if (result == NULL)
            return false;

        /* See if the user exists */
        user_exists = PyObject_IsTrue(result);

        /* We're done with the python result */
        Py_DECREF(result);
    }
    
    return user_exists;
}


static void
snudown_username_to_display_name(struct buf *display_name, const struct buf *username, void *opaque)
{
    struct snudown_renderopt*   options = opaque;
    PyObject*                   argument_list;
    PyObject*                   result;
    const char*                 username_from_callback;

    /* Use the callback if there is one */
    if (options->username_to_display_name) {
        
        /* Build the argument list and call the python function. */
        argument_list = Py_BuildValue("(s#)", username->data, username->size);
        result = PyObject_CallObject(options->username_to_display_name, argument_list);
        Py_DECREF(argument_list);

        /* If we got an error fallback to a copy */
        if (result == NULL)
            {
            bufput(display_name, username->data, username->size);
            return;
            }
            
        /* Copy the username returned from the callback. */
        username_from_callback = PyString_AsString(result);
        bufput(display_name, username_from_callback, strlen(username_from_callback));
        
        /* We're done with the python result */
        Py_DECREF(result);

    } else {
        bufput(display_name, username->data, username->size);
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
	state->options.html.user_exists = &snudown_user_exists;
	state->options.html.username_to_display_name = &snudown_username_to_display_name;
	state->options.html.html_element_whitelist = html_element_whitelist;
	state->options.html.html_attr_whitelist = html_attr_whitelist;

	return sd_markdown_new(markdownflags, 16, &state->callbacks, &state->options);
}

void init_default_renderer(PyObject *module) {
	PyModule_AddIntConstant(module, "RENDERER_USERTEXT", RENDERER_USERTEXT);
	sundown[RENDERER_USERTEXT].main_renderer = make_custom_renderer(&usertext_state, snudown_default_render_flags, snudown_default_md_flags, 0);
	sundown[RENDERER_USERTEXT].toc_renderer = make_custom_renderer(&usertext_toc_state, snudown_default_render_flags, snudown_default_md_flags, 1);
	sundown[RENDERER_USERTEXT].state = &usertext_state;
	sundown[RENDERER_USERTEXT].toc_state = &usertext_toc_state;
}

void init_wiki_renderer(PyObject *module) {
	PyModule_AddIntConstant(module, "RENDERER_WIKI", RENDERER_WIKI);
	sundown[RENDERER_WIKI].main_renderer = make_custom_renderer(&wiki_state, snudown_default_render_flags, snudown_default_md_flags, 0);
	sundown[RENDERER_WIKI].toc_renderer = make_custom_renderer(&wiki_toc_state, snudown_default_render_flags, snudown_default_md_flags, 1);
	sundown[RENDERER_WIKI].state = &wiki_state;
	sundown[RENDERER_WIKI].toc_state = &wiki_toc_state;
}

static PyObject *
snudown_md(PyObject *self, PyObject *args, PyObject *kwargs)
{
	static char *kwlist[] = {"text", "nofollow", "target", "domain", "toc_id_prefix", "renderer",
	    "enable_toc", NULL};

	struct buf              ib, *ob;
	PyObject *              py_result;
	const char*             result_text;
	int                     renderer = RENDERER_USERTEXT;
	int                     enable_toc = 0;
	struct snudown_renderer _snudown;
	int                     nofollow = 0;
	char*                   target = NULL;
	char*                   domain = NULL;
	char*                   toc_id_prefix = NULL;
	unsigned int            flags;

    /* Clear the buffer */
	memset(&ib, 0x0, sizeof(struct buf));

	/* Parse arguments */
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s#|izzzii", kwlist,
				&ib.data, &ib.size, &nofollow, &target, &domain,
				&toc_id_prefix, &renderer, &enable_toc)) {
		return NULL;
	}
	
	if (renderer < 0 || renderer >= RENDERER_COUNT) {
		PyErr_SetString(PyExc_ValueError, "Invalid renderer");
		return NULL;
	}

	_snudown = sundown[renderer];

	struct snudown_renderopt *options = &(_snudown.state->options);
	options->nofollow = nofollow;
	options->target = target;
	options->domain = domain;

	/* Output buffer */
	ob = bufnew(128);

	flags = options->html.flags;

	if (enable_toc) {
		_snudown.toc_state->options.html.toc_id_prefix = toc_id_prefix;
		sd_markdown_render(ob, ib.data, ib.size, _snudown.toc_renderer);
		_snudown.toc_state->options.html.toc_id_prefix = NULL;

		options->html.flags |= HTML_TOC;
	}

	options->html.toc_id_prefix = toc_id_prefix;

	/* do the magic */
	sd_markdown_render(ob, ib.data, ib.size, _snudown.main_renderer);

	options->html.toc_id_prefix = NULL;
	options->html.flags = flags;

	/* make a Python string */
	result_text = "";
	if (ob->data)
		result_text = (const char*)ob->data;
	py_result = Py_BuildValue("s#", result_text, (int)ob->size);

	/* Cleanup */
	bufrelease(ob);
	
	return py_result;
}


static PyObject *
set_username_callbacks(PyObject *self, PyObject *args, PyObject *kwargs)
{
	PyObject *                  py_result;
	PyObject*                   username_exists = NULL;
	PyObject*                   username_to_display_name = NULL;
    struct snudown_renderopt*   options = &(sundown[RENDERER_USERTEXT].state->options);

	/* Parse arguments */
	if (!PyArg_ParseTuple(args, "OO:set_username_callbacks", &username_exists, &username_to_display_name)) {
		return NULL;
	}

	/* Make sure username_exists is callable if one was passed in. */
	if (username_exists) {
	    if (!PyCallable_Check(username_exists)) {
            PyErr_SetString(PyExc_TypeError, "parameter:<username_exists> must be callable");
            return NULL;
        }
        
        /* Increment the reference count */
        Py_XINCREF(username_exists);
    }

	/* Make sure username_to_display_name is callable if one was passed in. */
	if (username_to_display_name) {
	    if (!PyCallable_Check(username_to_display_name)) {
            PyErr_SetString(PyExc_TypeError, "parameter:<username_to_display_name> must be callable");
            return NULL;
        }
        
        /* Increment the reference count */
        Py_XINCREF(username_to_display_name);
    }

	/* Remember the callbacks */
	options->username_exists = username_exists;
	options->username_to_display_name = username_to_display_name;

    /* NOTE: we are explicitly not decrementing the reference counts
       for the callback functions so they won't be garbage collected. */
    
	/* Return true if we get this far */
	py_result = Py_BuildValue("i", (int) true);
	return py_result;
}


static PyMethodDef snudown_methods[] = {
	{"markdown", (PyCFunction) snudown_md, METH_VARARGS | METH_KEYWORDS, snudown_md__doc__},
    {"set_username_callbacks", (PyCFunction) set_username_callbacks, METH_VARARGS, snudown_set_username_callbacks__doc__},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

PyMODINIT_FUNC initsnudown(void)
{
	PyObject *module;

	module = Py_InitModule3("snudown", snudown_methods, snudown_module__doc__);
	if (module == NULL)
		return;

	init_default_renderer(module);
	init_wiki_renderer(module);

	/* Version */
	PyModule_AddStringConstant(module, "__version__", SNUDOWN_VERSION);
}
