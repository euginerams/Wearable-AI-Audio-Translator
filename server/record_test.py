"""
Records a test sentence from THIS PC's microphone and saves it as
test_speech.wav (mono, 16-bit, 16000 Hz - the format the server expects).

Usage:  python record_test.py          (records 5 seconds)
        python record_test.py 8        (records 8 seconds)
"""

import sys
import time
import wave

import numpy as np
import sounddevice as sd

RATE = 16000
seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 5.0

for n in (3, 2, 1):
    print(f"recording in {n} ...")
    time.sleep(1)
print(f"SPEAK NOW (English) for {seconds:.0f} seconds")

audio = sd.rec(int(seconds * RATE), samplerate=RATE, channels=1, dtype="int16")
sd.wait()
audio = audio[:, 0]

with wave.open("test_speech.wav", "wb") as f:
    f.setnchannels(1)
    f.setsampwidth(2)
    f.setframerate(RATE)
    f.writeframes(audio.tobytes())

peak = int(np.abs(audio).max())
print(f"saved test_speech.wav, loudest sample = {peak} (of 32767)")
if peak < 1000:
    print("WARNING: very quiet. Check the microphone, Windows privacy settings and volume.")