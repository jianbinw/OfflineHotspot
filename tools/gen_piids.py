#!/usr/bin/env python3
"""Derive the WinRT parameterized interface IDs used by the app.

Implements the documented algorithm: SHA-1 over a fixed namespace GUID plus
the UTF-8 type signature, then the UUIDv5 version and variant bits.
Run with no arguments to print the values in src/wifidirect_abi.h.
"""
import hashlib
import re
import sys
import uuid

NS = uuid.UUID("11f47ad5-7b73-42c0-abae-878b1e16adee")

TYPED_EVENT_HANDLER = "9de1c534-6ae1-11e0-84e1-18a905bcc53f"
ASYNC_OP_COMPLETED = "fcdcf02c-e5d8-4478-915a-4d90b74b83a5"
ASYNC_OPERATION = "9fc2b0bb-e446-44e2-aa61-9cab8f636af2"
VECTOR_VIEW = "bbe1fa4c-b0e3-4583-baef-1f1b2e483e56"

WFD = "Windows.Devices.WiFiDirect."
OBJECT = "cinterface(IInspectable)"


def rc(name, default_iface):
    return "rc(%s;{%s})" % (name, default_iface)


def pinterface(generic, *args):
    return "pinterface({%s};%s)" % (generic, ";".join(args))


def piid(signature):
    digest = bytearray(hashlib.sha1(NS.bytes + signature.encode("utf-8")).digest()[:16])
    digest[6] = (digest[6] & 0x0F) | 0x50
    digest[8] = (digest[8] & 0x3F) | 0x80
    return uuid.UUID(bytes=bytes(digest))


PUBLISHER = rc(WFD + "WiFiDirectAdvertisementPublisher", "b35a2d1a-9b1f-45d9-925a-694d66df68ef")
PUB_ARGS = rc(WFD + "WiFiDirectAdvertisementPublisherStatusChangedEventArgs", "aafde53c-5481-46e6-90dd-32116518f192")
LISTENER = rc(WFD + "WiFiDirectConnectionListener", "699c1b0d-8d13-4ee9-b9ec-9c72f8251f7d")
REQ_ARGS = rc(WFD + "WiFiDirectConnectionRequestedEventArgs", "f99d20be-d38d-484f-8215-e7b65abf244c")
DEVICE = rc(WFD + "WiFiDirectDevice", "72deaaa8-72eb-4dae-8a28-8513355d2777")
ENDPOINT_PAIR = rc("Windows.Networking.EndpointPair", "33a0aa36-f8fa-4b30-b856-76517c3bd06d")

TARGETS = {
    "IID_StatusChangedHandler": pinterface(TYPED_EVENT_HANDLER, PUBLISHER, PUB_ARGS),
    "IID_ConnectionRequestedHandler": pinterface(TYPED_EVENT_HANDLER, LISTENER, REQ_ARGS),
    "IID_ConnectionStatusChangedHandler": pinterface(TYPED_EVENT_HANDLER, DEVICE, OBJECT),
    "IID_FromIdCompletedHandler": pinterface(ASYNC_OP_COMPLETED, DEVICE),
    "IID_AsyncOperationDevice": pinterface(ASYNC_OPERATION, DEVICE),
    "IID_VectorViewEndpointPair": pinterface(VECTOR_VIEW, ENDPOINT_PAIR),
}


def c_guid(u):
    h = u.hex
    return "{0x%s, 0x%s, 0x%s, {%s}}" % (
        h[0:8], h[8:12], h[12:16],
        ", ".join("0x" + h[i:i + 2] for i in range(16, 32, 2)))


if __name__ == "__main__":
    for name, sig in TARGETS.items():
        u = piid(sig)
        print("%-36s %s\n    %s\n    %s" % (name, u, sig, c_guid(u)))
