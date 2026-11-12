"""
Text to speech, one voice per language. Audio is produced one sentence at a
time, so the server can start sending before the whole translation has been
spoken.

  Piper    fast ONNX voices (English, Spanish, French, German, Portuguese)
  MMS-TTS  VITS voices in Meta's MMS-TTS format, for languages Piper does not
           have. Afrikaans uses UBC-NLP/Simba-TTS-afr (CC-BY 4.0). Saved by
           setup_languages.py into models/<model name>.
"""

import os
import re

import numpy as np

import languages

HERE = os.path.dirname(os.path.abspath(__file__))
_voices = {}


def _mms_dir(lang):
    return os.path.join(HERE, "models", lang["mms_model"].split("/")[-1])


def _voice(code):
    if code not in _voices:
        lang = languages.get(code)
        if lang["tts"] == "mms":
            path = _mms_dir(lang)
            if not os.path.isdir(path):
                raise RuntimeError(f"MMS voice missing for {lang['name']}: run setup_languages.py")
            from transformers import AutoTokenizer, VitsModel
            model = VitsModel.from_pretrained(path)
            model.eval()
            _voices[code] = ("mms", model, AutoTokenizer.from_pretrained(path))
        else:
            from piper import PiperVoice
            _voices[code] = ("piper", PiperVoice.load(os.path.join(HERE, lang["voice"] + ".onnx")), None)
    return _voices[code]


def load_all_voices():
    for lang in languages.LANGUAGES:
        _voice(lang["code"])


def _resample(pcm, native_rate, target_rate):
    if native_rate == target_rate or len(pcm) == 0:
        return pcm
    target_len = int(len(pcm) * target_rate / native_rate)
    return np.interp(
        np.linspace(0, len(pcm), target_len, endpoint=False),
        np.arange(len(pcm)),
        pcm,
    ).astype(np.int16)


def _sentences(text):
    parts = re.split(r"(?<=[.!?])\s+", text.strip())
    return [p for p in parts if p]


def _mms_chunks(model, tokenizer, text, sample_rate):
    import torch
    native_rate = model.config.sampling_rate          # 16000 for MMS
    for sentence in _sentences(text):
        inputs = tokenizer(sentence, return_tensors="pt")
        if inputs["input_ids"].shape[-1] == 0:
            continue
        with torch.no_grad():
            wave = model(**inputs).waveform[0].cpu().numpy()
        pcm = (np.clip(wave, -1.0, 1.0) * 32767).astype(np.int16)
        yield _resample(pcm, native_rate, sample_rate)


def synthesize_chunks(text, language, sample_rate):
    """Yields 16-bit mono audio at `sample_rate`, one piece per sentence."""
    kind, voice, tokenizer = _voice(language)
    if kind == "mms":
        yield from _mms_chunks(voice, tokenizer, text, sample_rate)
        return
    native_rate = voice.config.sample_rate
    for chunk in voice.synthesize(text):
        pcm = np.frombuffer(chunk.audio_int16_bytes, dtype=np.int16)
        yield _resample(pcm, native_rate, sample_rate)
