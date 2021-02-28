#include<libtransistor/environment.h>

#include<libtransistor/err.h>
#include<libtransistor/svc.h>
#include<libtransistor/ipc.h>
#include<libtransistor/ipc/sm.h>
#include<libtransistor/ipc_helpers.h>
#include<libtransistor/util.h>

void *env_stack_top;
void *env_aslr_base;

static target_version_t env_target_version = TARGET_VERSION_INVALID;

result_t env_get_target_version(target_version_t *target_version) {
	if(env_target_version == TARGET_VERSION_INVALID) {
		return LIBTRANSISTOR_ERR_TARGET_VERSION_NOT_SET;
	}

	*target_version = env_target_version;
	return RESULT_OK;
}

typedef struct {
	target_version_t target;
	int major;
	int minor;
	int patch;
} fw_version_t;

static const fw_version_t target_version_table[] = {
	{TARGET_VERSION_11_0_0, 11, 0, 0},
	{TARGET_VERSION_5_0_0, 5, 0, 0},
	{TARGET_VERSION_4_0_0, 4, 0, 0},
	{TARGET_VERSION_3_0_0, 3, 0, 0},
	{TARGET_VERSION_2_0_0, 2, 0, 0},
	{TARGET_VERSION_1_0_0, 1, 0, 0},
};

result_t env_infer_target_version_by_set_sys() {
	result_t r;
	ipc_object_t set_sys_object;

	if(env_target_version != TARGET_VERSION_INVALID) {
		return LIBTRANSISTOR_ERR_TARGET_VERSION_ALREADY_SET;
	}
	
	LIB_ASSERT_OK(fail, sm_init());
	LIB_ASSERT_OK(fail_sm, sm_get_service(&set_sys_object, "set:sys"));

	char firmware_version[0x100];
	ipc_request_t rq = ipc_make_request(3);

	ipc_buffer_t buffers[] = {
		ipc_make_buffer((void*) firmware_version, sizeof(firmware_version), 0x1a)
	};
	ipc_msg_set_buffers(rq, buffers, buffer_ptrs);

	ipc_response_fmt_t rs = ipc_default_response_fmt;

	LIB_ASSERT_OK(fail_set_sys, ipc_send(set_sys_object, &rq, &rs));

	int major = firmware_version[0];
	int minor = firmware_version[1];
	int patch = firmware_version[2];

	r = LIBTRANSISTOR_ERR_TARGET_VERSION_NOT_SUPPORTED;
	
	for(size_t i = 0; i < ARRAY_LENGTH(target_version_table); i++) {
		const fw_version_t *version = &target_version_table[i];
		if(major > version->major ||
			 (major == version->major &&
				(minor > version->minor ||
				 (minor == version->minor &&
					patch >= version->patch)))) {
			env_target_version = version->target;
			r = RESULT_OK;
			break;
		}
	}

fail_set_sys:
	ipc_close(set_sys_object);
fail_sm:
	sm_finalize();
fail:
	return r;
}

result_t env_set_target_version(target_version_t target_version) {
	if(env_target_version != TARGET_VERSION_INVALID) {
		return LIBTRANSISTOR_ERR_TARGET_VERSION_ALREADY_SET;
	}

	env_target_version = target_version;
	return RESULT_OK;
}

target_version_t env_get_svc_version() {
	static target_version_t version = TARGET_VERSION_INVALID;

	if(env_target_version != TARGET_VERSION_INVALID) {
		return env_target_version;
	}
	
	if(version == TARGET_VERSION_INVALID) {
		version = TARGET_VERSION_MAX;
		uint64_t info;
		result_t r;
		if((r = svcGetInfo(&info, 20, 0xffffffff, 0)) == 0xf001) { version = TARGET_VERSION_5_0_0 - 1; }
		if((r = svcGetInfo(&info, 19, 0xffffffff, 0)) == 0xf001) { // removed in 5.0.0
			if(version != TARGET_VERSION_MAX) {
				version = TARGET_VERSION_4_0_0 - 1;
			}
		}
		if((r = svcGetInfo(&info, 16, 0xffffffff, 0)) == 0xf001) { version = TARGET_VERSION_3_0_0 - 1; }
		if((r = svcGetInfo(&info, 12, 0xffffffff, 0)) == 0xf001) { version = TARGET_VERSION_2_0_0 - 1; }
	}
	return version;
}

void *env_get_stack_top() {
	return env_stack_top;
}

void *env_get_aslr_base() {
	return env_aslr_base;
}
