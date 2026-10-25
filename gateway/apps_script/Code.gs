/**
 * Penerima data gateway LoRa untuk Google Sheets.
 *
 * Skrip ini harus dibuat DARI DALAM spreadsheet tujuan (Extensions > Apps Script),
 * supaya SpreadsheetApp.getActiveSpreadsheet() menunjuk ke spreadsheet itu.
 * Langkah pemasangan lengkap ada di README.md pada folder ini.
 *
 * Dua lembar dipakai:
 *   LOG      satu baris per kejadian DATA atau ACK, kolomnya sama dengan CSV serial
 *            gateway (jenis, ts_ms, id_window, seq, arm, e_prev_mJ, vbatt, rssi, snr,
 *            ax, ay, az) ditambah epoch_s, boot, batch, buang. Unduh sebagai CSV
 *            lalu olah dengan tools/olah_siklus.py.
 *   KONTROL  B1 = id_window yang dibaca gateway; B3 sampai B5 = status penerimaan.
 */

var NAMA_LOG = 'LOG';
var NAMA_KONTROL = 'KONTROL';
var KOLOM = ['jenis', 'ts_ms', 'id_window', 'seq', 'arm', 'e_prev_mJ', 'vbatt', 'rssi',
             'snr', 'ax', 'ay', 'az', 'epoch_s', 'boot', 'batch', 'buang'];
var BATCH_DIINGAT = 300;   // ID paket yang diingat untuk menolak kiriman ganda

/** Jalankan SEKALI dari editor setelah menempel skrip ini: membuat kedua lembar. */
function siapkan() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var log = ss.getSheetByName(NAMA_LOG) || ss.insertSheet(NAMA_LOG);
  if (log.getLastRow() === 0) {
    log.appendRow(KOLOM);
    log.setFrozenRows(1);
  }
  log.getRange('C:C').setNumberFormat('@');   // id_window tetap teks, mis. "001" tidak jadi 1
  var kon = ss.getSheetByName(NAMA_KONTROL) || ss.insertSheet(NAMA_KONTROL);
  kon.getRange('A1').setValue('id_window (ubah sebelum tiap lintasan)');
  kon.getRange('A3').setValue('data terakhir diterima');
  kon.getRange('A4').setValue('jumlah baris di LOG');
  kon.getRange('A5').setValue('baris dibuang gateway (kumulatif sejak boot)');
  kon.getRange('B1').setNumberFormat('@');   // teks, agar ID seperti 001 tidak jadi angka
  kon.setColumnWidth(1, 320);
  kon.setColumnWidth(2, 240);
}

function jawab(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}

function tokenSah(t) {
  var benar = PropertiesService.getScriptProperties().getProperty('TOKEN');
  return !!benar && t === benar;
}

/** Gateway mengirim satu paket baris. */
function doPost(e) {
  var kunci = LockService.getScriptLock();
  if (!kunci.tryLock(20000)) return jawab({ ok: false, error: 'sibuk' });
  try {
    var isi;
    try {
      isi = JSON.parse(e.postData.contents);
    } catch (x) {
      return jawab({ ok: false, error: 'json' });
    }
    if (!tokenSah(isi.t)) return jawab({ ok: false, error: 'token' });
    if (!isi.b || !isi.r || !isi.r.length) return jawab({ ok: false, error: 'kosong' });

    var props = PropertiesService.getScriptProperties();
    var diingat = JSON.parse(props.getProperty('BATCH') || '[]');
    if (diingat.indexOf(isi.b) >= 0) {
      // Paket ini sudah masuk; balasan sebelumnya hilang di jalan.
      return jawab({ ok: true, ulang: true, n: isi.r.length });
    }

    var ss = SpreadsheetApp.getActiveSpreadsheet();
    var log = ss.getSheetByName(NAMA_LOG);
    if (!log) return jawab({ ok: false, error: 'lembar_log_belum_ada' });

    // Urutan di kiriman gateway: jenis, ts_ms, epoch, id, seq, arm, e, vbatt, rssi, snr, ax, ay, az
    var baris = isi.r.map(function (r) {
      return [r[0], r[1], r[3], r[4], nol(r[5]), nol(r[6]), nol(r[7]), nol(r[8]), nol(r[9]),
              nol(r[10]), nol(r[11]), nol(r[12]), r[2] || '', isi.boot, isi.b, isi.buang];
    });
    var awal = log.getLastRow() + 1;
    log.getRange(awal, 1, baris.length, KOLOM.length).setValues(baris);

    diingat.push(isi.b);
    if (diingat.length > BATCH_DIINGAT) diingat = diingat.slice(diingat.length - BATCH_DIINGAT);
    props.setProperty('BATCH', JSON.stringify(diingat));

    var kon = ss.getSheetByName(NAMA_KONTROL);
    if (kon) {
      kon.getRange('B3').setValue(new Date());
      kon.getRange('B4').setValue(log.getLastRow() - 1);
      kon.getRange('B5').setValue(isi.buang);
    }
    return jawab({ ok: true, n: baris.length });
  } catch (error) {
    return jawab({ ok: false, error: String(error) });
  } finally {
    kunci.releaseLock();
  }
}

/** Gateway menanyakan id_window yang sedang berlaku. */
function doGet(e) {
  var p = (e && e.parameter) || {};
  if (!tokenSah(p.t)) return jawab({ ok: false, error: 'token' });
  if (p.aksi === 'window') {
    var kon = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(NAMA_KONTROL);
    var id = kon ? String(kon.getRange('B1').getValue()).trim() : '';
    // Hanya huruf, angka, titik, garis bawah, dan tanda hubung; maksimal 31 karakter.
    id = id.replace(/[^A-Za-z0-9._-]/g, '_').slice(0, 31);
    return jawab({ ok: true, id_window: id });
  }
  return jawab({ ok: true, pesan: 'gateway-sheets siap' });
}

/** Sel kosong dari gateway (null) menjadi string kosong di Sheets. */
function nol(v) {
  return v === null || v === undefined ? '' : v;
}
