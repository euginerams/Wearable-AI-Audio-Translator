"""
Translation server (runs on the PC; intended to move to a Raspberry Pi later).

Connection from the wearable (TCP port 5005). Every frame starts with a 4-byte
little-endian word: the top byte is the frame TYPE and the low 24 bits are the
payload LENGTH in bytes.

  type 0  audio                16-bit mono PCM at 16 kHz (also used for the reply)
  type 1  set target language  payload = language code, for example "fr"
  type 2  end of utterance     no payload: the speaker has finished the sentence

Reply: translated audio in type-0 frames, then an empty frame (length 0) as the end marker.

Flow: receive speech -> end of utterance -> speech to text -> translate ->
text to speech -> send the audio back. A dashboard (http://localhost:8080)
shows the status, the transcript and the timings, and every result is also
appended to results.csv.
"""

import csv
import json
import os
import socket
import struct
import threading
import time
import traceback
import wave
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import numpy as np

try:
    import winsound                # Windows only: lets the PC play the translation (testing)
except ImportError:
    winsound = None

import languages
import tts
from stt import transcribe
from translator import translate

HERE = os.path.dirname(os.path.abspath(__file__))

HOST = "0.0.0.0"
PORT = 5005
DASHBOARD_HOST = "127.0.0.1"      # only this PC can open the dashboard; use "0.0.0.0" to allow other devices
DASHBOARD_PORT = 8080

SAMPLE_RATE = 16000
FRAME_SAMPLES = 320               # 20 ms, same as FRAME_SAMPLES in the firmware's config.h
PLAY_ON_PC = False                # True: the PC also plays each translation (for testing the server alone)
WARM_UP = True                    # run each model once at start so the first translation is not slow

IDLE_FLUSH_S = 1.5                # backup: no audio for this long also ends the sentence
MAX_SEGMENT_S = 15                # safety cap for one sentence
MIN_SEGMENT_S = 0.4               # ignore blips shorter than this
MAX_FRAME_BYTES = 65536           # anything bigger means the stream is out of sync

FRAME_AUDIO = 0
FRAME_SET_LANGUAGE = 1
FRAME_END_OF_UTTERANCE = 2

CSV_PATH = os.path.join(HERE, "results.csv")
CSV_COLUMNS = ["time", "from", "to", "audio_s", "peak", "rms", "heard", "translated",
               "stt_s", "translate_s", "tts_s", "first_audio_s", "total_s"]

# ----------------------------------------------------------------------------
# Shared state (read by the dashboard, written by the connection threads)
# ----------------------------------------------------------------------------
_lock = threading.Lock()
_state = {
    "device": None,
    "status": "waiting for the device",
    "target": languages.DEFAULT_TARGET,
    "history": [],                # newest first
}


def set_status(text):
    with _lock:
        _state["status"] = text


def get_target():
    with _lock:
        return _state["target"]


def set_target(code):
    if code not in languages.CODES:
        return False
    with _lock:
        _state["target"] = code
    return True


def add_history(entry):
    with _lock:
        _state["history"].insert(0, entry)
        del _state["history"][30:]


def snapshot():
    with _lock:
        return {
            "device": _state["device"],
            "status": _state["status"],
            "target": _state["target"],
            "languages": [{"code": l["code"], "name": l["name"]} for l in languages.LANGUAGES],
            "history": list(_state["history"]),
        }


# ----------------------------------------------------------------------------
# Dashboard (plain HTTP, standard library only)
# ----------------------------------------------------------------------------
class DashboardHandler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _send(self, code, body, content_type):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/api/state":
            body = json.dumps(snapshot(), ensure_ascii=False).encode("utf-8")
            self._send(200, body, "application/json; charset=utf-8")
        elif self.path in ("/", "/index.html"):
            page = os.path.join(HERE, "dashboard.html")
            if os.path.exists(page):
                with open(page, "rb") as f:
                    self._send(200, f.read(), "text/html; charset=utf-8")
            else:
                self._send(404, b"dashboard.html is missing from the server folder", "text/plain")
        else:
            self._send(404, b"not found", "text/plain")

    def do_POST(self):
        if self.path != "/api/language":
            self._send(404, b"not found", "text/plain")
            return
        try:
            size = int(self.headers.get("Content-Length", 0))
            code = json.loads(self.rfile.read(size)).get("code", "")
        except (ValueError, json.JSONDecodeError):
            code = ""
        if set_target(code):
            print(f"target language set from the dashboard: {code}")
            self._send(200, b"{}", "application/json")
        else:
            self._send(400, b"unknown language", "text/plain")


def start_dashboard():
    server = ThreadingHTTPServer((DASHBOARD_HOST, DASHBOARD_PORT), DashboardHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print(f"dashboard: http://localhost:{DASHBOARD_PORT}")


# ----------------------------------------------------------------------------
# Audio helpers
# ----------------------------------------------------------------------------
def write_wav(path, pcm):
    with wave.open(os.path.join(HERE, path), "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(SAMPLE_RATE)
        f.writeframes(pcm.astype(np.int16).tobytes())


def recv_exact(conn, n, allow_timeout):
    """Read exactly n bytes.
    Returns b"" if the peer closed the connection.
    Returns None if allow_timeout is True and nothing arrived in time."""
    buf = b""
    while len(buf) < n:
        try:
            chunk = conn.recv(n - len(buf))
        except socket.timeout:
            if allow_timeout and not buf:
                return None
            continue              # mid-frame: keep waiting for the rest
        if not chunk:
            return b""
        buf += chunk
    return buf


_send_lock = threading.Lock()
_device_conn = None               # the connection to the wearable, if one is open


def send_audio_frames(conn, pcm):
    pcm = pcm.astype(np.int16)
    for i in range(0, len(pcm), FRAME_SAMPLES):
        data = pcm[i:i + FRAME_SAMPLES].tobytes()
        with _send_lock:
            conn.sendall(struct.pack("<I", len(data)) + data)


def send_end_marker(conn):
    with _send_lock:
        conn.sendall(struct.pack("<I", 0))


def play_test_tone():
    """Sends a short tone straight to the wearable. If the speaker beeps, the whole
    reply path works: server, Wi-Fi, device playback, amplifier and speaker."""
    conn = _device_conn
    if conn is None:
        print("no device is connected, so there is nowhere to send the tone")
        return
    t = np.arange(int(SAMPLE_RATE * 1.0)) / SAMPLE_RATE
    tone = (8000 * np.sin(2 * np.pi * 800 * t)).astype(np.int16)
    try:
        send_audio_frames(conn, tone)
        send_end_marker(conn)
        print("test tone sent to the device: you should hear a 1 second beep from its speaker")
    except OSError as error:
        print(f"could not send the test tone: {error}")


def console_commands():
    """Type  t  and press Enter in this window to send the test tone."""
    while True:
        try:
            command = input().strip().lower()
        except EOFError:
            return
        if command == "t":
            play_test_tone()
        elif command:
            print("commands:  t = send a test tone to the device")


# ----------------------------------------------------------------------------
# One sentence: speech -> text -> translation -> speech
# ----------------------------------------------------------------------------
def log_result(entry):
    new_file = not os.path.exists(CSV_PATH)
    with open(CSV_PATH, "a", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_COLUMNS, extrasaction="ignore")
        if new_file:
            writer.writeheader()
        writer.writerow(entry)


def process_segment(conn, segment):
    target = get_target()
    audio_s = len(segment) / SAMPLE_RATE
    peak = int(np.abs(segment).max())
    rms = int(np.sqrt(np.mean(segment.astype(np.float64) ** 2)))
    print(f"\n--- sentence received: {audio_s:.2f} s of audio ---")
    print(f"level      : peak {peak}, rms {rms} (of 32767)")
    write_wav("received_audio.wav", segment)
    set_status("translating")

    entry = {"time": datetime.now().strftime("%H:%M:%S"), "to": target, "audio_s": round(audio_s, 2),
             "peak": peak, "rms": rms, "heard": "", "translated": "", "from": "",
             "stt_s": 0, "translate_s": 0, "tts_s": 0, "first_audio_s": 0, "total_s": 0}

    def finish(note):
        entry["translated"] = note
        entry["from_name"] = languages.get(entry["from"])["name"] if languages.get(entry["from"]) else "?"
        entry["to_name"] = languages.get(target)["name"]
        add_history(entry)
        log_result(entry)
        send_end_marker(conn)
        set_status("device connected")

    t0 = time.perf_counter()
    text, source = transcribe(segment, SAMPLE_RATE)
    t1 = time.perf_counter()
    entry["heard"], entry["from"], entry["stt_s"] = text, source, round(t1 - t0, 2)

    if not text.strip():
        print("no speech recognised (the audio may be too quiet or too short)")
        finish("(no speech recognised)")
        return
    print(f"heard      : [{source}] {text}")

    if source not in languages.CODES:
        finish(f"(unsupported language: {source})")
        return
    if source == target:
        print(f"the speaker used the target language ({target}): nothing to translate")
        finish("(same language as the target, nothing to translate)")
        return

    try:
        translated = translate(text, source, target)
    except Exception as error:
        print(f"translation failed: {error}  (run setup_languages.py?)")
        finish("(translation failed: language pack missing?)")
        return
    t2 = time.perf_counter()
    print(f"translated : [{target}] {translated}")

    pieces, first_audio = [], None
    for piece in tts.synthesize_chunks(translated, target, SAMPLE_RATE):
        if first_audio is None:
            first_audio = time.perf_counter() - t0
        pieces.append(piece)
        send_audio_frames(conn, piece)          # start sending before the whole sentence is spoken
    t3 = time.perf_counter()
    send_end_marker(conn)

    if pieces:
        write_wav("translated_output.wav", np.concatenate(pieces))
        if PLAY_ON_PC and winsound:
            winsound.PlaySound(os.path.join(HERE, "translated_output.wav"),
                               winsound.SND_FILENAME | winsound.SND_ASYNC)

    entry.update({"translated": translated, "translate_s": round(t2 - t1, 2), "tts_s": round(t3 - t2, 2),
                  "first_audio_s": round(first_audio or 0, 2), "total_s": round(t3 - t0, 2)})
    entry["from_name"] = languages.get(source)["name"]
    entry["to_name"] = languages.get(target)["name"]
    add_history(entry)
    log_result(entry)
    set_status("device connected")

    print(f"timing     : speech-to-text {entry['stt_s']} s | translation {entry['translate_s']} s"
          f" | voice {entry['tts_s']} s | first audio {entry['first_audio_s']} s | total {entry['total_s']} s")


# ----------------------------------------------------------------------------
# One connection from the wearable
# ----------------------------------------------------------------------------
def handle_client(conn, addr):
    who = "this PC (a test script, NOT the device)" if addr[0] in ("127.0.0.1", "::1") else "the device"
    print(f"client connected: {addr}  ->  {who}")
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    conn.settimeout(IDLE_FLUSH_S)
    global _device_conn
    _device_conn = conn
    with _lock:
        _state["device"] = addr[0]
    set_status("device connected")

    pieces, total = [], 0

    def flush():
        nonlocal pieces, total
        if total >= MIN_SEGMENT_S * SAMPLE_RATE:
            try:
                process_segment(conn, np.concatenate(pieces))
            except OSError:
                raise                              # the connection itself failed
            except Exception:
                print("processing this sentence failed:")
                traceback.print_exc()
                set_status("error: see the server window")
                send_end_marker(conn)
        elif total:
            print(f"ignored a {total / SAMPLE_RATE:.2f} s blip")
            send_end_marker(conn)
        pieces, total = [], 0
        set_status("device connected")

    try:
        while True:
            header = recv_exact(conn, 4, allow_timeout=True)
            if header is None:                     # nothing arrived: backup end of sentence
                if total:
                    flush()
                continue
            if header == b"":
                break
            (word,) = struct.unpack("<I", header)
            frame_type, length = word >> 24, word & 0xFFFFFF
            if length > MAX_FRAME_BYTES:
                print(f"bad frame length {length}, closing connection")
                break
            payload = recv_exact(conn, length, allow_timeout=False) if length else b""
            if length and not payload:
                break

            if frame_type == FRAME_AUDIO:
                if length == 0:
                    continue
                if length % 2:
                    print(f"bad audio frame length {length}, closing connection")
                    break
                if total == 0:
                    print(f"receiving speech from {addr[0]} ...")
                    set_status("receiving speech")
                pieces.append(np.frombuffer(payload, dtype=np.int16))
                total += length // 2
                if total >= MAX_SEGMENT_S * SAMPLE_RATE:
                    flush()
            elif frame_type == FRAME_SET_LANGUAGE:
                code = payload.decode("ascii", "ignore").strip()
                if set_target(code):
                    print(f"target language set by the device: {code}")
                else:
                    print(f"the device asked for an unknown language: {code!r}")
            elif frame_type == FRAME_END_OF_UTTERANCE:
                flush()
            else:
                print(f"ignored a frame of unknown type {frame_type}")
    finally:
        if _device_conn is conn:
            _device_conn = None
        conn.close()
        with _lock:
            _state["device"] = None
        set_status("waiting for the device")
        print(f"client disconnected: {addr}")


# ----------------------------------------------------------------------------
def warm_up():
    """Run every model once so the first real translation is not slowed by loading."""
    print("warming up the speech, translation and voice models (about a minute) ...")
    t0 = time.perf_counter()
    tts.load_all_voices()
    transcribe(np.zeros(SAMPLE_RATE, dtype=np.int16), SAMPLE_RATE)
    for lang in languages.LANGUAGES:
        code = lang["code"]
        try:
            if code != "en":
                translate("hello", "en", code)
                translate("hello", code, "en")
            for _ in tts.synthesize_chunks("hello", code, SAMPLE_RATE):
                pass
            print(f"  {lang['name']}: ready")
        except Exception as error:
            print(f"  {lang['name']}: NOT READY ({error}); run setup_languages.py")
    print(f"warm-up finished in {time.perf_counter() - t0:.1f} s")


def main():
    if WARM_UP:
        warm_up()
    start_dashboard()
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((HOST, PORT))
        srv.listen(1)
        print(f"listening for the device on port {PORT}")
        print(f"target language: {languages.get(get_target())['name']}")
        print("type  t  and press Enter to send a test tone to the device")
        threading.Thread(target=console_commands, daemon=True).start()
        while True:
            conn, addr = srv.accept()
            threading.Thread(target=handle_client, args=(conn, addr), daemon=True).start()


if __name__ == "__main__":
    main()
