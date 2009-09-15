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

#include "gdkdevice.h"
#include "gdkdeviceprivate.h"
#include "gdkintl.h"
#include "gdkinternals.h"
#include "gdkalias.h"

#define GDK_DEVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDK_TYPE_DEVICE, GdkDevicePrivate))

typedef struct _GdkDevicePrivate GdkDevicePrivate;
typedef struct _GdkAxisInfo GdkAxisInfo;

struct _GdkAxisInfo
{
  GdkAtom label;
  GdkAxisUse use;

  gdouble min_axis;
  gdouble max_axis;

  gdouble min_value;
  gdouble max_value;
  gdouble resolution;
};

struct _GdkDevicePrivate
{
  GdkDisplay *display;
  GdkDevice *relative;
  GdkDeviceType type;
  GArray *axes;
};

static void gdk_device_dispose      (GObject      *object);
static void gdk_device_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec);
static void gdk_device_get_property (GObject      *object,
                                     guint         prop_id,
                                     GValue       *value,
                                     GParamSpec   *pspec);


G_DEFINE_ABSTRACT_TYPE (GdkDevice, gdk_device, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_DISPLAY,
  PROP_NAME,
  PROP_RELATIVE,
  PROP_TYPE,
  PROP_INPUT_SOURCE,
  PROP_INPUT_MODE,
  PROP_HAS_CURSOR,
  PROP_N_AXES
};


static void
gdk_device_class_init (GdkDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gdk_device_dispose;
  object_class->set_property = gdk_device_set_property;
  object_class->get_property = gdk_device_get_property;

  g_object_class_install_property (object_class,
				   PROP_DISPLAY,
				   g_param_spec_object ("display",
                                                        P_("Device Display"),
                                                        P_("Display to which the device belongs to"),
                                                        GDK_TYPE_DISPLAY,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class,
				   PROP_NAME,
				   g_param_spec_string ("name",
                                                        P_("Device name"),
                                                        P_("Device name"),
                                                        NULL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class,
                                   PROP_TYPE,
                                   g_param_spec_enum ("type",
                                                      P_("Device type"),
                                                      P_("Device role in the device manager"),
                                                      GDK_TYPE_DEVICE_TYPE,
                                                      GDK_DEVICE_TYPE_MASTER,
                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class,
				   PROP_RELATIVE,
				   g_param_spec_object ("relative",
                                                        P_("Relative device"),
                                                        P_("Corresponding pointer or keyboard to this device"),
                                                        GDK_TYPE_DEVICE,
                                                        G_PARAM_READABLE));
  g_object_class_install_property (object_class,
				   PROP_INPUT_SOURCE,
				   g_param_spec_enum ("input-source",
                                                      P_("Input source"),
                                                      P_("Source type for the device"),
                                                      GDK_TYPE_INPUT_SOURCE,
                                                      GDK_SOURCE_MOUSE,
                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class,
                                   PROP_INPUT_MODE,
				   g_param_spec_enum ("input-mode",
                                                      P_("Input mode for the device"),
                                                      P_("Input mode for the device"),
                                                      GDK_TYPE_INPUT_MODE,
                                                      GDK_MODE_DISABLED,
                                                      G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
				   PROP_HAS_CURSOR,
				   g_param_spec_boolean ("has-cursor",
                                                         P_("Whether the device has cursor"),
                                                         P_("Whether there is a visible cursor following device motion"),
                                                         FALSE,
                                                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class,
				   PROP_N_AXES,
				   g_param_spec_uint ("n-axes",
                                                      P_("Number of axes in the device"),
                                                      P_("Number of axes in the device"),
                                                      0, G_MAXUINT, 0,
                                                      G_PARAM_READABLE));

  g_type_class_add_private (object_class, sizeof (GdkDevicePrivate));
}

static void
gdk_device_init (GdkDevice *device)
{
  GdkDevicePrivate *priv;

  priv = GDK_DEVICE_GET_PRIVATE (device);
  priv->axes = g_array_new (FALSE, TRUE, sizeof (GdkAxisInfo));
}

static void
gdk_device_dispose (GObject *object)
{
  GdkDevicePrivate *priv;

  priv = GDK_DEVICE_GET_PRIVATE (object);

  if (priv->relative)
    {
      _gdk_device_set_relative (priv->relative, NULL);
      g_object_unref (priv->relative);
      priv->relative = NULL;
    }

  G_OBJECT_CLASS (gdk_device_parent_class)->dispose (object);
}

static void
gdk_device_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  GdkDevice *device = GDK_DEVICE (object);
  GdkDevicePrivate *priv = GDK_DEVICE_GET_PRIVATE (object);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      priv->display = g_value_get_object (value);
      break;
    case PROP_NAME:
      if (device->name)
        g_free (device->name);

      device->name = g_value_dup_string (value);
      break;
    case PROP_TYPE:
      priv->type = g_value_get_enum (value);
      break;
    case PROP_INPUT_SOURCE:
      device->source = g_value_get_enum (value);
      break;
    case PROP_INPUT_MODE:
      gdk_device_set_mode (device, g_value_get_enum (value));
      break;
    case PROP_HAS_CURSOR:
      device->has_cursor = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gdk_device_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  GdkDevice *device = GDK_DEVICE (object);
  GdkDevicePrivate *priv = GDK_DEVICE_GET_PRIVATE (object);

  switch (prop_id)
    {
    case PROP_DISPLAY:
      g_value_set_object (value, priv->display);
      break;
    case PROP_RELATIVE:
      g_value_set_object (value, priv->relative);
      break;
    case PROP_NAME:
      g_value_set_string (value,
                          device->name);
      break;
    case PROP_TYPE:
      g_value_set_enum (value, priv->type);
      break;
    case PROP_INPUT_SOURCE:
      g_value_set_enum (value, device->source);
      break;
    case PROP_INPUT_MODE:
      g_value_set_enum (value, device->mode);
      break;
    case PROP_HAS_CURSOR:
      g_value_set_boolean (value,
                           device->has_cursor);
      break;
    case PROP_N_AXES:
      g_value_set_uint (value, priv->axes->len);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

void
gdk_device_get_state (GdkDevice       *device,
                      GdkWindow       *window,
                      gdouble         *axes,
                      GdkModifierType *mask)
{
  g_return_if_fail (GDK_IS_DEVICE (device));
  g_return_if_fail (GDK_IS_WINDOW (window));

  if (GDK_DEVICE_GET_CLASS (device)->get_state)
    GDK_DEVICE_GET_CLASS (device)->get_state (device, window, axes, mask);
}

gboolean
gdk_device_get_history (GdkDevice      *device,
                        GdkWindow      *window,
                        guint32         start,
                        guint32         stop,
                        GdkTimeCoord ***events,
                        guint          *n_events)
{
  g_return_val_if_fail (GDK_IS_DEVICE (device), FALSE);
  g_return_val_if_fail (GDK_IS_WINDOW (window), FALSE);

  if (!GDK_DEVICE_GET_CLASS (device)->get_history)
    return FALSE;

  return GDK_DEVICE_GET_CLASS (device)->get_history (device, window,
                                                     start, stop,
                                                     events, n_events);
}

void
gdk_device_free_history (GdkTimeCoord **events,
                         gint           n_events)
{
  gint i;

  for (i = 0; i < n_events; i++)
    g_free (events[i]);

  g_free (events);
}

void
gdk_device_set_source (GdkDevice      *device,
		       GdkInputSource  source)
{
  g_return_if_fail (GDK_IS_DEVICE (device));

  device->source = source;
}

gboolean
gdk_device_set_mode (GdkDevice    *device,
                     GdkInputMode  mode)
{
  g_return_val_if_fail (GDK_IS_DEVICE (device), FALSE);

  if (gdk_device_get_device_type (device) == GDK_DEVICE_TYPE_MASTER)
    return FALSE;

  if (device->mode == mode)
    return TRUE;

  /* FIXME: setting has_cursor when mode is window? */

  device->mode = mode;
  g_object_notify (G_OBJECT (device), "input-mode");

  _gdk_input_check_extension_events (device);

  return TRUE;
}

void
gdk_device_set_key (GdkDevice      *device,
		    guint           index,
		    guint           keyval,
		    GdkModifierType modifiers)
{
  g_return_if_fail (GDK_IS_DEVICE (device));
  g_return_if_fail (index < device->num_keys);

  device->keys[index].keyval = keyval;
  device->keys[index].modifiers = modifiers;
}

void
gdk_device_set_axis_use (GdkDevice   *device,
			 guint        index,
			 GdkAxisUse   use)
{
  g_return_if_fail (GDK_IS_DEVICE (device));
  g_return_if_fail (index < device->num_axes);

  device->axes[index].use = use;

  switch (use)
    {
    case GDK_AXIS_X:
    case GDK_AXIS_Y:
      device->axes[index].min = 0.;
      device->axes[index].max = 0.;
      break;
    case GDK_AXIS_XTILT:
    case GDK_AXIS_YTILT:
      device->axes[index].min = -1.;
      device->axes[index].max = 1;
      break;
    default:
      device->axes[index].min = 0.;
      device->axes[index].max = 1;
      break;
    }
}

GdkDisplay *
gdk_device_get_display (GdkDevice *device)
{
  GdkDevicePrivate *priv;

  g_return_val_if_fail (GDK_IS_DEVICE (device), NULL);

  priv = GDK_DEVICE_GET_PRIVATE (device);

  return priv->display;
}

GdkDevice *
gdk_device_get_relative (GdkDevice *device)
{
  GdkDevicePrivate *priv;

  g_return_val_if_fail (GDK_IS_DEVICE (device), NULL);

  priv = GDK_DEVICE_GET_PRIVATE (device);

  return priv->relative;
}

void
_gdk_device_set_relative (GdkDevice *device,
                          GdkDevice *relative)
{
  GdkDevicePrivate *priv;

  g_return_if_fail (GDK_IS_DEVICE (device));
  g_return_if_fail (GDK_IS_DEVICE (relative));

  priv = GDK_DEVICE_GET_PRIVATE (device);

  if (priv->relative == relative)
    return;

  if (priv->relative)
    {
      g_object_unref (priv->relative);
      priv->relative = NULL;
    }

  if (relative)
    priv->relative = g_object_ref (relative);
}

GdkDeviceType
gdk_device_get_device_type (GdkDevice *device)
{
  GdkDevicePrivate *priv;

  g_return_val_if_fail (GDK_IS_DEVICE (device), GDK_DEVICE_TYPE_MASTER);

  priv = GDK_DEVICE_GET_PRIVATE (device);

  return priv->type;
}

GList *
gdk_device_list_axes (GdkDevice *device)
{
  GdkDevicePrivate *priv;
  GList *axes = NULL;
  gint i;

  priv = GDK_DEVICE_GET_PRIVATE (device);

  for (i = 0; i < priv->axes->len; i++)
    {
      GdkAxisInfo axis_info;

      axis_info = g_array_index (priv->axes, GdkAxisInfo, i);
      axes = g_list_prepend (axes, GDK_ATOM_TO_POINTER (axis_info.label));
    }

  return g_list_reverse (axes);
}

gboolean
gdk_device_get_axis_value (GdkDevice *device,
                           gdouble   *axes,
                           GdkAtom    axis_label,
                           gdouble   *value)
{
  GdkDevicePrivate *priv;
  gint i;

  g_return_val_if_fail (GDK_IS_DEVICE (device), FALSE);

  if (axes == NULL)
    return FALSE;

  priv = GDK_DEVICE_GET_PRIVATE (device);

  for (i = 0; i < priv->axes->len; i++)
    {
      GdkAxisInfo axis_info;

      axis_info = g_array_index (priv->axes, GdkAxisInfo, i);

      if (axis_info.label != axis_label)
        continue;

      if (value)
        *value = axes[i];

      return TRUE;
    }

  return FALSE;
}

gboolean
gdk_device_get_axis (GdkDevice  *device,
                     gdouble    *axes,
                     GdkAxisUse  use,
                     gdouble    *value)
{
  GdkDevicePrivate *priv;
  gint i;

  g_return_val_if_fail (GDK_IS_DEVICE (device), FALSE);

  if (axes == NULL)
    return FALSE;

  priv = GDK_DEVICE_GET_PRIVATE (device);

  g_return_val_if_fail (priv->axes != NULL, FALSE);

  for (i = 0; i < priv->axes->len; i++)
    {
      GdkAxisInfo axis_info;

      axis_info = g_array_index (priv->axes, GdkAxisInfo, i);

      if (axis_info.use != use)
        continue;

      if (value)
        *value = axes[i];

      return TRUE;
    }

  return FALSE;
}

GdkGrabStatus
gdk_device_grab (GdkDevice        *device,
                 GdkWindow        *window,
                 GdkGrabOwnership  grab_ownership,
                 gboolean          owner_events,
                 GdkEventMask      event_mask,
                 GdkCursor        *cursor,
                 guint32           time_)
{
  GdkGrabStatus res;
  GdkWindow *native;

  g_return_val_if_fail (GDK_IS_DEVICE (device), 0);
  g_return_val_if_fail (GDK_IS_WINDOW (window), 0);

  if (_gdk_native_windows)
    native = window;
  else
    native = gdk_window_get_toplevel (window);

  while (((GdkWindowObject *) native)->window_type == GDK_WINDOW_OFFSCREEN)
    {
      native = gdk_offscreen_window_get_embedder (native);

      if (native == NULL ||
	  (!_gdk_window_has_impl (native) &&
	   !gdk_window_is_viewable (native)))
	return GDK_GRAB_NOT_VIEWABLE;

      native = gdk_window_get_toplevel (native);
    }

  res = _gdk_windowing_device_grab (device,
                                    window,
                                    native,
                                    owner_events,
                                    event_mask,
                                    NULL,
                                    cursor,
                                    time_);

  if (res == GDK_GRAB_SUCCESS)
    {
      GdkDisplay *display;
      gulong serial;

      display = gdk_drawable_get_display (window);
      serial = _gdk_windowing_window_get_next_serial (display);

      _gdk_display_add_device_grab (display,
                                    device,
                                    window,
                                    native,
                                    GDK_OWNERSHIP_NONE,
                                    owner_events,
                                    event_mask,
                                    serial,
                                    time_,
                                    FALSE);
    }

  return res;
}

/* Private API */
void
_gdk_device_reset_axes (GdkDevice *device)
{
  GdkDevicePrivate *priv;
  gint i;

  priv = GDK_DEVICE_GET_PRIVATE (device);

  for (i = priv->axes->len - 1; i >= 0; i--)
    g_array_remove_index (priv->axes, i);

  g_object_notify (G_OBJECT (device), "n-axes");

  /* This is done for backwards compatibility */
  g_free (device->axes);
  device->axes = NULL;
}

guint
_gdk_device_add_axis (GdkDevice   *device,
                      GdkAtom      label_atom,
                      GdkAxisUse   use,
                      gdouble      min_value,
                      gdouble      max_value,
                      gdouble      resolution)
{
  GdkDevicePrivate *priv;
  GdkAxisInfo axis_info;
  guint pos;

  priv = GDK_DEVICE_GET_PRIVATE (device);

  axis_info.use = use;
  axis_info.label = label_atom;
  axis_info.min_value = min_value;
  axis_info.max_value = max_value;
  axis_info.resolution = resolution;

  switch (use)
    {
    case GDK_AXIS_X:
    case GDK_AXIS_Y:
      axis_info.min_axis = 0.;
      axis_info.max_axis = 0.;
      break;
    case GDK_AXIS_XTILT:
    case GDK_AXIS_YTILT:
      axis_info.min_axis = -1.;
      axis_info.max_axis = 1.;
      break;
    default:
      axis_info.min_axis = 0.;
      axis_info.max_axis = 1.;
      break;
    }

  priv->axes = g_array_append_val (priv->axes, axis_info);
  device->num_axes = priv->axes->len;
  pos = device->num_axes - 1;

  /* This is done for backwards compatibility, since the public
   * struct doesn't actually store the device data.
   */
  device->axes = g_realloc (device->axes, sizeof (GdkDeviceAxis) * priv->axes->len);
  device->axes[pos].use = axis_info.use;
  device->axes[pos].min = axis_info.min_axis;
  device->axes[pos].max = axis_info.max_axis;

  g_object_notify (G_OBJECT (device), "n-axes");

  return pos;
}

GdkAxisInfo *
find_axis_info (GArray     *array,
                GdkAxisUse  use)
{
  GdkAxisInfo *info;
  gint i;

  for (i = 0; i < GDK_AXIS_LAST; i++)
    {
      info = &g_array_index (array, GdkAxisInfo, i);

      if (info->use == use)
        return info;
    }

  return NULL;
}

gboolean
_gdk_device_translate_axis (GdkDevice *device,
                            gdouble    window_width,
                            gdouble    window_height,
                            gdouble    window_x,
                            gdouble    window_y,
                            guint      index,
                            gdouble    value,
                            gdouble   *axis_value)
{
  GdkDevicePrivate *priv;
  GdkAxisInfo axis_info;
  gdouble out = 0;

  priv = GDK_DEVICE_GET_PRIVATE (device);

  if (index >= priv->axes->len)
    return FALSE;

  axis_info = g_array_index (priv->axes, GdkAxisInfo, index);

  if (axis_info.use == GDK_AXIS_X ||
      axis_info.use == GDK_AXIS_Y)
    {
      GdkAxisInfo *axis_info_x, *axis_info_y;
      gdouble device_width, device_height;
      gdouble x_offset, y_offset;
      gdouble x_scale, y_scale;

      if (axis_info.use == GDK_AXIS_X)
        {
          axis_info_x = &axis_info;
          axis_info_y = find_axis_info (priv->axes, GDK_AXIS_Y);
        }
      else
        {
          axis_info_x = find_axis_info (priv->axes, GDK_AXIS_X);
          axis_info_y = &axis_info;
        }

      device_width = axis_info_x->max_value - axis_info_x->min_value;
      device_height = axis_info_y->max_value - axis_info_y->min_value;

      if (device->mode == GDK_MODE_SCREEN)
        {
          if (axis_info.use == GDK_AXIS_X)
            out = window_x;
          else
            out = window_y;
        }
      else /* GDK_MODE_WINDOW */
        {
          gdouble x_resolution, y_resolution, device_aspect;

          x_resolution = axis_info_x->resolution;
          y_resolution = axis_info_y->resolution;

          /*
           * Some drivers incorrectly report the resolution of the device
           * as zero (in partiular linuxwacom < 0.5.3 with usb tablets).
           * This causes the device_aspect to become NaN and totally
           * breaks windowed mode.  If this is the case, the best we can
           * do is to assume the resolution is non-zero is equal in both
           * directions (which is true for many devices).  The absolute
           * value of the resolution doesn't matter since we only use the
           * ratio.
           */
          if (x_resolution == 0 || y_resolution == 0)
            {
              x_resolution = 1;
              y_resolution = 1;
            }

          device_aspect = (device_height * y_resolution) /
            (device_width * x_resolution);

          if (device_aspect * window_width >= window_height)
            {
              /* device taller than window */
              x_scale = window_width / device_width;
              y_scale = (x_scale * x_resolution) / y_resolution;

              x_offset = 0;
              y_offset = - (device_height * y_scale - window_height) / 2;
            }
          else
            {
              /* window taller than device */
              y_scale = window_height / device_height;
              x_scale = (y_scale * y_resolution) / x_resolution;

              y_offset = 0;
              x_offset = - (device_width * x_scale - window_width) / 2;
            }

          if (axis_info.use == GDK_AXIS_X)
            out = x_offset + x_scale * (value - axis_info.min_value);
          else
            out = y_offset + y_scale * (value - axis_info.min_value);
        }
    }
  else
    {
      gdouble axis_width;

      axis_width = axis_info.max_value - axis_info.min_value;
      out = (axis_info.max_axis * (value - axis_info.min_value) +
             axis_info.min_axis * (axis_info.max_value - value)) / axis_width;
    }

  if (axis_value)
    *axis_value = out;

  return TRUE;
}

#define __GDK_DEVICE_C__
#include "gdkaliasdef.c"
