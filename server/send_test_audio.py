"""
Streams a WAV file to server.py exactly like the wearable does: a language
frame (optional), the speech in 20 ms frames in real time, then an
end-of-utterance frame. It then waits for the translated audio.

Usage:  python send_test_audio.py test_speech.wav [language_code]
        python send_test_audio.py test_speech.wav fr
The WAV must be mono, 16-bit, 16000 Hz.
"""

import socket
import struct
import sys
import time
import wave

import numpy as np

SERVER_HOST = "127.0.0.1"
SERVER_PORT = 5005
FRAME_SAMPLES = 320
FRAME_SET_LANGUAGE = 1
FRAME_END_OF_UTTERANCE = 2
RESPONSE_TIMEOUT_S = 120


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("server closed the connection")
        buf += chunk
    return buf


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "test_speech.wav"
    language = sys.argv[2] if len(sys.argv) > 2 else None

    with wave.open(path, "rb") as f:
        assert f.getframerate() == 16000, "WAV must be 16000 Hz"
        assert f.getnchannels() == 1, "WAV must be mono"
        assert f.getsampwidth() == 2, "WAV must be 16-bit"
        pcm = np.frombuffer(f.readframes(f.getnframes()), dtype=np.int16)

    sock = socket.create_connection((SERVER_HOST, SERVER_PORT))
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.settimeout(RESPONSE_TIMEOUT_S)

    if language:
        code = language.encode("ascii")
        sock.sendall(struct.pack("<I", (FRAME_SET_LANGUAGE << 24) | len(code)) + code)
        print(f"asked for target language: {language}")

    print(f"streaming {len(pcm) / 16000:.2f} s of audio ...")
    for i in range(0, len(pcm), FRAME_SAMPLES):
        data = pcm[i:i + FRAME_SAMPLES].tobytes()
        sock.sendall(struct.pack("<I", len(data)) + data)
        time.sleep(FRAME_SAMPLES / 16000)            # real-time pacing
    sock.sendall(struct.pack("<I", FRAME_END_OF_UTTERANCE << 24))
    t_sent = time.perf_counter()
    print("end of speech sent, waiting for the translation ...")

    chunks, t_first = [], None
    while True:
        (length,) = struct.unpack("<I", recv_exact(sock, 4))
        if length == 0:                              # end-of-reply marker
            break
        if t_first is None:
            t_first = time.perf_counter()
        chunks.append(recv_exact(sock, length))
    sock.close()

    if not chunks:
        print("the server returned no audio (nothing to translate, or no speech recognised)")
        return

    audio = np.frombuffer(b"".join(chunks), dtype=np.int16)
    with wave.open("client_received.wav", "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(16000)
        f.writeframes(audio.tobytes())
    print(f"received {len(audio)} samples ({len(audio) / 16000:.2f} s) of translated audio")
    print(f"delay from end of speech to first translated audio: {t_first - t_sent:.2f} s")
    print("saved client_received.wav")


if __name__ == "__main__":
    main()
