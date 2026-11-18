#!/usr/bin/env python3
"""Set or read the Mac's Caps Lock state (IOKit), so keyboards get a Caps Lock LED report without a
key press. Used to check the R3S indicators. Usage: python3 tools/r3s/caps_lock.py on|off|get"""
import ctypes
import ctypes.util
import sys

iokit = ctypes.cdll.LoadLibrary(ctypes.util.find_library("IOKit"))
libc = ctypes.CDLL(None)
iokit.IOServiceMatching.restype = ctypes.c_void_p
iokit.IOServiceMatching.argtypes = [ctypes.c_char_p]
iokit.IOServiceGetMatchingService.restype = ctypes.c_uint32
iokit.IOServiceGetMatchingService.argtypes = [ctypes.c_uint32, ctypes.c_void_p]
iokit.IOServiceOpen.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
iokit.IOHIDSetModifierLockState.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_bool]
iokit.IOHIDGetModifierLockState.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.POINTER(ctypes.c_bool)]
CAPS_LOCK, HID_PARAM_CONNECT = 1, 1

service = iokit.IOServiceGetMatchingService(0, iokit.IOServiceMatching(b"IOHIDSystem"))
conn = ctypes.c_uint32()
task = ctypes.c_uint32.in_dll(libc, "mach_task_self_")
if iokit.IOServiceOpen(service, task, HID_PARAM_CONNECT, ctypes.byref(conn)) != 0:
    sys.exit("could not open IOHIDSystem")
if sys.argv[1] == "get":
    state = ctypes.c_bool()
    iokit.IOHIDGetModifierLockState(conn, CAPS_LOCK, ctypes.byref(state))
    print("on" if state.value else "off")
else:
    sys.exit(iokit.IOHIDSetModifierLockState(conn, CAPS_LOCK, sys.argv[1] == "on"))
