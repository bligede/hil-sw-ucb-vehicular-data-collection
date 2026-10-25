"""Uji empat warna biled melalui firmware gateway; tidak melakukan flashing."""
import argparse
import json
import time
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM11")
    parser.add_argument("--detik", type=int, default=40)
    args = parser.parse_args()
    if not 4 <= args.detik <= 60:
        parser.error("--detik harus 4..60")

    dev = serial.Serial()
    dev.port = args.port
    dev.baudrate = 115200
    dev.timeout = 0.4
    dev.write_timeout = 1
    dev.dtr = False
    dev.rts = False
    events = []
    samples = []
    carry = ""
    data = ack = 0
    output = Path(__file__).resolve().parents[1] / "gateway/.pio/uji_biled_GPIO.json"
    output.parent.mkdir(parents=True, exist_ok=True)

    def baca():
        nonlocal carry, data, ack
        carry += dev.read(4096).decode("utf-8", "replace")
        lines = carry.split("\n")
        carry = lines.pop()
        useful = []
        for raw in lines:
            line = raw.strip()
            if line.startswith("DATA,"):
                data += 1
            elif line.startswith("ACK,"):
                ack += 1
            elif line.startswith(("# biled:", "# uji biled", "# id_window=",
                                  "# sheets:", "# gateway-", "FATAL:", "rst:")):
                events.append(line)
                useful.append(line)
        return useful

    try:
        dev.open()
        dev.reset_input_buffer()
        dev.write(f"L {args.detik}\n".encode("ascii"))
        mulai = time.monotonic()
        reply = baca()
        mode_ok = any(f"# uji biled {args.detik * 1000} ms" in line for line in reply)
        print(f"Perintah L {args.detik}: {'diterima' if mode_ok else 'belum terkonfirmasi'}",
              flush=True)
        if not mode_ok:
            raise RuntimeError("Firmware belum mengonfirmasi mode uji; tidak menyimpulkan hasil LED.")

        quarter = args.detik / 4
        expected = [
            "LED1 merah / LED2 mati",
            "LED1 hijau / LED2 mati",
            "LED1 mati / LED2 merah",
            "LED1 mati / LED2 hijau",
        ]
        for i, label in enumerate(expected):
            deadline = mulai + i * quarter + min(1.0, quarter / 3)
            while time.monotonic() < deadline:
                time.sleep(0.05)
            dev.write(b"S\n")
            lines = baca()
            status = next((line for line in lines if line.startswith("# biled:")), "")
            sample = {"fase": i + 1, "harapan": label, "status": status}
            samples.append(sample)
            print(f"Fase {i + 1}: {label} | {status or 'tidak ada respons S'}", flush=True)

        while time.monotonic() - mulai < args.detik + 1:
            time.sleep(0.05)
        dev.write(b"L 0\nS\n")
        baca()
        print("Uji selesai; LED kembali mengikuti status gateway.", flush=True)
    finally:
        # Simpan bukti sebelum menutup port. Pembatalan I/O sebelum close
        # menghindari penutupan pySerial Windows yang tertahan oleh driver.
        output.write_text(json.dumps({
            "port": args.port, "durasi_s": args.detik,
            "fase": samples, "DATA_diamati": data, "ACK_diamati": ack,
            "diagnostik": events,
        }, ensure_ascii=False, indent=2), encoding="utf-8")
        if dev.is_open:
            dev.cancel_read()
            dev.cancel_write()
            dev.close()
        print(f"Bukti tersimpan: {output}", flush=True)


if __name__ == "__main__":
    main()
