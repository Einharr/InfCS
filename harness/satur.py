# -*- coding: utf-8 -*-
"""We repeat the client call that kills it.

The client (0x0041FAD0) does exactly this:
    DIPROPDWORD{20, 16, dwObj, dwHow}, dwData = value*10000
    SetProperty(DIPROP_SATURATION=MAKEDIPROP(6), &prop)
and treats everything except success and DIERR_OBJECTNOTFOUND (0x80070002) as fatal.
We look at what each device actually answers, and in what order.
"""
import ctypes, os, sys
from ctypes import wintypes, POINTER, byref, c_void_p, c_ulong

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import di

class DIPROPHEADER(ctypes.Structure):
    _fields_ = [('dwSize', wintypes.DWORD), ('dwHeaderSize', wintypes.DWORD),
                ('dwObj', wintypes.DWORD), ('dwHow', wintypes.DWORD)]

class DIPROPDWORD(ctypes.Structure):
    _fields_ = [('diph', DIPROPHEADER), ('dwData', wintypes.DWORD)]

DIPH_DEVICE, DIPH_BYOFFSET, DIPH_BYID = 0, 1, 2
DIPROP_RANGE, DIPROP_DEADZONE, DIPROP_SATURATION = 4, 5, 6

CODES = {0: 'S_OK', 1: 'S_FALSE',
         0x80004001: 'DIERR_UNSUPPORTED', 0x80070002: 'DIERR_OBJECTNOTFOUND',
         0x80070005: 'DIERR_INVALIDPARAM/ACCESS', 0x8007000C: 'DIERR_ACQUIRED',
         0x80070006: 'DIERR_NOTINITIALIZED', 0x80070057: 'E_INVALIDARG'}

def name(hr):
    return CODES.get(hr & 0xFFFFFFFF, '0x%08X' % (hr & 0xFFFFFFFF))

def set_prop(dev, prop, obj, how, data):
    p = DIPROPDWORD()
    p.diph.dwSize = ctypes.sizeof(DIPROPDWORD)
    p.diph.dwHeaderSize = ctypes.sizeof(DIPROPHEADER)
    p.diph.dwObj, p.diph.dwHow, p.dwData = obj, how, data
    fn = di._vt(dev._p, 6, c_ulong, c_void_p, POINTER(DIPROPDWORD))
    return fn(dev._p, ctypes.cast(prop, c_void_p), byref(p)) & 0xFFFFFFFF

def unacquire(dev):
    di._vt(dev._p, 8, c_ulong)(dev._p)

def acquire(dev):
    return di._vt(dev._p, 7, c_ulong)(dev._p) & 0xFFFFFFFF

devs = di.open_devices()
if not devs:
    print('no game devices were found'); raise SystemExit(1)

for d in devs:
    print('=== %s   %s' % (d.name, d.product))
    for label, obj, how in (('X by offset 0 (as the client does)', 0, DIPH_BYOFFSET),
                            ('Y by offset 4', 4, DIPH_BYOFFSET),
                            ('the whole device', 0, DIPH_DEVICE)):
        hr = set_prop(d, DIPROP_SATURATION, obj, how, 10000)
        mark = 'THE CLIENT WILL DIE' if hr not in (0, 1, 0x80070002) and hr < 0x80000000 + 1 and hr != 0 else ''
        fatal = hr != 0 and hr != 1 and hr != 0x80070002
        print('   saturation %-30s -> %-24s %s'
              % (label, name(hr), 'FATAL FOR THE CLIENT' if fatal else ''))
    hr = set_prop(d, DIPROP_DEADZONE, 0, DIPH_BYOFFSET, 1000)
    print('   deadzone   %-30s -> %s' % ('X by offset 0', name(hr)))
    # the same call, but on an unacquired device
    unacquire(d)
    hr = set_prop(d, DIPROP_SATURATION, 0, DIPH_BYOFFSET, 10000)
    print('   saturation %-30s -> %s' % ('after Unacquire', name(hr)))
    print('   Acquire back -> %s' % name(acquire(d)))
    print()
