/**
 * Copyright (c) 2020 Paul-Louis Ageneau
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "addr.h"
#include "log.h"

#include <string.h>

socklen_t addr_get_len(const struct sockaddr *sa) {
	switch (sa->sa_family) {
	case AF_INET:
		return sizeof(struct sockaddr_in);
	case AF_INET6:
		return sizeof(struct sockaddr_in6);
	default:
		JLOG_WARN("Unknown address family %hu", sa->sa_family);
		return 0;
	}
}

uint16_t addr_get_port(const struct sockaddr *sa) {
	switch (sa->sa_family) {
	case AF_INET:
		return ntohs(((struct sockaddr_in *)sa)->sin_port);
	case AF_INET6:
		return ntohs(((struct sockaddr_in6 *)sa)->sin6_port);
	default:
		JLOG_WARN("Unknown address family %hu", sa->sa_family);
		return 0;
	}
}

int addr_set_port(struct sockaddr *sa, uint16_t port) {
	switch (sa->sa_family) {
	case AF_INET:
		((struct sockaddr_in *)sa)->sin_port = htons(port);
		return 0;
	case AF_INET6:
		((struct sockaddr_in6 *)sa)->sin6_port = htons(port);
		return 0;
	default:
		JLOG_WARN("Unknown address family %hu", sa->sa_family);
		return -1;
	}
}

bool addr_is_local(struct sockaddr *sa) {
	switch (sa->sa_family) {
	case AF_INET: {
		const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
		const uint8_t *b = (const uint8_t *)&sin->sin_addr;
		if (b[0] == 127) // loopback
			return true;
		if (b[0] == 169 && b[1] == 254) // link-local
			return true;
		return false;
	}
	case AF_INET6: {
		const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
		if (IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr)) {
			return true;
		}
		if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
			return true;
		}
		if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
			const uint8_t *b = (const uint8_t *)&sin6->sin6_addr + 12;
			if (b[0] == 127) // loopback
				return true;
			if (b[0] == 169 && b[1] == 254) // link-local
				return true;
			return false;
		}
		return false;
	}
	default:
		return false;
	}
}

bool addr_is_temp_inet6(struct sockaddr *sa) {
	if (sa->sa_family != AF_INET6)
		return false;
	if (addr_is_local(sa))
		return false;
	const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
	const uint8_t *b = (const uint8_t *)&sin6->sin6_addr;
	return (b[8] & 0x02) ? false : true;
}

bool addr_unmap_inet6_v4mapped(struct sockaddr *sa, socklen_t *len) {
	if (sa->sa_family != AF_INET6)
		return false;

	const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
	if (!IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr))
		return false;

	struct sockaddr_in6 copy = *sin6;
	sin6 = &copy;

	struct sockaddr_in *sin = (struct sockaddr_in *)sa;
	memset(sin, 0, sizeof(*sin));
	sin->sin_family = AF_INET;
	sin->sin_port = sin6->sin6_port;
	memcpy(&sin->sin_addr, ((const uint8_t *)&sin6->sin6_addr) + 12, 4);
	*len = sizeof(*sin);
	return true;
}

bool addr_map_inet6_v4mapped(struct sockaddr_storage *ss, socklen_t *len) {
	if (ss->ss_family != AF_INET)
		return false;

	const struct sockaddr_in *sin = (const struct sockaddr_in *)ss;
	struct sockaddr_in copy = *sin;
	sin = &copy;

	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ss;
	memset(sin6, 0, sizeof(*sin6));
	sin6->sin6_family = AF_INET6;
	sin6->sin6_port = sin->sin_port;
	uint8_t *b = (uint8_t *)&sin6->sin6_addr;
	memset(b, 0, 10);
	memset(b + 10, 0xFF, 2);
	memcpy(b + 12, (const uint8_t *)&sin->sin_addr, 4);
	*len = sizeof(*sin6);
	return true;
}

bool addr_is_equal(const struct sockaddr *a, const struct sockaddr *b, bool compare_ports) {
	if (a->sa_family != b->sa_family)
		return false;

	switch (a->sa_family) {
	case AF_INET: {
		const struct sockaddr_in *ain = (const struct sockaddr_in *)a;
		const struct sockaddr_in *bin = (const struct sockaddr_in *)b;
		if (memcmp(&ain->sin_addr, &bin->sin_addr, 4) != 0)
			return false;
		if (compare_ports && ain->sin_port != bin->sin_port)
			return false;
		break;
	}
	case AF_INET6: {
		const struct sockaddr_in6 *ain6 = (const struct sockaddr_in6 *)a;
		const struct sockaddr_in6 *bin6 = (const struct sockaddr_in6 *)b;
		if (memcmp(&ain6->sin6_addr, &bin6->sin6_addr, 16) != 0)
			return false;
		if (compare_ports && ain6->sin6_port != bin6->sin6_port)
			return false;
		break;
	}
	default:
		return false;
	}

	return true;
}

int addr_resolve(const char *hostname, const char *service, addr_record_t *records, size_t count) {
	addr_record_t *end = records + count;

	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags = AI_ADDRCONFIG;
	struct addrinfo *ai_list = NULL;
	if (getaddrinfo(hostname, service, &hints, &ai_list)) {
		JLOG_WARN("Address resolution failed for %s:%s", hostname, service);
		return -1;
	}

	int ret = 0;
	for (struct addrinfo *ai = ai_list; ai; ai = ai->ai_next) {
		if (ai->ai_family == AF_INET || ai->ai_family == AF_INET6) {
			++ret;
			if (records != end) {
				memcpy(&records->addr, ai->ai_addr, ai->ai_addrlen);
				records->len = ai->ai_addrlen;
				++records;
			}
		}
	}

	freeaddrinfo(ai_list);
	return ret;
}
