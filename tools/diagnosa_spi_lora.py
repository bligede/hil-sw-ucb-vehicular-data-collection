"""Read SX127x registers via temporary ESP32 GPIO access through its ROM loader.

The gateway restarts before and after the probe. No firmware is flashed.
Requires the esptool 3.0 package already supplied by this PlatformIO setup.
"""

import argparse
import json
from pathlib import Path
import re
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--esptool-path", required=True)
    args = parser.parse_args()

    sys.path.insert(0, args.esptool_path)
    import esptool

    pin_map = Path(__file__).resolve().parents[1] / "gateway/src/config/pin_map.h"
    content = pin_map.read_text(encoding="utf-8")
    pins = {}
    for name in ("NSS", "RESET", "SCK", "MISO", "MOSI"):
        match = re.search(r"^\s*#define\s+LORA_" + name + r"\s+(\d+)\b", content, re.M)
        if not match:
            raise RuntimeError("Missing pin definition: " + name)
        pins[name] = int(match.group(1))
    expected = {"NSS": 5, "RESET": 14, "SCK": 18, "MISO": 19, "MOSI": 23}
    if pins != expected:
        raise RuntimeError("Pin mapping differs from the verified ESP32 register layout")

    # Verified against this toolchain's ESP32 soc/gpio_reg.h and io_mux_reg.h.
    gpio_base = 0x3FF44000
    mux_base = 0x3FF49000
    mux_offsets = {5: 0x6C, 14: 0x30, 18: 0x70, 19: 0x74, 23: 0x8C}
    out_set = gpio_base + 0x08
    out_clear = gpio_base + 0x0C
    enable_set = gpio_base + 0x24
    enable_clear = gpio_base + 0x28
    gpio_input = gpio_base + 0x3C

    esp = None
    try:
        esp = esptool.ESP32ROM(args.port, baud=115200)
        esp.connect(mode="default_reset", attempts=2)
        print("Chip: " + esp.get_chip_description(), flush=True)

        def level(pin, high):
            esp.write_reg(out_set if high else out_clear, 1 << pin)

        # Set idle levels before enabling outputs; leave UART and flash pins alone.
        esp.write_reg(enable_clear, sum(1 << p for p in pins.values()))
        level(pins["NSS"], True)
        level(pins["RESET"], True)
        level(pins["SCK"], False)
        level(pins["MOSI"], False)
        for pin, offset in mux_offsets.items():
            address = mux_base + offset
            value = esp.read_reg(address)
            value = (value & ~(0x7 << 12)) | (2 << 12) | (1 << 9)
            esp.write_reg(address, value)
            if pin != pins["MISO"]:
                # GPIO output signal 256 and output enable from GPIO_ENABLE.
                esp.write_reg(gpio_base + 0x530 + pin * 4, 256 | (1 << 10))
                pin_register = gpio_base + 0x88 + pin * 4
                esp.write_reg(pin_register, esp.read_reg(pin_register) & ~(1 << 2))
        outputs = [pins[name] for name in ("NSS", "RESET", "SCK", "MOSI")]
        esp.write_reg(enable_set, sum(1 << p for p in outputs))

        level(pins["RESET"], False)
        output_checks = {}
        for name in ("NSS", "RESET", "SCK", "MOSI"):
            pin = pins[name]
            readings = []
            for high in (False, True):
                level(pin, high)
                readings.append((esp.read_reg(gpio_input) >> pin) & 1)
            output_checks[name] = readings
            level(pin, name == "NSS")
        print("GPIO low/high readback: " + json.dumps(output_checks), flush=True)
        if any(readings != [0, 1] for readings in output_checks.values()):
            raise RuntimeError("An SPI output pin did not follow its requested GPIO level")
        time.sleep(0.02)
        level(pins["RESET"], True)
        time.sleep(0.02)

        def transfer(byte):
            received = 0
            for shift in range(7, -1, -1):
                level(pins["MOSI"], bool(byte & (1 << shift)))
                level(pins["SCK"], True)
                bit = (esp.read_reg(gpio_input) >> pins["MISO"]) & 1
                received = (received << 1) | bit
                level(pins["SCK"], False)
            return received

        def read_radio(address):
            level(pins["NSS"], False)
            try:
                transfer(address & 0x7F)
                return transfer(0)
            finally:
                level(pins["NSS"], True)

        result = {"port": args.port, "pins": pins, "expected_version": "0x12", "output_checks": output_checks}
        miso_mux = mux_base + mux_offsets[pins["MISO"]]
        original_mux = esp.read_reg(miso_mux)
        for mode, pull in (("none", 0), ("up", 1 << 8), ("down", 1 << 7)):
            esp.write_reg(miso_mux, (original_mux & ~((1 << 8) | (1 << 7))) | pull)
            versions = [read_radio(0x42) for _ in range(2)]
            result["version_pull_" + mode] = [f"0x{v:02X}" for v in versions]
            print("Version with MISO pull " + mode + ": " + str(result["version_pull_" + mode]), flush=True)
        print("RESULT " + json.dumps(result), flush=True)
    finally:
        if esp is not None:
            try:
                esp._port.setDTR(False)
                esp.hard_reset()
                print("Device on " + args.port + " reset to run its installed firmware.", flush=True)
            finally:
                esp._port.close()


if __name__ == "__main__":
    main()
