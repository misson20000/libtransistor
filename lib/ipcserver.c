#include<libtransistor/ipcserver.h>

#include<libtransistor/types.h>
#include<libtransistor/svc.h>
#include<libtransistor/tls.h>
#include<libtransistor/err.h>
#include<libtransistor/ipc.h>
#include<libtransistor/ipc_helpers.h>
#include<libtransistor/loader_config.h>

#include<string.h>
#include<stdlib.h>

result_t ipc_server_create(ipc_server_t *srv, waiter_t *waiter) {
	return ipc_server_create_ex(srv, waiter, 63, 63, 0x500); // some defaults
}

result_t ipc_server_create_ex(ipc_server_t *srv, waiter_t *waiter, uint32_t max_ports, uint32_t max_sessions, size_t pointer_buffer_size) {
	static const uint8_t syscalls_needed[] = {
		SVC_ID_CREATE_SESSION,
		SVC_ID_ACCEPT_SESSION,
		SVC_ID_REPLY_AND_RECEIVE,
		0
	};
	
	result_t r;
	if((r = lconfig_has_syscalls(syscalls_needed)) != RESULT_OK) {
		return r;
	}
	
	srv->num_ports = 0;
	srv->max_ports = max_ports;
	srv->max_sessions = max_sessions;
	srv->pointer_buffer_size = pointer_buffer_size;
	srv->ports = calloc(max_ports, sizeof(ipc_server_port_t));
	srv->sessions = calloc(max_sessions, sizeof(ipc_server_session_t));
	srv->pointer_buffers = calloc(max_sessions, pointer_buffer_size);
	
	if(srv->ports == NULL || srv->sessions == NULL || srv->pointer_buffers == NULL) {
		free(srv->ports);
		free(srv->sessions);
		free(srv->pointer_buffers);
		return LIBTRANSISTOR_ERR_OUT_OF_MEMORY;
	}
	
	for(uint32_t i = 0; i < max_sessions; i++) {
		srv->sessions[i].state = IPC_SESSION_STATE_INVALID;
	}
	
	srv->waiter = waiter;
	return RESULT_OK;
}

static bool ipc_server_port_signal_callback(void *data, handle_t handle) {
	ipc_server_port_t *port = data;
	ipc_server_accept_session(port->server, port); // ignore failure
	return true;
}

static bool ipc_server_session_signal_callback(void *data, handle_t handle) {
	ipc_server_session_t *session = data;
	ipc_server_session_receive(session); // ignore failure
	return true;
}

result_t ipc_server_add_port(ipc_server_t *srv, port_h port, ipc_server_object_factory_t factory, void *userdata) {
	if(srv->num_ports >= srv->max_ports) {
		return LIBTRANSISTOR_ERR_IPCSERVER_TOO_MANY_PORTS;
	}
	ipc_server_port_t *target = &srv->ports[srv->num_ports++];
	target->port = port;
	target->userdata = userdata;
	target->factory = factory;
	target->server = srv;
	target->wait_record = waiter_add(srv->waiter, port, ipc_server_port_signal_callback, target);
	if(target->wait_record == NULL) {
		srv->num_ports--;
		return LIBTRANSISTOR_ERR_OUT_OF_MEMORY;
	}
	return RESULT_OK;
}

static void hipc_manager_dispatch(ipc_server_object_t *obj, ipc_message_t *msg, uint32_t rqid);
static void hipc_manager_close(ipc_server_object_t *obj);

result_t ipc_server_create_session(ipc_server_t *srv, session_h server_side, session_h client_side, ipc_server_object_t *object) {
	uint32_t i = 0;
	
	for(; i < srv->max_sessions; i++) {
		if(srv->sessions[i].state == IPC_SESSION_STATE_INVALID) {
			break;
		}
	}
	if(i == srv->max_sessions) {
		svcCloseHandle(server_side);
		if(client_side != 0) {
			svcCloseHandle(client_side);
		}
		return LIBTRANSISTOR_ERR_IPCSERVER_TOO_MANY_SESSIONS;
	}
	
	ipc_server_session_t *sess = &(srv->sessions[i]);
	sess->handle = server_side;
	sess->client_handle = client_side;
	sess->state = IPC_SESSION_STATE_INITIALIZING;
	sess->is_domain = false;
	sess->owning_server = srv;
	sess->hipc_manager_object.userdata = NULL;
	sess->hipc_manager_object.is_domain_object = false;
	sess->hipc_manager_object.owning_domain = NULL;
	sess->hipc_manager_object.owning_session = sess;
	sess->hipc_manager_object.dispatch = hipc_manager_dispatch;
	sess->hipc_manager_object.close = hipc_manager_close;
	sess->domain.owning_session = sess;
	for(int i = 0; i < MAX_DOMAIN_OBJECTS; i++) {
		sess->domain.objects[i] = NULL;
	}
	sess->active_object = NULL;
	sess->object = object;
	sess->object->is_domain_object = false;
	sess->object->domain_id = 0;
	sess->object->owning_domain = NULL;
	sess->object->owning_session = sess;
	sess->pointer_buffer = srv->pointer_buffers + (srv->pointer_buffer_size * i);
	sess->state = IPC_SESSION_STATE_LISTENING;
	sess->wait_record = waiter_add(srv->waiter, sess->handle, ipc_server_session_signal_callback, sess);
	if(sess->wait_record == NULL) {
		sess->state = IPC_SESSION_STATE_INVALID;
		svcCloseHandle(server_side);
		if(client_side != 0) {
			svcCloseHandle(client_side);
		}
		return LIBTRANSISTOR_ERR_OUT_OF_MEMORY;
	}
	return RESULT_OK;
}

result_t ipc_server_accept_session(ipc_server_t *srv, ipc_server_port_t *port) {
	result_t r;
	session_h handle;
	if((r = svcAcceptSession(&handle, port->port)) != RESULT_OK) {
		return r;
	}

	ipc_server_object_t *object;
	if((r = port->factory(&object, port->userdata)) != RESULT_OK) {
		svcCloseHandle(handle);
		return r;
	}
	
	return ipc_server_create_session(srv, handle, 0, object);
}

result_t ipc_server_destroy(ipc_server_t *srv) {
	for(uint32_t i = 0; i < srv->num_ports; i++) {
		waiter_cancel(srv->waiter, srv->ports[i].wait_record);
		svcCloseHandle(srv->ports[i].port);
	}
	for(int i = 0; i < MAX_SERVICE_SESSIONS; i++) {
		if(srv->sessions[i].state != IPC_SESSION_STATE_INVALID) {
			ipc_server_session_close(&srv->sessions[i]);
		}
	}
	free(srv->ports);
	free(srv->sessions);
	free(srv->pointer_buffers);
	return RESULT_OK;
}

result_t ipc_server_object_register(ipc_server_object_t *parent, ipc_server_object_t *child) {
	result_t r;
	if(parent->is_domain_object) {
		return ipc_server_domain_add_object(parent->owning_domain, child);
	} else {
		session_h server;
		session_h client;
		if((r = svcCreateSession(&server, &client, false, 0)) != RESULT_OK) {
			return r;
		}
		return ipc_server_create_session(parent->owning_session->owning_server, server, client, child);
	}
}

result_t ipc_server_object_reply(ipc_server_object_t *obj, const ipc_response_t *rs) {
	if(obj->owning_session->state != IPC_SESSION_STATE_PROCESSING) {
		return LIBTRANSISTOR_ERR_IPCSERVER_INVALID_SESSION_STATE;
	}
	if(obj->owning_session->active_object != obj) {
		return LIBTRANSISTOR_ERR_IPCSERVER_OBJECT_NOT_ACTIVE;
	}

	result_t r;
	
	uint32_t *tls = (uint32_t*) get_tls()->ipc_buffer;
	uint32_t handle_index;
	if((r = ipc_pack_response(tls, rs, obj)) != RESULT_OK) {
		return r;
	}
	r = svcReplyAndReceive(&handle_index, NULL, 0, obj->owning_session->handle, 0);
	if(r != 0xea01) { // we should timeout instantly, as we're not listening on any handles
		obj->owning_session->state = IPC_SESSION_STATE_ERRORED;
		return r;
	}

	obj->owning_session->active_object = NULL;
	obj->owning_session->state = IPC_SESSION_STATE_LISTENING;
	return RESULT_OK;
}

result_t ipc_server_session_receive(ipc_server_session_t *sess) {
	result_t r;
	uint32_t handle_index = 0;
	session_h handle = sess->handle;

	if(sess->state != IPC_SESSION_STATE_LISTENING) {
		return LIBTRANSISTOR_ERR_IPCSERVER_INVALID_SESSION_STATE;
	}

	uint32_t *tls = get_tls()->ipc_buffer;

	// pointer buffer is indicated to kernel by c descriptor
	ipc_request_t message_for_ptrbuf = ipc_default_request;
	ipc_buffer_t buffers[] = {
		ipc_make_buffer(sess->pointer_buffer, sess->owning_server->pointer_buffer_size, 0x1a)
	};
	ipc_msg_set_buffers(message_for_ptrbuf, buffers, buffer_ptrs);
	memset(tls, 0, 0x1f8);
	ipc_object_t obj_for_ptrbuf;
	memset(&obj_for_ptrbuf, 0, sizeof(obj_for_ptrbuf));
	r = ipc_pack_request(tls, &message_for_ptrbuf, obj_for_ptrbuf);
	if(r != RESULT_OK) {
		goto failure;
	}
	
	r = svcReplyAndReceive(&handle_index, &handle, 1, 0, 0);

	if(r != RESULT_OK) {
		goto failure;
	}

	sess->state = IPC_SESSION_STATE_PROCESSING;
	
	memcpy(sess->message_buffer, tls, 0x100);

	ipc_message_t msg;
	if((r = ipc_unpack((uint32_t*) sess->message_buffer, &msg)) != RESULT_OK) {
		goto failure;
	}

	uint32_t *raw_data = msg.data_section;
	size_t raw_data_size = msg.raw_data_section_size * sizeof(uint32_t);

	ipc_server_object_t *dispatch = NULL;
	if(msg.message_type == 5) {
		dispatch = &sess->hipc_manager_object;
	} else if(msg.message_type == 4 || msg.message_type == 0) { // TODO: remove message_type == 0
		if(sess->is_domain) {
			uint16_t data_size = raw_data[0] >> 16;
			
			if((r = ipc_server_domain_get_object(&sess->domain, raw_data[1], &dispatch)) != RESULT_OK) {
				goto failure;
			}
			// TODO: check that raw_data_size is big enough
			raw_data+= 4;
			raw_data_size = data_size;
		} else {
			dispatch = sess->object;
		}
	} else {
		goto failure;
	}

	sess->active_object = dispatch;

	dispatch->dispatch(dispatch, &msg, raw_data[2]); // dispatch must handle failure itself

	return RESULT_OK;

failure:
	sess->state = IPC_SESSION_STATE_ERRORED;
	return ipc_server_session_close(sess);
}

result_t ipc_server_domain_add_object(ipc_server_domain_t *domain, ipc_server_object_t *object) {
	for(int i = 0; i < MAX_DOMAIN_OBJECTS; i++) {
		if(domain->objects[i] == NULL) {
			domain->objects[i] = object;
			object->is_domain_object = true;
			object->domain_id = i;
			object->owning_session = domain->owning_session;
			object->owning_domain = domain;
			return RESULT_OK;
		}
	}
	return LIBTRANSISTOR_ERR_TOO_MANY_OBJECTS;
}

result_t ipc_server_domain_get_object(ipc_server_domain_t *domain, uint32_t object_id, ipc_server_object_t **object) {
	if(object_id >= MAX_DOMAIN_OBJECTS) {
		return LIBTRANSISTOR_ERR_IPCSERVER_NO_SUCH_OBJECT;
	}
	if(domain->objects[object_id] == NULL) {
		return LIBTRANSISTOR_ERR_IPCSERVER_NO_SUCH_OBJECT;
	}
	*object = domain->objects[object_id];
	return RESULT_OK;
}

result_t ipc_server_session_close(ipc_server_session_t *sess) {
	if(sess->state == IPC_SESSION_STATE_INVALID) {
		return LIBTRANSISTOR_ERR_IPCSERVER_INVALID_SESSION_STATE;
	}

	waiter_cancel(sess->owning_server->waiter, sess->wait_record);
	svcCloseHandle(sess->handle);
	if(sess->client_handle != 0) {
		svcCloseHandle(sess->client_handle);
	}
	ipc_server_object_close(&sess->hipc_manager_object);
	if(sess->object != NULL) {
		ipc_server_object_close(sess->object);
	}
	for(int i = 0; i < MAX_DOMAIN_OBJECTS; i++) {
		if(sess->domain.objects[i] != NULL) {
			ipc_server_object_close(sess->domain.objects[i]);
		}
	}
	
	sess->state = IPC_SESSION_STATE_INVALID;
		
	return RESULT_OK;
}

result_t ipc_server_object_close(ipc_server_object_t *obj) {
	if(obj->close != NULL) {
		obj->close(obj);
	}
	return RESULT_OK;
}

static void hipc_manager_dispatch(ipc_server_object_t *obj, ipc_message_t *msg, uint32_t rqid) {
	result_t r = 0;
	
	switch(rqid) {
	case 3: { // QUERY_POINTER_BUFFER_SIZE
		ipc_request_fmt_t rq = ipc_default_request_fmt;
		LIB_ASSERT_OK(hard_failure, ipc_unflatten_request(msg, &rq, obj));

		uint16_t out = obj->owning_session->pointer_buffer_size;

		ipc_response_t rs = ipc_default_response;
		rs.raw_data_size = sizeof(out);
		rs.raw_data = (uint32_t*) &out;

		LIB_ASSERT_OK(hard_failure, ipc_server_object_reply(obj, &rs));

		break; }
	default:
		r = LIBTRANSISTOR_ERR_IPCSERVER_NO_SUCH_COMMAND;
		goto hard_failure;
	}
	return;

soft_failure: {
		ipc_response_t rs = ipc_default_response;
		rs.result_code = r;
		
		LIB_ASSERT_OK(hard_failure, ipc_server_object_reply(obj, &rs));
	}
hard_failure:
	ipc_server_session_close(obj->owning_session);
	return;
}

static void hipc_manager_close(ipc_server_object_t *obj) {
}
