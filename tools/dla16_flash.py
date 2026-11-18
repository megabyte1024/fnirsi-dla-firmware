# SPDX-License-Identifier: MIT
"""Install project bootloader and firmware on FNIRSI DLA-16 logic analyzer.

usage: python dla16_flash.py status
       python dla16_flash.py mcu FILE
       python dla16_flash.py bootloader BOOTLOADER.bin FIRMWARE.bin
       python dla16_flash.py full FILE [--force] [--rewrite]
       python dla16_flash.py fpga FILE [--force] [--rewrite]

status       show what runs on analyzer: firmware version and whose firmware it is, or which bootloader
mcu          install MCU firmware from FILE over USB through project bootloader (case stays closed). FILE:
             project firmware, or FNIRSI's firmware file (for example DLA32_V1.1_26052116.bin) to put
             FNIRSI's MCU firmware back; its FPGA part is not sent, FPGA image on analyzer stays as it is.
             FNIRSI's bootloader installs MCU firmware only together with FPGA image: there, use 'full'.
             Bootloader installer (build/dla16_installer.bin): writes project bootloader with case closed;
             script then says to install MCU firmware and ends without error.
bootloader   once, with case open: write project bootloader through WCH's chip bootloader (SW4 bridged
             while USB cable is plugged in), then install firmware. Replaces FNIRSI's bootloader for good.
full         install MCU part, then FPGA part, of firmware file. FILE: FNIRSI's firmware file or one
             made by dla16_pack.py. Project bootloader: parts one
             after another, both verified. FNIRSI's bootloader: both parts in one transfer, firmware started
             first each time; FPGA image cannot be read back there, so it is not verified. At end unplug
             analyzer and plug it in once: FPGA image loads only at power-up.
fpga         same for FPGA part only, through project bootloader. FILE: firmware file (its FPGA part is taken)
             or raw FPGA image.
--force      write FPGA image that is not known, or onto flash chip with other id. FPGA image of
             other model is always refused.
--rewrite    write FPGA image even if analyzer holds it already (FNIRSI's bootloader always writes it).

mcu, full and fpga: when USB drops during transfer, script waits up to 60 s for analyzer to return,
then starts same image again from its start, at most 3 attempts per image. FNIRSI's bootloader: any
failure starts whole transfer again from firmware, at most 8 attempts in all.

Needs Python 3.10 or later and: pip install pyusb libusb-package
"""
import importlib.util
import os
import struct
import sys
import time
import zlib

try:
    import libusb_package
    import usb.core
    import usb.util
except ImportError:
    print("This script needs pyusb and libusb-package:  pip install pyusb libusb-package")
    sys.exit(1)

DLA_VID, DLA_PID = 0x1A86, 0x5537
DLA_OUT, DLA_IN = 0x02, 0x82
PACKET = 512
CHUNK = 256
REPLY_TIMEOUT_MS = 15000
START_TIMEOUT_MS = 20000
WRITE_TIMEOUT_MS = 10000
WRITE_ATTEMPTS = 6
PACE_S = 0.03
PHASE_PAUSE_S = 0.5
OPEN_WAIT_S = 15
RAW_IMAGE_WIRE_NAME = "MCU_V1.1.bin"
RAW_FPGA_WIRE_NAME = "FPGA_V2.0.bin"
OWN_MARK = b"megabyte"
INSTALLER_MARK = b"DLA16-BOOTLOADER-INSTALLER"
BOOTLOADER_INSTALLED = "Bootloader installed. Now install MCU firmware: python tools/dla16_flash.py mcu FILE"
OWN_MARK_AT = 9
BUSY_AT = OWN_MARK_AT + len(OWN_MARK)
READY_WAIT_S = 10
APP_LIMIT = 0x65000 - 0x1000
MIN_MCU_BYTES = 1024
OUR_BOOTLOADER_MARK = 1
FNIRSI_BOOTLOADER_MARK = 0
FNIRSI_ANSWER_BYTES = PACKET
FNIRSI_ATTEMPTS = 8
ANALYZER_CHANNELS = 16
FLASH_ID = bytes([0x85, 0x20, 0x18])
FPGA_LIMIT = 0x100000
FPGA_MIN_BYTES = 8
FPGA_CHUNK = 256
SECTOR = 0x1000
CRC_PIECE = 0x10000
FPGA_ATTEMPTS = 3
MCU_ATTEMPTS = 3
USB_RETURN_WAIT_S = 60
USB_RETURN_POLL_S = 0.5
FPGA_PROGRESS_CHUNKS = 100
STATE_NONE, STATE_MCU, STATE_FPGA = 0, 1, 2

ISP_VID, ISP_PID = 0x4348, 0x55E0
ISP_OUT, ISP_IN = 0x02, 0x82
ISP_IDENTIFY, ISP_END, ISP_KEY, ISP_ERASE, ISP_PROGRAM, ISP_VERIFY, ISP_READ_CONFIG = (
    0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7)
ISP_CONFIG_BOOTLOADER_VERSION, ISP_CONFIG_UID = 0x08, 0x10
ISP_IDENTIFY_TEXT = b"MCU ISP & WCH.CN"
ISP_CHUNK = 56
ISP_TIMEOUT_MS = 10000
ISP_WAIT_S = 300
CH569_ID, CH569_TYPE = 0x69, 0x10
BOOTLOADER_LIMIT = 0x1000
ERASE_UNIT = 0x800
BOOTLOADER_ERASE_COUNT = 2


class FlashError(Exception):
    pass


class UsbLost(FlashError):
    def __init__(self, message, detail=None):
        super().__init__(message)
        self.detail = message if detail is None else detail

    def at(self, place):
        return UsbLost(f"USB lost at {place}: {self.detail}", self.detail)


def backend():
    return libusb_package.get_libusb1_backend()


def find(vid, pid):
    return usb.core.find(idVendor=vid, idProduct=pid, backend=backend())


def access_advice(vid, pid):
    if sys.platform == "win32":
        return (f"Windows needs WinUSB driver for device {vid:04x}:{pid:04x}: see README, "
                "section 'Windows drivers'.")
    return f"no access to USB device {vid:04x}:{pid:04x}: see README, section 'Linux permissions'."


class CannotOpen(FlashError):
    pass


def claim(device, vid, pid):
    try:
        usb.util.claim_interface(device, 0)
    except (usb.core.USBError, NotImplementedError) as error:
        raise CannotOpen(f"cannot open analyzer ({error}). {access_advice(vid, pid)}") from error


DRIVER_SWITCHED = ("Analyzer is back on USB, but Windows switched analyzer to WCH's driver, so script cannot ask which "
                   "firmware runs. Before next update with this script, switch it back to WinUSB (README, section "
                   "'Windows drivers').")


def driver_switched(error):
    return isinstance(error, CannotOpen) and sys.platform == "win32"


class Dla:
    def __init__(self):
        self.device = None

    def open(self, wait_s=OPEN_WAIT_S):
        deadline = time.monotonic() + wait_s
        while True:
            device = find(DLA_VID, DLA_PID)
            if device is not None:
                break
            if time.monotonic() >= deadline:
                raise FlashError(f"no DLA-16 found on USB ({DLA_VID:04x}:{DLA_PID:04x}). Is it plugged in?")
            time.sleep(0.2)
        claim(device, DLA_VID, DLA_PID)
        self.device = device

    def close(self):
        if self.device is not None:
            try:
                usb.util.release_interface(self.device, 0)
                usb.util.dispose_resources(self.device)
            except usb.core.USBError:
                pass
            self.device = None

    def write(self, packet, what):
        if self.device is None:
            raise UsbLost(f"{what}: analyzer is not connected")
        for attempt in range(1, WRITE_ATTEMPTS + 1):
            try:
                written = self.device.write(DLA_OUT, packet, WRITE_TIMEOUT_MS)
            except usb.core.USBTimeoutError:
                written = 0
            except usb.core.USBError as error:
                raise UsbLost(f"{what}: {error}", str(error)) from error
            if written == len(packet):
                return
            if attempt >= 2:
                for endpoint in (DLA_OUT, DLA_IN):
                    try:
                        self.device.clear_halt(endpoint)
                    except usb.core.USBError:
                        pass
            print(f"  {what}: analyzer did not take command, sending again ({attempt} of {WRITE_ATTEMPTS})")
        raise FlashError(f"{what}: analyzer took no command in {WRITE_ATTEMPTS} attempts")

    def read(self, timeout_ms):
        if self.device is None:
            raise UsbLost("reading reply: analyzer is not connected")
        try:
            return bytes(self.device.read(DLA_IN, PACKET, timeout_ms))
        except usb.core.USBTimeoutError:
            return None
        except usb.core.USBError as error:
            raise UsbLost(f"reading reply: {error}", str(error)) from error

    def drain(self):
        while self.read(100):
            pass

    def exchange(self, packet, what, timeout_ms=REPLY_TIMEOUT_MS, pad=True):
        self.write(packet.ljust(PACKET, b"\0") if pad else packet, what)
        time.sleep(PACE_S)
        reply = self.read(timeout_ms)
        time.sleep(PACE_S)
        return reply


class AnswerMismatch(FlashError):
    pass


def expect(reply, prefix, what):
    if reply is None:
        raise AnswerMismatch(f"{what}: analyzer did not answer")
    if reply[:len(prefix)] != prefix:
        raise AnswerMismatch(f"{what}: unexpected answer {reply[:16].hex(' ')}")


def version_reply(dla):
    dla.exchange(b"\x0a\x0b", "flush")
    reply = dla.exchange(b"\x0a\x81", "version query")
    expect(reply, b"\x0a\x81\x01", "version query")
    return reply


def application_running(reply):
    return reply[3] == 0x61 and (reply[4], reply[5]) != (0, 0)


def own_firmware(reply):
    return reply[OWN_MARK_AT:OWN_MARK_AT + len(OWN_MARK)] == OWN_MARK


def fnirsi_bootloader(reply):
    return (not application_running(reply) and reply[OWN_MARK_AT] == FNIRSI_BOOTLOADER_MARK
            and len(reply) == FNIRSI_ANSWER_BYTES)


def describe(reply):
    if fnirsi_bootloader(reply):
        return "FNIRSI's bootloader running"
    if not application_running(reply):
        return "bootloader running (waiting for firmware)"
    whose = "project firmware" if own_firmware(reply) else "FNIRSI's firmware"
    return f"{whose} {reply[4]}.{reply[5]} running"


def is_installer(image):
    return INSTALLER_MARK in image


def waits_in_own_bootloader(reply):
    return not application_running(reply) and reply[OWN_MARK_AT] == OUR_BOOTLOADER_MARK


def installer_failure(why):
    return FlashError(f"bootloader installer did not finish: {why}. If status light flashes red and blue, installer "
                      "failed: unplug analyzer and plug it in again. If it answers then, run 'status' and try again. If "
                      "nothing answers, install bootloader with case open (README, section 'Optional: bootloader').")


def start_written_image(dla, installer, starting_text):
    print("Starting bootloader installer ..." if installer else starting_text)
    try:
        reset_into(dla, bootloader=False)
        reply = version_reply(dla)
    except FlashError as error:
        if not installer:
            if driver_switched(error):
                print(DRIVER_SWITCHED)
                return None
            raise
        raise installer_failure(f"analyzer did not come back ({error})") from error
    print(f"Now: {describe(reply)}.")
    return reply


def installer_finished(reply):
    if application_running(reply):
        return False
    if not waits_in_own_bootloader(reply):
        raise installer_failure(f"analyzer came back in {describe(reply)}, not in project bootloader")
    print(BOOTLOADER_INSTALLED)
    return True


def reset_into(dla, bootloader):
    select = b"\x0a\x89" + (b"\x01" if bootloader else b"\x00") + b"\0" * 7
    expect(dla.exchange(select, "start selection", pad=False), b"\x0a\x89\x01", "start selection")
    dla.write(b"\x0a\x90".ljust(PACKET, b"\0"), "restart")
    dla.close()
    time.sleep(1.0)
    dla.open()
    dla.drain()


def parse_container(blob):
    entries = []
    for base in (0x00, 0x2C):
        name = blob[base:base + 0x20].split(b"\0")[0].decode("ascii")
        offset, size, last = struct.unpack_from("<III", blob, base + 0x20)
        if not name or last != offset + size - 1 or offset + size > len(blob):
            raise ValueError("not a firmware container")
        entries.append((name, blob[offset:offset + size]))
    return entries


def firmware_image(path):
    blob = open(path, "rb").read()
    try:
        (name, image), _ = parse_container(blob)
        return image, mcu_wire_name(name), f"FNIRSI's firmware from {os.path.basename(path)} ({name})"
    except (ValueError, struct.error, UnicodeDecodeError):
        pass
    if not 1024 <= len(blob) <= APP_LIMIT:
        raise FlashError(f"{path}: {len(blob)} bytes is no firmware image (1 KiB to {APP_LIMIT // 1024} KiB)")
    return blob, RAW_IMAGE_WIRE_NAME, f"firmware image {os.path.basename(path)}"


def send_image(dla, wire_name, image):
    header = b"\x0a\x82" + struct.pack("<I", len(image)) + wire_name.encode("ascii")
    chunks = [image[i:i + CHUNK] for i in range(0, len(image), CHUNK)]
    place = "image start"
    try:
        expect(dla.exchange(header, "image start", timeout_ms=START_TIMEOUT_MS), b"\x0a\x82\x01", "image start")
        for number, data in enumerate(chunks, 1):
            place = f"chunk {number} of {len(chunks)}"
            packet = b"\x0a\x83" + struct.pack("<H", len(data)) + data
            expect(dla.exchange(packet, f"chunk {number}"), b"\x0a\x83\x01", place)
            if number % 20 == 0 or number == len(chunks):
                print(f"\r  sent {number} of {len(chunks)} chunks", end="", flush=True)
    except UsbLost as lost:
        print()
        raise lost.at(place) from lost
    print()


def wait_for_analyzer(dla, reason):
    print(f"  {reason}; waiting up to {USB_RETURN_WAIT_S} s for analyzer on USB ...")
    dla.close()
    deadline = time.monotonic() + USB_RETURN_WAIT_S
    while True:
        try:
            dla.open(wait_s=0)
            dla.drain()
            return version_reply(dla)
        except FlashError as error:
            dla.close()
            last = str(error)
        if time.monotonic() >= deadline:
            raise FlashError(f"analyzer did not come back on USB within {USB_RETURN_WAIT_S} s ({last})")
        time.sleep(USB_RETURN_POLL_S)


def reconnect(dla, reason):
    reply = wait_for_analyzer(dla, reason)
    if application_running(reply):
        reply = enter_bootloader(dla)
    require_own_bootloader(reply)
    store_selection(dla, bootloader=True)
    time.sleep(PHASE_PAUSE_S)
    print("  analyzer is back in project bootloader")


def send_mcu_image(dla, wire_name, image, command):
    for attempt in range(1, MCU_ATTEMPTS + 1):
        try:
            send_image(dla, wire_name, image)
            return
        except UsbLost as lost:
            reason = str(lost)
            if attempt == MCU_ATTEMPTS:
                raise FlashError(f"MCU image incomplete: {reason}, {MCU_ATTEMPTS} attempts made. "
                                 f"Run '{command}' again.") from lost
            try:
                reconnect(dla, reason)
            except FlashError as gone:
                raise FlashError(f"MCU image incomplete: {reason}; {gone}; {attempt} of {MCU_ATTEMPTS} attempts "
                                 f"made. Run '{command}' again.") from gone
            print(f"  MCU image: starting again ({attempt + 1} of {MCU_ATTEMPTS})")


def install_firmware(path, dla=None):
    image, wire_name, what = firmware_image(path)
    print(f"Installing {what}, {len(image)} bytes.")
    if dla is None:
        dla = Dla()
        dla.open()
        dla.drain()
    try:
        reply = version_reply(dla)
        print(f"Now: {describe(reply)}.")
        if application_running(reply):
            print("Restarting analyzer into its bootloader ...")
            dla.write(b"\x0a\x87\x01".ljust(PACKET, b"\0"), "prepare restart")
            dla.drain()
            reset_into(dla, bootloader=True)
            reply = version_reply(dla)
            if application_running(reply) or reply[3] != 0x62:
                raise FlashError(f"analyzer did not stay in its bootloader (answer {reply[:9].hex(' ')})")
        if fnirsi_bootloader(reply):
            leave_foreign_bootloader(dla, FlashError(FNIRSI_NEEDS_FPGA_IMAGE))
        time.sleep(PHASE_PAUSE_S)
        send_mcu_image(dla, wire_name, image, "mcu")
        time.sleep(PHASE_PAUSE_S)
        installer = is_installer(image)
        reply = start_written_image(dla, installer, "Starting new firmware ...")
        if reply is None:
            return
        if installer and installer_finished(reply):
            return
        if not application_running(reply):
            raise FlashError("firmware did not start; analyzer waits in its bootloader, run 'mcu' again")
        wait_until_ready(dla, reply)
    finally:
        dla.close()


def wait_until_ready(dla, reply):
    if not own_firmware(reply) or reply[BUSY_AT] == 0:
        return
    print("Waiting for analyzer to finish FPGA set-up ...")
    deadline = time.monotonic() + READY_WAIT_S
    while reply[BUSY_AT] != 0:
        if time.monotonic() >= deadline:
            print(f"  analyzer still busy after {READY_WAIT_S} s; first capture may fail once")
            return
        time.sleep(0.1)
        reply = version_reply(dla)
    print("Analyzer ready.")


_pack = None


def pack_module():
    global _pack
    if _pack is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dla16_pack.py")
        if not os.path.isfile(path):
            raise FlashError("dla16_pack.py must be in same folder as this script")
        spec = importlib.util.spec_from_file_location("dla16_pack", path)
        _pack = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_pack)
    return _pack


def grouped(number):
    return f"{number:,}".replace(",", "'")


def hex_id(flash_id):
    return " ".join(f"{byte:02x}" for byte in flash_id)


def wire_name(prefix, part_name, fallback):
    versions = [part for part in part_name.removesuffix(".bin").split("_") if part.startswith("V")]
    return f"{prefix}_{versions[0]}.bin" if len(versions) == 1 else fallback


def mcu_wire_name(part_name):
    return wire_name("MCU", part_name, RAW_IMAGE_WIRE_NAME)


class FpgaStartRefused(FlashError):
    pass


class FpgaIncomplete(FlashError):
    pass


class FlashAnswer:
    def __init__(self, reply):
        self.ok = reply[2] == 1
        self.crc = struct.unpack_from("<I", reply, 4)[0]
        self.flash_id = bytes(reply[8:11])
        self.count = int.from_bytes(reply[12:15], "little")
        self.state = reply[15]


def flash_query(dla, address, length, crc_in=0):
    packet = b"\x0a\xb2" + struct.pack("<III", address, length, crc_in)
    reply = dla.exchange(packet, "flash check")
    expect(reply, b"\x0a\xb2", "flash check")
    if len(reply) < 16:
        raise FlashError(f"flash check: answer of {len(reply)} bytes is too short")
    answer = FlashAnswer(reply)
    if not answer.ok:
        raise FlashError(f"flash check at {grouped(address)}, {grouped(length)} bytes: analyzer refused")
    return answer


def flash_crc(dla, start, length):
    crc = 0
    for offset in range(0, length, CRC_PIECE):
        crc = flash_query(dla, start + offset, min(CRC_PIECE, length - offset), crc).crc
    return crc


def differing_ranges(dla, start, data):
    ranges = []
    expected = 0
    for offset in range(0, len(data), CRC_PIECE):
        piece = data[offset:offset + CRC_PIECE]
        following = zlib.crc32(piece, expected)
        if flash_query(dla, start + offset, len(piece), expected).crc != following:
            ranges.append((start + offset, start + offset + len(piece) - 1))
        expected = following
    return ranges


def last_sector_end(size):
    return -(-size // SECTOR) * SECTOR


def fpga_image_from_file(path, force):
    pack = pack_module()
    with open(path, "rb") as handle:
        blob = handle.read()
    try:
        parts = pack.parse_container(blob)
    except pack.PackError:
        image, name = blob, os.path.basename(path)
    else:
        image, name = parts[1].data, parts[1].name
    crc = pack.crc32(image)
    model = pack.model_of(image)
    if not FPGA_MIN_BYTES <= len(image) <= FPGA_LIMIT:
        raise FlashError(f"FPGA image of {grouped(len(image))} bytes is not allowed (at most {grouped(FPGA_LIMIT)} "
                         "bytes); nothing written")
    if model is not None and model != ANALYZER_CHANNELS:
        raise FlashError(f"FPGA image is for {model}-channel model, not for DLA-16 ({ANALYZER_CHANNELS} channels); "
                         "nothing written")
    if model is None and not force:
        raise FlashError(f"FPGA image CRC {crc:08x} is not known for DLA-16; nothing written "
                         "(--force writes it anyway)")
    return image, name


def mcu_part_from_container(path):
    pack = pack_module()
    with open(path, "rb") as handle:
        blob = handle.read()
    try:
        part = pack.parse_container(blob)[0]
    except pack.PackError as error:
        raise FlashError(f"{os.path.basename(path)}: {error}") from error
    if not MIN_MCU_BYTES <= part.size <= APP_LIMIT:
        raise FlashError(f"{os.path.basename(path)}: MCU part of {grouped(part.size)} bytes is no firmware image "
                         f"({MIN_MCU_BYTES} to {APP_LIMIT} bytes)")
    return part.data, mcu_wire_name(part.name), part.name


def require_own_bootloader(reply):
    mark = reply[OWN_MARK_AT]
    if fnirsi_bootloader(reply):
        raise FlashError("analyzer now runs FNIRSI's bootloader; nothing more written")
    if mark == FNIRSI_BOOTLOADER_MARK:
        raise FlashError(f"analyzer runs older build of project bootloader without FPGA support (byte 9 of version "
                         f"answer is 0, answer of {len(reply)} bytes): this script writes only through current project "
                         "bootloader or FNIRSI's bootloader; nothing written")
    if mark != OUR_BOOTLOADER_MARK:
        raise FlashError(f"analyzer runs unknown bootloader (byte 9 of version answer is {mark}); nothing written")


def store_selection(dla, bootloader):
    select = b"\x0a\x89" + (b"\x01" if bootloader else b"\x00") + b"\0" * 7
    expect(dla.exchange(select, "start selection", pad=False), b"\x0a\x89\x01", "start selection")


def enter_bootloader(dla):
    print("Restarting analyzer into its bootloader ...")
    dla.write(b"\x0a\x87\x01".ljust(PACKET, b"\0"), "prepare restart")
    dla.drain()
    reset_into(dla, bootloader=True)
    reply = version_reply(dla)
    if application_running(reply) or reply[3] != 0x62:
        raise FlashError(f"analyzer did not stay in its bootloader (answer {reply[:9].hex(' ')})")
    return reply


def answer_verdict(reply, command):
    if reply is None or reply[:2] != bytes([0x0A, command]) or reply[2] not in (0, 1):
        return None
    return reply[2] == 1


def ask_count(dla):
    dla.drain()
    try:
        return flash_query(dla, 0, 0)
    except UsbLost:
        raise
    except FlashError:
        return None


def send_fpga_chunk(dla, number, total, data, accepted, size, resent=False):
    packet = b"\x0a\x86" + struct.pack("<H", len(data)) + data
    verdict = answer_verdict(dla.exchange(packet, f"FPGA chunk {number}"), 0x86)
    if verdict is True:
        return None
    if verdict is False:
        return f"chunk {grouped(number)} of {grouped(total)} refused"
    print(f"  FPGA image: no answer to chunk {grouped(number)} of {grouped(total)}; asking analyzer how many "
          "bytes it holds")
    answer = ask_count(dla)
    if answer is None:
        return f"no answer about chunk {grouped(number)} of {grouped(total)}"
    after = accepted + len(data)
    if answer.count == after and (answer.state == STATE_FPGA or (after == size and answer.state == STATE_NONE)):
        return None
    if answer.count == accepted and answer.state == STATE_FPGA and not resent:
        print(f"  FPGA image: chunk {grouped(number)} did not arrive; sending it again")
        return send_fpga_chunk(dla, number, total, data, accepted, size, resent=True)
    return f"analyzer holds {grouped(answer.count)} bytes after chunk {grouped(number)} of {grouped(total)}"


def fpga_attempt(dla, image, wire_name):
    size = len(image)
    chunks = [image[i:i + FPGA_CHUNK] for i in range(0, size, FPGA_CHUNK)]
    header = b"\x0a\x85" + struct.pack("<I", size) + wire_name.encode("ascii")
    place = "image start"
    try:
        verdict = answer_verdict(dla.exchange(header, "FPGA image start", timeout_ms=START_TIMEOUT_MS), 0x85)
        if verdict is False:
            raise FpgaStartRefused()
        if verdict is None:
            answer = ask_count(dla)
            if answer is None or answer.state != STATE_FPGA or answer.count != 0:
                return "no clear answer to start"
        accepted = 0
        try:
            for number, data in enumerate(chunks, 1):
                place = f"chunk {grouped(number)} of {grouped(len(chunks))}"
                reason = send_fpga_chunk(dla, number, len(chunks), data, accepted, size)
                if reason is not None:
                    return reason
                accepted += len(data)
                if number % FPGA_PROGRESS_CHUNKS == 0 or number == len(chunks):
                    print(f"\r  sent {grouped(number)} of {grouped(len(chunks))} FPGA chunks", end="", flush=True)
        finally:
            print()
    except UsbLost as lost:
        raise lost.at(place) from lost
    return None


def verify_fpga(dla, image):
    size = len(image)
    problems = []
    answer = flash_query(dla, 0, 0)
    if answer.count != size or answer.state != STATE_NONE:
        problems.append(f"analyzer reports {grouped(answer.count)} of {grouped(size)} bytes accepted and image "
                        f"{'closed' if answer.state == STATE_NONE else 'still open'}")
    for first, last in differing_ranges(dla, 0, image):
        problems.append(f"bytes {grouped(first)}-{grouped(last)} read back differ from file")
    for first, last in differing_ranges(dla, size, b"\xff" * (last_sector_end(size) - size)):
        problems.append(f"bytes {grouped(first)}-{grouped(last)} after image are not erased")
    if problems:
        raise FpgaIncomplete("; ".join(problems))


def verify_after_write(dla, image):
    lost_here = False
    for tries in range(1, FPGA_ATTEMPTS + 1):
        try:
            verify_fpga(dla, image)
            return None
        except UsbLost as lost:
            reason = str(lost.at("verification"))
            try:
                reconnect(dla, reason)
            except FlashError as gone:
                raise FpgaIncomplete(f"{reason}; {gone}") from gone
            lost_here = True
            if tries == FPGA_ATTEMPTS:
                raise FpgaIncomplete(f"{reason}, {FPGA_ATTEMPTS} attempts made") from lost
            print("  FPGA image: verifying again")
        except FpgaIncomplete as problem:
            if not lost_here:
                raise
            return str(problem)
    return None


def fpga_failure(dla, image, before_crc, why, command):
    file_crc = zlib.crc32(image)
    consequence = ("Analyzer cannot capture after it is unplugged until FPGA image is written completely. "
                   f"Run '{command}' again.")
    try:
        now = flash_crc(dla, 0, len(image))
    except FlashError:
        return FlashError(f"FPGA image: {why}. Analyzer does not answer, so FPGA image may be incomplete. "
                          f"{consequence}")
    if now == before_crc:
        return FlashError(f"FPGA image: {why}. FPGA image unchanged (CRC {now:08x}). Run '{command}' again.")
    return FlashError(f"FPGA image incomplete: {why} (read back CRC {now:08x}, file {file_crc:08x}). {consequence}")


def write_fpga_image(dla, image, wire_name, force, rewrite, command):
    size, file_crc = len(image), zlib.crc32(image)
    answer = flash_query(dla, 0, 0)
    if answer.flash_id in (bytes(3), b"\xff\xff\xff"):
        raise FlashError("FPGA image: no flash answer; nothing written")
    if answer.flash_id != FLASH_ID and not force:
        raise FlashError(f"FPGA image: flash id {hex_id(answer.flash_id)} is not {hex_id(FLASH_ID)} (PUYA 16 MiB); "
                         "nothing written (--force writes anyway)")
    before_crc = flash_crc(dla, 0, size)
    tail = last_sector_end(size) - size
    if before_crc == file_crc and flash_crc(dla, size, tail) == zlib.crc32(b"\xff" * tail) and not rewrite:
        print(f"FPGA image already in analyzer (CRC {file_crc:08x}); nothing written (--rewrite writes it again).")
        return False
    print(f"Writing FPGA image, {grouped(size)} bytes ...")
    try:
        for attempt in range(1, FPGA_ATTEMPTS + 1):
            reported = False
            try:
                reason = fpga_attempt(dla, image, wire_name)
            except FpgaStartRefused:
                if attempt == 1:
                    raise
                reason = "analyzer refused start"
            except UsbLost as lost:
                reason = str(lost)
                try:
                    reconnect(dla, reason)
                except FlashError as gone:
                    raise FpgaIncomplete(f"{reason}; {gone}; {attempt} of {FPGA_ATTEMPTS} attempts made") from gone
                reported = True
            if reason is None:
                reason = verify_after_write(dla, image)
                if reason is None:
                    break
            if attempt == FPGA_ATTEMPTS:
                raise FpgaIncomplete(f"{reason}, {FPGA_ATTEMPTS} attempts made")
            if reported:
                print(f"  FPGA image: starting again ({attempt + 1} of {FPGA_ATTEMPTS})")
            else:
                print(f"  FPGA image: {reason}; starting again ({attempt + 1} of {FPGA_ATTEMPTS})")
    except FpgaStartRefused:
        raise FlashError("FPGA image: analyzer refused start; nothing written") from None
    except FlashError as error:
        raise fpga_failure(dla, image, before_crc, str(error), command) from error
    print(f"FPGA image written and verified ({grouped(size)} bytes, CRC {file_crc:08x}). "
          "Unplug analyzer and plug it in again to load it.")
    return True


def send_mcu_part(dla, mcu, command):
    image, wire_name, name = mcu
    print(f"Writing MCU part {name}, {grouped(len(image))} bytes ...")
    send_mcu_image(dla, wire_name, image, command)
    time.sleep(PHASE_PAUSE_S)


def start_application(dla):
    print("Starting firmware ...")
    reset_into(dla, bootloader=False)
    reply = version_reply(dla)
    if application_running(reply):
        wait_until_ready(dla, reply)
    return reply


def leave_foreign_bootloader(dla, error):
    try:
        reset_into(dla, bootloader=False)
    except FlashError:
        pass
    raise error


FNIRSI_NEEDS_FPGA_IMAGE = ("analyzer runs FNIRSI's bootloader, which keeps new firmware only together with complete "
                           "FPGA image: put firmware into FNIRSI's firmware file with dla16_pack.py and install that "
                           "file with 'full'; nothing written")
FNIRSI_NEEDS_FIRMWARE = ("analyzer runs FNIRSI's bootloader, which takes FPGA image only together with firmware: "
                         "install firmware file that holds both parts with 'full'; nothing written")


class FnirsiRestart(FlashError):
    pass


class FnirsiProgress:
    def __init__(self):
        self.fpga_touched = False


def fnirsi_check(reply, what, command, timeout_ms):
    if reply is None:
        raise FnirsiRestart(f"{what}: no answer in {timeout_ms // 1000} s")
    if reply[:3] != bytes([0x0A, command, 1]):
        raise FnirsiRestart(f"{what}: unexpected answer {reply[:16].hex(' ')}")


def fnirsi_send(dla, label, start, chunk_command, image, wire_name, start_timeout_ms, progress=None):
    chunks = [image[i:i + CHUNK] for i in range(0, len(image), CHUNK)]
    every = FPGA_PROGRESS_CHUNKS if progress is not None else 20
    header = bytes([0x0A, start]) + struct.pack("<I", len(image)) + wire_name.encode("ascii")
    place = f"{label} image start"
    try:
        touched_before = progress is not None and progress.fpga_touched
        if progress is not None:
            progress.fpga_touched = True
        reply = dla.exchange(header, place, timeout_ms=start_timeout_ms)
        if reply is None and progress is not None:
            progress.fpga_touched = touched_before
        fnirsi_check(reply, place, start, start_timeout_ms)
        for number, data in enumerate(chunks, 1):
            place = f"{label} chunk {grouped(number)} of {grouped(len(chunks))}"
            packet = bytes([0x0A, chunk_command]) + struct.pack("<H", len(data)) + data
            fnirsi_check(dla.exchange(packet, place), place, chunk_command, REPLY_TIMEOUT_MS)
            if number % every == 0 or number == len(chunks):
                print(f"\r  sent {grouped(number)} of {grouped(len(chunks))} {label} chunks", end="", flush=True)
    except UsbLost as lost:
        raise lost.at(place) from lost
    finally:
        print()


def other_bootloader(reply):
    return FlashError(f"analyzer now runs other bootloader (byte 9 of version answer is {reply[OWN_MARK_AT]}, answer "
                      f"of {len(reply)} bytes); nothing more written")


def fnirsi_attempt(dla, mcu, fpga, in_bootloader, progress):
    if not in_bootloader:
        dla.drain()
        reply = version_reply(dla)
        if not application_running(reply):
            if not fnirsi_bootloader(reply):
                raise other_bootloader(reply)
            print("Starting firmware first: FNIRSI's bootloader takes FPGA image only after firmware has run since "
                  "power-up.")
            reset_into(dla, bootloader=False)
            if not application_running(version_reply(dla)):
                raise FlashError("analyzer did not start its firmware from FNIRSI's bootloader")
        reply = enter_bootloader(dla)
        if not fnirsi_bootloader(reply):
            raise other_bootloader(reply)
    time.sleep(PHASE_PAUSE_S)
    fnirsi_send(dla, "MCU", 0x82, 0x83, mcu[0], mcu[1], REPLY_TIMEOUT_MS)
    time.sleep(PHASE_PAUSE_S)
    fnirsi_send(dla, "FPGA", 0x85, 0x86, fpga[0], wire_name("FPGA", fpga[1], RAW_FPGA_WIRE_NAME), START_TIMEOUT_MS,
                progress)


def start_firmware_after_failure(dla):
    try:
        dla.drain()
        reply = version_reply(dla)
        if not application_running(reply):
            reset_into(dla, bootloader=False)
            reply = version_reply(dla)
        return application_running(reply)
    except FlashError:
        return False


def fnirsi_failure(dla, progress, why, command):
    answers = start_firmware_after_failure(dla)
    if progress.fpga_touched:
        head = f"FPGA image incomplete: {why}. Analyzer cannot capture until FPGA image is written completely."
    else:
        head = f"{why}. FPGA image and firmware unchanged."
    if answers:
        return FlashError(f"{head} Run '{command}' again.")
    return FlashError(f"{head} Analyzer does not answer: unplug it, plug it in again and run '{command}' again.")


def update_through_fnirsi_bootloader(dla, mcu, fpga, command, entered_from_firmware):
    if mcu is None or fpga is None:
        leave_foreign_bootloader(dla, FlashError(FNIRSI_NEEDS_FIRMWARE))
    print(f"FNIRSI's bootloader: writing MCU part and FPGA part in one transfer, at most {FNIRSI_ATTEMPTS} attempts.")
    progress = FnirsiProgress()
    in_bootloader = entered_from_firmware
    for attempt in range(1, FNIRSI_ATTEMPTS + 1):
        reported = False
        try:
            fnirsi_attempt(dla, mcu, fpga, in_bootloader, progress)
            break
        except (FnirsiRestart, AnswerMismatch) as restart:
            reason = str(restart)
        except UsbLost as lost:
            reason = str(lost)
            try:
                wait_for_analyzer(dla, reason)
            except FlashError as gone:
                raise fnirsi_failure(dla, progress, f"{reason}; {gone}", command) from gone
            reported = True
        except FlashError as error:
            raise fnirsi_failure(dla, progress, str(error), command) from error
        in_bootloader = False
        if attempt == FNIRSI_ATTEMPTS:
            raise fnirsi_failure(dla, progress, f"{reason}, {FNIRSI_ATTEMPTS} attempts made", command)
        print(f"  {'' if reported else reason + '; '}starting again from firmware ({attempt + 1} of {FNIRSI_ATTEMPTS})")
    time.sleep(PHASE_PAUSE_S)
    print("Starting firmware ...")
    try:
        reset_into(dla, bootloader=False)
        reply = version_reply(dla)
    except FlashError as error:
        if not driver_switched(error):
            raise FlashError(f"firmware and FPGA image written, but analyzer did not start again ({error}): unplug "
                             "it and plug it in again") from error
        print(DRIVER_SWITCHED)
        print(f"FPGA image written ({grouped(len(fpga[0]))} bytes, CRC {zlib.crc32(fpga[0]):08x}). FNIRSI's "
              "bootloader cannot read it back, so it is not verified.")
        print("Unplug analyzer and plug it in again to load FPGA image.")
        return
    print(f"Now: {describe(reply)}.")
    if is_installer(mcu[0]) and waits_in_own_bootloader(reply):
        print(f"FPGA image written ({grouped(len(fpga[0]))} bytes, CRC {zlib.crc32(fpga[0]):08x}). FNIRSI's "
              "bootloader cannot read it back, so it is not verified.")
        print("Unplug analyzer and plug it in again to load FPGA image.")
        print(BOOTLOADER_INSTALLED)
        return
    if not application_running(reply):
        raise FlashError("firmware and FPGA image written, but analyzer stayed in its bootloader: unplug it and plug "
                         "it in again")
    if own_firmware(reply) != (OWN_MARK in mcu[0]):
        raise FlashError(f"FPGA image written, but analyzer still runs {describe(reply).removesuffix(' running')}: "
                         f"FNIRSI's bootloader did not put new firmware in place. Run '{command}' again.")
    wait_until_ready(dla, reply)
    print(f"FPGA image written ({grouped(len(fpga[0]))} bytes, CRC {zlib.crc32(fpga[0]):08x}). FNIRSI's bootloader "
          "cannot read it back, so it is not verified.")
    print("Unplug analyzer and plug it in again to load FPGA image.")


def update_through_bootloader(dla, mcu, fpga, force, rewrite, command):
    reply = version_reply(dla)
    print(f"Now: {describe(reply)}.")
    mcu_written = False
    had_firmware = application_running(reply)
    if had_firmware:
        reply = enter_bootloader(dla)
        if fnirsi_bootloader(reply):
            update_through_fnirsi_bootloader(dla, mcu, fpga, command, entered_from_firmware=True)
            return
        try:
            require_own_bootloader(reply)
        except FlashError as error:
            leave_foreign_bootloader(dla, error)
    elif fnirsi_bootloader(reply):
        update_through_fnirsi_bootloader(dla, mcu, fpga, command, entered_from_firmware=False)
        return
    else:
        require_own_bootloader(reply)
        print("FPGA image is written after firmware has run since power-up: starting it once.")
        reply = start_application(dla)
        if application_running(reply):
            had_firmware = True
            reply = enter_bootloader(dla)
        elif mcu is not None:
            print("No firmware installed: writing MCU part first.")
            store_selection(dla, bootloader=True)
            send_mcu_part(dla, mcu, command)
            mcu_written = True
            had_firmware = True
            reply = start_application(dla)
            if not application_running(reply):
                if is_installer(mcu[0]) and waits_in_own_bootloader(reply):
                    raise FlashError("bootloader installed, but FPGA image is not written yet: install firmware "
                                     f"with 'mcu', then run '{command}' again")
                raise FlashError(f"firmware did not start; analyzer waits in its bootloader, run '{command}' again")
            reply = enter_bootloader(dla)
    store_selection(dla, bootloader=True)
    time.sleep(PHASE_PAUSE_S)
    if mcu is not None and not mcu_written:
        send_mcu_part(dla, mcu, command)
    if fpga is not None:
        write_fpga_image(dla, fpga[0], fpga[1], force, rewrite, command)
    installer = mcu is not None and is_installer(mcu[0])
    reply = start_written_image(dla, installer, "Starting firmware ...")
    if reply is None:
        return
    if installer and installer_finished(reply):
        return
    if not application_running(reply):
        if had_firmware:
            raise FlashError(f"firmware did not start; analyzer waits in its bootloader, run '{command}' again")
        print("No firmware installed: analyzer waits in its bootloader. Install MCU firmware with 'mcu'.")
        return
    wait_until_ready(dla, reply)


def open_analyzer(dla):
    if dla is None:
        dla = Dla()
        dla.open()
    dla.drain()
    return dla


def install_full(path, force=False, rewrite=False, dla=None):
    mcu = mcu_part_from_container(path)
    fpga = fpga_image_from_file(path, force)
    print(f"{os.path.basename(path)}: MCU part {mcu[2]}, FPGA part {fpga[1]}, "
          f"FPGA image CRC {zlib.crc32(fpga[0]):08x}.")
    dla = open_analyzer(dla)
    try:
        update_through_bootloader(dla, mcu, fpga, force, rewrite, "full")
    finally:
        dla.close()


def install_fpga(path, force=False, rewrite=False, dla=None):
    fpga = fpga_image_from_file(path, force)
    dla = open_analyzer(dla)
    try:
        update_through_bootloader(dla, None, fpga, force, rewrite, "fpga")
    finally:
        dla.close()


def isp_request(command, data=b""):
    if len(data) + 3 > 64:
        raise FlashError(f"chip bootloader request 0x{command:02x}: {len(data)} bytes too long")
    return bytes([command]) + struct.pack("<H", len(data)) + bytes(data)


def isp_reply_data(command, raw):
    if len(raw) < 4 or raw[0] != command:
        raise FlashError(f"chip bootloader: odd answer to 0x{command:02x}: {bytes(raw[:8]).hex(' ')}")
    if raw[1] != 0:
        raise FlashError(f"chip bootloader refused 0x{command:02x}: error 0x{raw[1]:02x}")
    length = struct.unpack_from("<H", raw, 2)[0]
    if length != len(raw) - 4:
        raise FlashError(f"chip bootloader: answer to 0x{command:02x} says {length} bytes, {len(raw) - 4} came")
    return bytes(raw[4:])


def isp_xor_key(uid, chip_id):
    total = sum(uid) & 0xFF
    key = [total] * 8
    key[7] = (key[0] + chip_id) & 0xFF
    return bytes(key)


def isp_padded(image):
    return image + b"\xff" * (-len(image) % 64)


def isp_data_requests(command, address, image, key):
    packets = []
    for offset in range(0, len(image), ISP_CHUNK):
        part = image[offset:offset + ISP_CHUNK]
        coded = bytes(b ^ key[i % 8] for i, b in enumerate(part))
        packets.append(isp_request(command, struct.pack("<I", address + offset) + b"\0" + coded))
    return packets


def isp_program_requests(address, image, key):
    packets = isp_data_requests(ISP_PROGRAM, address, image, key)
    packets.append(isp_request(ISP_PROGRAM, struct.pack("<I", address + len(image)) + b"\0"))
    return packets


def isp_verify_requests(address, image, key):
    return isp_data_requests(ISP_VERIFY, address, image, key)


class ChipBootloader:
    def __init__(self):
        self.device = None
        self.key = None

    def open(self):
        deadline = time.monotonic() + ISP_WAIT_S
        while True:
            device = find(ISP_VID, ISP_PID)
            if device is not None:
                break
            if time.monotonic() >= deadline:
                raise FlashError(f"WCH chip bootloader ({ISP_VID:04x}:{ISP_PID:04x}) did not appear "
                                 f"within {ISP_WAIT_S} s")
            time.sleep(0.05)
        claim(device, ISP_VID, ISP_PID)
        self.device = device

    def close(self):
        if self.device is not None:
            try:
                usb.util.release_interface(self.device, 0)
            except usb.core.USBError:
                pass
            usb.util.dispose_resources(self.device)
            self.device = None

    def exchange(self, packet):
        try:
            self.device.write(ISP_OUT, packet, ISP_TIMEOUT_MS)
            raw = self.device.read(ISP_IN, 64, ISP_TIMEOUT_MS)
        except usb.core.USBError as error:
            raise FlashError(f"chip bootloader stopped answering ({error}); it gives up about 10 s after "
                             "plug-in, start again") from error
        return isp_reply_data(packet[0], raw)

    def start_session(self):
        ids = self.exchange(isp_request(ISP_IDENTIFY, bytes([0, 0]) + ISP_IDENTIFY_TEXT))
        if (ids[0], ids[1]) != (CH569_ID, CH569_TYPE):
            raise FlashError(f"chip 0x{ids[1]:02x}{ids[0]:02x} is no CH569: this is not DLA-16's MCU")
        uid = self.exchange(isp_request(ISP_READ_CONFIG, struct.pack("<H", ISP_CONFIG_UID)))[2:10]
        self.exchange(isp_request(ISP_READ_CONFIG, struct.pack("<H", ISP_CONFIG_BOOTLOADER_VERSION)))
        answer = self.exchange(isp_request(ISP_KEY, bytes(30)))
        if answer[0] != 0:
            raise FlashError(f"chip bootloader answered session key with 0x{answer[0]:02x}; version not supported")
        self.key = isp_xor_key(uid, ids[0])

    def erase(self, count):
        self.exchange(isp_request(ISP_ERASE, struct.pack("<I", count)))

    def program(self, address, image):
        for packet in isp_program_requests(address, isp_padded(image), self.key):
            answer = self.exchange(packet)
            if answer[:2] != b"\0\0":
                raise FlashError(f"chip bootloader did not write at 0x{struct.unpack_from('<I', packet, 3)[0]:x}")

    def verify(self, address, image):
        for packet in isp_verify_requests(address, isp_padded(image), self.key):
            if self.exchange(packet)[:2] != b"\0\0":
                return False
        return True

    def end(self):
        try:
            self.device.write(ISP_OUT, isp_request(ISP_END, b"\x01"), ISP_TIMEOUT_MS)
        except usb.core.USBError as error:
            raise FlashError(f"leaving chip bootloader: {error}") from error


def bootloader_image(path):
    image = open(path, "rb").read()
    if not 256 <= len(image) <= BOOTLOADER_LIMIT:
        raise FlashError(f"{path}: {len(image)} bytes is no bootloader image (at most {BOOTLOADER_LIMIT} bytes)")
    if BOOTLOADER_ERASE_COUNT * ERASE_UNIT < len(isp_padded(image)):
        raise FlashError(f"{path}: does not fit erased area")
    return image


def install_bootloader(boot_path, firmware_path):
    image = bootloader_image(boot_path)
    firmware_image(firmware_path)
    print(f"Project bootloader {os.path.basename(boot_path)}, {len(image)} bytes, then firmware "
          f"{os.path.basename(firmware_path)}.")
    print()
    print("1. Unplug USB cable from analyzer.")
    print("2. With back cover off, bridge two pads of SW4 (for example with tweezers) and keep them bridged.")
    print("3. Plug USB cable in while pads are bridged. Release them once this script says 'found'.")
    print(f"Waiting up to {ISP_WAIT_S} s for WCH chip bootloader ...")
    chip = ChipBootloader()
    try:
        chip.open()
        print("Chip bootloader found: pads may be released now.")
        chip.start_session()
        print("Erasing start of flash (0x0000-0x1FFF; settings and calibration data stay) ...")
        chip.erase(BOOTLOADER_ERASE_COUNT)
        print("Writing bootloader ...")
        chip.program(0, image)
        if not chip.verify(0, image):
            raise FlashError("bootloader read back different from file: run 'bootloader' again before unplugging")
        print("Bootloader written and verified. Starting it ...")
        chip.end()
    finally:
        chip.close()
    time.sleep(2.0)
    print("Waiting for bootloader on USB ...")
    dla = Dla()
    try:
        dla.open(wait_s=30)
    except FlashError as error:
        raise FlashError(f"{error} Bootloader is written; once analyzer is visible, run "
                         f"'python {os.path.basename(__file__)} firmware {firmware_path}'.") from error
    dla.drain()
    install_firmware(firmware_path, dla)


def show_status():
    dla = Dla()
    dla.open(wait_s=3)
    try:
        dla.drain()
        reply = version_reply(dla)
        print(f"DLA-16: {describe(reply)}.")
    finally:
        dla.close()


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help", "help"):
        print(__doc__)
        return 0 if len(argv) >= 2 else 1
    command = argv[1]
    files = [argument for argument in argv[2:] if not argument.startswith("--")]
    options = [argument for argument in argv[2:] if argument.startswith("--")]
    flags = {"force": "--force" in options, "rewrite": "--rewrite" in options}
    update_options_known = set(options) <= {"--force", "--rewrite"}
    try:
        if command == "status" and not files and not options:
            show_status()
        elif command == "mcu" and len(files) <= 1 and not options:
            if not files:
                print(__doc__)
                print(f"'mcu' needs MCU firmware file: python {os.path.basename(__file__)} mcu FILE")
                return 1
            install_firmware(files[0])
        elif command == "bootloader" and len(files) <= 2 and not options:
            if len(files) < 2:
                print(__doc__)
                print(f"'bootloader' needs bootloader file and firmware file: "
                      f"python {os.path.basename(__file__)} bootloader BOOTLOADER.bin FIRMWARE.bin")
                return 1
            install_bootloader(files[0], files[1])
        elif command == "full" and len(files) <= 1 and update_options_known:
            if not files:
                print(__doc__)
                print(f"'full' needs firmware file: python {os.path.basename(__file__)} full FILE")
                return 1
            install_full(files[0], **flags)
        elif command == "fpga" and len(files) == 1 and update_options_known:
            install_fpga(files[0], **flags)
        else:
            print(__doc__)
            return 1
    except FileNotFoundError as error:
        print(f"File not found: {error.filename}. Build it first (see README) or name another file.")
        return 1
    except FlashError as error:
        print(f"STOPPED: {error}")
        return 1
    except KeyboardInterrupt:
        print("\nStopped by user.")
        return 1
    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
