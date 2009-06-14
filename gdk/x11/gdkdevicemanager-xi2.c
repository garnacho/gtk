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

#include "gdkdevicemanager-xi2.h"
#include "gdkeventtranslator.h"
#include "gdkinputprivate.h"
#include "gdkx.h"

static void    gdk_device_manager_xi2_constructed (GObject *object);

static GList * gdk_device_manager_xi2_get_devices (GdkDeviceManager *device_manager,
                                                   GdkDeviceType     type);
static void    gdk_device_manager_xi2_set_window_events (GdkDeviceManager *device_manager,
                                                         GdkWindow        *window,
                                                         GdkEventMask      event_mask);

static void     gdk_device_manager_xi2_event_translator_init (GdkEventTranslatorIface *iface);

static gboolean gdk_device_manager_xi2_translate_event (GdkEventTranslator *translator,
                                                        GdkDisplay         *display,
                                                        GdkEvent           *event,
                                                        XEvent             *xevent);


G_DEFINE_TYPE_WITH_CODE (GdkDeviceManagerXI2, gdk_device_manager_xi2, GDK_TYPE_DEVICE_MANAGER,
                         G_IMPLEMENT_INTERFACE (GDK_TYPE_EVENT_TRANSLATOR,
                                                gdk_device_manager_xi2_event_translator_init))


static void
gdk_device_manager_xi2_class_init (GdkDeviceManagerXI2Class *klass)
{
  GdkDeviceManagerClass *device_manager_class = GDK_DEVICE_MANAGER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = gdk_device_manager_xi2_constructed;

  device_manager_class->get_devices = gdk_device_manager_xi2_get_devices;
  device_manager_class->set_window_events = gdk_device_manager_xi2_set_window_events;
}

static void
gdk_device_manager_xi2_init (GdkDeviceManagerXI2 *device_manager)
{
}

static void
translate_event_mask (GdkEventMask   event_mask,
                      unsigned char *mask)
{
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

  /* FIXME: Proximity in/out mask */
}

static void
_gdk_device_manager_xi2_select_events (GdkDeviceManager *device_manager,
                                       GdkWindow        *window,
                                       int               device_id,
                                       unsigned char    *mask)
{
  GdkDisplay *display;
  Display *xdisplay;
  Window xwindow;
  XIEventMask event_mask;

  display = gdk_device_manager_get_display (device_manager);
  xdisplay = GDK_DISPLAY_XDISPLAY (display);
  xwindow = GDK_WINDOW_XWINDOW (window);

  event_mask.deviceid = device_id;
  event_mask.mask_len = sizeof (mask);
  event_mask.mask = mask;

  XISelectEvents (xdisplay, xwindow, &event_mask, 1);
}

static void
gdk_device_manager_xi2_constructed (GObject *object)
{
  GdkDeviceManagerXI2 *device_manager_xi2;
  GdkDisplay *display;
  GdkScreen *screen;
  Display *xdisplay;
  XIDeviceInfo *info, *dev;
  int ndevices, i;
  unsigned char mask[2] = { 0 };

  device_manager_xi2 = GDK_DEVICE_MANAGER_XI2 (object);
  display = gdk_device_manager_get_display (GDK_DEVICE_MANAGER (object));
  xdisplay = GDK_DISPLAY_XDISPLAY (display);

  info = XIQueryDevice(xdisplay, XIAllDevices, &ndevices);

  /* Initialize devices list */
  for (i = 0; i < ndevices; i++)
    {
      GdkDevice *device;
      GdkDevicePrivate *private;

      dev = &info[i];

      device = g_object_new (GDK_TYPE_DEVICE, NULL);
      private = (GdkDevicePrivate *) device;

      device->name = g_strdup (dev->name);
      device->source = GDK_SOURCE_MOUSE;
      device->mode = GDK_MODE_SCREEN;
      device->has_cursor = TRUE;
      device->num_axes = 0;
      device->axes = NULL;
      device->num_keys = 0;
      device->keys = NULL;

      private->display = display;

      if (dev->use == XIMasterPointer)
        device_manager_xi2->master_devices = g_list_prepend (device_manager_xi2->master_devices, device);
      else if (dev->use == XISlavePointer)
        device_manager_xi2->slave_devices = g_list_prepend (device_manager_xi2->slave_devices, device);
      else if (dev->use == XIFloatingSlave)
        device_manager_xi2->floating_devices = g_list_prepend (device_manager_xi2->floating_devices, device);
      else
        {
          g_warning ("Unhandled device: %s\n", device->name);
          g_object_unref (device);
        }
    }

  XIFreeDeviceInfo(info);

  /* Connect to hierarchy change events */
  screen = gdk_display_get_default_screen (display);
  XISetMask (mask, XI_HierarchyChanged);

  _gdk_device_manager_xi2_select_events (GDK_DEVICE_MANAGER (object),
                                         gdk_screen_get_root_window (screen),
                                         XIAllDevices,
                                         mask);
}

static GList *
gdk_device_manager_xi2_get_devices (GdkDeviceManager *device_manager,
                                    GdkDeviceType     type)
{
  GdkDeviceManagerXI2 *device_manager_xi2;
  GList *list = NULL;

  device_manager_xi2 = GDK_DEVICE_MANAGER_XI2 (device_manager);

  switch (type)
    {
    case GDK_DEVICE_TYPE_MASTER:
      list = device_manager_xi2->master_devices;
      break;
    case GDK_DEVICE_TYPE_SLAVE:
      list = device_manager_xi2->slave_devices;
      break;
    case GDK_DEVICE_TYPE_FLOATING:
      list = device_manager_xi2->floating_devices;
      break;
    default:
      g_assert_not_reached ();
    }

  return g_list_copy (list);;
}

static void
gdk_device_manager_xi2_set_window_events (GdkDeviceManager *device_manager,
                                          GdkWindow        *window,
                                          GdkEventMask      event_mask)
{
  unsigned char mask[2] = { 0 };

  translate_event_mask (event_mask, mask);

  _gdk_device_manager_xi2_select_events (device_manager, window,
                                         XIAllMasterDevices,
                                         mask);
}

static void
gdk_device_manager_xi2_event_translator_init (GdkEventTranslatorIface *iface)
{
  iface->translate_event = gdk_device_manager_xi2_translate_event;
}

static gboolean
gdk_device_manager_xi2_translate_event (GdkEventTranslator *translator,
                                        GdkDisplay         *display,
                                        GdkEvent           *event,
                                        XEvent             *xevent)
{
  GdkDeviceManagerXI2 *device_manager;
  XIEvent *ev;
  gboolean return_val = TRUE;

  ev = (XIEvent *) xevent;
  device_manager = (GdkDeviceManagerXI2 *) translator;

  if (ev->type != GenericEvent || ev->extension != device_manager->opcode)
    return FALSE;

  switch (ev->evtype)
    {
    default:
      return_val = FALSE;
      break;
    }

  XIFreeEventData (ev);

  return return_val;
}
