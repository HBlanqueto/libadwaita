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

#define ADW_TYPE_LIVE_APPEARANCE (adw_live_appearance_get_type())

G_DECLARE_FINAL_TYPE (AdwLiveAppearance, adw_live_appearance, ADW, LIVE_APPEARANCE, GObject)

AdwLiveAppearance *adw_live_appearance_get_default (void);

const char *adw_live_appearance_get_css    (AdwLiveAppearance *self);
gboolean    adw_live_appearance_get_accent (AdwLiveAppearance *self,
                                            GdkRGBA           *rgba);

/* Tests: feed the values a SettingChanged would carry, without a portal. */
void adw_live_appearance_set_for_testing (AdwLiveAppearance *self,
                                          const char        *css,
                                          const GdkRGBA     *accent);

G_END_DECLS
