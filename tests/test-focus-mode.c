/*
 * Copyright (C) 2026 Malaysia contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <adwaita.h>

#include "adw-focus-mode-private.h"
#include "adw-toolbar-view-private.h"

typedef struct {
  GtkWindow *window;
  AdwToolbarView *view;
} Fixture;

static AdwToolbarView *
toolbar_view_new (gboolean top_bar)
{
  AdwToolbarView *view = ADW_TOOLBAR_VIEW (adw_toolbar_view_new ());

  if (top_bar)
    adw_toolbar_view_add_top_bar (view, adw_header_bar_new ());

  return view;
}

static void
fixture_setup (Fixture       *fixture,
               gconstpointer  data)
{
  fixture->window = GTK_WINDOW (gtk_window_new ());
  fixture->view = toolbar_view_new (TRUE);

  gtk_window_set_child (fixture->window, GTK_WIDGET (fixture->view));
}

static void
fixture_teardown (Fixture       *fixture,
                  gconstpointer  data)
{
  gtk_window_destroy (fixture->window);
}

static void
set_state (Fixture  *fixture,
           gboolean  active,
           gboolean  fullscreen,
           gboolean  bar)
{
  adw_focus_mode_set_state_for_testing (fixture->window, active, fullscreen, bar);
}

static gboolean
revealed (AdwToolbarView *view)
{
  return adw_toolbar_view_get_reveal_top_bars (view);
}

/* Without the compositor's global nothing changes */
static void
test_inactive (Fixture       *fixture,
               gconstpointer  data)
{
  set_state (fixture, FALSE, TRUE, FALSE);
  g_assert_true (revealed (fixture->view));

  set_state (fixture, FALSE, FALSE, FALSE);
  g_assert_true (revealed (fixture->view));
}

/* Hidden in fullscreen, shown with the bar, restored after */
static void
test_fullscreen (Fixture       *fixture,
                 gconstpointer  data)
{
  set_state (fixture, TRUE, FALSE, FALSE);
  g_assert_true (revealed (fixture->view));

  set_state (fixture, TRUE, TRUE, FALSE);
  g_assert_false (revealed (fixture->view));

  set_state (fixture, TRUE, TRUE, TRUE);
  g_assert_true (revealed (fixture->view));

  set_state (fixture, TRUE, TRUE, FALSE);
  g_assert_false (revealed (fixture->view));

  set_state (fixture, TRUE, FALSE, FALSE);
  g_assert_true (revealed (fixture->view));

  /* A bar notice outside fullscreen does nothing */
  set_state (fixture, TRUE, FALSE, TRUE);
  g_assert_true (revealed (fixture->view));
}

/* Losing the global while fullscreen restores the bars */
static void
test_global_removed (Fixture       *fixture,
                     gconstpointer  data)
{
  set_state (fixture, TRUE, TRUE, FALSE);
  g_assert_false (revealed (fixture->view));

  set_state (fixture, FALSE, TRUE, FALSE);
  g_assert_true (revealed (fixture->view));
}

/* An app that hid its bars before is left alone, then and after */
static void
test_app_hid_first (Fixture       *fixture,
                    gconstpointer  data)
{
  adw_toolbar_view_set_reveal_top_bars (fixture->view, FALSE);

  set_state (fixture, TRUE, TRUE, FALSE);
  g_assert_false (revealed (fixture->view));

  set_state (fixture, TRUE, TRUE, TRUE);
  g_assert_false (revealed (fixture->view));

  set_state (fixture, TRUE, FALSE, FALSE);
  g_assert_false (revealed (fixture->view));

  /* The next fullscreen looks again */
  adw_toolbar_view_set_reveal_top_bars (fixture->view, TRUE);
  set_state (fixture, TRUE, TRUE, FALSE);
  g_assert_false (revealed (fixture->view));
}

/* An app that changes the property in fullscreen takes the view over */
static void
test_app_takes_over (Fixture       *fixture,
                     gconstpointer  data)
{
  set_state (fixture, TRUE, TRUE, FALSE);
  g_assert_false (revealed (fixture->view));

  adw_toolbar_view_set_reveal_top_bars (fixture->view, TRUE);

  set_state (fixture, TRUE, TRUE, FALSE);
  g_assert_true (revealed (fixture->view));

  adw_toolbar_view_set_reveal_top_bars (fixture->view, FALSE);

  set_state (fixture, TRUE, TRUE, TRUE);
  g_assert_false (revealed (fixture->view));

  /* Not restored: it is the app's */
  set_state (fixture, TRUE, FALSE, FALSE);
  g_assert_false (revealed (fixture->view));
}

/* A view leaving the window gets its bars back */
static void
test_view_removed (Fixture       *fixture,
                   gconstpointer  data)
{
  AdwToolbarView *view = g_object_ref (fixture->view);

  set_state (fixture, TRUE, TRUE, FALSE);
  g_assert_false (revealed (view));

  gtk_window_set_child (fixture->window, NULL);
  g_assert_true (revealed (view));

  g_assert_finalize_object (view);
}

/* A view added in fullscreen is taken on the next update */
static void
test_view_added (Fixture       *fixture,
                 gconstpointer  data)
{
  AdwToolbarView *view = toolbar_view_new (TRUE);

  set_state (fixture, TRUE, TRUE, FALSE);

  gtk_window_set_child (fixture->window, GTK_WIDGET (view));
  set_state (fixture, TRUE, TRUE, FALSE);
  g_assert_false (revealed (view));
}

/* Only the outermost views with top bars: a split view's panes are both
 * taken, a view inside another one's content isn't */
static void
test_nested (Fixture       *fixture,
             gconstpointer  data)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  AdwToolbarView *sidebar = toolbar_view_new (TRUE);
  AdwToolbarView *outer = toolbar_view_new (FALSE);
  AdwToolbarView *inner = toolbar_view_new (TRUE);
  AdwToolbarView *nested = toolbar_view_new (TRUE);

  adw_toolbar_view_add_bottom_bar (outer, gtk_action_bar_new ());
  adw_toolbar_view_set_content (outer, GTK_WIDGET (inner));
  adw_toolbar_view_set_content (inner, GTK_WIDGET (nested));

  gtk_box_append (GTK_BOX (box), GTK_WIDGET (sidebar));
  gtk_box_append (GTK_BOX (box), GTK_WIDGET (outer));
  gtk_window_set_child (fixture->window, box);

  set_state (fixture, TRUE, TRUE, FALSE);
  g_assert_false (revealed (sidebar));
  g_assert_true (revealed (outer));
  g_assert_false (revealed (inner));
  g_assert_true (revealed (nested));

  set_state (fixture, TRUE, TRUE, TRUE);
  g_assert_true (revealed (sidebar));
  g_assert_true (revealed (inner));

  set_state (fixture, TRUE, FALSE, FALSE);
  g_assert_true (revealed (sidebar));
  g_assert_true (revealed (outer));
  g_assert_true (revealed (inner));
  g_assert_true (revealed (nested));
}

/* A view without top bars is not touched */
static void
test_no_top_bars (Fixture       *fixture,
                  gconstpointer  data)
{
  AdwToolbarView *view = toolbar_view_new (FALSE);

  gtk_window_set_child (fixture->window, GTK_WIDGET (view));

  set_state (fixture, TRUE, TRUE, FALSE);
  g_assert_true (revealed (view));
}

/* Version 2: the taken views keep their bars over the content, below the
 * bar, for the whole fullscreen */
static void
test_overlay (Fixture       *fixture,
              gconstpointer  data)
{
  adw_focus_mode_set_overlay_for_testing (fixture->window, 28);

  set_state (fixture, TRUE, FALSE, FALSE);
  g_assert_cmpint (adw_toolbar_view_get_focus_overlay (fixture->view), ==, -1);

  set_state (fixture, TRUE, TRUE, FALSE);
  g_assert_false (revealed (fixture->view));
  g_assert_cmpint (adw_toolbar_view_get_focus_overlay (fixture->view), ==, 28);
  g_assert_false (adw_toolbar_view_get_extend_content_to_top_edge (fixture->view));
  g_assert_cmpint (adw_toolbar_view_get_top_bar_style (fixture->view), ==, ADW_TOOLBAR_FLAT);

  set_state (fixture, TRUE, TRUE, TRUE);
  g_assert_true (revealed (fixture->view));
  g_assert_cmpint (adw_toolbar_view_get_focus_overlay (fixture->view), ==, 28);

  set_state (fixture, TRUE, FALSE, FALSE);
  g_assert_true (revealed (fixture->view));
  g_assert_cmpint (adw_toolbar_view_get_focus_overlay (fixture->view), ==, -1);
}

/* Version 1: no overlay, the bars push the content as before */
static void
test_no_overlay (Fixture       *fixture,
                 gconstpointer  data)
{
  set_state (fixture, TRUE, TRUE, TRUE);
  g_assert_true (revealed (fixture->view));
  g_assert_cmpint (adw_toolbar_view_get_focus_overlay (fixture->view), ==, -1);
}

/* An app taking a view over gets it back without the overlay */
static void
test_overlay_app_takes_over (Fixture       *fixture,
                             gconstpointer  data)
{
  adw_focus_mode_set_overlay_for_testing (fixture->window, 28);

  set_state (fixture, TRUE, TRUE, FALSE);
  g_assert_cmpint (adw_toolbar_view_get_focus_overlay (fixture->view), ==, 28);

  adw_toolbar_view_set_reveal_top_bars (fixture->view, TRUE);
  g_assert_cmpint (adw_toolbar_view_get_focus_overlay (fixture->view), ==, -1);

  set_state (fixture, TRUE, TRUE, TRUE);
  g_assert_cmpint (adw_toolbar_view_get_focus_overlay (fixture->view), ==, -1);
}

/* ADW_DISABLE_FOCUS_MODE: views are never followed */
static void
test_disabled (void)
{
  GtkWindow *window;
  AdwToolbarView *view;

  g_setenv ("ADW_DISABLE_FOCUS_MODE", "1", TRUE);

  window = GTK_WINDOW (gtk_window_new ());
  view = toolbar_view_new (TRUE);
  gtk_window_set_child (window, GTK_WIDGET (view));

  adw_focus_mode_set_state_for_testing (window, TRUE, TRUE, FALSE);
  g_assert_true (revealed (view));

  gtk_window_destroy (window);
  g_unsetenv ("ADW_DISABLE_FOCUS_MODE");
}

int
main (int   argc,
      char *argv[])
{
  g_unsetenv ("ADW_DISABLE_FOCUS_MODE");

  gtk_test_init (&argc, &argv, NULL);
  adw_init ();

#define ADD(path, func) \
  g_test_add ("/Adwaita/FocusMode/" path, Fixture, NULL, fixture_setup, func, fixture_teardown)

  ADD ("inactive", test_inactive);
  ADD ("fullscreen", test_fullscreen);
  ADD ("global_removed", test_global_removed);
  ADD ("app_hid_first", test_app_hid_first);
  ADD ("app_takes_over", test_app_takes_over);
  ADD ("view_removed", test_view_removed);
  ADD ("view_added", test_view_added);
  ADD ("nested", test_nested);
  ADD ("no_top_bars", test_no_top_bars);
  ADD ("overlay", test_overlay);
  ADD ("no_overlay", test_no_overlay);
  ADD ("overlay_app_takes_over", test_overlay_app_takes_over);

#undef ADD

  g_test_add_func ("/Adwaita/FocusMode/disabled", test_disabled);

  return g_test_run ();
}
