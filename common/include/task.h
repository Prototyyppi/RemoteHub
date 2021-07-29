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

#ifndef __REMOTEHUB_TASK_H__
#define __REMOTEHUB_TASK_H__

#include <stdint.h>

#include <pthread.h>

#define TASK_NAME_MAX_LEN	32

struct rh_task {
	char			task_name[TASK_NAME_MAX_LEN];
	bool			running;
	uint32_t		event_mask;
	pthread_mutex_t		event_lock;
	pthread_cond_t		event_cond;
	struct rh_event		*event;
	struct rh_task		*next;
};

#endif /* __REMOTEHUB_TASK_H__ */
