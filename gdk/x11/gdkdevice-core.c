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
#include "gdkdevice-core.h"
#include "gdkprivate-x11.h"
#include "gdkx.h"

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
                                                       GdkModifierType *mask);


G_DEFINE_TYPE (GdkDeviceCore, gdk_device_core, GDK_TYPE_DEVICE)

static GdkDeviceAxis gdk_device_core_axes[] = {
  { GDK_AXIS_X, 0, 0 },
  { GDK_AXIS_Y, 0, 0 }
};

static void
gdk_device_core_class_init (GdkDeviceCoreClass *klass)
{
  GdkDeviceClass *device_class = GDK_DEVICE_CLASS (klass);

  device_class->get_state = gdk_device_core_get_state;
  device_class->set_window_cursor = gdk_device_core_set_window_cursor;
  device_class->warp = gdk_device_core_warp;
  device_class->query_state = gdk_device_core_query_state;
  device_class->grab = gdk_device_core_grab;
  device_class->ungrab = gdk_device_core_ungrab;
  device_class->window_at_position = gdk_device_core_window_at_position;
}

static void
gdk_device_core_init (GdkDeviceCore *device_core)
{
  GdkDevice *device;

  device = GDK_DEVICE (device_core);

  device->num_axes = G_N_ELEMENTS (gdk_device_core_axes);
  device->axes = gdk_device_core_axes;

  device->num_keys = 0;
  device->keys = NULL;
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
                                    GdkModifierType *mask)
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
