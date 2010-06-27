/* GTK - The GIMP Toolkit
 * Copyright (C) 2010 Carlos Garnacho <carlosg@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include "gtkprivate.h"
#include "gtkstyleprovider.h"
#include "gtkintl.h"

#include "gtkalias.h"

static void gtk_style_provider_iface_init (gpointer g_iface);

GType
gtk_style_provider_get_type (void)
{
  static GType style_provider_type = 0;

  if (!style_provider_type)
    style_provider_type = g_type_register_static_simple (G_TYPE_INTERFACE,
                                                         I_("GtkStyleProvider"),
                                                         sizeof (GtkStyleProviderIface),
                                                         (GClassInitFunc) gtk_style_provider_iface_init,
                                                         0, NULL, 0);
  return style_provider_type;
}

static void
gtk_style_provider_iface_init (gpointer g_iface)
{
}

GtkStyleSet *
gtk_style_provider_get_style (GtkStyleProvider *provider,
                              GtkWidgetPath    *path)
{
  GtkStyleProviderIface *iface;

  g_return_val_if_fail (GTK_IS_STYLE_PROVIDER (provider), NULL);

  iface = GTK_STYLE_PROVIDER_GET_IFACE (provider);

  if (!iface->get_style)
    return NULL;

  return iface->get_style (provider, path);
}

gboolean
gtk_style_provider_get_style_property (GtkStyleProvider *provider,
                                       GtkWidgetPath    *widget_path,
                                       const gchar      *property_name,
                                       GValue           *value)
{
  GtkStyleProviderIface *iface;

  g_return_val_if_fail (GTK_IS_STYLE_PROVIDER (provider), FALSE);
  g_return_val_if_fail (widget_path != NULL, FALSE);
  g_return_val_if_fail (property_name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  iface = GTK_STYLE_PROVIDER_GET_IFACE (provider);

  if (!iface->get_style_property)
    return FALSE;

  return iface->get_style_property (provider, widget_path, property_name, value);
}

GtkIconFactory *
gtk_style_provider_get_icon_factory (GtkStyleProvider *provider,
				     GtkWidgetPath    *path)
{
  GtkStyleProviderIface *iface;

  g_return_val_if_fail (GTK_IS_STYLE_PROVIDER (provider), NULL);
  g_return_val_if_fail (path != NULL, NULL);

  iface = GTK_STYLE_PROVIDER_GET_IFACE (provider);

  if (!iface->get_icon_factory)
    return NULL;

  return iface->get_icon_factory (provider, path);
}

#define __GTK_STYLE_PROVIDER_C__
#include "gtkaliasdef.c"
