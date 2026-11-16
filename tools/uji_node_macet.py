# Disalin 16 Nov 2026 dari salinan kerja yang dijalankan 1 Nov 2026 sekitar 22:48 untuk rekaman
# data/tahap43/uji_node_macet_01_* (isi tidak diubah). Reset gateway di rekaman itu (POWERON_RESET)
# berasal dari langkah g.rts = True di bawah: EN gateway ditahan rendah 30 s dengan sengaja.

"""Uji: node berhenti memancar setelah gateway mati sementara di tengah window?

Merekam node (USB) dan gateway, menunggu node aktif, menahan EN gateway rendah
selama 30 s lewat RTS (seperti gateway diunggah ulang), lalu merekam 5 menit lagi.
"""
import sys
import threading
import time

sys.path.insert(0, "D:/Wahyu/TESIS/firmware/tools")
from rekam import buka_tanpa_reset, rekam, KEPALA  # noqa: E402

AWALAN = "D:/Wahyu/TESIS/firmware/data/tahap43/uji_node_macet_01"
g = buka_tanpa_reset("COM11")
n = buka_tanpa_reset("COM5")
henti = threading.Event()
cat = []
tg = threading.Thread(target=rekam, args=(g, AWALAN + "_gateway.csv", KEPALA, henti, cat), daemon=True)
tn = threading.Thread(target=rekam, args=(n, AWALAN + "_node.txt", "", henti, cat), daemon=True)
tg.start()
tn.start()
t0 = time.time()

def jumlah(pola):
    try:
        return open(AWALAN + "_gateway.csv", encoding="utf-8", errors="replace").read().count(pola)
    except OSError:
        return 0

# tunggu node aktif: minimal 15 paket baru, paling lama 6 menit (node bisa sedang cooldown)
while jumlah("\nDATA,") < 15 and time.time() - t0 < 360:
    time.sleep(1)
print("paket sebelum gateway dimatikan:", jumlah("\nDATA,"), "pada %.0f s" % (time.time() - t0))

g.rts = True                     # EN gateway rendah: gateway mati
print("gateway dimatikan pada %.0f s" % (time.time() - t0))
time.sleep(30)
g.rts = False                    # gateway menyala lagi
t_nyala = time.time()
print("gateway dinyalakan pada %.0f s" % (t_nyala - t0))
sebelum = jumlah("\nDATA,")
time.sleep(300)
print("paket dalam 5 menit sesudah gateway menyala:", jumlah("\nDATA,") - sebelum)
henti.set()
time.sleep(1)
g.close()
n.close()
