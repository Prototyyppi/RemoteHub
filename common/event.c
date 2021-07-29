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

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "event.h"
#include "logging.h"
#include "network.h"

#define for_each_task(task) \
	for (task = head; task != NULL; task = task->next)

static struct rh_task *head;

static pthread_mutex_t event_lock;
static pthread_cond_t terminate_signal;
static bool running = true;
static uint32_t event_count;

void event_task_register(struct rh_task *task)
{
	struct rh_task *tmp;

	pthread_mutex_init(&task->event_lock, NULL);
	pthread_cond_init(&task->event_cond, NULL);

	if (head == NULL) {
		head = task;
		rh_trace(LVL_TRC, "Task [%s] registered\n", head->task_name);
		return;
	}

	tmp = head;
	while (tmp->next != NULL)
		tmp = tmp->next;
	tmp->next = task;

	rh_trace(LVL_TRC, "Task [%s] registered\n", task->task_name);
}

bool event_handler(void)
{
	pthread_mutex_lock(&event_lock);
	if (running)
		pthread_cond_wait(&terminate_signal, &event_lock);
	pthread_mutex_unlock(&event_lock);

	rh_trace(LVL_TRC, "Event handling terminate\n");
	return true;
}

static void event_insert(struct rh_task *task, struct rh_event *ev, void *data)
{
	struct rh_event *tmp;
	struct rh_event *event;
	int depth = 1;

	event = calloc(1, sizeof(struct rh_event));
	if (!event) {
		rh_trace(LVL_ERR, "Out of memory\n");
		if (data)
			free(data);
		return;
	}

	event->type = ev->type;
	event->size = ev->size;
	event->data = data;
	event->link = ev->link;
	event->sts = ev->sts;

	if (task->event == NULL) {
		task->event = event;
		return;
	}

	tmp = task->event;
	while (tmp->next != NULL) {
		tmp = tmp->next;
		depth++;
	}
	tmp->next = event;

	rh_trace(LVL_DBG, "Task [%s] event [0x%x] depth [%d]\n", task->task_name, ev->type, depth);
	if (depth > 100) {
		rh_trace(LVL_CRIT, "Task [%s] got stuck!\n", task->task_name);
		// Will have aborted
	}
}

bool event_enqueue(struct rh_event *event)
{
	struct rh_task *task;
	void *data;

	if (!running)
		return false;

	pthread_mutex_lock(&event_lock);
	event_count++;

	for_each_task(task) {
		data = NULL;
		if (event->type & task->event_mask) {
			if (event->size) {
				data = malloc(event->size);
				memcpy(data, event->data, event->size);
			}
			pthread_mutex_lock(&task->event_lock);
			event_insert(task, event, data);
			pthread_mutex_unlock(&task->event_lock);
			pthread_cond_signal(&task->event_cond);
		}
	}

	if (event->type == EVENT_TERMINATE) {
		rh_trace(LVL_DBG, "Terminate event handling\n");
		running = false;
		pthread_cond_signal(&terminate_signal);
	}

	pthread_mutex_unlock(&event_lock);
	return true;
}

bool event_dequeue(struct rh_task *task, struct rh_event **event)
{
	pthread_mutex_lock(&task->event_lock);
	while (task->running && (task->event == NULL))
		pthread_cond_wait(&task->event_cond, &task->event_lock);

	if (!task->running) {
		pthread_mutex_unlock(&task->event_lock);
		return false;
	}

	*event = task->event;
	task->event = task->event->next;

	pthread_mutex_unlock(&task->event_lock);
	return true;
}

void event_cleanup(void)
{
	struct rh_task *task;
	struct rh_event *event;

	for_each_task(task) {
		rh_trace(LVL_TRC, "Cleanup for %s\n", task->task_name);
		while (task->event != NULL) {
			event = task->event;
			task->event = event->next;
			if (event->data)
				free(event->data);
			if (event->link) {
				network_close_link(event->link);
				free(event->link);
			}
			free(event);
		}
		rh_trace(LVL_TRC, "OK\n");
	}
}

void event_init(void)
{
	pthread_mutex_init(&event_lock, NULL);
	pthread_cond_init(&terminate_signal, NULL);
}
