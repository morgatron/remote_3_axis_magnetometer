#!/usr/bin/env python3
"""
Termux USB Serial / CDC Driver (`termux_usb_driver.py`)

Low-level USB driver for unrooted Android (Termux) using Linux USBDEVFS ioctls.
Supports:
- Espressif Native USB CDC / USB Serial JTAG (ESP32-S3, ESP32-C3, ESP32-C6)
- Silicon Labs CP2102 / CP2104 USB-to-UART bridges
- WCH CH340 / CH341 USB-to-UART bridges
- FTDI FT232R / FT2232 bridges
- Generic USB-CDC ACM devices
"""

import os
import sys
import time
import ctypes
import fcntl
import struct
from typing import Optional, Tuple, List, Dict, Any

# Platform pointer size check
IS_64BIT = (ctypes.sizeof(ctypes.c_void_p) == 8)

if IS_64BIT:
    class BulkTransfer(ctypes.Structure):
        _fields_ = [
            ('ep', ctypes.c_uint),
            ('len', ctypes.c_uint),
            ('timeout', ctypes.c_uint),
            ('pad', ctypes.c_uint),
            ('data', ctypes.c_void_p),
        ]
    class CtrlTransfer(ctypes.Structure):
        _fields_ = [
            ('bRequestType', ctypes.c_uint8),
            ('bRequest', ctypes.c_uint8),
            ('wValue', ctypes.c_uint16),
            ('wIndex', ctypes.c_uint16),
            ('wLength', ctypes.c_uint16),
            ('timeout', ctypes.c_uint32),
            ('pad', ctypes.c_uint32),
            ('data', ctypes.c_void_p),
        ]
else:
    class BulkTransfer(ctypes.Structure):
        _fields_ = [
            ('ep', ctypes.c_uint),
            ('len', ctypes.c_uint),
            ('timeout', ctypes.c_uint),
            ('data', ctypes.c_void_p),
        ]
    class CtrlTransfer(ctypes.Structure):
        _fields_ = [
            ('bRequestType', ctypes.c_uint8),
            ('bRequest', ctypes.c_uint8),
            ('wValue', ctypes.c_uint16),
            ('wIndex', ctypes.c_uint16),
            ('wLength', ctypes.c_uint16),
            ('timeout', ctypes.c_uint32),
            ('data', ctypes.c_void_p),
        ]

USBDEVFS_CONTROL = (3 << 30) | (ctypes.sizeof(CtrlTransfer) << 16) | (ord('U') << 8) | 0
USBDEVFS_BULK = (3 << 30) | (ctypes.sizeof(BulkTransfer) << 16) | (ord('U') << 8) | 2
USBDEVFS_CLAIMINTERFACE = (1 << 30) | (4 << 16) | (ord('U') << 8) | 15
USBDEVFS_RELEASEINTERFACE = (1 << 30) | (4 << 16) | (ord('U') << 8) | 16
USBDEVFS_DISCONNECT = (1 << 30) | (4 << 16) | (ord('U') << 8) | 22


class TermuxUsbDevice:
    def __init__(self, fd: int, baudrate: int = 921600):
        self.fd = fd
        self.baudrate = baudrate
        self.vid = 0
        self.pid = 0
        self.in_ep = None
        self.out_ep = None
        self.interfaces = []
        self.chipset = "Generic USB CDC"
        self._init_device()

    def _control_transfer(self, req_type: int, req: int, value: int, index: int, data: Optional[bytes] = None, timeout: int = 1000) -> bytes:
        ctrl = CtrlTransfer()
        ctrl.bRequestType = req_type
        ctrl.bRequest = req
        ctrl.wValue = value
        ctrl.wIndex = index
        ctrl.timeout = timeout

        if data:
            buf = ctypes.create_string_buffer(data, len(data))
            ctrl.wLength = len(data)
            ctrl.data = ctypes.addressof(buf)
        else:
            ctrl.wLength = 0
            ctrl.data = None

        res = fcntl.ioctl(self.fd, USBDEVFS_CONTROL, ctrl)
        if res < 0:
            raise OSError(f"Control transfer failed with code {res}")
        return data if data else b""

    def _get_descriptor(self, desc_type: int, desc_index: int = 0, length: int = 256) -> bytes:
        buf = ctypes.create_string_buffer(length)
        ctrl = CtrlTransfer()
        ctrl.bRequestType = 0x80 # Device-to-Host, Standard, Device
        ctrl.bRequest = 0x06    # GET_DESCRIPTOR
        ctrl.wValue = (desc_type << 8) | desc_index
        ctrl.wIndex = 0
        ctrl.wLength = length
        ctrl.timeout = 1000
        ctrl.data = ctypes.addressof(buf)

        res = fcntl.ioctl(self.fd, USBDEVFS_CONTROL, ctrl)
        if res > 0:
            return buf.raw[:res]
        return b""

    def _parse_descriptors(self):
        # 1. Device Descriptor (Type 1)
        dev_desc = self._get_descriptor(1, 0, 18)
        if len(dev_desc) >= 18:
            _, _, _, _, _, _, _, _, vid, pid, _, _, _, _ = struct.unpack("<BBHBBBBHHHBBB", dev_desc[:18])
            self.vid = vid
            self.pid = pid

        # 2. Configuration Descriptor (Type 2)
        cfg_desc = self._get_descriptor(2, 0, 512)
        offset = 0
        in_eps = []
        out_eps = []
        ifaces = []

        while offset + 2 <= len(cfg_desc):
            bLength = cfg_desc[offset]
            if bLength == 0 or offset + bLength > len(cfg_desc):
                break
            bType = cfg_desc[offset + 1]

            if bType == 4: # Interface
                iface_num = cfg_desc[offset + 2]
                if iface_num not in ifaces:
                    ifaces.append(iface_num)
            elif bType == 5: # Endpoint
                ep_addr = cfg_desc[offset + 2]
                ep_attr = cfg_desc[offset + 3]
                # Bulk transfer endpoint (0x02)
                if (ep_attr & 0x03) == 2:
                    if ep_addr & 0x80:
                        in_eps.append(ep_addr)
                    else:
                        out_eps.append(ep_addr)

            offset += bLength

        self.interfaces = ifaces if ifaces else [0, 1]
        self.in_ep = in_eps[0] if in_eps else 0x81
        self.out_ep = out_eps[0] if out_eps else 0x01

        # Identify Chipset
        if self.vid == 0x303A:
            self.chipset = "Espressif Native USB CDC / JTAG"
        elif self.vid == 0x10C4:
            self.chipset = "Silicon Labs CP210x"
        elif self.vid == 0x1A86:
            self.chipset = "WCH CH340/CH341"
        elif self.vid == 0x0403:
            self.chipset = "FTDI FT232"
        else:
            self.chipset = f"USB Device (VID: 0x{self.vid:04X}, PID: 0x{self.pid:04X})"

    def _init_device(self):
        try:
            self._parse_descriptors()
        except Exception:
            self.interfaces = [0, 1]
            self.in_ep = 0x81
            self.out_ep = 0x01

        # Claim Interfaces
        for iface in self.interfaces:
            try:
                fcntl.ioctl(self.fd, USBDEVFS_CLAIMINTERFACE, struct.pack("I", iface))
            except Exception:
                pass

        # Apply Chipset-specific initialization & Line Coding
        if self.vid == 0x10C4: # CP210x
            try:
                self._control_transfer(0x41, 0x00, 0x0001, 0) # IFC_ENABLE
                # Set Baud Rate (0x1E)
                baud_bytes = struct.pack("<I", self.baudrate)
                self._control_transfer(0x41, 0x1E, 0, 0, baud_bytes)
                # DTR=1, RTS=0 (MHS: 0x0101 = DTR high, RTS low)
                self._control_transfer(0x41, 0x07, 0x0101, 0)
            except Exception:
                pass
        elif self.vid == 0x1A86: # CH340
            try:
                self._control_transfer(0x40, 0xA1, 0, 0)
                self._control_transfer(0x40, 0x9A, 0x2518, 0x0056)
                self._control_transfer(0x40, 0xA4, 0x0020, 0) # DTR=1, RTS=0
            except Exception:
                pass
        else: # Standard USB CDC ACM (Espressif / Generic)
            try:
                # 1. SET_LINE_CODING (0x20): Baud, 1 stop bit, no parity, 8 data bits
                line_coding = struct.pack("<IBBB", self.baudrate, 0, 0, 8)
                self._control_transfer(0x21, 0x20, 0, 0, line_coding)
            except Exception:
                pass
            try:
                # 2. SET_CONTROL_LINE_STATE (0x22): DTR=1, RTS=0 (CRITICAL: RTS must be 0 for ESP32!)
                self._control_transfer(0x21, 0x22, 0x0001, 0)
            except Exception:
                pass

    def read(self, size: int = 4096, timeout_ms: int = 1000) -> bytes:
        """Reads raw bytes from the Bulk IN endpoint."""
        buf = ctypes.create_string_buffer(size)
        transfer = BulkTransfer()
        transfer.ep = self.in_ep
        transfer.len = size
        transfer.timeout = timeout_ms
        transfer.data = ctypes.addressof(buf)

        try:
            res = fcntl.ioctl(self.fd, USBDEVFS_BULK, transfer)
            if res > 0:
                return buf.raw[:res]
        except Exception:
            pass
        return b""

    def write(self, data: bytes, timeout_ms: int = 1000) -> int:
        """Writes raw bytes to the Bulk OUT endpoint."""
        buf = ctypes.create_string_buffer(data, len(data))
        transfer = BulkTransfer()
        transfer.ep = self.out_ep
        transfer.len = len(data)
        transfer.timeout = timeout_ms
        transfer.data = ctypes.addressof(buf)

        try:
            res = fcntl.ioctl(self.fd, USBDEVFS_BULK, transfer)
            return res if res >= 0 else 0
        except Exception:
            return 0
