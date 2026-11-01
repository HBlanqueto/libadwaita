/*
 * Copyright (C) 2026 Malaysia contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#if !defined(_ADWAITA_INSIDE) && !defined(ADWAITA_COMPILATION)
#error "Only <adwaita.h> can be included directly."
#endif

#include "adw-toolbar-view.h"

G_BEGIN_DECLS

gboolean adw_toolbar_view_has_top_bars (AdwToolbarView *self);

void adw_toolbar_view_set_focus_overlay (AdwToolbarView *self,
                                         int             offset);
int  adw_toolbar_view_get_focus_overlay (AdwToolbarView *self);

G_END_DECLS
