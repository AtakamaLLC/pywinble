#define PY_SSIZE_T_CLEAN

#pragma comment(lib, "windowsapp")

#include <Python.h>

#include <iostream>
#include <atlbase.h>

#include "winrt/Windows.Foundation.h"
#include "winrt/Windows.Storage.Streams.h"
#include "winrt/Windows.Devices.Bluetooth.h"
#include "winrt/Windows.Devices.Bluetooth.Advertisement.h"
#include "winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h"

//#include <collection.h>
using namespace std;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Devices;
using namespace winrt::Windows::Storage::Streams;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;


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

#define Py_RETURN_ERROR(type, msg) { PyErr_SetString(type, msg); return NULL; }

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
        gil_lock lock;
        int err = (int) status.Error();
        int stat = (int) status.Status();
        PyObject *result = PyObject_CallFunction(on_adstatus_callback, "ii", (int)status.Error(), (int)status.Status());
        Py_XDECREF(result);
    }
}

static PyObject* pywinble_info(PyObject *self, PyObject *args) {
    if (!bluetooth_adapter) {
        bluetooth_adapter = BluetoothAdapter::GetDefaultAsync().get();
        if (!bluetooth_adapter)
            Py_RETURN_ERROR(PyExc_RuntimeError, "Adapter discovery error");
    }

    #define ADD_DICT(dict, key, var) PyDict_SetItemString(dict, key, PyVar(var))
    #define ADD_DICT_O(dict, ob, var) PyDict_SetItemString(dict, #var, PyVar(ob.var()))

    PyObject *dict = PyDict_New();

    ADD_DICT_O(dict, bluetooth_adapter, IsLowEnergySupported);
    ADD_DICT_O(dict, bluetooth_adapter, IsClassicSupported);
    ADD_DICT_O(dict, bluetooth_adapter, IsPeripheralRoleSupported);
    ADD_DICT_O(dict, bluetooth_adapter, IsAdvertisementOffloadSupported);
    ADD_DICT_O(dict, bluetooth_adapter, DeviceId);
    ADD_DICT(dict, "BluetoothAddress", hexlify(bluetooth_adapter.BluetoothAddress(), true));
    ADD_DICT_O(dict, bluetooth_adapter, AreLowEnergySecureConnectionsSupported);

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


static PyObject* pywinble_provide(PyObject *self, PyObject *args, PyObject *kws) {
    static char *kwlist[] = {"uuid" , "characteristics", NULL};
    const char *uuid_str;
    PyObject *chardict;

    if (!PyArg_ParseTupleAndKeywords(args, kws, "sO!", kwlist, &uuid_str, &PyDict_Type, &chardict))
        return NULL;
   
    USES_CONVERSION;

    GUID uuid;
    IIDFromString(A2W(uuid_str), &uuid);

    GattServiceProviderResult result = GattServiceProvider::CreateAsync(uuid).get();

    if (result.Error() != BluetoothError::Success)
        Py_RETURN_ERROR(PyExc_RuntimeError, "Bluetooth error");

    auto serviceProvider = result.ServiceProvider();
    PyObject *key, *value;
    Py_ssize_t pos = 0;

    while (PyDict_Next(chardict, &pos, &key, &value)) {
        if (!PyDict_Check(value) || !PyUnicode_Check(key)) {
            Py_RETURN_ERROR(PyExc_TypeError, "Invalid characteristic dict {uuid:{key:v},...}");
        }

        char * key_str = PyUnicode_AsUTF8(key);

        IIDFromString(A2W(key_str), &uuid);
        GattLocalCharacteristicParameters cParams;

        PyObject *ckey, *cvalue;
        Py_ssize_t cpos = 0;

        while (PyDict_Next(value, &cpos, &ckey, &cvalue)) {
            char * ckey_str = PyUnicode_AsUTF8(key);
            if (!ckey_str) {
                Py_RETURN_ERROR(PyExc_TypeError, "Invalid characteristic dict {uuid:{key:v},...}");
            }

            if (!strcmp(ckey_str, "flags")) {
                cParams.CharacteristicProperties((GattCharacteristicProperties)PyLong_AsLong(cvalue));
            } else if (!strcmp(ckey_str, "description")) {
                char * cval_str = PyUnicode_AsUTF8(cvalue);
                cParams.UserDescription(A2W(cval_str));
            }
        }

        serviceProvider.Service().CreateCharacteristicAsync(uuid, cParams);
    }

    Py_RETURN_NONE;
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

static PyMethodDef pywinbleMethods[] = {
    {"advertise", (PyCFunction) pywinble_advertise, METH_VARARGS|METH_KEYWORDS, PyDoc_STR("ble advertise")},
    {"provide", (PyCFunction) pywinble_provide, METH_VARARGS|METH_KEYWORDS, PyDoc_STR("start gatt provider")},
    {"info", (PyCFunction) pywinble_info, METH_NOARGS, PyDoc_STR("init ble")},
    {NULL, NULL, 0, NULL},
};

#if PY_MAJOR_VERSION >= 3
    static struct PyModuleDef pywinbleDef = {
        PyModuleDef_HEAD_INIT,
        "pywinble",
        NULL,
        -1,
        pywinbleMethods,
        NULL,
        NULL,
        NULL,
        NULL,
    };
	PyMODINIT_FUNC PyInit_pywinble(void) {
        PyEval_InitThreads();
        if (!winrt_ok())
            return NULL;
		return PyModule_Create(&pywinbleDef);
	}
#else
	PyMODINIT_FUNC initpywinble(void)
	{
        PyEval_InitThreads();
        if (!winrt_ok())
            return;
		(void) Py_InitModule("pywinble", pywinbleMethods);
	}
#endif
