/* GDK - The GIMP Drawing Kit
 * Copyright (C) 2009 Carlos Garnacho <carlosg@gnome.org>
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

#include "gdkdevice-xi2.h"
#include "gdkintl.h"


#define GDK_DEVICE_XI2_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDK_TYPE_DEVICE_XI2, GdkDeviceXI2Private))

typedef struct GdkDeviceXI2Private GdkDeviceXI2Private;

struct GdkDeviceXI2Private
{
  int device_id;
  GArray *axes;
};

static void gdk_device_xi2_get_property (GObject      *object,
                                         guint         prop_id,
                                         GValue       *value,
                                         GParamSpec   *pspec);
static void gdk_device_xi2_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec);

static void gdk_device_xi2_get_state (GdkDevice       *device,
                                      GdkWindow       *window,
                                      gdouble         *axes,
                                      GdkModifierType *mask);
static gboolean gdk_device_xi2_get_axis (GdkDevice    *device,
                                         gdouble      *axes,
                                         GdkAxisUse    use,
                                         gdouble      *value);

G_DEFINE_TYPE (GdkDeviceXI2, gdk_device_xi2, GDK_TYPE_DEVICE)

enum {
  PROP_0,
  PROP_DEVICE_ID
};

static void
gdk_device_xi2_class_init (GdkDeviceXI2Class *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GdkDeviceClass *device_class = GDK_DEVICE_CLASS (klass);

  object_class->get_property = gdk_device_xi2_get_property;
  object_class->set_property = gdk_device_xi2_set_property;

  device_class->get_state = gdk_device_xi2_get_state;
  device_class->get_axis = gdk_device_xi2_get_axis;

  g_object_class_install_property (object_class,
				   PROP_DEVICE_ID,
				   g_param_spec_int ("device-id",
                                                     P_("Device ID"),
                                                     P_("Device identifier"),
                                                     0, G_MAXINT, 0,
                                                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_type_class_add_private (object_class, sizeof (GdkDeviceXI2Private));
}

static void
gdk_device_xi2_init (GdkDeviceXI2 *device)
{
  GdkDeviceXI2Private *priv;

  priv = GDK_DEVICE_XI2_GET_PRIVATE (device);
  priv->axes = g_array_new (FALSE, TRUE, sizeof (GdkDeviceAxis));
}

static void
gdk_device_xi2_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  GdkDeviceXI2Private *priv;

  priv = GDK_DEVICE_XI2_GET_PRIVATE (object);

  switch (prop_id)
    {
    case PROP_DEVICE_ID:
      g_value_set_int (value, priv->device_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gdk_device_xi2_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  GdkDeviceXI2Private *priv;

  priv = GDK_DEVICE_XI2_GET_PRIVATE (object);

  switch (prop_id)
    {
    case PROP_DEVICE_ID:
      priv->device_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gdk_device_xi2_get_state (GdkDevice       *device,
                          GdkWindow       *window,
                          gdouble         *axes,
                          GdkModifierType *mask)
{
}

static gboolean
gdk_device_xi2_get_axis (GdkDevice  *device,
                         gdouble    *axes,
                         GdkAxisUse  use,
                         gdouble    *value)
{
  GdkDeviceXI2Private *priv;
  gint i;

  priv = GDK_DEVICE_XI2_GET_PRIVATE (device);

  for (i = 0; i < priv->axes->len; i++)
    {
      GdkDeviceAxis axis_info;

      axis_info = g_array_index (priv->axes, GdkDeviceAxis, i);

      if (axis_info.use == use)
        {
          if (value)
            *value = axes[i];

          return TRUE;
        }
    }

  return FALSE;
}

void
gdk_device_xi2_add_axis (GdkDeviceXI2 *device,
                         GdkAxisUse    use)
{
  GdkDeviceXI2Private *priv;
  GdkDeviceAxis axis_info;

  priv = GDK_DEVICE_XI2_GET_PRIVATE (device);
  axis_info.use = use;

  switch (use)
    {
    case GDK_AXIS_X:
    case GDK_AXIS_Y:
      axis_info.min = 0.;
      axis_info.max = 0.;
      break;
    case GDK_AXIS_XTILT:
    case GDK_AXIS_YTILT:
      axis_info.min = -1.;
      axis_info.max = 1.;
      break;
    default:
      axis_info.min = 0.;
      axis_info.max = 1.;
      break;
    }

  g_array_append_val (priv->axes, axis_info);
}
