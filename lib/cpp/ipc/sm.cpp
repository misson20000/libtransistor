#include<libtransistor/cpp/ipc/sm.hpp>

#include<libtransistor/cpp/types.hpp>
#include<libtransistor/types.h>
#include<libtransistor/ipc/sm.h>

namespace trn {
namespace service {

Result<SM> SM::Initialize() {
	SM obj;
	return ResultCode::ExpectOk(sm_init()).map([obj](auto const &v) -> SM { return obj; } );
}

SM::SM(const SM& other) {
	// add another reference
	ResultCode::AssertOk(sm_init());
}

SM::SM(SM&& other) {
	// add another reference
	ResultCode::AssertOk(sm_init());
}

SM::SM() { // should only be called from SM::Initialize()
}

SM::~SM() {
	sm_finalize();
}

Result<ipc::client::Object> SM::GetService(const char *name) {
	ipc_object_t object;
	return ResultCode::ExpectOk(sm_get_service(&object, name)).map([&object](auto const &v) {
			return ipc::client::Object(object);
		});
}

Result<handle_t> SM::GetHandleForService(const char *name) {
	handle_t handle;
	return ResultCode::ExpectOk(sm_get_handle_for_service(&handle, name)).map([&handle](auto const &v) {
			return handle;
		});
}

Result<KPort> SM::RegisterService(const char *name, uint32_t max_sessions) {
	port_h handle;
	return ResultCode::ExpectOk(sm_register_service(&handle, name, max_sessions)).map([&handle](auto const &v) -> KPort {
			return KPort(handle);
		});
}

}
}
