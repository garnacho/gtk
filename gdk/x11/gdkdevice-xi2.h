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

#ifndef __GDK_DEVICE_XI2_H__
#define __GDK_DEVICE_XI2_H__

#include <gdk/gdkdeviceprivate.h>

G_BEGIN_DECLS

#define GDK_TYPE_DEVICE_XI2         (gdk_device_xi2_get_type ())
#define GDK_DEVICE_XI2(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDK_TYPE_DEVICE_XI2, GdkDeviceXI2))
#define GDK_DEVICE_XI2_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c), GDK_TYPE_DEVICE_XI2, GdkDeviceXI2Class))
#define GDK_IS_DEVICE_XI2(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDK_TYPE_DEVICE_XI2))
#define GDK_IS_DEVICE_XI2_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c), GDK_TYPE_DEVICE_XI2))
#define GDK_DEVICE_XI2_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDK_TYPE_DEVICE_XI2, GdkDeviceXI2Class))

typedef struct _GdkDeviceXI2 GdkDeviceXI2;
typedef struct _GdkDeviceXI2Class GdkDeviceXI2Class;

struct _GdkDeviceXI2
{
  GdkDevice parent_instance;
};

struct _GdkDeviceXI2Class
{
  GdkDeviceClass parent_class;
};

GType gdk_device_xi2_get_type (void) G_GNUC_CONST;

guchar * gdk_device_xi2_translate_event_mask (GdkEventMask  event_mask,
                                              int          *len);
guint    gdk_device_xi2_translate_state      (XIModifierState *mods_state,
                                              XIButtonState   *buttons_state);


G_END_DECLS

#endif /* __GDK_DEVICE_XI2_H__ */