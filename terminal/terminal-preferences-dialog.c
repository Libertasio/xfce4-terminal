/*-
 * Copyright (c) 2004-2007 os-cillation e.K.
 *
 * Written by Benedikt Meurer <benny@xfce.org>.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libxfce4ui/libxfce4ui.h>

#ifdef GDK_WINDOWING_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#include <terminal/terminal-util.h>
#include <terminal/terminal-enum-types.h>
#include <terminal/terminal-preferences-dialog.h>
#include <terminal/terminal-encoding-action.h>
#include <terminal/terminal-private.h>

#include "terminal-preferences-ui.h"

static void     terminal_preferences_dialog_finalize          (GObject                   *object);
static void     terminal_preferences_dialog_disc_bindings     (GtkWidget                 *widget,
                                                               TerminalPreferencesDialog *dialog);
static void     terminal_preferences_dialog_died              (gpointer                   user_data,
                                                               GObject                   *where_the_object_was);
static void     terminal_preferences_dialog_response          (GtkWidget                 *widget,
                                                               gint                       response,
                                                               TerminalPreferencesDialog *dialog);
static gboolean terminal_preferences_dialog_color_press_event (GtkWidget                 *widget,
                                                               GdkEventButton            *event);
#ifdef GDK_WINDOWING_X11
static void     terminal_preferences_dialog_geometry_changed  (TerminalPreferencesDialog *dialog);
static void     terminal_preferences_dialog_geometry_columns  (GtkAdjustment             *adj,
                                                               TerminalPreferencesDialog *dialog);
static void     terminal_preferences_dialog_geometry_rows     (GtkAdjustment             *adj,
                                                               TerminalPreferencesDialog *dialog);
#endif
static void     terminal_preferences_dialog_palette_changed   (GtkWidget                 *button,
                                                               TerminalPreferencesDialog *dialog);
static void     terminal_preferences_dialog_palette_notify    (TerminalPreferencesDialog *dialog);
static void     terminal_preferences_dialog_presets_load      (TerminalPreferencesDialog *dialog);
#if VTE_CHECK_VERSION (0, 51, 3)
static void     terminal_preferences_dialog_reset_cell_scale  (GtkWidget                 *button,
                                                               TerminalPreferencesDialog *dialog);
#endif
static void     terminal_preferences_dialog_reset_compat      (GtkWidget                 *button,
                                                               TerminalPreferencesDialog *dialog);
static void     terminal_preferences_dialog_reset_word_chars  (GtkWidget                 *button,
                                                               TerminalPreferencesDialog *dialog);
static void     terminal_preferences_dialog_background_mode   (GtkWidget                 *combobox,
                                                               TerminalPreferencesDialog *dialog);
static void     terminal_preferences_dialog_background_notify (GObject                   *object,
                                                               GParamSpec                *pspec,
                                                               GObject                   *widget);
static void     terminal_preferences_dialog_background_set    (GtkFileChooserButton      *widget,
                                                               TerminalPreferencesDialog *dialog);
static void     terminal_preferences_dialog_encoding_changed  (GtkComboBox               *combobox,
                                                               TerminalPreferencesDialog *dialog);
static gboolean monospace_filter                              (const PangoFontFamily     *family,
                                                               const PangoFontFace       *face,
                                                               gpointer                   data);



struct _TerminalPreferencesDialogClass
{
  GtkBuilderClass parent_class;
};

struct _TerminalPreferencesDialog
{
  GtkBuilder           parent_instance;

  TerminalPreferences *preferences;
  GSList              *bindings;

  gulong               bg_image_signal_id;
  gulong               palette_signal_id;
  gulong               geometry_signal_id;
};

enum
{
  PRESET_COLUMN_TITLE,
  PRESET_COLUMN_IS_SEPARATOR,
  PRESET_COLUMN_PATH,
  N_PRESET_COLUMNS
};



G_DEFINE_TYPE (TerminalPreferencesDialog, terminal_preferences_dialog, GTK_TYPE_BUILDER)



static void
terminal_preferences_dialog_class_init (TerminalPreferencesDialogClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = terminal_preferences_dialog_finalize;
}



#define BIND_PROPERTIES(name, property) \
  G_STMT_START { \
  object = gtk_builder_get_object (GTK_BUILDER (dialog), name); \
  terminal_return_if_fail (G_IS_OBJECT (object)); \
  binding = g_object_bind_property (G_OBJECT (dialog->preferences), name, \
                                    G_OBJECT (object), property, \
                                    G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL); \
  dialog->bindings = g_slist_prepend (dialog->bindings, binding); \
  } G_STMT_END



#define RESET_PROPERTIES(properties) \
  G_STMT_START { \
  guint i; \
  for (i = 0; i < G_N_ELEMENTS (properties); i++) \
    { \
      GParamSpec *spec = g_object_class_find_property (G_OBJECT_GET_CLASS (dialog->preferences), properties[i]); \
      if (G_LIKELY (spec != NULL)) \
        { \
          GValue value = { 0, }; \
          g_value_init (&value, spec->value_type); \
          g_param_value_set_default (spec, &value); \
          g_object_set_property (G_OBJECT (dialog->preferences), properties[i], &value); \
          g_value_unset (&value); \
        } \
    } \
  } G_STMT_END



static void
terminal_preferences_dialog_init (TerminalPreferencesDialog *dialog)
{
  GError           *error = NULL;
  guint             i;
  GObject          *object, *object2;
  gchar             palette_name[16];
  GtkFileFilter    *filter;
  GBinding         *binding;
  GtkTreeModel     *model;
  gchar            *current;
  GtkTreeIter       current_iter;
  const gchar      *props_active[] = { "title-mode", "command-login-shell",
                                       "command-update-records", "run-custom-command",
                                       "use-default-working-dir", "scrolling-on-output",
                                       "scrolling-on-keystroke", "scrolling-bar",
                                       "scrolling-unlimited", "misc-cursor-shape",
                                       "misc-cursor-blinks", "font-allow-bold",
                                       "font-use-system", "text-blink-mode",
                                       "misc-show-unsafe-paste-dialog", "misc-menubar-default",
                                       "misc-toolbar-default", "misc-borders-default",
                                       "misc-tab-close-middle-click", "misc-middle-click-opens-uri",
                                       "misc-mouse-autohide", "misc-rewrap-on-resize",
                                       "misc-copy-on-select", "misc-slim-tabs",
                                       "misc-new-tab-adjacent", "misc-bell",
                                       "misc-bell-urgent", "shortcuts-no-helpkey",
                                       "shortcuts-no-mnemonics", "shortcuts-no-menukey",
                                       "binding-backspace", "binding-delete",
                                       "binding-ambiguous-width", "background-mode",
                                       "background-image-style", "color-background-vary",
                                       "color-bold-is-bright", "color-use-theme",
                                       "dropdown-keep-open-default", "dropdown-keep-above",
                                       "dropdown-toggle-focus", "dropdown-status-icon",
                                       "dropdown-move-to-active", "dropdown-always-show-tabs",
                                       "dropdown-show-borders"
                                     };
  const gchar      *props_color[] =  { "color-foreground", "color-background",
                                       "tab-activity-color", "color-cursor-foreground",
                                       "color-cursor", "color-selection",
                                       "color-selection-background", "color-bold"
                                     };
  const gchar      *props_value[] =  { "dropdown-height", "dropdown-width",
                                       "dropdown-position", "dropdown-position-vertical",
                                       "dropdown-opacity", "dropdown-animation-time"
                                     };

  dialog->preferences = terminal_preferences_get ();

  if (!gtk_builder_add_from_string (GTK_BUILDER (dialog), terminal_preferences_ui,
                                    terminal_preferences_ui_length, &error)) {
      g_critical ("Error loading UI: %s", error->message);
      g_error_free (error);
      return;
  }

  /* connect response to dialog */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "dialog");
  terminal_return_if_fail (G_IS_OBJECT (object));
  g_object_weak_ref (object, (GWeakNotify) terminal_preferences_dialog_died, dialog);
  g_signal_connect (object, "destroy",
      G_CALLBACK (terminal_preferences_dialog_disc_bindings), dialog);
  g_signal_connect (object, "response",
      G_CALLBACK (terminal_preferences_dialog_response), dialog);

  /* bind active properties */
  for (i = 0; i < G_N_ELEMENTS (props_active); i++)
    BIND_PROPERTIES (props_active[i], "active");

  /* bind color properties and click handler */
  for (i = 0; i < G_N_ELEMENTS (props_color); i++)
    {
      BIND_PROPERTIES (props_color[i], "rgba");
      g_signal_connect (object, "button-press-event",
          G_CALLBACK (terminal_preferences_dialog_color_press_event), object);
    }

  /* bind value properties */
  for (i = 0; i < G_N_ELEMENTS (props_value); i++)
    BIND_PROPERTIES (props_value[i], "value");

  /* bind color palette properties */
  for (i = 1; i <= 16; i++)
    {
      g_snprintf (palette_name, sizeof (palette_name), "color-palette%d", i);
      object = gtk_builder_get_object (GTK_BUILDER (dialog), palette_name);
      terminal_return_if_fail (G_IS_OBJECT (object));
      g_signal_connect (object, "color-set",
          G_CALLBACK (terminal_preferences_dialog_palette_changed), dialog);

      /* don't show palette when editing colors */
      g_object_set (object, "show-editor", TRUE, NULL);
    }

  /* watch color changes in property */
  dialog->palette_signal_id = g_signal_connect_swapped (G_OBJECT (dialog->preferences),
      "notify::color-palette", G_CALLBACK (terminal_preferences_dialog_palette_notify), dialog);
  terminal_preferences_dialog_palette_notify (dialog);

  /* color presets */
  terminal_preferences_dialog_presets_load (dialog);

  /* other properties */
  BIND_PROPERTIES ("font-name", "font-name");
  BIND_PROPERTIES ("title-initial", "text");
  BIND_PROPERTIES ("custom-command", "text");
  BIND_PROPERTIES ("default-working-dir", "text");
  BIND_PROPERTIES ("word-chars", "text");
  BIND_PROPERTIES ("scrolling-lines", "value");
  BIND_PROPERTIES ("tab-activity-timeout", "value");
  BIND_PROPERTIES ("background-darkness", "value");
  BIND_PROPERTIES ("background-image-shading", "value");

#ifndef HAVE_LIBUTEMPTER
  /* hide "Update utmp/wtmp records" if no support for that */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "command-update-records");
  terminal_return_if_fail (G_IS_OBJECT (object));
  gtk_widget_hide (GTK_WIDGET (object));
#endif

#if VTE_CHECK_VERSION (0, 51, 3)
  /* bind cell width scale */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "spin-cell-width-scale");
  object2 = G_OBJECT (gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (object)));
  terminal_return_if_fail (G_IS_OBJECT (object) && G_IS_OBJECT (object2));
  binding = g_object_bind_property (G_OBJECT (dialog->preferences), "cell-width-scale",
                                    G_OBJECT (object2), "value",
                                    G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  dialog->bindings = g_slist_prepend (dialog->bindings, binding);

  /* bind cell height scale */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "spin-cell-height-scale");
  object2 = G_OBJECT (gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (object)));
  terminal_return_if_fail (G_IS_OBJECT (object) && G_IS_OBJECT (object2));
  binding = g_object_bind_property (G_OBJECT (dialog->preferences), "cell-height-scale",
                                    G_OBJECT (object2), "value",
                                    G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  dialog->bindings = g_slist_prepend (dialog->bindings, binding);

  /* cell scale "Reset" button */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "reset-cell-scale");
  terminal_return_if_fail (G_IS_OBJECT (object));
  g_signal_connect (object, "clicked",
                    G_CALLBACK (terminal_preferences_dialog_reset_cell_scale), dialog);
#else
  /* hide "Text blinks" if vte doesn't support it */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "box-text-blink");
  terminal_return_if_fail (G_IS_OBJECT (object));
  gtk_widget_hide (GTK_WIDGET (object));

  /* hide "Cell spacing" if vte doesn't support it */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "cell-sp-box");
  terminal_return_if_fail (G_IS_OBJECT (object));
  gtk_widget_hide (GTK_WIDGET (object));

  /* hide "Bold is bright" if vte doesn't support it */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "color-bold-is-bright");
  terminal_return_if_fail (G_IS_OBJECT (object));
  gtk_widget_hide (GTK_WIDGET (object));
#endif

#if VTE_CHECK_VERSION (0, 58, 0)
  /* hide "Rewrap on resize" if vte's support for it has been dropped */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "misc-rewrap-on-resize");
  terminal_return_if_fail (G_IS_OBJECT (object));
  gtk_widget_hide (GTK_WIDGET (object));
#endif

  /* run custom command button */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "run-custom-command");
  object2 = gtk_builder_get_object (GTK_BUILDER (dialog), "hbox3");
  terminal_return_if_fail (G_IS_OBJECT (object) && G_IS_OBJECT (object2));
  g_object_bind_property (object, "active",
                          object2, "sensitive",
                          G_BINDING_SYNC_CREATE);

  /* working directory button */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "use-default-working-dir");
  object2 = gtk_builder_get_object (GTK_BUILDER (dialog), "default-working-dir");
  terminal_return_if_fail (G_IS_OBJECT (object) && G_IS_OBJECT (object2));
  g_object_bind_property (object, "active",
                          object2, "sensitive",
                          G_BINDING_SYNC_CREATE);

  /* unlimited scrollback button */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "scrolling-unlimited");
  object2 = gtk_builder_get_object (GTK_BUILDER (dialog), "scrolling-lines");
  terminal_return_if_fail (G_IS_OBJECT (object) && G_IS_OBJECT (object2));
  g_object_bind_property (object, "active",
                          object2, "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);

  /* use system font button */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "font-use-system");
  object2 = gtk_builder_get_object (GTK_BUILDER (dialog), "font-name");
  terminal_return_if_fail (G_IS_OBJECT (object) && G_IS_OBJECT (object2));
  gtk_font_chooser_set_filter_func (GTK_FONT_CHOOSER (object2), monospace_filter, NULL, NULL);
  g_object_bind_property (object, "active",
                          object2, "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);

  /* reset comparibility button */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "reset-compatibility");
  terminal_return_if_fail (G_IS_OBJECT (object));
  g_signal_connect (object, "clicked",
      G_CALLBACK (terminal_preferences_dialog_reset_compat), dialog);

  /* reset word-chars button */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "reset-word-chars");
  terminal_return_if_fail (G_IS_OBJECT (object));
  g_signal_connect (object, "clicked",
      G_CALLBACK (terminal_preferences_dialog_reset_word_chars), dialog);

  /* position scale */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "scale-position");
  terminal_return_if_fail (G_IS_OBJECT (object));
  for (i = 0; i <= 100; i += 25)
    gtk_scale_add_mark (GTK_SCALE (object), i, GTK_POS_BOTTOM, NULL);

  /* inverted custom colors and set sensitivity */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "color-use-theme");
  terminal_return_if_fail (G_IS_OBJECT (object));
  object2 = gtk_builder_get_object (GTK_BUILDER (dialog), "color-foreground");
  terminal_return_if_fail (G_IS_OBJECT (object2));
  g_object_bind_property (object, "active",
                          object2, "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);
  object2 = gtk_builder_get_object (GTK_BUILDER (dialog), "color-background");
  terminal_return_if_fail (G_IS_OBJECT (object2));
  g_object_bind_property (object, "active",
                          object2, "sensitive",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE);

  object = gtk_builder_get_object (GTK_BUILDER (dialog), "color-cursor-custom");
  terminal_return_if_fail (G_IS_OBJECT (object));
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-cursor-use-default",
                          object, "active",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  object2 = gtk_builder_get_object (GTK_BUILDER (dialog), "color-cursor-foreground");
  terminal_return_if_fail (G_IS_OBJECT (object2));
  g_object_bind_property (object, "active",
                          object2, "sensitive",
                          G_BINDING_SYNC_CREATE);
  object2 = gtk_builder_get_object (GTK_BUILDER (dialog), "color-cursor");
  terminal_return_if_fail (G_IS_OBJECT (object2));
  g_object_bind_property (object, "active",
                          object2, "sensitive",
                          G_BINDING_SYNC_CREATE);

  object = gtk_builder_get_object (GTK_BUILDER (dialog), "color-selection-custom");
  terminal_return_if_fail (G_IS_OBJECT (object));
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-selection-use-default",
                          object, "active",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  object2 = gtk_builder_get_object (GTK_BUILDER (dialog), "color-selection");
  terminal_return_if_fail (G_IS_OBJECT (object2));
  g_object_bind_property (object, "active",
                          object2, "sensitive",
                          G_BINDING_SYNC_CREATE);
  object2 = gtk_builder_get_object (GTK_BUILDER (dialog), "color-selection-background");
  terminal_return_if_fail (G_IS_OBJECT (object2));
  g_object_bind_property (object, "active",
                          object2, "sensitive",
                          G_BINDING_SYNC_CREATE);

  object = gtk_builder_get_object (GTK_BUILDER (dialog), "color-bold-custom");
  terminal_return_if_fail (G_IS_OBJECT (object));
  g_object_bind_property (G_OBJECT (dialog->preferences), "color-bold-use-default",
                          object, "active",
                          G_BINDING_INVERT_BOOLEAN | G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
  object2 = gtk_builder_get_object (GTK_BUILDER (dialog), "color-bold");
  terminal_return_if_fail (G_IS_OBJECT (object2));
  g_object_bind_property (object, "active",
                          object2, "sensitive",
                          G_BINDING_SYNC_CREATE);

#ifdef GDK_WINDOWING_X11
  terminal_preferences_dialog_geometry_changed (dialog);
  dialog->geometry_signal_id = g_signal_connect_swapped (G_OBJECT (dialog->preferences),
      "notify::misc-default-geometry",
      G_CALLBACK (terminal_preferences_dialog_geometry_changed), dialog);

  /* geo changes */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "geo-columns");
  terminal_return_if_fail (G_IS_OBJECT (object));
  g_signal_connect (object, "value-changed",
      G_CALLBACK (terminal_preferences_dialog_geometry_columns), dialog);
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "geo-rows");
  terminal_return_if_fail (G_IS_OBJECT (object));
  g_signal_connect (object, "value-changed",
      G_CALLBACK (terminal_preferences_dialog_geometry_rows), dialog);
#else
  /* hide */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "geo-box");
  terminal_return_if_fail (G_IS_OBJECT (object));
  gtk_widget_hide (GTK_WIDGET (object));
#endif

  /* background widgets visibility */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "background-mode");
  terminal_return_if_fail (G_IS_OBJECT (object));
  g_signal_connect (object, "changed",
      G_CALLBACK (terminal_preferences_dialog_background_mode), dialog);
  terminal_preferences_dialog_background_mode (GTK_WIDGET (object), dialog);

  /* background image file */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "background-image-file");
  terminal_return_if_fail (G_IS_OBJECT (object));
  dialog->bg_image_signal_id = g_signal_connect (G_OBJECT (dialog->preferences),
      "notify::background-image-file", G_CALLBACK (terminal_preferences_dialog_background_notify), object);
  terminal_preferences_dialog_background_notify (G_OBJECT (dialog->preferences), NULL, object);
  g_signal_connect (object, "file-set",
      G_CALLBACK (terminal_preferences_dialog_background_set), dialog);

  /* add file filters */
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (object), filter);

  /* add "Image Files" filter */
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Image Files"));
  gtk_file_filter_add_pixbuf_formats (filter);
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (object), filter);
  gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (object), filter);

  /* encoding combo */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "encoding-combo");
  g_object_get (dialog->preferences, "encoding", &current, NULL);
  model = terminal_encoding_model_new (current, &current_iter);
  gtk_combo_box_set_model (GTK_COMBO_BOX (object), model);
  gtk_combo_box_set_active_iter (GTK_COMBO_BOX (object), &current_iter);
  g_signal_connect (object, "changed",
      G_CALLBACK (terminal_preferences_dialog_encoding_changed), dialog);
  g_object_unref (G_OBJECT (model));
  g_free (current);
}



static void
terminal_preferences_dialog_finalize (GObject *object)
{
  TerminalPreferencesDialog *dialog = TERMINAL_PREFERENCES_DIALOG (object);

  /* disconnect signals */
  if (G_LIKELY (dialog->bg_image_signal_id != 0))
    g_signal_handler_disconnect (dialog->preferences, dialog->bg_image_signal_id);
  if (G_LIKELY (dialog->palette_signal_id != 0))
    g_signal_handler_disconnect (dialog->preferences, dialog->palette_signal_id);
  if (G_LIKELY (dialog->geometry_signal_id != 0))
    g_signal_handler_disconnect (dialog->preferences, dialog->geometry_signal_id);

  /* release the preferences */
  g_object_unref (G_OBJECT (dialog->preferences));

  (*G_OBJECT_CLASS (terminal_preferences_dialog_parent_class)->finalize) (object);
}



static void
terminal_preferences_dialog_disc_bindings (GtkWidget                 *widget,
                                           TerminalPreferencesDialog *dialog)
{
  GSList *li;

  /* disconnect all the bindings */
  for (li = dialog->bindings; li != NULL; li = li->next)
    g_object_unref (G_OBJECT (li->data));
  g_slist_free (dialog->bindings);
}



static void
terminal_preferences_dialog_died (gpointer  user_data,
                                  GObject  *where_the_object_was)
{
  g_object_unref (G_OBJECT (user_data));
}



static gboolean
terminal_preferences_dialog_color_press_event (GtkWidget      *widget,
                                               GdkEventButton *event)
{
  gboolean show_editor;

  if (event->type == GDK_BUTTON_PRESS && event->button == 1 && event->state == GDK_CONTROL_MASK)
    {
      /* use Ctrl+click to open color editor directly */
      g_object_get (G_OBJECT (widget), "show-editor", &show_editor, NULL);
      g_object_set (G_OBJECT (widget), "show-editor", TRUE, NULL);
      gtk_button_clicked (GTK_BUTTON (widget));
      g_object_set (G_OBJECT (widget), "show-editor", show_editor, NULL);
      return TRUE;
    }

  return FALSE;
}



static void
terminal_preferences_dialog_response (GtkWidget                 *widget,
                                      gint                       response,
                                      TerminalPreferencesDialog *dialog)
{
  GObject     *object;
  GObject     *notebook;
  const gchar *section;

  /* check if we should open the user manual */
  if (G_UNLIKELY (response == 1))
    {
      /* if the drop-down preferences are shown, we open that page in the wiki */
      notebook = gtk_builder_get_object (GTK_BUILDER (dialog), "notebook");
      terminal_return_if_fail (GTK_IS_NOTEBOOK (notebook));
      object = gtk_builder_get_object (GTK_BUILDER (dialog), "dropdown-box");
      terminal_return_if_fail (G_IS_OBJECT (object));
      if (gtk_notebook_page_num (GTK_NOTEBOOK (notebook), GTK_WIDGET (object))
          == gtk_notebook_get_current_page (GTK_NOTEBOOK (notebook)))
        section = "dropdown";
      else
        section = "preferences";

      /* open the "Preferences" section of the user manual */
      xfce_dialog_show_help (GTK_WINDOW (widget), "terminal",
                             section, NULL);
    }
  else
    {
      /* close the preferences dialog */
      gtk_widget_destroy (widget);
    }
}



#ifdef GDK_WINDOWING_X11
static void
terminal_preferences_dialog_geometry_changed (TerminalPreferencesDialog *dialog)
{
  GObject *object;
  gchar   *geo;
  guint    w = 0, h = 0;
  gint     x, y;

  g_object_get (G_OBJECT (dialog->preferences), "misc-default-geometry", &geo, NULL);
  if (G_LIKELY (geo != NULL))
    {
      /* parse the string */
      XParseGeometry (geo, &x, &y, &w, &h);
      g_free (geo);
    }

  /* set cols */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "geo-columns");
  terminal_return_if_fail (GTK_IS_ADJUSTMENT (object));
  g_signal_handlers_block_by_func (object,
      terminal_preferences_dialog_geometry_columns, dialog);
  gtk_adjustment_set_value (GTK_ADJUSTMENT (object), w != 0 ? w : 80);
  g_signal_handlers_unblock_by_func (object,
      terminal_preferences_dialog_geometry_columns, dialog);

  /* set rows */
  object = gtk_builder_get_object (GTK_BUILDER (dialog), "geo-rows");
  terminal_return_if_fail (GTK_IS_ADJUSTMENT (object));
  g_signal_handlers_block_by_func (object,
      terminal_preferences_dialog_geometry_columns, dialog);
  gtk_adjustment_set_value (GTK_ADJUSTMENT (object), h != 0 ? h : 24);
  g_signal_handlers_unblock_by_func (object,
      terminal_preferences_dialog_geometry_columns, dialog);
}



static void
terminal_preferences_dialog_geometry (TerminalPreferencesDialog *dialog,
                                      gint                       columns,
                                      gint                       rows)
{
  gint     x, y;
  guint    w = 0, h = 0;
  gchar   *geo;
  gint     mask = NoValue;

  g_object_get (G_OBJECT (dialog->preferences), "misc-default-geometry", &geo, NULL);
  if (G_LIKELY (geo != NULL))
    {
      /* parse the string */
      mask = XParseGeometry (geo, &x, &y, &w, &h);
      g_free (geo);
    }

  /* set new value */
  if (columns > 0)
    w = columns;
  if (rows > 0)
    h = rows;

  /* if there is an x or y value, preserve this */
  if ((mask & XValue) != 0 || (mask & YValue) != 0)
    geo = g_strdup_printf ("%ux%u%+d%+d", w, h, x, y);
  else
    geo = g_strdup_printf ("%ux%u", w, h);

  /* save */
  g_signal_handler_block (G_OBJECT (dialog->preferences), dialog->geometry_signal_id);
  g_object_set (G_OBJECT (dialog->preferences), "misc-default-geometry", geo, NULL);
  g_signal_handler_unblock (G_OBJECT (dialog->preferences), dialog->geometry_signal_id);
  g_free (geo);
}



static void
terminal_preferences_dialog_geometry_columns (GtkAdjustment             *adj,
                                              TerminalPreferencesDialog *dialog)
{
  terminal_return_if_fail (GTK_IS_ADJUSTMENT (adj));
  terminal_preferences_dialog_geometry (dialog, gtk_adjustment_get_value (adj), -1);
}



static void
terminal_preferences_dialog_geometry_rows (GtkAdjustment             *adj,
                                           TerminalPreferencesDialog *dialog)
{
  terminal_return_if_fail (GTK_IS_ADJUSTMENT (adj));
  terminal_preferences_dialog_geometry (dialog, -1, gtk_adjustment_get_value (adj));
}
#endif



static void
terminal_preferences_dialog_palette_changed (GtkWidget                 *button,
                                             TerminalPreferencesDialog *dialog)
{
  gchar    name[16];
  guint    i;
  GObject *obj;
  GdkRGBA  color;
  gchar   *color_str;
  GString *array;

  array = g_string_sized_new (225);

  for (i = 1; i <= 16; i++)
    {
      /* get color value from button */
      g_snprintf (name, sizeof (name), "color-palette%d", i);
      obj = gtk_builder_get_object (GTK_BUILDER (dialog), name);
      terminal_return_if_fail (GTK_IS_COLOR_BUTTON (obj));
      gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (obj), &color);

      /* append to string */
      color_str = gdk_rgba_to_string (&color);
      g_string_append (array, color_str);
      g_free (color_str);

      if (i != 16)
        g_string_append_c (array, ';');
    }

  /* set property */
  g_signal_handler_block (dialog->preferences, dialog->palette_signal_id);
  g_object_set (dialog->preferences, "color-palette", array->str, NULL);
  g_signal_handler_unblock (dialog->preferences, dialog->palette_signal_id);
  g_string_free (array, TRUE);
}



static void
terminal_preferences_dialog_palette_notify (TerminalPreferencesDialog *dialog)
{
  gchar   *color_str;
  gchar  **colors;
  guint    i;
  gchar    name[16];
  GObject *obj;
  GdkRGBA  color;

  g_object_get (dialog->preferences, "color-palette", &color_str, NULL);
  if (G_LIKELY (color_str != NULL))
    {
      /* make array */
      colors = g_strsplit (color_str, ";", -1);
      g_free (color_str);

      /* apply values to buttons */
      if (colors != NULL)
        for (i = 0; i < 16 && colors[i] != NULL; i++)
          {
            g_snprintf (name, sizeof (name), "color-palette%d", i + 1);
            obj = gtk_builder_get_object (GTK_BUILDER (dialog), name);
            terminal_return_if_fail (GTK_IS_COLOR_BUTTON (obj));

            if (gdk_rgba_parse (&color, colors[i]))
              gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (obj), &color);
          }

      g_strfreev (colors);
    }
}



static gboolean
terminal_preferences_dialog_presets_sepfunc (GtkTreeModel *model,
                                             GtkTreeIter  *iter,
                                             gpointer      user_data)
{
  gboolean is_separator;
  gtk_tree_model_get (model, iter, PRESET_COLUMN_IS_SEPARATOR, &is_separator, -1);
  return is_separator;
}



static void
terminal_preferences_dialog_presets_changed (GtkComboBox               *combobox,
                                             TerminalPreferencesDialog *dialog)
{
  GtkTreeModel *model;
  GtkTreeIter   iter;
  gchar        *path;
  XfceRc       *rc;
  GParamSpec  **pspecs, *pspec;
  guint         nspecs;
  guint         n;
  const gchar  *blurb;
  const gchar  *name;
  const gchar  *str;
  GValue        src = { 0, };
  GValue        dst = { 0, };

  if (!gtk_combo_box_get_active_iter (combobox, &iter))
    return;

  model = gtk_combo_box_get_model (combobox);
  gtk_tree_model_get (model, &iter, PRESET_COLUMN_PATH, &path, -1);
  if (path == NULL)
    return;

  /* load file */
  rc = xfce_rc_simple_open (path, TRUE);
  g_free (path);
  if (G_UNLIKELY (rc == NULL))
    return;

  xfce_rc_set_group (rc, "Scheme");

  g_value_init (&src, G_TYPE_STRING);

  /* walk all properties and look for items in the scheme */
  pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (dialog->preferences), &nspecs);
  for (n = 0; n < nspecs; ++n)
    {
      pspec = pspecs[n];

      /* get color keys */
      blurb = g_param_spec_get_blurb (pspec);
      if (strstr (blurb, "Color") == NULL)
        continue;

      /* read value */
      name = g_param_spec_get_name (pspec);
      str = xfce_rc_read_entry_untranslated (rc, blurb, NULL);

      if (str == NULL || *str == '\0')
        {
          /* reset to the default value */
          g_value_init (&dst, G_PARAM_SPEC_VALUE_TYPE (pspec));
          g_param_value_set_default (pspec, &dst);
          g_object_set_property (G_OBJECT (dialog->preferences), name, &dst);
          g_value_unset (&dst);
        }
      else
        {
          g_value_set_static_string (&src, str);

          if (G_PARAM_SPEC_VALUE_TYPE (pspec) == G_TYPE_STRING)
            {
              /* set the string property */
              g_object_set_property (G_OBJECT (dialog->preferences), name, &src);
            }
          else
            {
              /* transform value */
              g_value_init (&dst, G_PARAM_SPEC_VALUE_TYPE (pspec));
              if (G_LIKELY (g_value_transform (&src, &dst)))
                g_object_set_property (G_OBJECT (dialog->preferences), name, &dst);
              else
                g_warning ("Unable to convert scheme property \"%s\"", name);
              g_value_unset (&dst);
            }
        }
    }

  g_free (pspecs);
  g_value_unset (&src);
  xfce_rc_close (rc);
}



static void
terminal_preferences_dialog_presets_load (TerminalPreferencesDialog *dialog)
{
  gchar       **global, **user, **presets;
  guint         n_global, n_user, n_presets = 0, n;
  GObject      *object;
  XfceRc       *rc;
  GtkListStore *store;
  GtkTreeIter   iter;
  const gchar  *title;
  gchar        *path;

  /* load schemes */
  global = xfce_resource_match (XFCE_RESOURCE_DATA, "xfce4/terminal/colorschemes/*", TRUE);
  user = xfce_resource_match (XFCE_RESOURCE_CONFIG, "xfce4/terminal/colorschemes/*", TRUE);
  n_global = g_strv_length (global);
  n_user = g_strv_length (user);
  presets = g_new0 (gchar *, n_global + n_user);
  if (G_LIKELY (presets != NULL))
    {
      /* copy pointers to global- and user-defined presets */
      for (n = 0; n < n_global; n++)
        presets[n] = global[n];
      for (n = 0; n < n_user; n++)
        presets[n_global + n] = user[n];

      /* create sorting store */
      store = gtk_list_store_new (N_PRESET_COLUMNS, G_TYPE_STRING,
                                  G_TYPE_BOOLEAN, G_TYPE_STRING);
      gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
                                            PRESET_COLUMN_TITLE,
                                            GTK_SORT_ASCENDING);

      /* append files */
      for (n = 0; n < n_global + n_user && presets[n] != NULL; n++)
        {
          /* open the scheme */
          path = xfce_resource_lookup (n < n_global ? XFCE_RESOURCE_DATA : XFCE_RESOURCE_CONFIG,
                                       presets[n]);
          if (G_UNLIKELY (path == NULL))
            continue;

          rc = xfce_rc_simple_open (path, TRUE);
          if (G_UNLIKELY (rc == NULL))
            {
              g_free (path);
              continue;
            }

          xfce_rc_set_group (rc, "Scheme");

          /* translated name */
          title = xfce_rc_read_entry (rc, "Name", NULL);
          if (G_LIKELY (title != NULL))
            {
              gtk_list_store_insert_with_values (store, NULL, n_presets++,
                                                 PRESET_COLUMN_TITLE, title,
                                                 PRESET_COLUMN_PATH, path,
                                                 -1);
            }

          xfce_rc_close (rc);
          g_free (path);
        }

      /* stop sorting */
      gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
                                            GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,
                                            GTK_SORT_ASCENDING);

      /* default item + separator */
      gtk_list_store_insert_with_values (store, &iter, 0,
                                         PRESET_COLUMN_TITLE, _("Load Presets..."),
                                         -1);
      gtk_list_store_insert_with_values (store, NULL, 1,
                                         PRESET_COLUMN_IS_SEPARATOR, TRUE,
                                         -1);

      /* set model */
      object = gtk_builder_get_object (GTK_BUILDER (dialog), "color-presets");
      terminal_return_if_fail (GTK_IS_COMBO_BOX (object));
      gtk_combo_box_set_model (GTK_COMBO_BOX (object), GTK_TREE_MODEL (store));
      gtk_combo_box_set_active_iter  (GTK_COMBO_BOX (object), &iter);
      gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (object),
          terminal_preferences_dialog_presets_sepfunc, NULL, NULL);
      g_signal_connect (object, "changed",
          G_CALLBACK (terminal_preferences_dialog_presets_changed), dialog);
      g_object_unref (store);
    }

  g_strfreev (global);
  g_strfreev (user);
  g_free (presets);

  if (n_presets == 0)
    {
      /* hide frame + combo */
      object = gtk_builder_get_object (GTK_BUILDER (dialog), "color-presets-frame");
      terminal_return_if_fail (GTK_IS_WIDGET (object));
      gtk_widget_hide (GTK_WIDGET (object));
    }
}



#if VTE_CHECK_VERSION (0, 51, 3)
static void
terminal_preferences_dialog_reset_cell_scale (GtkWidget                 *button,
                                              TerminalPreferencesDialog *dialog)
{
  const gchar *properties[] = { "cell-width-scale", "cell-height-scale" };
  RESET_PROPERTIES (properties);
}
#endif



static void
terminal_preferences_dialog_reset_compat (GtkWidget                 *button,
                                          TerminalPreferencesDialog *dialog)
{
  const gchar *properties[] = { "binding-backspace", "binding-delete", "binding-ambiguous-width" };
  RESET_PROPERTIES (properties);
}



static void
terminal_preferences_dialog_reset_word_chars (GtkWidget                 *button,
                                              TerminalPreferencesDialog *dialog)
{
  const gchar *properties[] = { "word-chars" };
  RESET_PROPERTIES (properties);
}



static void
terminal_preferences_dialog_background_mode (GtkWidget                 *combobox,
                                             TerminalPreferencesDialog *dialog)
{
  GObject  *object;
  gint      active;
  gboolean  composited;

  terminal_return_if_fail (TERMINAL_IS_PREFERENCES_DIALOG (dialog));
  terminal_return_if_fail (GTK_IS_COMBO_BOX (combobox));

  active = gtk_combo_box_get_active (GTK_COMBO_BOX (combobox));
  composited = gdk_screen_is_composited (gtk_widget_get_screen (combobox));

  object = gtk_builder_get_object (GTK_BUILDER (dialog), "box-file");
  terminal_return_if_fail (G_IS_OBJECT (object));
  g_object_set (object, "visible", active == 1, NULL);

  object = gtk_builder_get_object (GTK_BUILDER (dialog), "box-shading");
  terminal_return_if_fail (G_IS_OBJECT (object));
  g_object_set (object, "visible", active == 1, NULL);

  object = gtk_builder_get_object (GTK_BUILDER (dialog), "box-opacity");
  terminal_return_if_fail (G_IS_OBJECT (object));
  g_object_set (object, "visible", active > 1, NULL);
  g_object_set (object, "sensitive", active > 1 && composited, NULL);

  object = gtk_builder_get_object (GTK_BUILDER (dialog), "label-opacity-not-available");
  terminal_return_if_fail (G_IS_OBJECT (object));
  g_object_set (object, "visible", active > 1 && !composited, NULL);

}



static void
terminal_preferences_dialog_background_notify (GObject    *object,
                                               GParamSpec *pspec,
                                               GObject    *widget)
{
  gchar *button_file, *prop_file;

  terminal_return_if_fail (TERMINAL_IS_PREFERENCES (object));
  terminal_return_if_fail (GTK_IS_FILE_CHOOSER_BUTTON (widget));

  button_file = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
  g_object_get (object, "background-image-file", &prop_file, NULL);
  if (g_strcmp0 (button_file, prop_file) != 0)
    gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (widget), prop_file);
  g_free (button_file);
  g_free (prop_file);
}



static void
terminal_preferences_dialog_background_set (GtkFileChooserButton      *widget,
                                            TerminalPreferencesDialog *dialog)
{
  gchar *filename;

  terminal_return_if_fail (TERMINAL_IS_PREFERENCES_DIALOG (dialog));
  terminal_return_if_fail (GTK_IS_FILE_CHOOSER_BUTTON (widget));

  filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
  g_object_set (G_OBJECT (dialog->preferences), "background-image-file", filename, NULL);
  g_free (filename);
}



static void
terminal_preferences_dialog_encoding_changed (GtkComboBox               *combobox,
                                              TerminalPreferencesDialog *dialog)
{
  GtkTreeIter   iter;
  gchar        *encoding;
  GtkTreeModel *model;
  gboolean      is_charset;
  GtkTreeIter   child_iter;

  if (gtk_combo_box_get_active_iter (combobox, &iter))
    {
      model = gtk_combo_box_get_model (combobox);
      gtk_tree_model_get (model, &iter,
                          ENCODING_COLUMN_IS_CHARSET, &is_charset,
                          ENCODING_COLUMN_VALUE, &encoding, -1);

      /* select the child if a menu header is clicked */
      if (encoding == NULL && !is_charset)
        {
          if (gtk_tree_model_iter_children (model, &child_iter, &iter))
            gtk_combo_box_set_active_iter (combobox, &child_iter);
          return;
        }

      /* save new default */
      g_object_set (dialog->preferences, "encoding", encoding, NULL);
      g_free (encoding);
    }
}



static gboolean
monospace_filter (const PangoFontFamily *family,
                  const PangoFontFace   *face,
                  gpointer data)
{
  return pango_font_family_is_monospace ((PangoFontFamily *) family);
}



/**
 * terminal_preferences_dialog_new:
 *
 * Return value :
 **/
GtkWidget*
terminal_preferences_dialog_new (gboolean show_drop_down,
                                 gboolean drop_down_mode)
{
  static GtkBuilder *builder = NULL;

  GObject *dialog;
  GObject *object;
  GObject *notebook;

  if (builder == NULL)
    {
      builder = g_object_new (TERMINAL_TYPE_PREFERENCES_DIALOG, NULL);
      g_object_add_weak_pointer (G_OBJECT (builder), (gpointer) &builder);
    }

  object = gtk_builder_get_object (builder, "dropdown-box");
  terminal_return_val_if_fail (GTK_IS_WIDGET (object), NULL);
  gtk_widget_set_visible (GTK_WIDGET (object), show_drop_down);

  /* focus the drop-down tab if in drop-down mode */
  if (show_drop_down && drop_down_mode)
    {
      notebook = gtk_builder_get_object (builder, "notebook");
      terminal_return_val_if_fail (GTK_IS_NOTEBOOK (notebook), NULL);
      gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook),
          gtk_notebook_page_num (GTK_NOTEBOOK (notebook), GTK_WIDGET (object)));
    }

  /* show warning and disable control if WM does not support compositing */
  if (show_drop_down && !gdk_screen_is_composited (gtk_widget_get_screen (GTK_WIDGET (object))))
    {
      object = gtk_builder_get_object (builder, "dropdown-opacity-not-available");
      terminal_return_val_if_fail (G_IS_OBJECT (object), NULL);
      gtk_widget_set_visible (GTK_WIDGET (object), TRUE);
      object = gtk_builder_get_object (builder, "scale-opacity");
      terminal_return_val_if_fail (G_IS_OBJECT (object), NULL);
      gtk_widget_set_sensitive (GTK_WIDGET (object), FALSE);
    }

  dialog = gtk_builder_get_object (builder, "dialog");
  terminal_return_val_if_fail (XFCE_IS_TITLED_DIALOG (dialog), NULL);
  gtk_window_set_type_hint (GTK_WINDOW (dialog), GDK_WINDOW_TYPE_HINT_DIALOG);
  return GTK_WIDGET (dialog);
}
