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

#include "logging.h"

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>

#include <pthread.h>

static int debug_level = LVL_CRIT;

static pthread_mutex_t print_lock = PTHREAD_MUTEX_INITIALIZER;

void rh_set_debug_level(int level)
{
	pthread_mutex_lock(&print_lock);
	debug_level = level;
	pthread_mutex_unlock(&print_lock);
}

void rh_trace_print(int level, char const *func, int line, char const *format, ...)
{
	va_list args;

	if (level > debug_level)
		return;

	pthread_mutex_lock(&print_lock);

	switch (level) {
	case LVL_CRIT:
		fprintf(stderr, "CRIT: ");
		va_start(args, format);
		vfprintf(stderr, format, args);
		va_end(args);
		fflush(stderr);
		abort();
		break;
	case LVL_ERR:
		fprintf(stderr, "ERR : [%-20.20s@%*d]: ", func, 4, line);
		break;
	case LVL_WARN:
		fprintf(stderr, "WARN: [%-20.20s@%*d]: ", func, 4, line);
		break;
	case LVL_INFO:
		fprintf(stderr, "INFO: [%-20.20s@%*d]: ", func, 4, line);
		break;
	case LVL_TRC:
		fprintf(stderr, "TRC : [%-20.20s@%*d]: ", func, 4, line);
		break;
	case LVL_DBG:
		fprintf(stderr, "DBG : [%-20.20s@%*d]: ", func, 4, line);
		break;
	default:
		fprintf(stderr, "UNKN: [%-20.20s@%*d]: ", func, 4, line);
		break;
	}

	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fflush(stderr);

	pthread_mutex_unlock(&print_lock);
}
