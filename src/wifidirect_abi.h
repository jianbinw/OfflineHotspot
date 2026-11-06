// Hand-written C bindings for the small part of Windows.Devices.WiFiDirect
// this app uses, so it builds without the C++/WRL projection.
//
// Do not edit by hand without running tools/verify_abi.py. Each method's slot
// must match Microsoft's metadata exactly, and each generic interface ID comes
// from tools/gen_piids.py.
#pragma once

#include <windows.h>
#include <inspectable.h>
#include <hstring.h>

#define WFD_STATUS_CREATED 0
#define WFD_STATUS_STARTED 1
#define WFD_STATUS_STOPPED 2
#define WFD_STATUS_ABORTED 3

#define WFD_ERROR_SUCCESS 0
#define WFD_ERROR_RADIO_NOT_AVAILABLE 1
#define WFD_ERROR_RESOURCE_IN_USE 2

#define WFD_CONNECTION_DISCONNECTED 0
#define WFD_CONNECTION_CONNECTED 1

#define WFD_ASYNC_STARTED 0
#define WFD_ASYNC_COMPLETED 1
#define WFD_ASYNC_CANCELED 2
#define WFD_ASYNC_ERROR 3

#define WFD_INSPECTABLE_METHODS(T)                                              \
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(T *, REFIID, void **);           \
    ULONG(STDMETHODCALLTYPE *AddRef)(T *);                                      \
    ULONG(STDMETHODCALLTYPE *Release)(T *);                                     \
    HRESULT(STDMETHODCALLTYPE *GetIids)(T *, ULONG *, IID **);                  \
    HRESULT(STDMETHODCALLTYPE *GetRuntimeClassName)(T *, HSTRING *);            \
    HRESULT(STDMETHODCALLTYPE *GetTrustLevel)(T *, int *)

#define WFD_DECLARE(T)                                                          \
    typedef struct T##Vtbl T##Vtbl;                                             \
    typedef struct T {                                                          \
        const T##Vtbl *lpVtbl;                                                  \
    } T

WFD_DECLARE(WfdAdvertisement);
WFD_DECLARE(WfdPublisher);
WFD_DECLARE(WfdPublisherStatusChangedEventArgs);
WFD_DECLARE(WfdConnectionListener);
WFD_DECLARE(WfdConnectionRequest);
WFD_DECLARE(WfdConnectionRequestedEventArgs);
WFD_DECLARE(WfdDevice);
WFD_DECLARE(WfdDeviceStatics);
WFD_DECLARE(WfdLegacySettings);
WFD_DECLARE(WfdPasswordCredential);
WFD_DECLARE(WfdDeviceInformation);
WFD_DECLARE(WfdEndpointPair);
WFD_DECLARE(WfdHostName);
WFD_DECLARE(WfdEndpointPairVector);
WFD_DECLARE(WfdAsyncOperationDevice);

struct WfdAdvertisementVtbl {
    WFD_INSPECTABLE_METHODS(WfdAdvertisement);
    void *get_InformationElements;
    void *put_InformationElements;
    void *get_ListenStateDiscoverability;
    void *put_ListenStateDiscoverability;
    void *get_IsAutonomousGroupOwnerEnabled;
    HRESULT(STDMETHODCALLTYPE *put_IsAutonomousGroupOwnerEnabled)(WfdAdvertisement *, unsigned char);
    HRESULT(STDMETHODCALLTYPE *get_LegacySettings)(WfdAdvertisement *, WfdLegacySettings **);
};

struct WfdPublisherVtbl {
    WFD_INSPECTABLE_METHODS(WfdPublisher);
    HRESULT(STDMETHODCALLTYPE *get_Advertisement)(WfdPublisher *, WfdAdvertisement **);
    void *get_Status;
    HRESULT(STDMETHODCALLTYPE *add_StatusChanged)(WfdPublisher *, void *, __int64 *);
    HRESULT(STDMETHODCALLTYPE *remove_StatusChanged)(WfdPublisher *, __int64);
    HRESULT(STDMETHODCALLTYPE *Start)(WfdPublisher *);
    HRESULT(STDMETHODCALLTYPE *Stop)(WfdPublisher *);
};

struct WfdPublisherStatusChangedEventArgsVtbl {
    WFD_INSPECTABLE_METHODS(WfdPublisherStatusChangedEventArgs);
    HRESULT(STDMETHODCALLTYPE *get_Status)(WfdPublisherStatusChangedEventArgs *, int *);
    HRESULT(STDMETHODCALLTYPE *get_Error)(WfdPublisherStatusChangedEventArgs *, int *);
};

struct WfdConnectionListenerVtbl {
    WFD_INSPECTABLE_METHODS(WfdConnectionListener);
    HRESULT(STDMETHODCALLTYPE *add_ConnectionRequested)(WfdConnectionListener *, void *, __int64 *);
    HRESULT(STDMETHODCALLTYPE *remove_ConnectionRequested)(WfdConnectionListener *, __int64);
};

struct WfdConnectionRequestVtbl {
    WFD_INSPECTABLE_METHODS(WfdConnectionRequest);
    HRESULT(STDMETHODCALLTYPE *get_DeviceInformation)(WfdConnectionRequest *, WfdDeviceInformation **);
};

struct WfdConnectionRequestedEventArgsVtbl {
    WFD_INSPECTABLE_METHODS(WfdConnectionRequestedEventArgs);
    HRESULT(STDMETHODCALLTYPE *GetConnectionRequest)(WfdConnectionRequestedEventArgs *, WfdConnectionRequest **);
};

struct WfdDeviceVtbl {
    WFD_INSPECTABLE_METHODS(WfdDevice);
    HRESULT(STDMETHODCALLTYPE *get_ConnectionStatus)(WfdDevice *, int *);
    HRESULT(STDMETHODCALLTYPE *get_DeviceId)(WfdDevice *, HSTRING *);
    HRESULT(STDMETHODCALLTYPE *add_ConnectionStatusChanged)(WfdDevice *, void *, __int64 *);
    HRESULT(STDMETHODCALLTYPE *remove_ConnectionStatusChanged)(WfdDevice *, __int64);
    HRESULT(STDMETHODCALLTYPE *GetConnectionEndpointPairs)(WfdDevice *, WfdEndpointPairVector **);
};

struct WfdDeviceStaticsVtbl {
    WFD_INSPECTABLE_METHODS(WfdDeviceStatics);
    void *GetDeviceSelector;
    HRESULT(STDMETHODCALLTYPE *FromIdAsync)(WfdDeviceStatics *, HSTRING, WfdAsyncOperationDevice **);
};

struct WfdLegacySettingsVtbl {
    WFD_INSPECTABLE_METHODS(WfdLegacySettings);
    void *get_IsEnabled;
    HRESULT(STDMETHODCALLTYPE *put_IsEnabled)(WfdLegacySettings *, unsigned char);
    void *get_Ssid;
    HRESULT(STDMETHODCALLTYPE *put_Ssid)(WfdLegacySettings *, HSTRING);
    HRESULT(STDMETHODCALLTYPE *get_Passphrase)(WfdLegacySettings *, WfdPasswordCredential **);
    void *put_Passphrase;
};

struct WfdPasswordCredentialVtbl {
    WFD_INSPECTABLE_METHODS(WfdPasswordCredential);
    void *get_Resource;
    void *put_Resource;
    void *get_UserName;
    void *put_UserName;
    void *get_Password;
    HRESULT(STDMETHODCALLTYPE *put_Password)(WfdPasswordCredential *, HSTRING);
};

struct WfdDeviceInformationVtbl {
    WFD_INSPECTABLE_METHODS(WfdDeviceInformation);
    HRESULT(STDMETHODCALLTYPE *get_Id)(WfdDeviceInformation *, HSTRING *);
};

struct WfdEndpointPairVtbl {
    WFD_INSPECTABLE_METHODS(WfdEndpointPair);
    HRESULT(STDMETHODCALLTYPE *get_LocalHostName)(WfdEndpointPair *, WfdHostName **);
    void *put_LocalHostName;
    void *get_LocalServiceName;
    void *put_LocalServiceName;
    HRESULT(STDMETHODCALLTYPE *get_RemoteHostName)(WfdEndpointPair *, WfdHostName **);
};

struct WfdHostNameVtbl {
    WFD_INSPECTABLE_METHODS(WfdHostName);
    void *get_IPInformation;
    void *get_RawName;
    HRESULT(STDMETHODCALLTYPE *get_DisplayName)(WfdHostName *, HSTRING *);
};

// IVectorView<EndpointPair>
struct WfdEndpointPairVectorVtbl {
    WFD_INSPECTABLE_METHODS(WfdEndpointPairVector);
    HRESULT(STDMETHODCALLTYPE *GetAt)(WfdEndpointPairVector *, UINT32, WfdEndpointPair **);
    HRESULT(STDMETHODCALLTYPE *get_Size)(WfdEndpointPairVector *, UINT32 *);
};

// IAsyncOperation<WiFiDirectDevice>
struct WfdAsyncOperationDeviceVtbl {
    WFD_INSPECTABLE_METHODS(WfdAsyncOperationDevice);
    HRESULT(STDMETHODCALLTYPE *put_Completed)(WfdAsyncOperationDevice *, void *);
    void *get_Completed;
    HRESULT(STDMETHODCALLTYPE *GetResults)(WfdAsyncOperationDevice *, WfdDevice **);
};

// Runtime class names
#define WFD_CLASS_PUBLISHER L"Windows.Devices.WiFiDirect.WiFiDirectAdvertisementPublisher"
#define WFD_CLASS_LISTENER L"Windows.Devices.WiFiDirect.WiFiDirectConnectionListener"
#define WFD_CLASS_DEVICE L"Windows.Devices.WiFiDirect.WiFiDirectDevice"

// Interface IDs from Microsoft's metadata
static const GUID IID_WfdPublisher = {0xb35a2d1a, 0x9b1f, 0x45d9, {0x92, 0x5a, 0x69, 0x4d, 0x66, 0xdf, 0x68, 0xef}};
static const GUID IID_WfdConnectionListener = {0x699c1b0d, 0x8d13, 0x4ee9, {0xb9, 0xec, 0x9c, 0x72, 0xf8, 0x25, 0x1f, 0x7d}};
static const GUID IID_WfdDeviceStatics = {0xe86cb57c, 0x3aac, 0x4851, {0xa7, 0x92, 0x48, 0x2a, 0xaf, 0x93, 0x1b, 0x04}};

// Parameterized interface IDs from tools/gen_piids.py
static const GUID IID_StatusChangedHandler = {0xde73cba7, 0x370d, 0x550c, {0xb2, 0x3a, 0x53, 0xdd, 0x0b, 0x4e, 0x48, 0x0d}};
static const GUID IID_ConnectionRequestedHandler = {0xd04b0403, 0x1fe2, 0x532f, {0x8e, 0x47, 0x48, 0x23, 0xa1, 0x4e, 0x62, 0x4f}};
static const GUID IID_ConnectionStatusChangedHandler = {0x9208929a, 0x2a3c, 0x50ad, {0xaa, 0x08, 0xa0, 0xa9, 0x86, 0xed, 0xba, 0xbe}};
static const GUID IID_FromIdCompletedHandler = {0xd34abe17, 0xfb19, 0x57be, {0xbc, 0x41, 0x0e, 0xb8, 0x3d, 0xea, 0x15, 0x1c}};

// Standard COM interfaces that delegates answer to
static const GUID IID_WfdIUnknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_WfdIAgileObject = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
