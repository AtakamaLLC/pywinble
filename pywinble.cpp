#define PY_SSIZE_T_CLEAN

#pragma comment(lib, "windowsapp")

#include <Python.h>
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"

namespace py = pybind11;

#include <memory>
#include <iostream>

#include <atlbase.h>

#include "winrt/Windows.Foundation.h"
#include "winrt/Windows.Storage.Streams.h"
#include "winrt/Windows.Devices.Bluetooth.h"
#include "winrt/Windows.Devices.Enumeration.h"
#include "winrt/Windows.Devices.Bluetooth.Advertisement.h"
#include "winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h"

//#include <collection.h>
using namespace std;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Devices;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace winrt::Windows::Devices::Enumeration;


// ################ GENERIC WINRT
//
std::string w2a(const winrt::hstring& hs) {
    // # can be used in an exception
    USES_CONVERSION;
    return std::string(W2A(hs.c_str()));
}

bool winrt_ok() {
    static bool did = false;
    static bool ok = false;

    if (did)
        return ok;

    winrt::hstring errMsg;

    {
        USES_CONVERSION;
        try {
            winrt::init_apartment();
            // start the GIL in the correct thread
            did = true;
            ok = true;
        } catch (const winrt::hresult_error &e) {
            PyErr_SetString(PyExc_TypeError, w2a(e.message()).c_str());
        }

        return ok;
    }
}

// ################ GENERIC PY

// allow other threads to call callbacks
class gil_lock
{
public:
  gil_lock()  { state_ = PyGILState_Ensure(); }
  ~gil_lock() { PyGILState_Release(state_);   }
private:
  PyGILState_STATE state_;
};

PyObject *PyVar(int var) {
    return PyLong_FromLong(var);
}

PyObject *PyVar(const winrt::hstring &var) {
#if PY_MAJOR_VERSION > 2
    return PyUnicode_FromWideChar(var.c_str(), var.size());
#else
    // todo : support unicode in py 2
    USES_CONVERSION;
    return PyString_FromString(W2A(var.c_str()));
#endif
}

#if PY_MAJOR_VERSION <= 2
    #define PyUnicode_FromString PyString_FromString
#endif

#if PY_MAJOR_VERSION <= 2
    #define PyUnicode_AsUTF8 PyString_AsString
#endif

#define Py_RETURN_ERROR(type, msg) { PyErr_SetString(type, msg); throw pybind11::error_already_set(); }

PyObject *PyVar(const std::string &var) {
    return PyUnicode_FromString(var.c_str());
}

std::string hexlify(uint64_t v, bool add_colons = false) {
    std::string ret;
    ret.resize(33);
    int len = sprintf(&ret[0], "%" PRIx64, v);
    ret.resize(len);
    if (! add_colons || len <= 2) 
        return ret;
    std::string ret2;
    ret2.resize(len + (len / 2 - 1));
    for (int i = 0, j = 0; i < len; ++i, ++j) {
        ret2[j]=ret[i];
        if (i % 2 && i < (len-1))
            ret2[++j] = ':';
    }
    return ret2;
}

// ################ BLUTOOTH
BluetoothAdapter bluetooth_adapter = nullptr;
Advertisement::BluetoothLEAdvertisementPublisher bleAdPub = nullptr;

static PyObject* on_adstatus_callback = NULL;

void call_on_adstatus_callback(const Advertisement::BluetoothLEAdvertisementPublisher &pub, const Advertisement::BluetoothLEAdvertisementPublisherStatusChangedEventArgs &status) {
    if (on_adstatus_callback) {
        py::gil_scoped_acquire acquire;
        int err = (int) status.Error();
        int stat = (int) status.Status();
        PyObject *result = PyObject_CallFunction(on_adstatus_callback, "ii", (int)status.Error(), (int)status.Status());
        Py_XDECREF(result);
    }
}

py::dict pywinble_info() {
    USES_CONVERSION;

    if (!bluetooth_adapter) {
        bluetooth_adapter = BluetoothAdapter::GetDefaultAsync().get();
        if (!bluetooth_adapter)
            throw std::exception("Adapter discovery error");
    }


    py::dict dict;

    #define ADD_DICT(key, var) dict[key]=var
    #define ADD_DICT_O(ob, var) dict[#var]=ob.var()

    ADD_DICT("BluetoothAddress", hexlify(bluetooth_adapter.BluetoothAddress(), true));
    ADD_DICT("DeviceId", W2A(bluetooth_adapter.DeviceId().c_str()));

    ADD_DICT_O(bluetooth_adapter, IsLowEnergySupported);
    ADD_DICT_O(bluetooth_adapter, IsClassicSupported);
    ADD_DICT_O(bluetooth_adapter, IsPeripheralRoleSupported);
    ADD_DICT_O(bluetooth_adapter, IsAdvertisementOffloadSupported);
    ADD_DICT_O(bluetooth_adapter, AreLowEnergySecureConnectionsSupported);

    return dict;
}

static PyObject* pywinble_advertise(PyObject *self, PyObject *args, PyObject *kws) {
    PyObject *adstatus_callback = NULL;

    static char *kwlist[] = {"data", "on_status" , NULL};
    const char *cdat;

    if (!PyArg_ParseTupleAndKeywords(args, kws, "s|O", kwlist, &cdat, &adstatus_callback, &cdat, &adstatus_callback))
        return NULL;

    if (adstatus_callback) {
        if (!PyCallable_Check(adstatus_callback)) {
            Py_RETURN_ERROR(PyExc_TypeError, "parameter must be callable");
        }
        Py_XINCREF(adstatus_callback);
        Py_XDECREF(on_adstatus_callback);
        on_adstatus_callback = adstatus_callback;
    }

    if (bleAdPub) {
        bleAdPub.Stop();
        bleAdPub = nullptr;
    }

    bleAdPub = Bluetooth::Advertisement::BluetoothLEAdvertisementPublisher();
    if (!bleAdPub) {
        Py_RETURN_ERROR(PyExc_RuntimeError, "Pub create failed");
    }

    auto advertisement = bleAdPub.Advertisement();

    Advertisement::BluetoothLEManufacturerData mandat;
    mandat.CompanyId(0xFFFE);
    auto writer = DataWriter();
    USES_CONVERSION;
    writer.WriteString(A2W(cdat));
    mandat.Data(writer.DetachBuffer());
    advertisement.ManufacturerData().Append(mandat);

    try {
    	bleAdPub.Start();
    } catch (const winrt::hresult_error &e) {
        Py_RETURN_ERROR(PyExc_RuntimeError, w2a(e.message()).c_str());
    }

    if (on_adstatus_callback) {
        bleAdPub.StatusChanged(call_on_adstatus_callback);
    }

    Py_RETURN_NONE;
}

class BLEProvider {
    public:
        GattServiceProvider provider;

        BLEProvider(GattServiceProvider ref) : provider(ref) {
            if (!provider) 
                throw std::exception("empty provider error");
        }

        ~BLEProvider() {
            provider.StopAdvertising();
        }

        wstring getUUID() {
            wstring out;
            auto uuid = provider.Service().Uuid();
            out.resize(64);
            int len = StringFromGUID2(uuid, &out[0], (int) out.size());
            out.resize(len);
            return out;
        }

        GattLocalService Service() {
            return provider.Service();
        }

        void StartAdvertising() {
            auto cparam = GattServiceProviderAdvertisingParameters();
            cparam.IsDiscoverable(true);
            provider.StartAdvertising(cparam);
        }

        void StopAdvertising() {
            provider.StopAdvertising();
        }

};


unique_ptr<BLEProvider> pywinble_provide(const wchar_t * uuid_str, map<string, map<string, py::handle>> characteristics) {
    USES_CONVERSION;

    GUID uuid;
    IIDFromString(uuid_str, &uuid);

    GattServiceProviderResult result = GattServiceProvider::CreateAsync(uuid).get();

    if (result.Error() != BluetoothError::Success)
        Py_RETURN_ERROR(PyExc_RuntimeError, "Bluetooth error");

    auto ble = make_unique<BLEProvider>(result.ServiceProvider());
    
    for (auto item : characteristics) {
        auto key = item.first;
        auto value = item.second;

        if (key.empty()) {
            Py_RETURN_ERROR(PyExc_TypeError, "Invalid characteristic dict {uuid:{key:v},...}");
        }

        IIDFromString(A2W(key.c_str()), &uuid);
        GattLocalCharacteristicParameters cParams;

        for (auto item : value) {
            auto ckey = item.first;
            auto cvalue = item.second;

            if (ckey.empty()) {
                Py_RETURN_ERROR(PyExc_TypeError, "Invalid characteristic dict {uuid:{key:v},...}");
            }

            if (ckey == "flags") {
                cParams.CharacteristicProperties((GattCharacteristicProperties)PyLong_AsLong(cvalue.ptr()));
            } else if (ckey == "description") {
                py::str cv = py::str(cvalue);
                cParams.UserDescription(A2W(string(cv).c_str()));
            }
        }

        auto result = ble->Service().CreateCharacteristicAsync(uuid, cParams).get();

        if (result.Error() != BluetoothError::Success)
            Py_RETURN_ERROR(PyExc_RuntimeError, "Bluetooth error");
    }

    return ble;
}

typedef function< void(string, map<string, map<string, py::handle > >) > *watch_cb_type;

class BLEWatcher {
    public:
        DeviceWatcher watcher;
        watch_cb_type callback;

        BLEWatcher(std::vector<std::string> props,  watch_cb_type callback) : watcher(
            DeviceInformation::CreateWatcher(
                    BluetoothLEDevice::GetDeviceSelectorFromPairingState(false),
                    props,
                    DeviceInformationKind::AssociationEndpoint)) {
            watcher.Added(bind(&BLEWatcher::onAdded, this));
            watcher.Removed(bind(&BLEWatcher::onRemoved, this));
            watcher.Updated(bind(&BLEWatcher::onUpdated, this));
            watcher.EnumerationCompleted(bind(&BLEWatcher::onEnumerationCompleted, this));
            watcher.Stopped(bind(&BLEWatcher::onStopped, this));
        }

        void onCb(const char *type, const DeviceInformation &devinfo) {

        }

        void onAdded(const DeviceWatcher &watcher, const DeviceInformation &devinfo) {
            onCb("added", devinfo);
        }

        void onUpdated(const DeviceWatcher &watcher, const DeviceInformation &devinfo) {
            onCb("updated", devinfo);
        }

        void onRemoved(const DeviceWatcher &watcher, const DeviceInformation &devinfo) {
            onCb("removed", devinfo);
        }

        void onEnumerationCompleted(const DeviceWatcher &watcher, const DeviceInformation &devinfo) {
            onCb("completed", devinfo);
        }

        void onStopped(const DeviceWatcher &watcher, const DeviceInformation &devinfo) {
            onCb("stopped", devinfo);
        }
    
        void start() {
            watcher.Start();
        }
        void stop() {
            watcher.Stop();
        }

        ~BLEWatcher() {
            watcher.Stop();
        }
};


unique_ptr<BLEWatcher> pywinble_watch(vector<string> props, watch_cb_type callback) {
    auto ble = make_unique<BLEWatcher>(props, callback);
    return ble;
}

class cleanup_module {
    public:
    ~cleanup_module() {
        if (bleAdPub) {
            bleAdPub.Stop();
        }
    }
};
auto __guard = cleanup_module();

PYBIND11_MODULE(pywinble, m) {
    winrt_ok();

    py::class_<BLEProvider>(m, "BLEProvider")
        .def_property_readonly("uuid", &BLEProvider::getUUID)
        .def("start", &BLEProvider::StartAdvertising)
        .def("stop", &BLEProvider::StopAdvertising);

    py::class_<BLEWatcher>(m, "BLEWatcher")
        .def("start", &BLEWatcher::start)
        .def("stop", &BLEWatcher::stop);

    m.def("advertise", pywinble_advertise);

    m.def("provide", pywinble_provide);

    m.def("info", pywinble_info);

    m.def("watch", pywinble_watch);
}
