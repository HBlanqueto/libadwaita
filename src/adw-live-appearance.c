/*
 * Copyright (C) 2026 Malaysia contributors
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * Live appearance from the settings portal (MalaysiaDreams).
 *
 * The standard `org.freedesktop.appearance` namespace only carries the
 * colour scheme and an accent that libadwaita rounds to one of its presets.
 * A desktop that tints its windows (the exact accent, tinted window, sidebar
 * and popover backgrounds) publishes the stylesheet for that in its own
 * portal namespace:
 *
 *   org.malaysia.appearance
 *     gtk4-css      s      Named colours and CSS variables for libadwaita,
 *                          one `@media (prefers-color-scheme: …)` block per
 *                          scheme. Empty when the desktop tints nothing.
 *     accent-color  (ddd)  The exact accent, sRGB in [0, 1]; out of range
 *                          when unset.
 *
 * Both are read once with ReadAll and followed through SettingChanged, so a
 * change of accent or tint restyles running applications without reloading
 * the user `gtk.css` (which GTK reads only at startup). AdwStyleManager loads
 * the stylesheet into a provider above GTK_STYLE_PROVIDER_PRIORITY_USER: the
 * same colours may also sit, stale, in the user `gtk.css` written for
 * unpatched libraries, and the live ones have to win over them.
 *
 * Without the namespace (any other desktop) nothing is published and
 * libadwaita keeps its stock behaviour. ADW_DISABLE_LIVE_APPEARANCE, or
 * ADW_DISABLE_PORTAL, turn the module off.
 */

#include "config.h"

#include "adw-live-appearance-private.h"

#include "adw-settings-impl-private.h"

#define PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define PORTAL_SETTINGS_INTERFACE "org.freedesktop.portal.Settings"

#define NAMESPACE "org.malaysia.appearance"
#define KEY_CSS "gtk4-css"
#define KEY_ACCENT "accent-color"

struct _AdwLiveAppearance
{
  GObject parent_instance;

  GDBusProxy *settings_portal;

  char *css;
  gboolean has_accent;
  GdkRGBA accent;
};

G_DEFINE_FINAL_TYPE (AdwLiveAppearance, adw_live_appearance, G_TYPE_OBJECT)

enum {
  SIGNAL_CHANGED,
  SIGNAL_LAST_SIGNAL,
};

static guint signals[SIGNAL_LAST_SIGNAL];

static AdwLiveAppearance *default_instance = NULL;

static gboolean
set_css (AdwLiveAppearance *self,
         const char        *css)
{
  if (css && !*css)
    css = NULL;

  if (!g_strcmp0 (self->css, css))
    return FALSE;

  g_free (self->css);
  self->css = g_strdup (css);

  return TRUE;
}

static gboolean
set_accent (AdwLiveAppearance *self,
            const GdkRGBA     *accent)
{
  if (!accent) {
    if (!self->has_accent)
      return FALSE;

    self->has_accent = FALSE;

    return TRUE;
  }

  if (self->has_accent && gdk_rgba_equal (&self->accent, accent))
    return FALSE;

  self->has_accent = TRUE;
  self->accent = *accent;

  return TRUE;
}

/* Returns whether anything changed. Values of the wrong type are ignored. */
static gboolean
apply_value (AdwLiveAppearance *self,
             const char        *key,
             GVariant          *value)
{
  if (!g_strcmp0 (key, KEY_CSS)) {
    if (!g_variant_is_of_type (value, G_VARIANT_TYPE_STRING)) {
      g_warning ("Invalid type for %s.%s: %s", NAMESPACE, key,
                 g_variant_get_type_string (value));
      return FALSE;
    }

    return set_css (self, g_variant_get_string (value, NULL));
  }

  if (!g_strcmp0 (key, KEY_ACCENT)) {
    double r, g, b;
    GdkRGBA rgba;

    if (!g_variant_is_of_type (value, G_VARIANT_TYPE ("(ddd)"))) {
      g_warning ("Invalid type for %s.%s: %s", NAMESPACE, key,
                 g_variant_get_type_string (value));
      return FALSE;
    }

    g_variant_get (value, "(ddd)", &r, &g, &b);

    if (r < 0 || g < 0 || b < 0 || r > 1 || g > 1 || b > 1)
      return set_accent (self, NULL);

    rgba.red = r;
    rgba.green = g;
    rgba.blue = b;
    rgba.alpha = 1;

    return set_accent (self, &rgba);
  }

  return FALSE;
}

static void
changed_cb (GDBusProxy        *proxy,
            const char        *sender_name,
            const char        *signal_name,
            GVariant          *parameters,
            AdwLiveAppearance *self)
{
  const char *namespace;
  const char *key;
  GVariant *value = NULL;

  if (g_strcmp0 (signal_name, "SettingChanged"))
    return;

  if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(ssv)")))
    return;

  g_variant_get (parameters, "(&s&sv)", &namespace, &key, &value);

  if (!g_strcmp0 (namespace, NAMESPACE) && apply_value (self, key, value))
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);

  g_variant_unref (value);
}

static void
read_all (AdwLiveAppearance *self)
{
  const char *namespaces[] = { NAMESPACE, NULL };
  GError *error = NULL;
  GVariant *ret;
  GVariant *all;
  GVariant *settings;

  ret = g_dbus_proxy_call_sync (self->settings_portal,
                                "ReadAll",
                                g_variant_new ("(^as)", namespaces),
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                NULL,
                                &error);
  if (error) {
    g_debug ("Couldn't read %s: %s", NAMESPACE, error->message);
    g_error_free (error);
    return;
  }

  all = g_variant_get_child_value (ret, 0);
  settings = g_variant_lookup_value (all, NAMESPACE, G_VARIANT_TYPE_VARDICT);

  if (settings) {
    GVariantIter iter;
    const char *key;
    GVariant *value;

    g_variant_iter_init (&iter, settings);
    while (g_variant_iter_next (&iter, "{&sv}", &key, &value)) {
      apply_value (self, key, value);
      g_variant_unref (value);
    }

    g_variant_unref (settings);
  }

  g_variant_unref (all);
  g_variant_unref (ret);
}

static void
adw_live_appearance_dispose (GObject *object)
{
  AdwLiveAppearance *self = ADW_LIVE_APPEARANCE (object);

  g_clear_object (&self->settings_portal);
  g_clear_pointer (&self->css, g_free);

  G_OBJECT_CLASS (adw_live_appearance_parent_class)->dispose (object);
}

static void
adw_live_appearance_class_init (AdwLiveAppearanceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = adw_live_appearance_dispose;

  signals[SIGNAL_CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL, NULL,
                  G_TYPE_NONE,
                  0);
}

static void
adw_live_appearance_init (AdwLiveAppearance *self)
{
}

static void
connect_portal (AdwLiveAppearance *self)
{
  GError *error = NULL;

  if (g_getenv ("ADW_DISABLE_LIVE_APPEARANCE") || adw_get_disable_portal ())
    return;

  self->settings_portal =
    g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                   G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                   NULL,
                                   PORTAL_BUS_NAME,
                                   PORTAL_OBJECT_PATH,
                                   PORTAL_SETTINGS_INTERFACE,
                                   NULL,
                                   &error);
  if (error) {
    g_debug ("Settings portal not found: %s", error->message);
    g_error_free (error);
    return;
  }

  read_all (self);

  g_signal_connect_object (self->settings_portal,
                           "g-signal",
                           G_CALLBACK (changed_cb),
                           self,
                           0);
}

/*
 * The process-wide instance. It always exists; it publishes nothing when
 * disabled or when the portal doesn't have the namespace.
 */
AdwLiveAppearance *
adw_live_appearance_get_default (void)
{
  if (!default_instance) {
    default_instance = g_object_new (ADW_TYPE_LIVE_APPEARANCE, NULL);
    connect_portal (default_instance);
  }

  return default_instance;
}

/*
 * The stylesheet to load above the user CSS, or NULL.
 */
const char *
adw_live_appearance_get_css (AdwLiveAppearance *self)
{
  g_return_val_if_fail (ADW_IS_LIVE_APPEARANCE (self), NULL);

  return self->css;
}

/*
 * The exact accent, when the desktop publishes one.
 */
gboolean
adw_live_appearance_get_accent (AdwLiveAppearance *self,
                                GdkRGBA           *rgba)
{
  g_return_val_if_fail (ADW_IS_LIVE_APPEARANCE (self), FALSE);

  if (!self->has_accent)
    return FALSE;

  if (rgba)
    *rgba = self->accent;

  return TRUE;
}

void
adw_live_appearance_set_for_testing (AdwLiveAppearance *self,
                                     const char        *css,
                                     const GdkRGBA     *accent)
{
  gboolean changed;

  g_return_if_fail (ADW_IS_LIVE_APPEARANCE (self));

  changed = set_css (self, css);
  changed = set_accent (self, accent) || changed;

  if (changed)
    g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}
