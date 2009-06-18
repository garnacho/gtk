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

#define BIT_IS_ON(ptr, bit) (((unsigned char *) (ptr))[(bit)>>3] & (1 << ((bit) & 7)))

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

static guint
translate_state (XIModifierState *mods_state,
                 XIButtonState   *buttons_state)
{
  guint state = 0;

  if (mods_state)
    {
      /* FIXME: What is mods_state->latched for? */
      state = ((guint) mods_state->base | (guint) mods_state->locked);
    }

  if (buttons_state)
    {
      gint len, i;

      /* We're only interested in the first 5 buttons */
      len = MIN (5, buttons_state->mask_len * 8);

      for (i = 0; i < len; i++)
        {
          if (!BIT_IS_ON (buttons_state->mask, i))
            continue;

          switch (i)
            {
            case 1:
              state |= GDK_BUTTON1_MASK;
              break;
            case 2:
              state |= GDK_BUTTON2_MASK;
              break;
            case 3:
              state |= GDK_BUTTON3_MASK;
              break;
            case 4:
              state |= GDK_BUTTON4_MASK;
              break;
            case 5:
              state |= GDK_BUTTON5_MASK;
              break;
            default:
              break;
            }
        }
    }

  return state;
}

static gboolean
set_screen_from_root (GdkDisplay *display,
		      GdkEvent   *event,
		      Window      xrootwin)
{
  GdkScreen *screen;

  screen = _gdk_x11_display_screen_for_xrootwin (display, xrootwin);

  if (screen)
    {
      gdk_event_set_screen (event, screen);

      return TRUE;
    }

  return FALSE;
}

static void
set_user_time (GdkEvent *event)
{
  GdkWindow *window;
  guint32 time;

  window = gdk_window_get_toplevel (event->any.window);
  g_return_if_fail (GDK_IS_WINDOW (window));

  time = gdk_event_get_time (event);

  /* If an event doesn't have a valid timestamp, we shouldn't use it
   * to update the latest user interaction time.
   */
  if (time != GDK_CURRENT_TIME)
    gdk_x11_window_set_user_time (window, time);
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

  if (ev->type == XI_Motion ||
      ev->type == XI_ButtonRelease)
    {
      if (_gdk_moveresize_handle_event (xevent))
        return FALSE;
    }

  switch (ev->evtype)
    {
    case XI_HierarchyChanged:
      handle_hierarchy_changed (device_manager,
                                (XIHierarchyEvent *) ev);
      return_val = FALSE;
      break;
    case XI_ButtonPress:
    case XI_ButtonRelease:
      {
        XIDeviceEvent *xev = (XIDeviceEvent *) ev;

        switch (xev->detail)
          {
          case 4:
          case 5:
          case 6:
          case 7:
            event->scroll.type = GDK_SCROLL;

            if (xev->detail == 4)
              event->scroll.direction = GDK_SCROLL_UP;
            else if (xev->detail == 5)
              event->scroll.direction = GDK_SCROLL_DOWN;
            else if (xev->detail == 6)
              event->scroll.direction = GDK_SCROLL_LEFT;
            else
              event->scroll.direction = GDK_SCROLL_RIGHT;

            event->scroll.window = gdk_window_lookup_for_display (display, xev->event);
            event->scroll.time = xev->time;
            event->scroll.x = (gdouble) xev->event_x;
            event->scroll.y = (gdouble) xev->event_y;
            event->scroll.x_root = (gdouble) xev->root_x;
            event->scroll.y_root = (gdouble) xev->root_y;

            event->scroll.device = g_hash_table_lookup (device_manager->id_table,
                                                        GUINT_TO_POINTER (xev->deviceid));

            event->scroll.state = translate_state (xev->mods, xev->buttons);
            break;
          default:
            event->button.type = (ev->evtype == XI_ButtonPress) ? GDK_BUTTON_PRESS : GDK_BUTTON_RELEASE;

            event->button.window = gdk_window_lookup_for_display (display, xev->event);
            event->button.time = xev->time;
            event->button.x = (gdouble) xev->event_x;
            event->button.y = (gdouble) xev->event_y;
            event->button.x_root = (gdouble) xev->root_x;
            event->button.y_root = (gdouble) xev->root_y;

            event->button.device = g_hash_table_lookup (device_manager->id_table,
                                                        GUINT_TO_POINTER (xev->deviceid));

            event->button.state = translate_state (xev->mods, xev->buttons);
            event->button.button = xev->detail;
          }

        if (!set_screen_from_root (display, event, xev->root))
          {
            return_val = FALSE;
            break;
          }

        if (event->any.type == GDK_BUTTON_PRESS)
          {
            _gdk_event_button_generate (display, event);
            set_user_time (event);
          }

        /* _gdk_xgrab_check_button_event (event->button.window, xev); */
        break;
      }
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

        event->motion.state = translate_state (xev->mods, xev->buttons);

        /* FIXME: There doesn't seem to be motion hints in XI */
        event->motion.is_hint = FALSE;

        /* FIXME: missing axes */
        event->motion.axes = NULL;
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
        event->crossing.state = translate_state (xev->mods, xev->buttons);
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
