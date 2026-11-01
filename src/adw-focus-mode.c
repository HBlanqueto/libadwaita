/*
 * Copyright (C) 2026 Malaysia contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * Focus mode for compositors that offer `infinity_focus_manager_v1`.
 *
 * In MalaysiaDreams, maximizing a window gives it a space of its own and the
 * xdg fullscreen state (focus mode). Moving the pointer to the top edge
 * brings the bar down and the compositor moves a client-decorated window
 * down by the bar's height; `infinity_focus_surface_v1.bar` tells the client
 * when the bar comes down (1) and goes up (0).
 *
 * With that global, a fullscreen window behaves like a fullscreen macOS
 * window: its `AdwToolbarView`s hide their top bars and show them only while
 * the bar is down. Leaving fullscreen shows them again.
 *
 * Without the global, or with ADW_DISABLE_FOCUS_MODE set, nothing changes.
 *
 * Which toolbar views: every `AdwToolbarView` rooted in the window, in the
 * window's own surface (not in a popover), outside an `AdwDialog`, that has
 * visible top bars and no ancestor toolbar view with visible top bars. So
 * the outer header bar goes, and with split views both panes' header bars,
 * but not the bars of a toolbar view nested inside another one's content.
 *
 * Apps that manage `reveal-top-bars` themselves are left alone:
 *
 * - The views are taken when the window becomes fullscreen, from an idle
 *   callback, so a handler or binding on `GtkWindow:fullscreened` has run
 *   already. A view whose top bars are already hidden then belongs to the
 *   app until fullscreen ends.
 * - If `reveal-top-bars` changes on a taken view and the change isn't ours,
 *   the app took it over: it is no longer touched, nor restored when
 *   fullscreen ends.
 *
 * Only taken views are restored (to visible, which they were) when
 * fullscreen ends, when the global goes away, or when they leave the window.
 */

#include "config.h"

#include "adw-focus-mode-private.h"

#include <string.h>

#include "adw-dialog.h"
#include "adw-toolbar-view-private.h"

#ifdef HAVE_INFINITY_FOCUS
#include <gdk/wayland/gdkwayland.h>

#include "infinity-focus-v1-client-protocol.h"
#endif

#define WINDOW_KEY "adw-focus-mode-window"
#define DISPLAY_KEY "adw-focus-mode-display"

typedef struct {
  AdwToolbarView *view;
  gulong notify_id;
  /* We drive reveal-top-bars during this fullscreen */
  gboolean taken;
  /* The app drives it during this fullscreen */
  gboolean app_owned;
} ViewRecord;

typedef struct _DisplayState DisplayState;

typedef struct {
  GtkWindow *window;
  GPtrArray *views;

  /* The compositor sends notices for this window */
  gboolean active;
  gboolean fullscreen;
  gboolean bar_revealed;

  gboolean applying;
  guint idle_id;

#ifdef HAVE_INFINITY_FOCUS
  DisplayState *display;
  struct infinity_focus_surface_v1 *notice;
#endif
} WindowState;

static gboolean
view_is_eligible (WindowState    *state,
                  AdwToolbarView *view)
{
  GtkWidget *widget = GTK_WIDGET (view);
  GtkWidget *parent;

  if (gtk_widget_get_native (widget) != GTK_NATIVE (state->window))
    return FALSE;

  if (gtk_widget_get_ancestor (widget, ADW_TYPE_DIALOG))
    return FALSE;

  if (!adw_toolbar_view_has_top_bars (view))
    return FALSE;

  for (parent = gtk_widget_get_parent (widget);
       parent;
       parent = gtk_widget_get_parent (parent)) {
    if (ADW_IS_TOOLBAR_VIEW (parent) &&
        adw_toolbar_view_has_top_bars (ADW_TOOLBAR_VIEW (parent)))
      return FALSE;
  }

  return TRUE;
}

static void
set_reveal (WindowState    *state,
            AdwToolbarView *view,
            gboolean        reveal)
{
  gboolean applying = state->applying;

  state->applying = TRUE;
  adw_toolbar_view_set_reveal_top_bars (view, reveal);
  state->applying = applying;
}

static void
release_view (WindowState *state,
              ViewRecord  *record)
{
  if (record->taken && !record->app_owned)
    set_reveal (state, record->view, TRUE);

  record->taken = FALSE;
  record->app_owned = FALSE;
}

static void
apply (WindowState *state)
{
  gboolean focus = state->active && state->fullscreen;
  guint i;

  g_clear_handle_id (&state->idle_id, g_source_remove);

  for (i = 0; i < state->views->len; i++) {
    ViewRecord *record = g_ptr_array_index (state->views, i);

    if (!focus) {
      release_view (state, record);
      continue;
    }

    if (record->app_owned)
      continue;

    if (!record->taken) {
      if (!view_is_eligible (state, record->view))
        continue;

      if (!adw_toolbar_view_get_reveal_top_bars (record->view)) {
        record->app_owned = TRUE;
        continue;
      }

      record->taken = TRUE;
    }

    set_reveal (state, record->view, state->bar_revealed);
  }
}

static gboolean
apply_idle_cb (gpointer data)
{
  WindowState *state = data;

  state->idle_id = 0;
  apply (state);

  return G_SOURCE_REMOVE;
}

static void
queue_apply (WindowState *state)
{
  if (state->idle_id)
    return;

  state->idle_id = g_idle_add (apply_idle_cb, state);
}

static ViewRecord *
find_record (WindowState    *state,
             AdwToolbarView *view,
             guint          *index)
{
  guint i;

  for (i = 0; i < state->views->len; i++) {
    ViewRecord *record = g_ptr_array_index (state->views, i);

    if (record->view == view) {
      if (index)
        *index = i;

      return record;
    }
  }

  return NULL;
}

static void
notify_reveal_top_bars_cb (AdwToolbarView *view,
                           GParamSpec     *pspec,
                           WindowState    *state)
{
  ViewRecord *record;

  if (state->applying)
    return;

  record = find_record (state, view, NULL);

  /* Not our change: the app drives this view now */
  if (record && record->taken) {
    record->taken = FALSE;
    record->app_owned = TRUE;
  }
}

#ifdef HAVE_INFINITY_FOCUS

struct _DisplayState {
  struct wl_registry *registry;
  struct infinity_focus_manager_v1 *manager;
  guint32 manager_name;
  GList *windows;
};

static void
notice_bar_cb (void                             *data,
               struct infinity_focus_surface_v1 *notice,
               uint32_t                          revealed)
{
  WindowState *state = data;

  state->bar_revealed = !!revealed;

  /* In an idle, so leaving fullscreen in the same batch doesn't hide the
   * bars only to show them again */
  queue_apply (state);
}

static const struct infinity_focus_surface_v1_listener notice_listener = {
  notice_bar_cb,
};

static void
attach_notice (WindowState *state)
{
  GdkSurface *surface;
  struct wl_surface *wl_surface;

  if (state->notice || !state->display || !state->display->manager)
    return;

  if (!gtk_widget_get_realized (GTK_WIDGET (state->window)))
    return;

  surface = gtk_native_get_surface (GTK_NATIVE (state->window));
  if (!GDK_IS_WAYLAND_SURFACE (surface))
    return;

  wl_surface = gdk_wayland_surface_get_wl_surface (surface);
  if (!wl_surface)
    return;

  state->notice =
    infinity_focus_manager_v1_get_focus_surface (state->display->manager,
                                                 wl_surface);
  infinity_focus_surface_v1_add_listener (state->notice, &notice_listener,
                                          state);

  state->active = TRUE;
  state->bar_revealed = FALSE;
  state->fullscreen = gtk_window_is_fullscreen (state->window);

  queue_apply (state);
}

static void
detach_notice (WindowState *state,
               gboolean     restore)
{
  g_clear_pointer (&state->notice, infinity_focus_surface_v1_destroy);

  state->active = FALSE;
  state->bar_revealed = FALSE;

  if (restore)
    apply (state);
}

static void
registry_global_cb (void               *data,
                    struct wl_registry *registry,
                    uint32_t            name,
                    const char         *interface,
                    uint32_t            version)
{
  DisplayState *display = data;
  GList *l;

  if (display->manager ||
      strcmp (interface, infinity_focus_manager_v1_interface.name) != 0)
    return;

  display->manager =
    wl_registry_bind (registry, name, &infinity_focus_manager_v1_interface, 1);
  display->manager_name = name;

  for (l = display->windows; l; l = l->next)
    attach_notice (l->data);
}

static void
registry_global_remove_cb (void               *data,
                           struct wl_registry *registry,
                           uint32_t            name)
{
  DisplayState *display = data;
  GList *l;

  if (!display->manager || name != display->manager_name)
    return;

  for (l = display->windows; l; l = l->next)
    detach_notice (l->data, TRUE);

  g_clear_pointer (&display->manager, infinity_focus_manager_v1_destroy);
}

static const struct wl_registry_listener registry_listener = {
  registry_global_cb,
  registry_global_remove_cb,
};

static void
display_closed_cb (GdkDisplay   *gdk_display,
                   gboolean      is_error,
                   DisplayState *display)
{
  GList *l;

  for (l = display->windows; l; l = l->next) {
    WindowState *state = l->data;

    detach_notice (state, TRUE);
    state->display = NULL;
  }

  g_clear_pointer (&display->windows, g_list_free);
  g_clear_pointer (&display->manager, infinity_focus_manager_v1_destroy);
  g_clear_pointer (&display->registry, wl_registry_destroy);
}

static void
display_state_free (gpointer data)
{
  DisplayState *display = data;

  g_list_free (display->windows);
  g_free (display);
}

static DisplayState *
get_display_state (GdkDisplay *gdk_display)
{
  DisplayState *display;
  struct wl_display *wl_display;

  if (!GDK_IS_WAYLAND_DISPLAY (gdk_display))
    return NULL;

  display = g_object_get_data (G_OBJECT (gdk_display), DISPLAY_KEY);
  if (display)
    return display->registry ? display : NULL;

  wl_display = gdk_wayland_display_get_wl_display (gdk_display);
  if (!wl_display)
    return NULL;

  display = g_new0 (DisplayState, 1);
  g_object_set_data_full (G_OBJECT (gdk_display), DISPLAY_KEY, display,
                          display_state_free);

  /* The globals arrive when GDK next dispatches; windows realized before
   * that get their notice object then */
  display->registry = wl_display_get_registry (wl_display);
  wl_registry_add_listener (display->registry, &registry_listener, display);

  g_signal_connect (gdk_display, "closed",
                    G_CALLBACK (display_closed_cb), display);

  return display;
}

static void
set_display (WindowState  *state,
             DisplayState *display)
{
  if (state->display == display)
    return;

  if (state->display)
    state->display->windows = g_list_remove (state->display->windows, state);

  state->display = display;

  if (display)
    display->windows = g_list_prepend (display->windows, state);
}

static void
window_realize_cb (GtkWindow   *window,
                   WindowState *state)
{
  set_display (state, get_display_state (gtk_widget_get_display (GTK_WIDGET (window))));
  attach_notice (state);
}

static void
window_unrealize_cb (GtkWindow   *window,
                     WindowState *state)
{
  detach_notice (state, TRUE);
}

#endif /* HAVE_INFINITY_FOCUS */

static void
window_notify_fullscreened_cb (GtkWindow   *window,
                               GParamSpec  *pspec,
                               WindowState *state)
{
  state->fullscreen = gtk_window_is_fullscreen (window);

  /* Let the app react to fullscreen first, see the top comment */
  queue_apply (state);
}

static void
window_state_free (gpointer data)
{
  WindowState *state = data;

  g_clear_handle_id (&state->idle_id, g_source_remove);

#ifdef HAVE_INFINITY_FOCUS
  detach_notice (state, FALSE);
  set_display (state, NULL);
#endif

  /* Views leave on unroot, before the window goes */
  g_ptr_array_unref (state->views);
  g_free (state);
}

static WindowState *
get_window_state (GtkWindow *window)
{
  WindowState *state = g_object_get_data (G_OBJECT (window), WINDOW_KEY);

  if (state)
    return state;

  state = g_new0 (WindowState, 1);
  state->window = window;
  state->views = g_ptr_array_new_with_free_func (g_free);
  state->fullscreen = gtk_window_is_fullscreen (window);

  g_object_set_data_full (G_OBJECT (window), WINDOW_KEY, state,
                          window_state_free);

  g_signal_connect (window, "notify::fullscreened",
                    G_CALLBACK (window_notify_fullscreened_cb), state);

#ifdef HAVE_INFINITY_FOCUS
  g_signal_connect (window, "realize",
                    G_CALLBACK (window_realize_cb), state);
  g_signal_connect (window, "unrealize",
                    G_CALLBACK (window_unrealize_cb), state);

  if (gtk_widget_get_realized (GTK_WIDGET (window)))
    window_realize_cb (window, state);
#endif

  return state;
}

/*
 * Called when @view is rooted.
 */
void
adw_focus_mode_add_view (AdwToolbarView *view)
{
  GtkRoot *root;
  WindowState *state;
  ViewRecord *record;

  g_return_if_fail (ADW_IS_TOOLBAR_VIEW (view));

  if (g_getenv ("ADW_DISABLE_FOCUS_MODE"))
    return;

  root = gtk_widget_get_root (GTK_WIDGET (view));
  if (!GTK_IS_WINDOW (root))
    return;

  state = get_window_state (GTK_WINDOW (root));

  if (find_record (state, view, NULL))
    return;

  record = g_new0 (ViewRecord, 1);
  record->view = view;
  record->notify_id =
    g_signal_connect (view, "notify::reveal-top-bars",
                      G_CALLBACK (notify_reveal_top_bars_cb), state);

  g_ptr_array_add (state->views, record);

  if (state->active && state->fullscreen)
    queue_apply (state);
}

/*
 * Called when @view is unrooted, while it still has its root.
 */
void
adw_focus_mode_remove_view (AdwToolbarView *view)
{
  GtkRoot *root;
  WindowState *state;
  ViewRecord *record;
  guint index;

  g_return_if_fail (ADW_IS_TOOLBAR_VIEW (view));

  root = gtk_widget_get_root (GTK_WIDGET (view));
  if (!GTK_IS_WINDOW (root))
    return;

  state = g_object_get_data (G_OBJECT (root), WINDOW_KEY);
  if (!state)
    return;

  record = find_record (state, view, &index);
  if (!record)
    return;

  release_view (state, record);
  g_clear_signal_handler (&record->notify_id, view);
  g_ptr_array_remove_index (state->views, index);
}

void
adw_focus_mode_set_state_for_testing (GtkWindow *window,
                                      gboolean   active,
                                      gboolean   fullscreen,
                                      gboolean   bar_revealed)
{
  WindowState *state;

  g_return_if_fail (GTK_IS_WINDOW (window));

  state = get_window_state (window);
  state->active = !!active;
  state->fullscreen = !!fullscreen;
  state->bar_revealed = !!bar_revealed;

  apply (state);
}
