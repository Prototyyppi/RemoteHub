/*
 * Copyright (C) 2021 Jani Laitinen
 *
 * This file is part of RemoteHub.
 *
 * RemoteHub is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * RemoteHub is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RemoteHub.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __REMOTEHUB_SRV_EVENT_H__
#define __REMOTEHUB_SRV_EVENT_H__

#define EVENT_TIMER_1S				0x0001
#define EVENT_TIMER_5S				0x0002
#define EVENT_LOCAL_DEVICELIST			0x0004
#define EVENT_REQ_DEVICELIST			0x0008
#define EVENT_REQ_IMPORT			0x0010
#define EVENT_DEVICE_EXPORTED			0x0020
#define EVENT_DEVICE_UNEXPORTED			0x0040
#define EVENT_DEVICE_ATTACHED			0x0080
#define EVENT_DEVICE_DETACHED			0x0100


#endif /* __REMOTEHUB_SRV_EVENT_H__ */
