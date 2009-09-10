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
#include "gdkx.h"
#include "gdkdevicemanager-core.h"

#ifdef XINPUT_2
#include "gdkdevicemanager-xi2.h"
#endif /* XINPUT_2 */

GdkDeviceManager *
_gdk_device_manager_new (GdkDisplay *display)
{
  GdkDeviceManager *device_manager;
  int opcode, firstevent, firsterror;
  int major, minor;
  Display *xdisplay;

  if (G_UNLIKELY (!g_getenv ("GDK_CORE_DEVICE_EVENTS")))
    {
#if defined (XINPUT_2) || defined (XINPUT_XFREE)
      xdisplay = GDK_DISPLAY_XDISPLAY (display);

      if (XQueryExtension (xdisplay, "XInputExtension",
                           &opcode, &firstevent, &firsterror))
        {
#if defined (XINPUT_2)
          major = 2;
          minor = 0;

          if (XIQueryVersion (xdisplay, &major, &minor) != BadRequest)
            {
              GdkDeviceManagerXI2 *device_manager_xi2;

              device_manager = g_object_new (GDK_TYPE_DEVICE_MANAGER_XI2,
                                             "display", display,
                                             NULL);

              device_manager_xi2 = GDK_DEVICE_MANAGER_XI2 (device_manager);
              device_manager_xi2->opcode = opcode;

              return device_manager;
            }
#endif
        }
#endif /* XINPUT_2 || XINPUT_XFREE */
    }

  return g_object_new (GDK_TYPE_DEVICE_MANAGER_CORE,
                       "display", display,
                       NULL);
}
