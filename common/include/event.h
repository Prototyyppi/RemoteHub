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

#ifndef __REMOTEHUB_EVENT_H__
#define __REMOTEHUB_EVENT_H__

#include <stdbool.h>
#include <stdint.h>

#include "remotehub.h"
#include "task.h"

struct rh_event_status {
	uint32_t	success;
	uint32_t	devid;
	uint32_t	port;
	char		remote_server[RH_IP_NAME_MAX_LEN];
};

struct rh_event {
	uint32_t	type;
	uint32_t	size;
	void		*data;
	struct rh_event_status sts;
	struct est_conn *link;
	struct rh_event *next;
};

#define EVENT_TERMINATE		0x00

void event_init(void);
bool event_handler(void);
void event_cleanup(void);
bool event_enqueue(struct rh_event *event);
bool event_dequeue(struct rh_task *task, struct rh_event **event);
void event_task_register(struct rh_task *task);

#endif /* __REMOTEHUB_EVENT_H__ */
