/*
 * Unix SMB/CIFS implementation.
 * Register _smb._tcp with avahi
 *
 * Copyright (C) Volker Lendecke 2009
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "includes.h"
#include "smbd/smbd.h"

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>

struct avahi_state_struct {
	struct AvahiPoll *poll;
	AvahiClient *client;
	AvahiEntryGroup *entry_group;
	uint16_t port;
};

static void avahi_entry_group_callback(AvahiEntryGroup *g,
				       AvahiEntryGroupState status,
				       void *userdata)
{
	struct avahi_state_struct *state = talloc_get_type_abort(
		userdata, struct avahi_state_struct);
	int error;

	switch (status) {
	case AVAHI_ENTRY_GROUP_ESTABLISHED:
		DBG_DEBUG("AVAHI_ENTRY_GROUP_ESTABLISHED\n");
		break;
	case AVAHI_ENTRY_GROUP_FAILURE:
		error = avahi_client_errno(state->client);

		DBG_DEBUG("AVAHI_ENTRY_GROUP_FAILURE: %s\n",
			  avahi_strerror(error));
		break;
	case AVAHI_ENTRY_GROUP_COLLISION:
		DBG_DEBUG("AVAHI_ENTRY_GROUP_COLLISION\n");
		break;
	case AVAHI_ENTRY_GROUP_UNCOMMITED:
		DBG_DEBUG("AVAHI_ENTRY_GROUP_UNCOMMITED\n");
		break;
	case AVAHI_ENTRY_GROUP_REGISTERING:
		DBG_DEBUG("AVAHI_ENTRY_GROUP_REGISTERING\n");
		break;
	}
}

static void avahi_client_callback(AvahiClient *c, AvahiClientState status,
				  void *userdata)
{
	struct avahi_state_struct *state = talloc_get_type_abort(
		userdata, struct avahi_state_struct);
	int error;

	switch (status) {
	case AVAHI_CLIENT_S_RUNNING:
		DBG_DEBUG("AVAHI_CLIENT_S_RUNNING\n");

		state->entry_group = avahi_entry_group_new(
			c, avahi_entry_group_callback, state);
		if (state->entry_group == NULL) {
			error = avahi_client_errno(c);
			DBG_DEBUG("avahi_entry_group_new failed: %s\n",
				  avahi_strerror(error));
			break;
		}

		error = avahi_entry_group_add_service(
			    state->entry_group, AVAHI_IF_UNSPEC,
			    AVAHI_PROTO_UNSPEC, 0, lp_netbios_name(),
			    "_smb._tcp", NULL, NULL, state->port, NULL);
		if (error != AVAHI_OK) {
			DBG_DEBUG("avahi_entry_group_add_service failed: %s\n",
				  avahi_strerror(error));
			avahi_entry_group_free(state->entry_group);
			state->entry_group = NULL;
			break;
		}

		error = avahi_entry_group_commit(state->entry_group);
		if (error != AVAHI_OK) {
			DBG_DEBUG("avahi_entry_group_commit failed: %s\n",
				  avahi_strerror(error));
			avahi_entry_group_free(state->entry_group);
			state->entry_group = NULL;
			break;
		}
		break;
	case AVAHI_CLIENT_FAILURE:
		error = avahi_client_errno(c);

		DBG_DEBUG("AVAHI_CLIENT_FAILURE: %s\n", avahi_strerror(error));

		if (error != AVAHI_ERR_DISCONNECTED) {
			break;
		}
		avahi_client_free(c);
		state->client = avahi_client_new(state->poll, AVAHI_CLIENT_NO_FAIL,
						 avahi_client_callback, state,
						 &error);
		if (state->client == NULL) {
			DBG_DEBUG("avahi_client_new failed: %s\n",
				  avahi_strerror(error));
			break;
		}
		break;
	case AVAHI_CLIENT_S_COLLISION:
		DBG_DEBUG("AVAHI_CLIENT_S_COLLISION\n");
		break;
	case AVAHI_CLIENT_S_REGISTERING:
		DBG_DEBUG("AVAHI_CLIENT_S_REGISTERING\n");
		break;
	case AVAHI_CLIENT_CONNECTING:
		DBG_DEBUG("AVAHI_CLIENT_CONNECTING\n");
		break;
	}
}

void *avahi_start_register(TALLOC_CTX *mem_ctx, struct tevent_context *ev,
			   uint16_t port)
{
	struct avahi_state_struct *state;
	int error;

	state = talloc(mem_ctx, struct avahi_state_struct);
	if (state == NULL) {
		return state;
	}
	state->port = port;
	state->poll = tevent_avahi_poll(state, ev);
	if (state->poll == NULL) {
		goto fail;
	}
	state->client = avahi_client_new(state->poll, AVAHI_CLIENT_NO_FAIL,
					 avahi_client_callback, state,
					 &error);
	if (state->client == NULL) {
		DBG_DEBUG("avahi_client_new failed: %s\n",
			  avahi_strerror(error));
		goto fail;
	}
	return state;

 fail:
	TALLOC_FREE(state);
	return NULL;
}
