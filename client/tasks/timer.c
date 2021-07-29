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

#include <string.h>
#include <unistd.h>

#include "logging.h"
#include "event.h"
#include "task.h"
#include "cli_event.h"

static pthread_t timer_thread;

static struct rh_task timer;

static void *timer_event_generate(void *args)
{
	struct rh_event timer_event = {0};
	uint8_t ctr = 5;

	(void) args;
	rh_trace(LVL_TRC, "Timer starting\n");

	while (timer.running) {
		timer_event.type = EVENT_TIMER_1S;
		(void) event_enqueue(&timer_event);
		usleep(1000000);
		if (!(ctr % 5)) {
			timer_event.type = EVENT_TIMER_5S;
			(void) event_enqueue(&timer_event);
			ctr = 0;
		}
		ctr++;
	}

	rh_trace(LVL_TRC, "Timer quit\n");

	return NULL;
}

void timer_exit(void)
{
	rh_trace(LVL_TRC, "Timer terminate\n");
	timer.running = false;
	pthread_cond_signal(&timer.event_cond);
	if (timer_thread)
		pthread_join(timer_thread, NULL);
}

bool timer_task_init(void)
{
	rh_trace(LVL_TRC, "Timer init\n");

	timer.event_mask = 0;
	timer.running = true;
	strcpy(timer.task_name, "Timer task");

	event_task_register(&timer);

	if (pthread_create(&timer_thread, NULL, timer_event_generate, NULL)) {
		rh_trace(LVL_ERR, "Failed to start timer\n");
		return false;
	}

	return true;
}
