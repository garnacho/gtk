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
static void    gdk_device_manager_xi2_finalize    (GObject *object);

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
  object_class->finalize = gdk_device_manager_xi2_finalize;

  device_manager_class->get_devices = gdk_device_manager_xi2_get_devices;
  device_manager_class->set_window_events = gdk_device_manager_xi2_set_window_events;
}

static void
gdk_device_manager_xi2_init (GdkDeviceManagerXI2 *device_manager)
{
  device_manager->id_table = g_hash_table_new_full (g_direct_hash,
                                                    g_direct_equal,
                                                    NULL,
                                                    (GDestroyNotify) g_object_unref);
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

static GdkDevice *
create_device (XIDeviceInfo *dev)
{
  GdkDevice *device;

  device = g_object_new (GDK_TYPE_DEVICE, NULL);

  device->name = g_strdup (dev->name);
  device->source = GDK_SOURCE_MOUSE;

  device->mode = (dev->use == XIMasterPointer) ? GDK_MODE_SCREEN : GDK_MODE_DISABLED;
  device->has_cursor = (dev->use == XIMasterPointer);
  device->num_axes = 0;
  device->axes = NULL;
  device->num_keys = 0;
  device->keys = NULL;

  return device;
}

static GdkDevice *
add_device (GdkDeviceManagerXI2 *device_manager,
            XIDeviceInfo        *dev,
            gboolean             emit_signal)
{
  GdkDevice *device;

  device = create_device (dev);

  g_hash_table_replace (device_manager->id_table,
                        GINT_TO_POINTER (dev->deviceid),
                        device);

  if (dev->use == XIMasterPointer)
    device_manager->master_devices = g_list_prepend (device_manager->master_devices, device);
  else if (dev->use == XISlavePointer)
    device_manager->slave_devices = g_list_prepend (device_manager->slave_devices, device);
  else if (dev->use == XIFloatingSlave)
    device_manager->floating_devices = g_list_prepend (device_manager->floating_devices, device);
  else
    g_warning ("Unhandled device: %s\n", device->name);

  if (emit_signal)
    g_signal_emit_by_name (device_manager, "device-added", device);

  return device;
}

static void
remove_device (GdkDeviceManagerXI2 *device_manager,
               int                  device_id)
{
  GdkDevice *device;

  device = g_hash_table_lookup (device_manager->id_table,
                                GINT_TO_POINTER (device_id));

  if (device)
    {
      device_manager->master_devices = g_list_remove (device_manager->master_devices, device);
      device_manager->slave_devices = g_list_remove (device_manager->slave_devices, device);
      device_manager->floating_devices = g_list_remove (device_manager->floating_devices, device);

      g_signal_emit_by_name (device_manager, "device-removed", device);

      g_hash_table_remove (device_manager->id_table,
                           GINT_TO_POINTER (device_id));
    }
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
      device = add_device (device_manager_xi2, dev, FALSE);

      if (device)
        {
          private = (GdkDevicePrivate *) device;
          private->display = display;
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

static void
gdk_device_manager_xi2_finalize (GObject *object)
{
  GdkDeviceManagerXI2 *device_manager_xi2;

  device_manager_xi2 = GDK_DEVICE_MANAGER_XI2 (object);

  g_list_free (device_manager_xi2->master_devices);
  g_list_free (device_manager_xi2->slave_devices);
  g_list_free (device_manager_xi2->floating_devices);

  g_hash_table_destroy (device_manager_xi2->id_table);

  G_OBJECT_CLASS (gdk_device_manager_xi2_parent_class)->finalize (object);
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

static void
handle_hierarchy_changed (GdkDeviceManagerXI2 *device_manager,
                          XIHierarchyEvent    *ev)
{
  GdkDevice *device;
  gint i;

  /* We only care about enabled devices */
  if (!(ev->flags & XIDeviceEnabled) &&
      !(ev->flags & XIDeviceDisabled))
    return;

  for (i = 0; i < ev->num_devices; i++)
    {
      if (ev->info[i].flags & XIDeviceEnabled)
        {
          GdkDisplay *display;
          Display *xdisplay;
          XIDeviceInfo *info;
          int ndevices;

          display = gdk_device_manager_get_display (GDK_DEVICE_MANAGER (device_manager));
          xdisplay = GDK_DISPLAY_XDISPLAY (display);

          info = XIQueryDevice(xdisplay, ev->info[i].deviceid, &ndevices);
          device = add_device (device_manager, &info[0], TRUE);
          XIFreeDeviceInfo(info);
        }
      else if (ev->info[i].flags & XIDeviceDisabled)
        remove_device (device_manager, ev->info[i].deviceid);
    }
}

static GdkCrossingMode
translate_crossing_mode (int mode)
{
  switch (mode)
    {
    case NotifyNormal:
      return GDK_CROSSING_NORMAL;
    case NotifyGrab:
      return GDK_CROSSING_GRAB;
    case NotifyUngrab:
      return GDK_CROSSING_UNGRAB;
    default:
      g_assert_not_reached ();
    }
}

static GdkNotifyType
translate_notify_type (int detail)
{
  switch (detail)
    {
    case NotifyInferior:
      return GDK_NOTIFY_INFERIOR;
    case NotifyAncestor:
      return GDK_NOTIFY_ANCESTOR;
    case NotifyVirtual:
      return GDK_NOTIFY_VIRTUAL;
    case NotifyNonlinear:
      return GDK_NOTIFY_NONLINEAR;
    case NotifyNonlinearVirtual:
      return GDK_NOTIFY_NONLINEAR_VIRTUAL;
    default:
      g_assert_not_reached ();
    }
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
    case XI_HierarchyChanged:
      handle_hierarchy_changed (device_manager,
                                (XIHierarchyEvent *) ev);
      return_val = FALSE;
      break;
    case XI_Motion:
      {
        XIDeviceEvent *xev = (XIDeviceEvent *) ev;

        event->motion.type = GDK_MOTION_NOTIFY;

        event->motion.window = gdk_window_lookup_for_display (display, xev->event);

        event->motion.time = xev->time;
        event->motion.x = (gdouble) xev->event_x;
        event->motion.y = (gdouble) xev->event_y;
        event->motion.x_root = (gdouble) xev->root_x;
        event->motion.y_root = (gdouble) xev->root_y;

        event->motion.device = g_hash_table_lookup (device_manager->id_table,
                                                    GINT_TO_POINTER (xev->deviceid));

        /* FIXME: missing axes, state, is_hint */
      }
      break;
    case XI_Enter:
    case XI_Leave:
      {
        XIEnterEvent *xev = (XIEnterEvent *) ev;

        event->crossing.type = (ev->evtype == XI_Enter) ? GDK_ENTER_NOTIFY : GDK_LEAVE_NOTIFY;

        event->crossing.x = (gdouble) xev->event_x;
        event->crossing.y = (gdouble) xev->event_y;
        event->crossing.x_root = (gdouble) xev->root_x;
        event->crossing.y_root = (gdouble) xev->root_y;
        event->crossing.time = xev->time;
        event->crossing.focus = xev->focus;

        event->crossing.window = gdk_window_lookup_for_display (display, xev->event);
        event->crossing.subwindow = gdk_window_lookup_for_display (display, xev->child);

        event->crossing.mode = translate_crossing_mode (xev->mode);
        event->crossing.detail = translate_notify_type (xev->detail);

        /* FIXME: missing event->crossing.state */
      }
      break;
    default:
      return_val = FALSE;
      break;
    }

  event->any.send_event = ev->send_event;

  if (return_val)
    {
      if (event->any.window)
        g_object_ref (event->any.window);

      if (((event->any.type == GDK_ENTER_NOTIFY) ||
	   (event->any.type == GDK_LEAVE_NOTIFY)) &&
	  (event->crossing.subwindow != NULL))
        g_object_ref (event->crossing.subwindow);
    }
  else
    {
      /* Mark this event as having no resources to be freed */
      event->any.window = NULL;
      event->any.type = GDK_NOTHING;
    }

  XIFreeEventData (ev);

  return return_val;
}
