#!/usr/bin/env python3
"""Checks src/wifidirect_abi.h against independent sources.

  verify_abi.py <windows-rs checkout> <mingw-w64-headers/include>

windows-rs supplies Microsoft's generated vtables and IIDs. MinGW-w64 supplies
the generic interface layouts and published parameterized IIDs, which confirm
that gen_piids.py implements the algorithm correctly.
"""
import os
import re
import sys
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_piids as g

HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "wifidirect_abi.h")
INSPECTABLE_SLOTS = 6

rs_root, mingw = sys.argv[1], sys.argv[2]
RS = os.path.join(rs_root, "crates/libs/windows/src/Windows")
SOURCES = {
    "WfdAdvertisement": ("Devices/WiFiDirect", "IWiFiDirectAdvertisement"),
    "WfdPublisher": ("Devices/WiFiDirect", "IWiFiDirectAdvertisementPublisher"),
    "WfdPublisherStatusChangedEventArgs": ("Devices/WiFiDirect", "IWiFiDirectAdvertisementPublisherStatusChangedEventArgs"),
    "WfdConnectionListener": ("Devices/WiFiDirect", "IWiFiDirectConnectionListener"),
    "WfdConnectionRequest": ("Devices/WiFiDirect", "IWiFiDirectConnectionRequest"),
    "WfdConnectionRequestedEventArgs": ("Devices/WiFiDirect", "IWiFiDirectConnectionRequestedEventArgs"),
    "WfdDevice": ("Devices/WiFiDirect", "IWiFiDirectDevice"),
    "WfdDeviceStatics": ("Devices/WiFiDirect", "IWiFiDirectDeviceStatics"),
    "WfdLegacySettings": ("Devices/WiFiDirect", "IWiFiDirectLegacySettings"),
    "WfdPasswordCredential": ("Security/Credentials", "IPasswordCredential"),
    "WfdDeviceInformation": ("Devices/Enumeration", "IDeviceInformation"),
    "WfdEndpointPair": ("Networking", "IEndpointPair"),
    "WfdHostName": ("Networking", "IHostName"),
}

failures = []
checks = 0


def check(ok, message):
    global checks
    checks += 1
    if not ok:
        failures.append(message)


def rust_slots(module, name):
    text = open(os.path.join(RS, module, "mod.rs")).read()
    body = re.search(r"pub struct %s_Vtbl \{(.*?)^\}" % name, text, re.S | re.M).group(1)
    slots = []
    for line in body.splitlines():
        line = line.strip()
        if "base__" in line or line.startswith("#[cfg(feature"):
            continue
        if line.startswith("#[cfg(not"):
            slots.append(None)  # the matching feature-gated line is already recorded
            continue
        m = re.match(r"(?:pub )?(\w+): (.*),$", line)
        if m:
            if slots and slots[-1] is None:
                slots.pop()
                continue
            slots.append((m.group(1), m.group(2).count("*mut") + m.group(2).count(", ")))
    return slots


def rust_iid(module, name):
    text = open(os.path.join(RS, module, "mod.rs")).read()
    m = re.search(r"define_interface!\(%s, %s_Vtbl, 0x([0-9a-f_]+)\)" % (name, name), text)
    return uuid.UUID(m.group(1).replace("_", ""))


def header_slots(name):
    text = open(HEADER).read()
    body = re.search(r"struct %sVtbl \{(.*?)^\};" % name, text, re.S | re.M).group(1)
    slots = []
    for line in body.splitlines():
        line = line.strip()
        m = re.match(r"void \*(\w+);", line) or re.match(r"(?:HRESULT|ULONG)\(STDMETHODCALLTYPE \*(\w+)\)", line)
        if m:
            slots.append(m.group(1))
    return slots


def expected_name(rust_name, rust_names):
    """Maps a windows-rs method name to its ABI name."""
    if rust_name.startswith("Remove") and rust_name[6:] in rust_names:
        return "remove_" + rust_name[6:]
    if rust_name.startswith("Set") and rust_name[3:] in rust_names:
        return "put_" + rust_name[3:]
    return rust_name


def abi_matches(mine, theirs):
    if mine == theirs:
        return True
    return theirs in ("get_" + mine, "add_" + mine) or mine in ("get_" + theirs, "add_" + theirs)


header_text = open(HEADER).read()

for name, (module, rust_name) in SOURCES.items():
    theirs = rust_slots(module, rust_name)
    names = {slot[0] for slot in theirs}
    mine = header_slots(name)
    check(len(mine) <= len(theirs), "%s has too many slots" % name)
    for index, method in enumerate(mine):
        wanted = expected_name(theirs[index][0], names)
        check(abi_matches(method, wanted),
              "%s slot %d: header has %s, metadata has %s" % (name, index + INSPECTABLE_SLOTS, method, wanted))
    print("%-36s %2d slots match %s" % (name, len(mine), rust_name))


def header_guid(name):
    m = re.search(r"GUID %s = \{0x(\w+), 0x(\w+), 0x(\w+), \{(.*?)\}\};" % name, header_text)
    tail = "".join(x.strip()[2:] for x in m.group(4).split(","))
    return uuid.UUID(m.group(1) + m.group(2) + m.group(3) + tail)


for name, (module, rust_name) in {
    "IID_WfdPublisher": ("Devices/WiFiDirect", "IWiFiDirectAdvertisementPublisher"),
    "IID_WfdConnectionListener": ("Devices/WiFiDirect", "IWiFiDirectConnectionListener"),
    "IID_WfdDeviceStatics": ("Devices/WiFiDirect", "IWiFiDirectDeviceStatics"),
}.items():
    check(header_guid(name) == rust_iid(module, rust_name), "%s differs from metadata" % name)

# Default interface IDs fed into the signature hashes.
for sig_iid, module, rust_name in [
    ("b35a2d1a-9b1f-45d9-925a-694d66df68ef", "Devices/WiFiDirect", "IWiFiDirectAdvertisementPublisher"),
    ("aafde53c-5481-46e6-90dd-32116518f192", "Devices/WiFiDirect", "IWiFiDirectAdvertisementPublisherStatusChangedEventArgs"),
    ("699c1b0d-8d13-4ee9-b9ec-9c72f8251f7d", "Devices/WiFiDirect", "IWiFiDirectConnectionListener"),
    ("f99d20be-d38d-484f-8215-e7b65abf244c", "Devices/WiFiDirect", "IWiFiDirectConnectionRequestedEventArgs"),
    ("72deaaa8-72eb-4dae-8a28-8513355d2777", "Devices/WiFiDirect", "IWiFiDirectDevice"),
    ("33a0aa36-f8fa-4b30-b856-76517c3bd06d", "Networking", "IEndpointPair"),
]:
    check(uuid.UUID(sig_iid) == rust_iid(module, rust_name), "signature IID for %s differs" % rust_name)

# Header values match the generator.
for name in ("IID_StatusChangedHandler", "IID_ConnectionRequestedHandler",
             "IID_ConnectionStatusChangedHandler", "IID_FromIdCompletedHandler"):
    check(header_guid(name) == g.piid(g.TARGETS[name]), "%s differs from gen_piids.py" % name)

# Generic GUIDs and layouts, from MinGW's IDL.
idl = open(os.path.join(mingw, "windows.foundation.collections.idl")).read()
for generic, declaration in [
    (g.TYPED_EVENT_HANDLER, "TypedEventHandler<"),
    (g.ASYNC_OP_COMPLETED, "AsyncOperationCompletedHandler<"),
    (g.ASYNC_OPERATION, "IAsyncOperation<"),
    (g.VECTOR_VIEW, "IVectorView<"),
]:
    m = re.search(r"uuid\(%s\)" % generic, idl)
    check(m is not None and declaration in idl[m.end():m.end() + 200], "generic GUID %s not found" % declaration)

async_op = re.search(r"interface IAsyncOperation<TResult> : IInspectable\s*\{(.*?)\}", idl, re.S).group(1)
methods = re.findall(r"HRESULT (\w+)\(", async_op)
check(methods[0] == "Completed" and methods[1] == "Completed" and methods[2] == "GetResults",
      "IAsyncOperation layout: %s" % methods)
check("propput" in async_op.split("HRESULT")[0], "IAsyncOperation: put_Completed must be first")
vector = re.search(r"interface IVectorView<T> : IInspectable.*?\{(.*?)\}", idl, re.S).group(1)
check(re.findall(r"HRESULT (\w+)\(", vector)[:2] == ["GetAt", "Size"], "IVectorView layout")


def published_guid(path, symbol):
    text = open(os.path.join(mingw, path)).read()
    m = re.search(r"DEFINE_GUID\(IID_%s, 0x(\w+), 0x(\w+), 0x(\w+), (.*?)\);" % re.escape(symbol), text)
    if not m:
        return None
    tail = "".join(x.strip()[2:] for x in m.group(4).split(","))
    return uuid.UUID(m.group(1).rjust(8, "0") + m.group(2).rjust(4, "0") + m.group(3).rjust(4, "0") + tail)


# Algorithm check: published values covering each signature form the app uses.
DEVICE_INFO = g.rc("Windows.Devices.Enumeration.DeviceInformation", "aba0fb95-4398-489d-8e44-e6130927011f")
DEVICE_WATCHER = g.rc("Windows.Devices.Enumeration.DeviceWatcher", str(rust_iid("Devices/Enumeration", "IDeviceWatcher")))
PREFIX = "Windows__CDevices__CEnumeration__C"
for header, symbol, signature in [
    ("windows.foundation.h", "__FIVectorView_1_HSTRING", g.pinterface(g.VECTOR_VIEW, "string")),
    ("windows.foundation.h", "__FIVectorView_1_IInspectable", g.pinterface(g.VECTOR_VIEW, g.OBJECT)),
    ("windows.devices.enumeration.h", "__FIAsyncOperation_1_%sDeviceInformation" % PREFIX,
     g.pinterface(g.ASYNC_OPERATION, DEVICE_INFO)),
    ("windows.devices.enumeration.h", "__FIAsyncOperationCompletedHandler_1_%sDeviceInformation" % PREFIX,
     g.pinterface(g.ASYNC_OP_COMPLETED, DEVICE_INFO)),
    ("windows.devices.enumeration.h", "__FITypedEventHandler_2_%sDeviceWatcher_%sDeviceInformation" % (PREFIX, PREFIX),
     g.pinterface(g.TYPED_EVENT_HANDLER, DEVICE_WATCHER, DEVICE_INFO)),
    ("windows.devices.enumeration.h", "__FITypedEventHandler_2_%sDeviceWatcher_IInspectable" % PREFIX,
     g.pinterface(g.TYPED_EVENT_HANDLER, DEVICE_WATCHER, g.OBJECT)),
]:
    published = published_guid(header, symbol)
    check(published is not None, "no published value for %s" % symbol)
    if published:
        check(published == g.piid(signature), "algorithm mismatch for %s" % symbol)
        print("algorithm matches published %-70s %s" % (symbol[:70], published))

objidl = open(os.path.join(mingw, "objidlbase.idl")).read()
check(str(header_guid("IID_WfdIAgileObject")) in objidl, "IAgileObject IID")

if failures:
    print("\nFAILED:")
    for failure in failures:
        print("  " + failure)
    sys.exit(1)
print("\nAll %d ABI checks passed" % checks)
