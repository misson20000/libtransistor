/**
 * @file libtransistor/cpp/ipc/usb_ds.hpp
 * @brief USB-DS IPC bindings (C++ header)
 */

#pragma once

#include<libtransistor/cpp/types.hpp>
#include<libtransistor/cpp/ipc/usb.hpp>
#include<libtransistor/ipc/usb.h>

#include<memory>

namespace trn {
namespace service {
namespace usb {
namespace ds {

enum class State : uint32_t {
	INITIAL = USB_DS_STATE_INITIAL,
	INIT_STARTING = USB_DS_STATE_INIT_STARTING,
	INIT2 = USB_DS_STATE_INIT2,
	INIT3 = USB_DS_STATE_INIT3,
	INITIALIZED = USB_DS_STATE_INITIALIZED,
};

class Interface;
class Endpoint;

class DS {
 public:
	static Result<DS> Initialize(uint32_t complex_id, usb_device_data_t *device_data);
	DS(const DS& other);
	DS(DS&& other);
	~DS();

	Result<KEvent> GetStateChangeEvent();
	Result<State> GetState();
	Result<std::shared_ptr<Interface>> GetInterface(usb_interface_descriptor_t &descriptor, const char *name);
	
 private:
	DS(uint32_t complex_id, usb_device_data_t *device_data); // use DS::Initialize
	uint32_t complex_id;
	usb_device_data_t *device_data;
};

class Interface {
 public:
	Interface(usb_ds_interface_t intf);

	Result<std::nullopt_t> Enable();
	Result<std::nullopt_t> Disable();
	Result<std::shared_ptr<Endpoint>> GetEndpoint(usb_endpoint_descriptor_t &descriptor);
	Result<std::shared_ptr<Endpoint>> GetCtrlInEndpoint();
	Result<std::shared_ptr<Endpoint>> GetCtrlOutEndpoint();
	Result<std::nullopt_t> StallCtrl();
	
	~Interface();
 private:
	usb_ds_interface_t intf;
};

class Endpoint {
 public:
	Endpoint(KEvent &&completion_event);

	// returns urb_id
	virtual Result<uint32_t> PostBufferAsync(void *buffer, size_t size) = 0;
	virtual Result<usb_ds_report_t> GetReportData() = 0;
	virtual Result<std::nullopt_t> Stall() = 0;
	
	KEvent completion_event;
	
	virtual ~Endpoint() = 0;
};

}
}
}
}
