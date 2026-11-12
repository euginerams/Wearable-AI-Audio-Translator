# Server: speech-to-text, translation and text-to-speech

The server receives speech from the wearable over Wi-Fi, translates it and sends the translated
speech back. Everything runs offline once the models have been downloaded. It currently runs on a
PC (developed on Windows with Python 3.11) and is intended to move to a Raspberry Pi.

```
speech from the device -> Whisper (speech to text) -> Argos / NLLB (translation)
                       -> Piper / MMS (text to speech) -> audio back to the device
```

## Files

| File | Responsibility |
|---|---|
| `server.py` | TCP server for the device (port 5005), dashboard (port 8080), logging to `results.csv` |
| `languages.py` | The list of supported languages and which engine each step uses |
| `stt.py` | Speech to text with faster-whisper, including language detection |
| `translator.py` | Translation: Argos Translate, or NLLB-200 for Afrikaans |
| `tts.py` | Text to speech: Piper voices, or MMS-TTS for Afrikaans |
| `setup_languages.py` | One-time download of all translation models and voices, plus a self-test |
| `dashboard.html` | Local dashboard: status, transcript, translation and timings |
| `send_test_audio.py` | Sends a WAV file to the server exactly like the device does (testing without hardware) |
| `record_test.py` | Records `test_speech.wav` from the PC microphone |
| `test_speech.wav` | Sample English sentence for testing (mono, 16-bit, 16 kHz) |

## Languages

| Code | Language | Speech to text | Translation | Voice |
|---|---|---|---|---|
| `en` | English | Whisper | Argos | Piper (lessac) |
| `es` | Spanish | Whisper | Argos | Piper (davefx) |
| `fr` | French | Whisper | Argos | Piper (siwis) |
| `de` | German | Whisper | Argos | Piper (thorsten) |
| `pt` | Portuguese | Whisper | Argos | Piper (faber) |
| `af` | Afrikaans | Whisper | NLLB-200 (600M, int8) | Simba-TTS-afr (MMS-TTS format) |

Argos Translate and Piper do not support Afrikaans, so Afrikaans uses Meta's NLLB-200 translation
model and the Simba-TTS Afrikaans voice from UBC-NLP (Meta's MMS has no Afrikaans voice). Whisper often detects Afrikaans as Dutch; `stt.py` treats Dutch as Afrikaans.
To add a language, add a line to `languages.py`, add it to `firmware/main/language.c` in the same
position, and run `setup_languages.py` again.

## Setup (run all commands from this `server` folder)

```
python -m venv venv
venv\Scripts\activate            (Windows)   or   source venv/bin/activate   (Linux / Raspberry Pi)
pip install -r requirements.txt
python setup_languages.py        (needs internet once; downloads about 3 GB of models)
```

`setup_languages.py` ends with a translation test and writes `voice_test_af.wav` so you can check
the Afrikaans voice. Models are saved in `models/` and next to the scripts (`*.onnx`); they are not
committed to git.

## Run

```
python server.py
```

- Open the dashboard at http://localhost:8080.
- Type `t` and press Enter to send a test tone to a connected device.
- Settings such as `PLAY_ON_PC`, `WARM_UP` and the dashboard address are at the top of `server.py`.

## Test without the device

Keep `server.py` running and, in a second terminal in this folder:

```
python send_test_audio.py test_speech.wav es     (English -> Spanish)
python send_test_audio.py test_speech.wav af     (English -> Afrikaans)
```

The test script saves the audio it receives as `client_received.wav` (the server also keeps its
own copies of the last sentence and translation). To record your own test sentence:
`python record_test.py 5` (5 seconds).

## Performance

`server.py` appends every sentence to `results.csv`. A copy of the team's test log is in
`../docs/test_results.csv` (116 sentences, 68 of them translated and spoken; the others were
blips, same-language sentences or failed early tests). For those 68 translations on the
development PC, the median server processing time from receiving the sentence to the first
translated audio was about 1.9 s, and 78% started within 3 s. Speech to text is the slowest step
(median 1.4 s); translation (0.1 s) and the voice (0.3 s) are fast. These times do not include the
0.6 s end-of-speech wait on the device or the Wi-Fi transfer.

## Hardware notes

- A laptop runs everything comfortably.
- A Raspberry Pi needs about 4 GB of RAM or more, mainly because of NLLB and the Afrikaans voice.
  A Raspberry Pi Zero 2 W (512 MB) is not enough for the full language set.
- Whisper `tiny` is used for speed. `base` or `small` in `stt.py` are more accurate (especially for
  Afrikaans) but slower.

## Licences of the models

Whisper (MIT), Argos Translate (MIT), Piper voices (see each voice's model card), NLLB-200
(CC-BY-NC 4.0) and Simba-TTS-afr (CC-BY 4.0). NLLB allows non-commercial use only, which suits
this student project but not a commercial product.
