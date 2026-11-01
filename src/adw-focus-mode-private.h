/*
 * Copyright (C) 2026 Malaysia contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#if !defined(_ADWAITA_INSIDE) && !defined(ADWAITA_COMPILATION)
#error "Only <adwaita.h> can be included directly."
#endif

#include <gtk/gtk.h>

#include "adw-toolbar-view.h"

G_BEGIN_DECLS

void adw_focus_mode_add_view    (AdwToolbarView *view);
void adw_focus_mode_remove_view (AdwToolbarView *view);

/* Tests: act as if the compositor offered the global (@active), the window
 * were fullscreen and the bar were down, then apply at once. */
void adw_focus_mode_set_state_for_testing (GtkWindow *window,
                                           gboolean   active,
                                           gboolean   fullscreen,
                                           gboolean   bar_revealed);

G_END_DECLS
