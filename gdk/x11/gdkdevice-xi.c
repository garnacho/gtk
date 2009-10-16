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

#define MAX_DEVICE_CLASSES 13

static GQuark quark_window_input_info = 0;

typedef struct
{
  gdouble root_x;
  gdouble root_y;
} GdkWindowInputInfo;

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
static void gdk_device_xi_set_window_cursor (GdkDevice *device,
                                             GdkWindow *window,
                                             GdkCursor *cursor);
static void gdk_device_xi_warp              (GdkDevice *device,
                                             GdkScreen *screen,
                                             gint       x,
                                             gint       y);
static gboolean gdk_device_xi_query_state   (GdkDevice        *device,
                                             GdkWindow        *window,
                                             GdkWindow       **root_window,
                                             GdkWindow       **child_window,
                                             gint             *root_x,
                                             gint             *root_y,
                                             gint             *win_x,
                                             gint             *win_y,
                                             GdkModifierType  *mask);
static GdkGrabStatus gdk_device_xi_grab     (GdkDevice    *device,
                                             GdkWindow    *window,
                                             gboolean      owner_events,
                                             GdkEventMask  event_mask,
                                             GdkWindow    *confine_to,
                                             GdkCursor    *cursor,
                                             guint32       time_);
static void          gdk_device_xi_ungrab   (GdkDevice    *device,
                                             guint32       time_);

static GdkWindow* gdk_device_xi_window_at_position (GdkDevice       *device,
                                                    gint            *win_x,
                                                    gint            *win_y,
                                                    GdkModifierType *mask);

static void gdk_device_xi_select_window_events (GdkDevice    *device,
                                                GdkWindow    *window,
                                                GdkEventMask  mask);


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

  quark_window_input_info = g_quark_from_static_string ("gdk-window-input-info");

  object_class->constructed = gdk_device_xi_constructed;
  object_class->set_property = gdk_device_xi_set_property;
  object_class->get_property = gdk_device_xi_get_property;
  object_class->dispose = gdk_device_xi_dispose;

  device_class->get_history = gdk_device_xi_get_history;
  device_class->get_state = gdk_device_xi_get_state;
  device_class->set_window_cursor = gdk_device_xi_set_window_cursor;
  device_class->warp = gdk_device_xi_warp;
  device_class->query_state = gdk_device_xi_query_state;
  device_class->grab = gdk_device_xi_grab;
  device_class->ungrab = gdk_device_xi_ungrab;
  device_class->window_at_position = gdk_device_xi_window_at_position;
  device_class->select_window_events = gdk_device_xi_select_window_events;

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

static void
gdk_device_xi_dispose (GObject *object)
{
  GdkDeviceXI *device_xi;
  GdkDisplay *display;

  device_xi = GDK_DEVICE_XI (object);
  display = gdk_device_get_display (GDK_DEVICE (device_xi));

  if (device_xi->xdevice)
    {
      XCloseDevice (GDK_DISPLAY_XDISPLAY (display), device_xi->xdevice);
      device_xi->xdevice = NULL;
    }

  G_OBJECT_CLASS (gdk_device_xi_parent_class)->dispose (object);
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
  gint i;

  device_xi = GDK_DEVICE_XI (device);
  impl_window = _gdk_window_get_impl_window (window);

  device_coords = XGetDeviceMotionEvents (GDK_WINDOW_XDISPLAY (impl_window),
					  device_xi->xdevice,
					  start, stop,
					  &n_events_return,
                                          &mode_return,
					  &axis_count_return);

  if (!device_coords)
    return FALSE;

  *n_events = (guint) n_events_return;
  coords = _gdk_device_allocate_history (device, *n_events);

  for (i = 0; i < *n_events; i++)
    {
      coords[i]->time = device_coords[i].time;
      gdk_device_xi_translate_axes (device, window,
                                    device_coords[i].data,
                                    coords[i]->axes,
                                    NULL, NULL);
    }

  XFreeDeviceMotionEvents (device_coords);

  *events = coords;

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
  gint i;

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
            gdk_device_xi_translate_axes (device, window,
                                          ((XValuatorState *) input_class)->valuators,
                                          axes, NULL, NULL);
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

static void
gdk_device_xi_set_window_cursor (GdkDevice *device,
                                 GdkWindow *window,
                                 GdkCursor *cursor)
{
}

static void
gdk_device_xi_warp (GdkDevice *device,
                    GdkScreen *screen,
                    gint       x,
                    gint       y)
{
}

static void
find_events (GdkDevice    *device,
             GdkEventMask  mask,
             XEventClass  *classes,
             int          *num_classes)
{
  GdkDeviceXI *device_xi;
  XEventClass class;
  gint i;

  device_xi = GDK_DEVICE_XI (device);
  i = 0;

  if (mask & GDK_BUTTON_PRESS_MASK)
    {
      DeviceButtonPress (device_xi->xdevice, device_xi->button_press_type, class);
      if (class != 0)
        classes[i++] = class;

      DeviceButtonPressGrab (device_xi->xdevice, 0, class);
      if (class != 0)
        classes[i++] = class;
    }

  if (mask & GDK_BUTTON_RELEASE_MASK)
    {
      DeviceButtonRelease (device_xi->xdevice, device_xi->button_release_type, class);
      if (class != 0)
        classes[i++] = class;
    }

  if (mask & GDK_POINTER_MOTION_MASK)
    {
      DeviceMotionNotify (device_xi->xdevice, device_xi->motion_notify_type, class);
      if (class != 0)
        classes[i++] = class;
    }
  else if (mask & (GDK_BUTTON1_MOTION_MASK | GDK_BUTTON2_MOTION_MASK |
                   GDK_BUTTON3_MOTION_MASK | GDK_BUTTON_MOTION_MASK |
                   GDK_POINTER_MOTION_HINT_MASK))
    {
      /* Make sure device->motionnotify_type is set */
      DeviceMotionNotify (device_xi->xdevice, device_xi->motion_notify_type, class);
    }

  if (mask & GDK_BUTTON1_MOTION_MASK)
    {
      DeviceButton1Motion (device_xi->xdevice, 0, class);
      if (class != 0)
        classes[i++] = class;
    }

  if (mask & GDK_BUTTON2_MOTION_MASK)
    {
      DeviceButton2Motion (device_xi->xdevice, 0, class);
      if (class != 0)
        classes[i++] = class;
    }

  if (mask & GDK_BUTTON3_MOTION_MASK)
    {
      DeviceButton3Motion (device_xi->xdevice, 0, class);
      if (class != 0)
        classes[i++] = class;
    }

  if (mask & GDK_BUTTON_MOTION_MASK)
    {
      DeviceButtonMotion (device_xi->xdevice, 0, class);
      if (class != 0)
        classes[i++] = class;
    }

  if (mask & GDK_POINTER_MOTION_HINT_MASK)
    {
      /* We'll get into trouble if the macros change, but at
       * least we'll know about it, and we avoid warnings now
       */
      DevicePointerMotionHint (device_xi->xdevice, 0, class);
      if (class != 0)
        classes[i++] = class;
    }

  if (mask & GDK_KEY_PRESS_MASK)
    {
      DeviceKeyPress (device_xi->xdevice, device_xi->key_press_type, class);
      if (class != 0)
        classes[i++] = class;
    }

  if (mask & GDK_KEY_RELEASE_MASK)
    {
      DeviceKeyRelease (device_xi->xdevice, device_xi->key_release_type, class);
      if (class != 0)
        classes[i++] = class;
    }

  if (mask & GDK_PROXIMITY_IN_MASK)
    {
      ProximityIn (device_xi->xdevice, device_xi->proximity_in_type, class);
      if (class != 0)
        classes[i++] = class;
    }

  if (mask & GDK_PROXIMITY_OUT_MASK)
    {
      ProximityOut (device_xi->xdevice, device_xi->proximity_out_type, class);
      if (class != 0)
        classes[i++] = class;
    }

  *num_classes = i;
}

static gboolean
gdk_device_xi_query_state (GdkDevice        *device,
                           GdkWindow        *window,
                           GdkWindow       **root_window,
                           GdkWindow       **child_window,
                           gint             *root_x,
                           gint             *root_y,
                           gint             *win_x,
                           gint             *win_y,
                           GdkModifierType  *mask)
{
  return FALSE;
}

static GdkGrabStatus
gdk_device_xi_grab (GdkDevice    *device,
                    GdkWindow    *window,
                    gboolean      owner_events,
                    GdkEventMask  event_mask,
                    GdkWindow    *confine_to,
                    GdkCursor    *cursor,
                    guint32       time_)
{
  XEventClass event_classes[MAX_DEVICE_CLASSES];
  gint status, num_classes;
  GdkDeviceXI *device_xi;

  device_xi = GDK_DEVICE_XI (device);
  find_events (device, event_mask, event_classes, &num_classes);

  status = XGrabDevice (GDK_WINDOW_XDISPLAY (window),
                        device_xi->xdevice,
                        GDK_WINDOW_XWINDOW (window),
                        owner_events,
                        num_classes, event_classes,
                        GrabModeAsync, GrabModeAsync,
                        time_);

  return gdk_x11_convert_grab_status (status);
}

static void
gdk_device_xi_ungrab (GdkDevice *device,
                      guint32    time_)
{
  GdkDisplay *display;
  GdkDeviceXI *device_xi;

  device_xi = GDK_DEVICE_XI (device);
  display = gdk_device_get_display (device);

  XUngrabDevice (GDK_DISPLAY_XDISPLAY (device),
                 device_xi->xdevice,
                 time_);
}

static GdkWindow*
gdk_device_xi_window_at_position (GdkDevice       *device,
                                  gint            *win_x,
                                  gint            *win_y,
                                  GdkModifierType *mask)
{
  return NULL;
}
static void
gdk_device_xi_select_window_events (GdkDevice    *device,
                                    GdkWindow    *window,
                                    GdkEventMask  event_mask)
{
  XEventClass event_classes[MAX_DEVICE_CLASSES];
  GdkDeviceXI *device_xi;
  gint num_classes;

  event_mask |= (GDK_PROXIMITY_IN_MASK |
                 GDK_PROXIMITY_OUT_MASK);

  device_xi = GDK_DEVICE_XI (device);
  find_events (device, event_mask, event_classes, &num_classes);

  XSelectExtensionEvent (GDK_WINDOW_XDISPLAY (window),
			 GDK_WINDOW_XWINDOW (window),
			 event_classes, num_classes);

  if (event_mask)
    {
      GdkWindowInputInfo *info;

      info = g_new0 (GdkWindowInputInfo, 1);
      g_object_set_qdata_full (G_OBJECT (window),
                               quark_window_input_info,
                               info,
                               (GDestroyNotify) g_free);
    }
  else
    g_object_set_qdata (G_OBJECT (window),
                        quark_window_input_info,
                        NULL);
}

void
gdk_device_xi_update_window_info (GdkWindow *window)
{
  GdkWindowInputInfo *info;
  gint root_x, root_y;

  info = g_object_get_qdata (G_OBJECT (window),
                             quark_window_input_info);

  if (!info)
    return;

  gdk_window_get_origin (window, &root_x, &root_y);
  info->root_x = (gdouble) root_x;
  info->root_y = (gdouble) root_y;
}

static gboolean
gdk_device_xi_get_window_info (GdkWindow *window,
                               gdouble   *root_x,
                               gdouble   *root_y)
{
  GdkWindowInputInfo *info;

  info = g_object_get_qdata (G_OBJECT (window),
                             quark_window_input_info);

  if (!info)
    return FALSE;

  *root_x = info->root_x;
  *root_y = info->root_y;

  return TRUE;
}

void
gdk_device_xi_translate_axes (GdkDevice *device,
                              GdkWindow *window,
                              gint      *axis_data,
                              gdouble   *axes,
                              gdouble   *x,
                              gdouble   *y)
{
  GdkWindow *impl_window;
  gdouble root_x, root_y;
  gdouble temp_x, temp_y;
  gint i;

  impl_window = _gdk_window_get_impl_window (window);
  temp_x = temp_y = 0;

  if (!gdk_device_xi_get_window_info (impl_window, &root_x, &root_y))
    return;

  for (i = 0; i < device->num_axes; i++)
    {
      GdkAxisUse use;

      use = _gdk_device_get_axis_use (device, i);

      switch (use)
        {
        case GDK_AXIS_X:
        case GDK_AXIS_Y:
          if (device->mode == GDK_MODE_WINDOW)
            _gdk_device_translate_window_coord (device, window,
                                                i, axis_data[i],
                                                &axes[i]);
          else
            _gdk_device_translate_screen_coord (device, window,
                                                root_x, root_y,
                                                i, axis_data[i],
                                                &axes[i]);
          if (use == GDK_AXIS_X)
            temp_x = axes[i];
          else if (use == GDK_AXIS_Y)
            temp_y = axes[i];

          break;
        default:
          _gdk_device_translate_axis (device, i, axis_data[i], &axes[i]);
          break;
        }
    }

  if (x)
    *x = temp_x;

  if (y)
    *y = temp_y;
}
