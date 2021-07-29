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

#ifndef __REMOTEHUB_LOGGING_H__
#define __REMOTEHUB_LOGGING_H__

#include <stdarg.h>

enum trace_level {
	LVL_CRIT	= 0,
	LVL_ERR		= 1,
	LVL_WARN	= 2,
	LVL_INFO	= 3,
	LVL_DBG		= 4,
	LVL_TRC		= 5,
};

#define rh_trace(level, ...) \
	rh_trace_print(level, __func__, __LINE__, __VA_ARGS__)

void rh_set_debug_level(int level);
void rh_trace_print(int level, char const *func, int line, char const *format, ...);

#endif /* __REMOTEHUB_LOGGING_H__ */
