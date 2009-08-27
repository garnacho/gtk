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

#include <X11/extensions/XInput2.h>
#include "gdkdevice-xi2.h"
#include "gdkintl.h"
#include "gdkx.h"


#define GDK_DEVICE_XI2_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDK_TYPE_DEVICE_XI2, GdkDeviceXI2Private))

typedef struct GdkDeviceXI2Private GdkDeviceXI2Private;

struct GdkDeviceXI2Private
{
  int device_id;
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
static void gdk_device_xi2_set_window_cursor (GdkDevice *device,
                                              GdkWindow *window,
                                              GdkCursor *cursor);
static void gdk_device_xi2_warp (GdkDevice *device,
                                 GdkScreen *screen,
                                 gint       x,
                                 gint       y);
static gboolean gdk_device_xi2_query_state (GdkDevice        *device,
                                            GdkWindow        *window,
                                            GdkWindow       **root_window,
                                            GdkWindow       **child_window,
                                            gint             *root_x,
                                            gint             *root_y,
                                            gint             *win_x,
                                            gint             *win_y,
                                            GdkModifierType  *mask);
static GdkWindow * gdk_device_xi2_window_at_position (GdkDevice *device,
                                                      gint      *win_x,
                                                      gint      *win_y);


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
  device_class->set_window_cursor = gdk_device_xi2_set_window_cursor;
  device_class->warp = gdk_device_xi2_warp;
  device_class->query_state = gdk_device_xi2_query_state;
  device_class->window_at_position = gdk_device_xi2_window_at_position;

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
  /* FIXME: Implement */
}

static void
gdk_device_xi2_set_window_cursor (GdkDevice *device,
                                  GdkWindow *window,
                                  GdkCursor *cursor)
{
  GdkDeviceXI2Private *priv;
  GdkCursorPrivate *cursor_private;

  priv = GDK_DEVICE_XI2_GET_PRIVATE (device);

  if (cursor)
    {
      cursor_private = (GdkCursorPrivate*) cursor;

      XIDefineCursor (GDK_WINDOW_XDISPLAY (window),
                      priv->device_id,
                      GDK_WINDOW_XWINDOW (window),
                      cursor_private->xcursor);
    }
  else
    XIUndefineCursor (GDK_WINDOW_XDISPLAY (window),
                      priv->device_id,
                      GDK_WINDOW_XWINDOW (window));
}

static void
gdk_device_xi2_warp (GdkDevice *device,
                     GdkScreen *screen,
                     gint       x,
                     gint       y)
{
  GdkDeviceXI2Private *priv;
  Window dest;

  priv = GDK_DEVICE_XI2_GET_PRIVATE (device);
  dest = GDK_WINDOW_XWINDOW (gdk_screen_get_root_window (screen));

  XIWarpPointer (GDK_SCREEN_XDISPLAY (screen),
                 priv->device_id,
                 None, dest,
                 0, 0, 0, 0, x, y);
}

static gboolean
gdk_device_xi2_query_state (GdkDevice        *device,
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
  GdkDeviceXI2Private *priv;
  Window xroot_window, xchild_window;
  int xroot_x, xroot_y, xwin_x, xwin_y;
  XIButtonState button_state;
  XIModifierState mod_state;
  XIGroupState group_state;
  unsigned int xmask;

  if (!window || GDK_WINDOW_DESTROYED (window))
    return FALSE;

  priv = GDK_DEVICE_XI2_GET_PRIVATE (device);
  display = gdk_drawable_get_display (window);

  /* FIXME: XIQueryPointer crashes ATM, use when Xorg is fixed */
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

static GdkWindow *
gdk_device_xi2_window_at_position (GdkDevice *device,
                                   gint      *win_x,
                                   gint      *win_y)
{
  GdkDisplay *display;
  GdkScreen *screen;
  Display *xdisplay;
  GdkWindow *window;
  Window xwindow, root, child, last = None;
  int xroot_x, xroot_y, xwin_x, xwin_y;
  unsigned int xmask;

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

  /* FIXME: XIQueryPointer crashes ATM, use when Xorg is fixed */
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

  return window;
}

guchar *
gdk_device_xi2_translate_event_mask (GdkEventMask  event_mask,
                                     int          *len)
{
  guchar *mask;
  int mask_len;

  *len = XIMaskLen (XI_LASTEVENT);
  mask = g_new0 (guchar, *len);

  if (event_mask & GDK_POINTER_MOTION_MASK ||
      event_mask & GDK_POINTER_MOTION_HINT_MASK)
    XISetMask (mask, XI_Motion);

  if (event_mask & GDK_BUTTON_MOTION_MASK ||
      event_mask & GDK_BUTTON1_MOTION_MASK ||
      event_mask & GDK_BUTTON2_MOTION_MASK ||
      event_mask & GDK_BUTTON3_MOTION_MASK)
    {
      XISetMask (mask, XI_ButtonPress);
      XISetMask (mask, XI_ButtonRelease);
      XISetMask (mask, XI_Motion);
    }

  if (event_mask & GDK_SCROLL_MASK)
    {
      XISetMask (mask, XI_ButtonPress);
      XISetMask (mask, XI_ButtonRelease);
    }

  if (event_mask & GDK_BUTTON_PRESS_MASK)
    XISetMask (mask, XI_ButtonPress);

  if (event_mask & GDK_BUTTON_RELEASE_MASK)
    XISetMask (mask, XI_ButtonRelease);

  if (event_mask & GDK_KEY_PRESS_MASK)
    XISetMask (mask, XI_KeyPress);

  if (event_mask & GDK_KEY_RELEASE_MASK)
    XISetMask (mask, XI_KeyRelease);

  if (event_mask & GDK_ENTER_NOTIFY_MASK)
    XISetMask (mask, XI_Enter);

  if (event_mask & GDK_LEAVE_NOTIFY_MASK)
    XISetMask (mask, XI_Leave);

  if (event_mask & GDK_FOCUS_CHANGE_MASK)
    {
      XISetMask (mask, XI_FocusIn);
      XISetMask (mask, XI_FocusOut);
    }

  return mask;
}
