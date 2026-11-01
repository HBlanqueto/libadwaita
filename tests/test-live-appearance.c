/*
 * Copyright (C) 2026 Malaysia contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <adwaita.h>

#include "adw-live-appearance-private.h"

#define LIVE_CSS \
  "@media (prefers-color-scheme: light) {\n" \
  "  @define-color accent_bg_color #ff0000;\n" \
  "  @define-color window_bg_color #fff0f0;\n" \
  "}\n" \
  "@media (prefers-color-scheme: dark) {\n" \
  "  @define-color accent_bg_color #0000ff;\n" \
  "  @define-color window_bg_color #10102a;\n" \
  "}\n"

static void
increment (int *data)
{
  (*data)++;
}

static void
assert_named_color (GtkWidget  *widget,
                    const char *name,
                    const char *expected)
{
  GtkStyleContext *context = gtk_widget_get_style_context (widget);
  GdkRGBA color, expected_color;

  g_assert_true (gdk_rgba_parse (&expected_color, expected));
  g_assert_true (gtk_style_context_lookup_color (context, name, &color));
  g_assert_true (gdk_rgba_equal (&color, &expected_color));
}

static void
test_accent_rgba (void)
{
  AdwStyleManager *manager = adw_style_manager_get_default ();
  AdwLiveAppearance *live = adw_live_appearance_get_default ();
  GdkRGBA exact = { 0.2, 0.4, 0.6, 1 };
  GdkRGBA preset;
  GdkRGBA *rgba;
  int notified = 0;

  g_signal_connect_swapped (manager, "notify::accent-color-rgba",
                            G_CALLBACK (increment), &notified);

  adw_accent_color_to_rgba (adw_style_manager_get_accent_color (manager), &preset);

  rgba = adw_style_manager_get_accent_color_rgba (manager);
  g_assert_true (gdk_rgba_equal (rgba, &preset));
  gdk_rgba_free (rgba);

  adw_live_appearance_set_for_testing (live, NULL, &exact);
  g_assert_cmpint (notified, ==, 1);

  rgba = adw_style_manager_get_accent_color_rgba (manager);
  g_assert_true (gdk_rgba_equal (rgba, &exact));
  gdk_rgba_free (rgba);

  /* The same values again change nothing. */
  adw_live_appearance_set_for_testing (live, NULL, &exact);
  g_assert_cmpint (notified, ==, 1);

  adw_live_appearance_set_for_testing (live, NULL, NULL);
  g_assert_cmpint (notified, ==, 2);

  rgba = adw_style_manager_get_accent_color_rgba (manager);
  g_assert_true (gdk_rgba_equal (rgba, &preset));
  gdk_rgba_free (rgba);

  g_signal_handlers_disconnect_by_func (manager, increment, &notified);
}

static void
test_stylesheet (void)
{
  AdwStyleManager *manager = adw_style_manager_get_default ();
  AdwLiveAppearance *live = adw_live_appearance_get_default ();
  GtkWidget *window = gtk_window_new ();
  GtkWidget *label = gtk_label_new ("");
  GdkRGBA stock;

  gtk_window_set_child (GTK_WINDOW (window), label);

  adw_style_manager_set_color_scheme (manager, ADW_COLOR_SCHEME_FORCE_LIGHT);
  g_assert_true (gtk_style_context_lookup_color (gtk_widget_get_style_context (label),
                                                 "window_bg_color", &stock));

  /* A stale copy at the user priority, as the gtk.css bridge leaves it. */
  {
    GtkCssProvider *user = gtk_css_provider_new ();

    gtk_css_provider_load_from_string (user, "@define-color accent_bg_color #00ff00;");
    gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                                GTK_STYLE_PROVIDER (user),
                                                GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref (user);
  }

  adw_live_appearance_set_for_testing (live, LIVE_CSS, NULL);
  assert_named_color (label, "accent_bg_color", "#ff0000");
  assert_named_color (label, "window_bg_color", "#fff0f0");

  /* The scheme the application forces picks the block. */
  adw_style_manager_set_color_scheme (manager, ADW_COLOR_SCHEME_FORCE_DARK);
  assert_named_color (label, "accent_bg_color", "#0000ff");
  assert_named_color (label, "window_bg_color", "#10102a");

  /* Nothing published: the stock colours come back. */
  adw_style_manager_set_color_scheme (manager, ADW_COLOR_SCHEME_FORCE_LIGHT);
  adw_live_appearance_set_for_testing (live, "", NULL);
  assert_named_color (label, "accent_bg_color", "#00ff00");
  {
    GdkRGBA color;

    g_assert_true (gtk_style_context_lookup_color (gtk_widget_get_style_context (label),
                                                   "window_bg_color", &color));
    g_assert_true (gdk_rgba_equal (&color, &stock));
  }

  adw_style_manager_set_color_scheme (manager, ADW_COLOR_SCHEME_DEFAULT);
  gtk_window_destroy (GTK_WINDOW (window));
}

int
main (int   argc,
      char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);
  adw_init ();

  g_test_add_func ("/Adwaita/LiveAppearance/accent_rgba", test_accent_rgba);
  g_test_add_func ("/Adwaita/LiveAppearance/stylesheet", test_stylesheet);

  return g_test_run ();
}
