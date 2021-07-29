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

#ifndef __REMOTEHUB_CLI_EVENT_H__
#define __REMOTEHUB_CLI_EVENT_H__

#define EVENT_TIMER_1S				0x0001
#define EVENT_TIMER_5S				0x0002

#define EVENT_SERVER_DISCOVERED			0x0004

#define EVENT_ATTACH_REQUESTED			0x0008
#define EVENT_ATTACHED				0x0010
#define EVENT_ATTACH_FAILED			0x0020

#define EVENT_DETACH_REQUESTED			0x0040
#define EVENT_DETACHED				0x0080
#define EVENT_DETACH_FAILED			0x0100

#define EVENT_DEVICELIST_REQUEST		0x0200
#define EVENT_DEVICELIST_READY			0x0400
#define EVENT_DEVICELIST_FAILED			0x0800

#endif /* __REMOTEHUB_CLI_EVENT_H__ */
