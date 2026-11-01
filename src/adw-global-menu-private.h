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

G_BEGIN_DECLS

#define ADW_GLOBAL_MENU_APP_MENU_ATTRIBUTE "x-adw-app-menu"

void adw_global_menu_attach (GtkWindow *window);

G_END_DECLS
