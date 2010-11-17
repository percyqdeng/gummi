/**
 * @file   latex.c
 * @brief  
 *
 * Copyright (C) 2010 Gummi-Dev Team <alexvandermey@gmail.com>
 * All Rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "latex.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib.h>

#include "configfile.h"
#include "editor.h"
#include "environment.h"
#include "latex.h"
#include "utils.h"
#include "gui/gui-preview.h"

GuLatex* latex_init(GuFileInfo* fc, GuEditor* ec) {
    L_F_DEBUG;
    GuLatex* m = g_new0(GuLatex, 1);

    /* initialize basis */
    m->b_finfo = fc;
    m->b_editor = ec;

    /* initialize members */
    m->typesetter = g_strdup(config_get_value("typesetter"));
    m->errorline = 0;
    m->prev_errorline = 0;
    m->modified_since_compile = FALSE;
    return m;
}

void latex_update_workfile(GuLatex* lc) {
    L_F_DEBUG;
    GtkTextIter start, end;
    gchar *text;
    FILE *fp;

    /* save selection */
    gtk_text_buffer_get_selection_bounds(
            GTK_TEXT_BUFFER(lc->b_editor->sourcebuffer), &start, &end);
    text = editor_grab_buffer(lc->b_editor);

    /* restore selection */
    gtk_text_buffer_select_range(
            GTK_TEXT_BUFFER(lc->b_editor->sourcebuffer), &start, &end);
    
    fp = fopen(lc->b_finfo->workfile, "w");
    
    if(fp == NULL) {
        slog(L_ERROR, "unable to create workfile in tmpdir\n");
        return;
    }
    fwrite(text, strlen(text), 1, fp);
    g_free(text);
    fclose(fp);
    // TODO: Maybe add editorviewer grab focus line here if necessary
}

void latex_update_pdffile(GuLatex* lc) {
    L_F_DEBUG;
    if (!lc->modified_since_compile) return;
    gchar* dirname = g_path_get_dirname(lc->b_finfo->workfile);
    gchar* command = g_strdup_printf("cd \"%s\";"
                                     "env openout_any=a %s "
                                     "-interaction=nonstopmode "
                                     "-file-line-error "
                                     "-halt-on-error "
                                     "-output-directory=\"%s\" \"%s\"",
                                     dirname,
                                     lc->typesetter,
                                     lc->b_finfo->tmpdir,
                                     lc->b_finfo->workfile);
    g_free(dirname);

    previewgui_update_statuslight("gtk-refresh");
 
    g_free(lc->errormessage);
    pdata cresult = utils_popen_r(command);
    lc->errorline = cresult.ret;
    lc->errormessage = cresult.data;
    lc->modified_since_compile = FALSE;

    /* find error line */
    if (cresult.ret == 1 &&
            (strstr(cresult.data, "Fatal error") ||
            (strstr(cresult.data, "No pages of output.")))) {
        gchar** result = 0;
        GError* error = NULL;
        GRegex* match_str = 0;
        GMatchInfo* match_info;
        match_str = g_regex_new(":([\\d+]+):", G_REGEX_DOTALL, 0, &error);

        if (g_regex_match(match_str, cresult.data, 0, &match_info)) {
            result = g_match_info_fetch_all(match_info);
            if (result[1])
                lc->errorline = atoi(result[1]);
            g_strfreev(result);
        }
        g_match_info_free(match_info);
        g_regex_unref(match_str);

        /* update status light */
        previewgui_update_statuslight("gtk-no");
    } else if (strstr(cresult.data, "No pages of output.")) {
        lc->errorline = -1;
        previewgui_update_statuslight("gtk-no");
    } else
        previewgui_update_statuslight("gtk-yes");
    g_free(command);
}

void latex_update_auxfile(GuLatex* lc) {
    L_F_DEBUG;
    gchar* dirname = g_path_get_dirname(lc->b_finfo->workfile);
    gchar* command = g_strdup_printf("cd \"%s\";"
                                     "env openout_any=a %s "
                                     "--draftmode "
                                     "-interaction=nonstopmode "
                                     "--output-directory=\"%s\" \"%s\"",
                                     dirname,
                                     lc->typesetter,
                                     lc->b_finfo->tmpdir,
                                     lc->b_finfo->workfile);
    g_free(dirname);
    pdata res = utils_popen_r(command);
    g_free(res.data);
    g_free(command);
}

void latex_export_pdffile(GuLatex* lc, const gchar* path) {
    L_F_DEBUG;
    gchar* savepath;
    gint ret = 0;

    if (0 != strcmp(path + strlen(path) -4, ".pdf"))
        savepath = g_strdup_printf("%s.pdf", path);
    else
        savepath = g_strdup(path);
    if (utils_path_exists(savepath)) {
        ret = utils_yes_no_dialog(_("The file already exists. Overwrite?"));
        if (GTK_RESPONSE_YES != ret) {
            g_free(savepath);
            return;
        }
    }
    utils_copy_file(lc->b_finfo->pdffile, savepath);
    g_free(savepath);
}