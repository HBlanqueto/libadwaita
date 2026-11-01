/*
 * Copyright (C) 2026 Malaysia contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * Global menu export for compositors that draw the menu bar themselves.
 *
 * When the compositor advertises `gtk-shell-shows-menubar` (gtk_shell1
 * `global_menu_bar`), libadwaita windows have no menubar to give it: their
 * menu lives in the header bar, in the `GtkMenuButton` marked as primary.
 * This module publishes that menu as the application menubar so the
 * compositor can read it over `org.gtk.Menus`.
 *
 * GTK sends the menubar path through gtk_shell1 only when a window is
 * realized, so the exported model is created once per application, before
 * the first window realizes, and its contents are swapped afterwards. It
 * holds one submenu, labelled with the application name and marked with
 * `x-adw-app-menu`, whose link points at the primary menu of the most
 * recently active window.
 *
 * Nothing is exported when the compositor doesn't show the menubar, when
 * the application set its own menubar, or when ADW_DISABLE_GLOBAL_MENU is
 * set.
 */

#include "config.h"

#include "adw-global-menu-private.h"

#define STATE_KEY "adw-global-menu-state"
#define WINDOW_KEY "adw-global-menu-attached"

typedef struct {
  GMenu *menubar;
  GMenuModel *current;

  GtkWindow *window;
  GtkMenuButton *button;
  gulong button_handler;

  guint idle_id;
} State;

static void update (State *state);

static void
set_button (State         *state,
            GtkMenuButton *button)
{
  if (state->button == button)
    return;

  if (state->button)
    g_clear_signal_handler (&state->button_handler, state->button);
  else
    state->button_handler = 0;

  g_set_weak_pointer (&state->button, button);

  if (button)
    state->button_handler =
      g_signal_connect_swapped (button, "notify::menu-model",
                                G_CALLBACK (update), state);
}

static void
state_free (gpointer data)
{
  State *state = data;

  g_clear_handle_id (&state->idle_id, g_source_remove);
  set_button (state, NULL);
  g_clear_weak_pointer (&state->window);
  g_clear_object (&state->current);
  g_clear_object (&state->menubar);
  g_free (state);
}

static GtkMenuButton *
find_primary (GtkWidget      *widget,
              GtkMenuButton **fallback)
{
  GtkWidget *child;

  if (GTK_IS_MENU_BUTTON (widget)) {
    GtkMenuButton *button = GTK_MENU_BUTTON (widget);

    if (gtk_menu_button_get_primary (button) &&
        gtk_menu_button_get_menu_model (button)) {
      if (gtk_widget_get_mapped (widget))
        return button;

      if (!*fallback)
        *fallback = button;
    }
  }

  for (child = gtk_widget_get_first_child (widget);
       child;
       child = gtk_widget_get_next_sibling (child)) {
    GtkMenuButton *found = find_primary (child, fallback);

    if (found)
      return found;
  }

  return NULL;
}

static const char *
app_label (void)
{
  const char *name = g_get_application_name ();

  return name ? name : "";
}

static void
update (State *state)
{
  GtkMenuButton *button = NULL;
  GMenuModel *model = NULL;

  if (state->window) {
    GtkMenuButton *fallback = NULL;

    button = find_primary (GTK_WIDGET (state->window), &fallback);
    if (!button)
      button = fallback;
  }

  set_button (state, button);

  if (button)
    model = gtk_menu_button_get_menu_model (button);

  if (model == state->current)
    return;

  g_set_object (&state->current, model);

  g_menu_remove_all (state->menubar);

  if (model) {
    GMenuItem *item = g_menu_item_new_submenu (app_label (), model);

    g_menu_item_set_attribute (item, ADW_GLOBAL_MENU_APP_MENU_ATTRIBUTE,
                               "b", TRUE);
    g_menu_append_item (state->menubar, item);
    g_object_unref (item);
  }
}

static gboolean
update_idle_cb (gpointer data)
{
  State *state = data;

  state->idle_id = 0;
  update (state);

  return G_SOURCE_REMOVE;
}

static void
queue_update (State *state)
{
  if (state->idle_id)
    return;

  state->idle_id = g_idle_add (update_idle_cb, state);
}

static State *
get_state (GtkWindow *window)
{
  GtkApplication *app = gtk_window_get_application (window);

  if (!app)
    return NULL;

  return g_object_get_data (G_OBJECT (app), STATE_KEY);
}

static void
notify_is_active_cb (GtkWindow *window)
{
  State *state = get_state (window);

  if (!state || !gtk_window_is_active (window))
    return;

  if (state->window != window)
    g_set_weak_pointer (&state->window, window);

  queue_update (state);
}

static void
notify_focus_widget_cb (GtkWindow *window)
{
  State *state = get_state (window);

  if (state && state->window == window)
    queue_update (state);
}

static gboolean
shell_shows_menubar (GtkWindow *window)
{
  gboolean shows = FALSE;

  g_object_get (gtk_widget_get_settings (GTK_WIDGET (window)),
                "gtk-shell-shows-menubar", &shows,
                NULL);

  return shows;
}

/*
 * Called from the window's realize, before chaining up: GTK reads the
 * application menubar path while realizing.
 */
void
adw_global_menu_attach (GtkWindow *window)
{
  GtkApplication *app;
  State *state;

  g_return_if_fail (GTK_IS_WINDOW (window));

  app = gtk_window_get_application (window);
  if (!app)
    return;

  if (g_getenv ("ADW_DISABLE_GLOBAL_MENU"))
    return;

  if (!shell_shows_menubar (window))
    return;

  state = g_object_get_data (G_OBJECT (app), STATE_KEY);

  if (!state) {
    if (gtk_application_get_menubar (app))
      return;

    state = g_new0 (State, 1);
    state->menubar = g_menu_new ();
    g_object_set_data_full (G_OBJECT (app), STATE_KEY, state, state_free);

    gtk_application_set_menubar (app, G_MENU_MODEL (state->menubar));
  }

  if (g_object_get_data (G_OBJECT (window), WINDOW_KEY))
    return;

  g_object_set_data (G_OBJECT (window), WINDOW_KEY, GINT_TO_POINTER (TRUE));

  g_signal_connect (window, "notify::is-active",
                    G_CALLBACK (notify_is_active_cb), NULL);
  g_signal_connect (window, "notify::focus-widget",
                    G_CALLBACK (notify_focus_widget_cb), NULL);

  if (!state->window || gtk_window_is_active (window)) {
    g_set_weak_pointer (&state->window, window);
    queue_update (state);
  }
}
