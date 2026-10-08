/*
    KindlePDFViewer: MuPDF abstraction for Lua
    Copyright (C) 2011 Hans-Werner Hilse <hilse@web.de>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include "wrap-mupdf.h"

static double LOG_TRESHOLD_PERC = 0.05; // 5%

enum {
    MAGIC = 0x3795d42b,
};

typedef struct header {
    int magic;
    size_t sz;
} header;

static size_t msize = 0U;
static size_t msize_prev;
static size_t msize_max;
static size_t msize_min;
static bool is_realloc = false;

#if 0
static size_t msize_iniz;

static void resetMsize() {
    msize_iniz = msize;
    msize_prev = 0;
    msize_max = 0;
    msize_min = (size_t) -1;
}

static void showMsize() {
    char buf[15], buf2[15], buf3[15], buf4[15];
    //printf("§§§ now: %s was: %s - min: %s - max: %s\n", readable_fs(msize, buf), readable_fs(msize_iniz, buf2), readable_fs(msize_min, buf3), readable_fs(msize_max, buf4));
    resetMsize();
}
#endif

static void log_size(char *funcName) {
    if (msize_max < msize) {
        msize_max = msize;
    }
    if (msize_min > msize) {
        msize_min = msize;
    }
    if (1==0 && abs(msize - msize_prev) > msize_prev * LOG_TRESHOLD_PERC) {
        //char buf[15], buf2[15];
        //printf("§§§ %s - total: %s (was %s)\n",funcName, readable_fs(msize,buf),readable_fs(msize_prev,buf2));
        msize_prev = msize;
    }
}

static void *
my_malloc_default(void *opaque, size_t size)
{
    if (size > SIZE_MAX - sizeof(header)) {
        return NULL;
    }
    struct header * h = malloc(size + sizeof(header));
    if (h == NULL) {
        return NULL;
    }

    h->magic = MAGIC;
    h->sz = size;
    msize += size + sizeof(struct header);
    if (!is_realloc) {
        log_size("alloc");
    }
    return (void *)(h + 1);
}

static void
my_free_default(void *opaque, void *ptr)
{
    fprintf(stderr, "free %p (%zu)\n", ptr, msize);
    if (ptr != NULL) {
        struct header * h = ((struct header *)ptr) - 1;
        if (h->magic != MAGIC) { /* Not allocated by us */
            fprintf(stderr, "attempt to free something that doesn't belong to us!\n");
        } else {
            msize -= h->sz + sizeof(struct header);
            free(h);
        }
    }
    if (!is_realloc) {
        log_size("free");
    }
}

static void *
my_realloc_default(void *opaque, void *old, size_t size)
{
    void * newp;
    if (old == NULL) { //practically, it's a malloc
        newp = my_malloc_default(opaque, size);
    } else {
        struct header * h = ((struct header *)old) - 1;
        if (h->magic != MAGIC) { // Not allocated by my_malloc_default
            //printf("§§§ warn: not allocated by my_malloc_default, new size: %zu\n", size);
            newp = realloc(old, size);
        } else { // malloc + free
            is_realloc = true;
            size_t oldsize = h->sz;
            //printf("realloc %zu -> %zu\n", oldsize, size);
            newp = my_malloc_default(opaque, size);
            if (NULL != newp) {
                memcpy(newp, old, oldsize < size ? oldsize : size);
                my_free_default(opaque, old);
            }
            log_size("realloc");
            is_realloc = false;
        }
    }

    return(newp);
}

fz_alloc_context my_alloc_default =
{
    NULL,
    my_malloc_default,
    my_realloc_default,
    my_free_default
};

fz_alloc_context* mupdf_get_my_alloc_context() {
    return &my_alloc_default;
}

int mupdf_get_cache_size() {
    return msize;
}

int mupdf_error_code(fz_context *ctx) {
    return ctx->error.errcode;
}
char* mupdf_error_message(fz_context *ctx) {
    return ctx->error.message;
}

fz_matrix *mupdf_fz_scale(fz_matrix *m, float sx, float sy) {
    *m = fz_scale(sx, sy);
    return m;
}

fz_matrix *mupdf_fz_translate(fz_matrix *m, float tx, float ty) {
    *m = fz_translate(tx, ty);
    return m;
}

fz_matrix *mupdf_fz_pre_rotate(fz_matrix *m, float theta) {
    *m = fz_pre_rotate(*m, theta);
    return m;
}

fz_matrix *mupdf_fz_pre_translate(fz_matrix *m, float tx, float ty) {
    *m = fz_pre_translate(*m, tx, ty);
    return m;
}

fz_rect *mupdf_fz_transform_rect(fz_rect *r, const fz_matrix *m) {
    *r = fz_transform_rect(*r, *m);
    return r;
}

fz_irect *mupdf_fz_round_rect(fz_irect *ir, const fz_rect *r) {
    *ir = fz_round_rect(*r);
    return ir;
}

fz_rect *mupdf_fz_union_rect(fz_rect *a, const fz_rect *b) {
    *a = fz_union_rect(*a, *b);
    return a;
}

fz_rect *mupdf_fz_rect_from_quad(fz_rect *r, const fz_quad *q) {
    *r = fz_rect_from_quad(*q);
    return r;
}

fz_rect *mupdf_fz_bound_page(fz_context *ctx, fz_page *page, fz_rect *r) {
    *r = fz_bound_page(ctx, page);
    return r;
}

static void transparency_mask_close_device(fz_context *ctx, fz_device *dev)
{
    fz_close_device(ctx, dev->passthrough);
}

static void transparency_mask_drop_device(fz_context *ctx, fz_device *dev)
{
    fz_drop_device(ctx, dev->passthrough);
}

static void transparency_mask_fill_image(fz_context* ctx, fz_device* dev, fz_image* img, fz_matrix ctm, float alpha, fz_color_params color_params)
{
    if (img->mask) {
        float black = 0.f;
        fz_fill_image_mask(ctx, dev->passthrough, img->mask, ctm, fz_device_gray(ctx), &black, alpha, color_params);
    }
}

fz_device *new_transparency_mask_device(fz_context* ctx, fz_device* dev)
{
    fz_device *transparency_mask_dev = fz_new_derived_device(ctx, fz_device);
    transparency_mask_dev->passthrough = fz_keep_device(ctx, dev);
    transparency_mask_dev->close_device = transparency_mask_close_device;
    transparency_mask_dev->drop_device = transparency_mask_drop_device;
    transparency_mask_dev->fill_image = transparency_mask_fill_image;
    return transparency_mask_dev;
}

int page_has_transparency_mask(fz_context* ctx, fz_page* p)
{
    /* Other document types (EPUB, XPS, etc.) don't use smasks. */
    pdf_page* page = pdf_page_from_fz_page(ctx, p);
    if (!page) {
        return 0;
    }

    pdf_obj* resources = pdf_page_resources(ctx, page);
    pdf_obj* xobj = pdf_dict_get(ctx, resources, PDF_NAME(XObject));
    if (!xobj) {
        return 0;
    }

    static pdf_obj* const mask_keys[] = {
        PDF_NAME(SMask),
        PDF_NAME(Mask)
    };
    const int num_keys = sizeof(mask_keys) / sizeof(mask_keys[0]);

    const int n = pdf_dict_len(ctx, xobj);
    for (int i = 0; i < n; i++) {
        pdf_obj* val = pdf_dict_get_val(ctx, xobj, i);
        for (int k = 0; k < num_keys; k++) {
            if (pdf_dict_get(ctx, val, mask_keys[k]))
                return 1;
        }
    }
    return 0;
}

/*
 * Invert every color of a PDF page in its content: paint the page box white
 * below the original content, then paint it white again on top of it with the
 * Difference blend mode. Annotations are drawn after the page content and keep
 * their colors.
 */
void invert_pdf_page_colors(fz_context *ctx, pdf_document *doc, int page_no)
{
    pdf_obj *page = pdf_lookup_page_obj(ctx, doc, page_no);
    fz_rect box = pdf_to_rect(ctx, pdf_dict_get_inheritable(ctx, page, PDF_NAME(MediaBox)));
    if (fz_is_empty_rect(box)) {
        box = fz_make_rect(0, 0, 612, 792);
    }

    pdf_obj *resources = pdf_dict_get_inheritable(ctx, page, PDF_NAME(Resources));
    if (!resources) {
        resources = pdf_dict_put_dict(ctx, page, PDF_NAME(Resources), 1);
    }
    pdf_obj *ext_g_states = pdf_dict_get(ctx, resources, PDF_NAME(ExtGState));
    if (!ext_g_states) {
        ext_g_states = pdf_dict_put_dict(ctx, resources, PDF_NAME(ExtGState), 1);
    }

    fz_buffer *before = NULL;
    fz_buffer *after = NULL;
    pdf_obj *gs = NULL;
    pdf_obj *before_ref = NULL;
    pdf_obj *after_ref = NULL;
    pdf_obj *contents = NULL;
    fz_var(before);
    fz_var(after);
    fz_var(gs);
    fz_var(before_ref);
    fz_var(after_ref);
    fz_var(contents);
    fz_try(ctx) {
        gs = pdf_new_dict(ctx, doc, 2);
        pdf_dict_put(ctx, gs, PDF_NAME(Type), PDF_NAME(ExtGState));
        pdf_dict_put_name(ctx, gs, PDF_NAME(BM), "Difference");
        pdf_dict_puts(ctx, ext_g_states, "KOReaderInvert", gs);

        float w = box.x1 - box.x0;
        float h = box.y1 - box.y0;
        before = fz_new_buffer(ctx, 64);
        fz_append_printf(ctx, before, "q 1 1 1 rg %g %g %g %g re f Q\nq\n", box.x0, box.y0, w, h);
        after = fz_new_buffer(ctx, 96);
        fz_append_printf(ctx, after, "\nQ\nq /KOReaderInvert gs 1 1 1 rg %g %g %g %g re f Q\n", box.x0, box.y0, w, h);
        before_ref = pdf_add_stream(ctx, doc, before, NULL, 0);
        after_ref = pdf_add_stream(ctx, doc, after, NULL, 0);

        pdf_obj *old_contents = pdf_dict_get(ctx, page, PDF_NAME(Contents));
        contents = pdf_new_array(ctx, doc, 3);
        pdf_array_push(ctx, contents, before_ref);
        if (pdf_is_array(ctx, old_contents)) {
            int n = pdf_array_len(ctx, old_contents);
            for (int i = 0; i < n; i++) {
                pdf_array_push(ctx, contents, pdf_array_get(ctx, old_contents, i));
            }
        } else if (old_contents) {
            pdf_array_push(ctx, contents, old_contents);
        }
        pdf_array_push(ctx, contents, after_ref);
        pdf_dict_put(ctx, page, PDF_NAME(Contents), contents);
    }
    fz_always(ctx) {
        pdf_drop_obj(ctx, contents);
        pdf_drop_obj(ctx, after_ref);
        pdf_drop_obj(ctx, before_ref);
        pdf_drop_obj(ctx, gs);
        fz_drop_buffer(ctx, after);
        fz_drop_buffer(ctx, before);
    }
    fz_catch(ctx) {
        fz_rethrow(ctx);
    }
}

/* wrappers for functions that throw exceptions mupdf-style (setjmp/longjmp) */

#define MUPDF_DO_WRAP
#include "wrap-mupdf.h"
