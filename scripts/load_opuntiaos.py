#!/usr/bin/env python3
import usb.core
import struct
import sys
import argparse
import time
from elftools.elf.elffile import ELFFile

parser = argparse.ArgumentParser(description='Kernel image to upload')

parser.add_argument('-k', '--kernel', dest='kernel',
                    help='Path to kernel image to upload')
parser.add_argument('-d', '--devtree', dest='devtree',
                    help='Path to comiled devtree to upload')
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

with open(args.kernel, 'rb') as elffile:
    for segment in ELFFile(elffile).iter_segments():
        seg_head = segment.header
        if seg_head.p_type == "PT_LOAD":
            if len(segment.data()) == 0:
                continue

            senddata = struct.pack('Q', seg_head.p_paddr) + \
                struct.pack('Q', seg_head.p_memsz) + segment.data()
            datalen = len(senddata)

            print("PT_LOAD segement, size ", datalen,
                  seg_head.p_paddr, seg_head.p_memsz)
            dev.ctrl_transfer(0x21, 2, 0, 0, 0)
            dev.ctrl_transfer(0x21, 1, 0, 0, struct.pack('I', datalen))
            dev.write(2, senddata, 1000000)

            dev.ctrl_transfer(0x21, 4, 0, 0, 0)
            print("Sending segment...")
            try:
                dev.ctrl_transfer(0x21, 3, 0, 0, "elfsego\n")
            except:
                print("Segment loaded")


devtree = open(args.devtree, "rb").read()
devtree_size = len(devtree)
dev.ctrl_transfer(0x21, 2, 0, 0, 0)
dev.ctrl_transfer(0x21, 1, 0, 0, struct.pack('I', devtree_size))
dev.write(2, devtree, 1000000)
dev.ctrl_transfer(0x21, 4, 0, 0, 0)
print("Sending devtree...")
try:
    dev.ctrl_transfer(0x21, 3, 0, 0, "devtreeo\n")
except:
    print("Devtree loaded")


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
