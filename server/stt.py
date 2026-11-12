"""
Speech to text with faster-whisper. Returns the text and the language that
was spoken, limited to the languages this system supports.
"""

import numpy as np
from faster_whisper import WhisperModel

from languages import CODES

# "tiny" is the fastest multilingual model. A bigger model ("base", "small") is
# more accurate but slower, which makes a good experiment for the report.
# Afrikaans is recognised much better by "base" or "small" than by "tiny".
_model = WhisperModel("tiny", device="cpu", compute_type="int8")


def _run(audio, language):
    segments, info = _model.transcribe(audio, language=language, beam_size=1)
    text = " ".join(seg.text for seg in segments).strip()   # segments is lazy: read it now
    return text, info


def transcribe(pcm_int16, sample_rate):
    audio = pcm_int16.astype(np.float32) / 32768.0
    text, info = _run(audio, None)                           # None = detect the language
    language = info.language

    # Whisper often hears Afrikaans as Dutch (they are closely related).
    # Dutch is not supported here, so treat it as Afrikaans and transcribe again.
    if language == "nl" and "af" in CODES:
        language = "af"
        text, _ = _run(audio, language)
        return text, language

    if language not in CODES:
        # Detected something we do not support: pick the most likely supported language instead
        probs = getattr(info, "all_language_probs", None) or []
        supported = [p for p in probs if p[0] in CODES]
        if supported:
            language = max(supported, key=lambda p: p[1])[0]
            text, _ = _run(audio, language)
    return text, language
