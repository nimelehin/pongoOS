#!/usr/bin/env python3
import usb.core
import struct
import sys
import argparse
import time
from elftools.elf.elffile import ELFFile

parser = argparse.ArgumentParser(description='Kernel image to upload')

parser.add_argument('-k', '--kernel', dest='kernel',
                    help='Path to kernel image (or raw image) to upload')
parser.add_argument('-r', '--ramdisk', dest='ramdisk',
                    help='Path to ramdisk to upload')

args = parser.parse_args()

if args.kernel is None:
    print(f"error: No kernel specified! Run `{sys.argv[0]} --help` for usage.")
    exit(1)

dev = usb.core.find(idVendor=0x05ac, idProduct=0x4141)
if dev is None:
    print("Waiting for device...")

    while dev is None:
        dev = usb.core.find(idVendor=0x05ac, idProduct=0x4141)
        if dev is not None:
            dev.set_configuration()
            break
        time.sleep(2)
else:
    dev.set_configuration()

rawimage = open(args.kernel, "rb").read()
rawimage_size = len(rawimage)
dev.ctrl_transfer(0x21, 2, 0, 0, 0)
dev.ctrl_transfer(0x21, 1, 0, 0, struct.pack('I', rawimage_size))
dev.write(2, rawimage, 1000000)
dev.ctrl_transfer(0x21, 4, 0, 0, 0)
print("Sending rawimage...")
try:
    dev.ctrl_transfer(0x21, 3, 0, 0, "rawimgo\n")
except:
    print("Rawimage loaded")

ramdisk = open(args.ramdisk, "rb").read()
ramdisk_size = len(ramdisk)
dev.ctrl_transfer(0x21, 2, 0, 0, 0)
dev.ctrl_transfer(0x21, 1, 0, 0, struct.pack('I', ramdisk_size))
dev.write(2, ramdisk, 1000000)
dev.ctrl_transfer(0x21, 4, 0, 0, 0)
print("Sending ramdisk...")
try:
    dev.ctrl_transfer(0x21, 3, 0, 0, "ramdisko\n")
except:
    print("Ramdisk loaded")


dev.ctrl_transfer(0x21, 4, 0, 0, 0)
print("Booting into opuntiaOS...")
try:
    dev.ctrl_transfer(0x21, 3, 0, 0, "booto\n")
except:
    # if the device disconnects without acknowledging it usually means it succeeded
    print("Success.")
