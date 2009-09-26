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

#include <gdk/gdkwindow.h>
#include "gdkdeviceprivate.h"
#include "gdkdevice-xi.h"
#include "gdkprivate-x11.h"
#include "gdkintl.h"
#include "gdkx.h"

static void gdk_device_xi_constructed  (GObject *object);
static void gdk_device_xi_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec);
static void gdk_device_xi_get_property (GObject      *object,
                                        guint         prop_id,
                                        GValue       *value,
                                        GParamSpec   *pspec);

static gboolean gdk_device_xi_get_history (GdkDevice      *device,
                                           GdkWindow      *window,
                                           guint32         start,
                                           guint32         stop,
                                           GdkTimeCoord ***events,
                                           guint          *n_events);

static void gdk_device_xi_get_state       (GdkDevice       *device,
                                           GdkWindow       *window,
                                           gdouble         *axes,
                                           GdkModifierType *mask);


G_DEFINE_TYPE (GdkDeviceXI, gdk_device_xi, GDK_TYPE_DEVICE)

enum {
  PROP_0,
  PROP_DEVICE_ID
};

static void
gdk_device_xi_class_init (GdkDeviceXIClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GdkDeviceClass *device_class = GDK_DEVICE_CLASS (klass);

  object_class->constructed = gdk_device_xi_constructed;
  object_class->set_property = gdk_device_xi_set_property;
  object_class->get_property = gdk_device_xi_get_property;

  device_class->get_history = gdk_device_xi_get_history;
  device_class->get_state = gdk_device_xi_get_state;

  g_object_class_install_property (object_class,
				   PROP_DEVICE_ID,
				   g_param_spec_int ("device-id",
                                                     P_("Device ID"),
                                                     P_("Device ID"),
                                                     0, G_MAXINT, 0,
                                                     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
gdk_device_xi_init (GdkDeviceXI *device)
{
}

static void
gdk_device_xi_constructed (GObject *object)
{
  GdkDeviceXI *device;
  GdkDisplay *display;

  device = GDK_DEVICE_XI (object);
  display = gdk_device_get_display (GDK_DEVICE (object));

  gdk_error_trap_push ();
  device->xdevice = XOpenDevice (GDK_DISPLAY_XDISPLAY (display),
                                 device->device_id);

  if (gdk_error_trap_pop ())
    g_warning ("Device %s can't be opened", GDK_DEVICE (device)->name);

  if (G_OBJECT_CLASS (gdk_device_xi_parent_class)->constructed)
    G_OBJECT_CLASS (gdk_device_xi_parent_class)->constructed (object);
}

static void
gdk_device_xi_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  GdkDeviceXI *device = GDK_DEVICE_XI (object);

  switch (prop_id)
    {
    case PROP_DEVICE_ID:
      device->device_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gdk_device_xi_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  GdkDeviceXI *device = GDK_DEVICE_XI (object);

  switch (prop_id)
    {
    case PROP_DEVICE_ID:
      g_value_set_int (value, device->device_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static GdkTimeCoord **
allocate_history (GdkDevice *device,
                  gint       n_events)
{
  GdkTimeCoord **result = g_new (GdkTimeCoord *, n_events);
  gint i;

  for (i = 0; i < n_events; i++)
    result[i] = g_malloc (sizeof (GdkTimeCoord) -
			  sizeof (double) * (GDK_MAX_TIMECOORD_AXES - device->num_axes));
  return result;
}

static gboolean
gdk_device_xi_get_history (GdkDevice      *device,
                           GdkWindow      *window,
                           guint32         start,
                           guint32         stop,
                           GdkTimeCoord ***events,
                           guint          *n_events)
{
  GdkTimeCoord **coords;
  XDeviceTimeCoord *device_coords;
  GdkWindow *impl_window;
  GdkDeviceXI *device_xi;
  gint n_events_return;
  gint mode_return;
  gint axis_count_return;
  gint n_axes, i, j;
  gint width, height;

  device_xi = GDK_DEVICE_XI (device);
  impl_window = _gdk_window_get_impl_window (window);
  g_object_get (device, "n-axes", &n_axes, NULL);

  gdk_drawable_get_size (GDK_DRAWABLE (window), &width, &height);

  device_coords = XGetDeviceMotionEvents (GDK_WINDOW_XDISPLAY (impl_window),
					  device_xi->xdevice,
					  start, stop,
					  &n_events_return,
                                          &mode_return,
					  &axis_count_return);

  if (!device_coords)
    return FALSE;

  coords = allocate_history (device, *n_events);

  for (i = 0; i < *n_events; i++)
    {
      coords[i]->time = device_coords[i].time;

      for (j = 0; j < n_axes; j++)
        {
          _gdk_device_translate_axis (device,
                                      width, height,
                                      0, 0,
                                      j,
                                      (gdouble) device_coords[i].data[j],
                                      &coords[i]->axes[j]);
        }
    }

  XFreeDeviceMotionEvents (device_coords);

  *events = coords;
  *n_events = (guint) n_events_return;

  return TRUE;
}

static void
gdk_device_xi_get_state (GdkDevice       *device,
                         GdkWindow       *window,
                         gdouble         *axes,
                         GdkModifierType *mask)
{
  GdkDeviceXI *device_xi;
  XDeviceState *state;
  XInputClass *input_class;
  gint i, j;

  if (mask)
    gdk_window_get_pointer (window, NULL, NULL, mask);

  device_xi = GDK_DEVICE_XI (device);

  state = XQueryDeviceState (GDK_WINDOW_XDISPLAY (window),
                             device_xi->xdevice);
  input_class = state->data;

  for (i = 0; i < state->num_classes; i++)
    {
      switch (input_class->class)
        {
        case ValuatorClass:
          if (axes)
            {
              gint width, height;

              gdk_drawable_get_size (GDK_DRAWABLE (window), &width, &height);

              for (j = 0; j < device->num_axes; j++)
                {
                  _gdk_device_translate_axis (device,
                                              width, height,
                                              0, 0,
                                              j,
                                              (gdouble) ((XValuatorState *) input_class)->valuators[j],
                                              &axes[j]);
                }
            }
          break;

        case ButtonClass:
          if (mask)
            {
              *mask &= 0xFF;
              if (((XButtonState *)input_class)->num_buttons > 0)
                *mask |= ((XButtonState *)input_class)->buttons[0] << 7;
              /* GDK_BUTTON1_MASK = 1 << 8, and button n is stored
               * in bit 1<<(n%8) in byte n/8. n = 1,2,... */
            }
          break;
        }

      input_class = (XInputClass *)(((char *)input_class)+input_class->length);
    }

  XFreeDeviceState (state);
}
