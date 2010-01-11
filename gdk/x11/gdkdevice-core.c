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

#include "config.h"

#include <gdk/gdkwindow.h>
#include "gdkdevice-core.h"
#include "gdkprivate-x11.h"
#include "gdkx.h"

static gboolean gdk_device_core_get_history (GdkDevice      *device,
                                             GdkWindow      *window,
                                             guint32         start,
                                             guint32         stop,
                                             GdkTimeCoord ***events,
                                             guint          *n_events);
static void gdk_device_core_get_state (GdkDevice       *device,
                                       GdkWindow       *window,
                                       gdouble         *axes,
                                       GdkModifierType *mask);
static void gdk_device_core_set_window_cursor (GdkDevice *device,
                                               GdkWindow *window,
                                               GdkCursor *cursor);
static void gdk_device_core_warp (GdkDevice *device,
                                  GdkScreen *screen,
                                  gint       x,
                                  gint       y);
static gboolean gdk_device_core_query_state (GdkDevice        *device,
                                             GdkWindow        *window,
                                             GdkWindow       **root_window,
                                             GdkWindow       **child_window,
                                             gint             *root_x,
                                             gint             *root_y,
                                             gint             *win_x,
                                             gint             *win_y,
                                             GdkModifierType  *mask);
static GdkGrabStatus gdk_device_core_grab   (GdkDevice     *device,
                                             GdkWindow     *window,
                                             gboolean       owner_events,
                                             GdkEventMask   event_mask,
                                             GdkWindow     *confine_to,
                                             GdkCursor     *cursor,
                                             guint32        time_);
static void          gdk_device_core_ungrab (GdkDevice     *device,
                                             guint32        time_);
static GdkWindow * gdk_device_core_window_at_position (GdkDevice       *device,
                                                       gint            *win_x,
                                                       gint            *win_y,
                                                       GdkModifierType *mask,
                                                       gboolean         get_toplevel);
static void      gdk_device_core_select_window_events (GdkDevice       *device,
                                                       GdkWindow       *window,
                                                       GdkEventMask     event_mask);


G_DEFINE_TYPE (GdkDeviceCore, gdk_device_core, GDK_TYPE_DEVICE)

static void
gdk_device_core_class_init (GdkDeviceCoreClass *klass)
{
  GdkDeviceClass *device_class = GDK_DEVICE_CLASS (klass);

  device_class->get_history = gdk_device_core_get_history;
  device_class->get_state = gdk_device_core_get_state;
  device_class->set_window_cursor = gdk_device_core_set_window_cursor;
  device_class->warp = gdk_device_core_warp;
  device_class->query_state = gdk_device_core_query_state;
  device_class->grab = gdk_device_core_grab;
  device_class->ungrab = gdk_device_core_ungrab;
  device_class->window_at_position = gdk_device_core_window_at_position;
  device_class->select_window_events = gdk_device_core_select_window_events;
}

static void
gdk_device_core_init (GdkDeviceCore *device_core)
{
  GdkDevice *device;

  device = GDK_DEVICE (device_core);

  _gdk_device_add_axis (device, GDK_NONE, GDK_AXIS_X, 0, 0, 1);
  _gdk_device_add_axis (device, GDK_NONE, GDK_AXIS_Y, 0, 0, 1);
}

static gboolean
impl_coord_in_window (GdkWindow *window,
		      int        impl_x,
		      int        impl_y)
{
  GdkWindowObject *priv = (GdkWindowObject *) window;

  if (impl_x < priv->abs_x ||
      impl_x > priv->abs_x + priv->width)
    return FALSE;

  if (impl_y < priv->abs_y ||
      impl_y > priv->abs_y + priv->height)
    return FALSE;

  return TRUE;
}

static gboolean
gdk_device_core_get_history (GdkDevice      *device,
                             GdkWindow      *window,
                             guint32         start,
                             guint32         stop,
                             GdkTimeCoord ***events,
                             guint          *n_events)
{
  GdkWindowObject *priv;
  XTimeCoord *xcoords;
  GdkTimeCoord **coords;
  GdkWindow *impl_window;
  int tmp_n_events;
  int i, j;

  impl_window = _gdk_window_get_impl_window (window);
  xcoords = XGetMotionEvents (GDK_DRAWABLE_XDISPLAY (window),
                              GDK_DRAWABLE_XID (impl_window),
                              start, stop, &tmp_n_events);
  if (!xcoords)
    return FALSE;

  priv = (GdkWindowObject *) window;
  coords = _gdk_device_allocate_history (device, tmp_n_events);

  for (i = 0, j = 0; i < tmp_n_events; i++)
    {
      if (impl_coord_in_window (window, xcoords[i].x, xcoords[i].y))
        {
          coords[j]->time = xcoords[i].time;
          coords[j]->axes[0] = xcoords[i].x - priv->abs_x;
          coords[j]->axes[1] = xcoords[i].y - priv->abs_y;
          j++;
        }
    }

  XFree (xcoords);

  /* free the events we allocated too much */
  for (i = j; i < tmp_n_events; i++)
    {
      g_free (coords[i]);
      coords[i] = NULL;
    }

  tmp_n_events = j;

  if (tmp_n_events == 0)
    {
      gdk_device_free_history (coords, tmp_n_events);
      return FALSE;
    }

  if (n_events)
    *n_events = tmp_n_events;

  if (events)
    *events = coords;
  else if (coords)
    gdk_device_free_history (coords, tmp_n_events);

  return TRUE;
}

static void
gdk_device_core_get_state (GdkDevice       *device,
                           GdkWindow       *window,
                           gdouble         *axes,
                           GdkModifierType *mask)
{
  gint x_int, y_int;

  gdk_window_get_pointer (window, &x_int, &y_int, mask);

  if (axes)
    {
      axes[0] = x_int;
      axes[1] = y_int;
    }
}

static void
gdk_device_core_set_window_cursor (GdkDevice *device,
                                   GdkWindow *window,
                                   GdkCursor *cursor)
{
  GdkCursorPrivate *cursor_private;
  Cursor xcursor;

  cursor_private = (GdkCursorPrivate*) cursor;

  if (!cursor)
    xcursor = None;
  else
    xcursor = cursor_private->xcursor;

  XDefineCursor (GDK_WINDOW_XDISPLAY (window),
                 GDK_WINDOW_XID (window),
                 xcursor);
}

static void
gdk_device_core_warp (GdkDevice *device,
                      GdkScreen *screen,
                      gint       x,
                      gint       y)
{
  Display *xdisplay;
  Window dest;

  xdisplay = GDK_DISPLAY_XDISPLAY (gdk_device_get_display (device));
  dest = GDK_WINDOW_XWINDOW (gdk_screen_get_root_window (screen));

  XWarpPointer (xdisplay, None, dest, 0, 0, 0, 0, x, y);
}

static gboolean
gdk_device_core_query_state (GdkDevice        *device,
                             GdkWindow        *window,
                             GdkWindow       **root_window,
                             GdkWindow       **child_window,
                             gint             *root_x,
                             gint             *root_y,
                             gint             *win_x,
                             gint             *win_y,
                             GdkModifierType  *mask)
{
  GdkDisplay *display;
  Window xroot_window, xchild_window;
  int xroot_x, xroot_y, xwin_x, xwin_y;
  unsigned int xmask;

  display = gdk_drawable_get_display (window);

  if (!XQueryPointer (GDK_WINDOW_XDISPLAY (window),
                      GDK_WINDOW_XID (window),
                      &xroot_window,
                      &xchild_window,
                      &xroot_x,
                      &xroot_y,
                      &xwin_x,
                      &xwin_y,
                      &xmask))
    {
      return FALSE;
    }

  if (root_window)
    *root_window = gdk_window_lookup_for_display (display, xroot_window);

  if (child_window)
    *child_window = gdk_window_lookup_for_display (display, xchild_window);

  if (root_x)
    *root_x = xroot_x;

  if (root_y)
    *root_y = xroot_y;

  if (win_x)
    *win_x = xwin_x;

  if (win_y)
    *win_y = xwin_y;

  if (mask)
    *mask = xmask;

  return TRUE;
}

static GdkGrabStatus
gdk_device_core_grab (GdkDevice    *device,
                      GdkWindow    *window,
                      gboolean      owner_events,
                      GdkEventMask  event_mask,
                      GdkWindow    *confine_to,
                      GdkCursor    *cursor,
                      guint32       time_)
{
  GdkDisplay *display;
  Window xwindow, xconfine_to;
  int status;

  display = gdk_device_get_display (device);

  xwindow = GDK_WINDOW_XID (window);

  if (confine_to)
    confine_to = _gdk_window_get_impl_window (confine_to);

  if (!confine_to || GDK_WINDOW_DESTROYED (confine_to))
    xconfine_to = None;
  else
    xconfine_to = GDK_WINDOW_XID (confine_to);

  if (device->source == GDK_SOURCE_KEYBOARD)
    {
      /* Device is a keyboard */
      status = XGrabKeyboard (GDK_DISPLAY_XDISPLAY (display),
                              xwindow,
                              owner_events,
                              GrabModeAsync, GrabModeAsync,
                              time_);
    }
  else
    {
      Cursor xcursor;
      guint xevent_mask;
      gint i;

      /* Device is a pointer */
      if (!cursor)
        xcursor = None;
      else
        {
          _gdk_x11_cursor_update_theme (cursor);
          xcursor = ((GdkCursorPrivate *) cursor)->xcursor;
        }

      xevent_mask = 0;

      for (i = 0; i < _gdk_nenvent_masks; i++)
        {
          if (event_mask & (1 << (i + 1)))
            xevent_mask |= _gdk_event_mask_table[i];
        }

      /* We don't want to set a native motion hint mask, as we're emulating motion
       * hints. If we set a native one we just wouldn't get any events.
       */
      xevent_mask &= ~PointerMotionHintMask;

      status = XGrabPointer (GDK_DISPLAY_XDISPLAY (display),
                             xwindow,
                             owner_events,
                             xevent_mask,
                             GrabModeAsync, GrabModeAsync,
                             xconfine_to,
                             xcursor,
                             time_);
    }

  return gdk_x11_convert_grab_status (status);
}

static void
gdk_device_core_ungrab (GdkDevice *device,
                        guint32    time_)
{
  GdkDisplay *display;

  display = gdk_device_get_display (device);

  if (device->source == GDK_SOURCE_KEYBOARD)
    XUngrabKeyboard (GDK_DISPLAY_XDISPLAY (display), time_);
  else
    XUngrabPointer (GDK_DISPLAY_XDISPLAY (display), time_);
}

static GdkWindow *
gdk_device_core_window_at_position (GdkDevice       *device,
                                    gint            *win_x,
                                    gint            *win_y,
                                    GdkModifierType *mask,
                                    gboolean         get_toplevel)
{
  GdkDisplay *display;
  GdkScreen *screen;
  Display *xdisplay;
  GdkWindow *window;
  Window xwindow, root, child, last;
  int xroot_x, xroot_y, xwin_x, xwin_y;
  unsigned int xmask;

  last = None;
  display = gdk_device_get_display (device);
  screen = gdk_display_get_default_screen (display);

  /* This function really only works if the mouse pointer is held still
   * during its operation. If it moves from one leaf window to another
   * than we'll end up with inaccurate values for win_x, win_y
   * and the result.
   */
  gdk_x11_display_grab (display);

  xdisplay = GDK_SCREEN_XDISPLAY (screen);
  xwindow = GDK_SCREEN_XROOTWIN (screen);

  XQueryPointer (xdisplay, xwindow,
                 &root, &child,
                 &xroot_x, &xroot_y,
                 &xwin_x, &xwin_y,
                 &xmask);

  if (root == xwindow)
    xwindow = child;
  else
    xwindow = root;

  while (xwindow)
    {
      last = xwindow;
      XQueryPointer (xdisplay, xwindow,
                     &root, &xwindow,
                     &xroot_x, &xroot_y,
                     &xwin_x, &xwin_y,
                     &xmask);

      if (get_toplevel && last != root &&
          (window = gdk_window_lookup_for_display (display, last)) != NULL &&
          GDK_WINDOW_TYPE (window) != GDK_WINDOW_FOREIGN)
        {
          xwindow = last;
          break;
        }
    }

  gdk_x11_display_ungrab (display);

  window = gdk_window_lookup_for_display (display, last);

  if (win_x)
    *win_x = (window) ? xwin_x : -1;

  if (win_y)
    *win_y = (window) ? xwin_y : -1;

  if (mask)
    *mask = xmask;

  return window;
}

static void
gdk_device_core_select_window_events (GdkDevice    *device,
                                      GdkWindow    *window,
                                      GdkEventMask  event_mask)
{
  GdkEventMask filter_mask, window_mask;
  guint xmask = 0;
  gint i;

  window_mask = gdk_window_get_events (window);
  filter_mask = (GDK_POINTER_MOTION_MASK &
                 GDK_POINTER_MOTION_HINT_MASK &
                 GDK_BUTTON_MOTION_MASK &
                 GDK_BUTTON1_MOTION_MASK &
                 GDK_BUTTON2_MOTION_MASK &
                 GDK_BUTTON3_MOTION_MASK &
                 GDK_BUTTON_PRESS_MASK &
                 GDK_BUTTON_RELEASE_MASK &
                 GDK_KEY_PRESS_MASK &
                 GDK_KEY_RELEASE_MASK &
                 GDK_ENTER_NOTIFY_MASK &
                 GDK_LEAVE_NOTIFY_MASK &
                 GDK_FOCUS_CHANGE_MASK &
                 GDK_PROXIMITY_IN_MASK &
                 GDK_PROXIMITY_OUT_MASK &
                 GDK_SCROLL_MASK);

  /* Filter out non-device events */
  event_mask &= filter_mask;

  /* Unset device events on window mask */
  window_mask &= ~(filter_mask);

  /* Combine masks */
  event_mask |= window_mask;

  for (i = 0; i < _gdk_nenvent_masks; i++)
    {
      if (event_mask & (1 << (i + 1)))
        xmask |= _gdk_event_mask_table[i];
    }

  if (GDK_WINDOW_XID (window) != GDK_WINDOW_XROOTWIN (window))
    xmask |= StructureNotifyMask | PropertyChangeMask;

  XSelectInput (GDK_WINDOW_XDISPLAY (window),
                GDK_WINDOW_XWINDOW (window),
                xmask);
}
