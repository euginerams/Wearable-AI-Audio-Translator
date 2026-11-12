"""
The languages the system supports. The speech, translation and voice
modules all read this one list, so adding a language means adding a line here
(plus its models, which setup_languages.py installs).

Each language says which engine to use for each step:
  mt   translation  "argos" (small, fast) or "nllb" (Meta NLLB-200, covers
                    languages Argos does not have, such as Afrikaans)
  tts  voice        "piper" (fast ONNX voices) or "mms" (an MMS-TTS / VITS voice
                    loaded with transformers, for languages Piper does not have.
                    Afrikaans uses UBC-NLP/Simba-TTS-afr: Meta has no MMS Afrikaans voice)
  nllb              the NLLB code for the language. Every language needs one,
                    because a pair that involves an NLLB language (for example
                    Afrikaans -> Spanish) is translated by NLLB directly.
Speech to text is Whisper for every language (Whisper understands Afrikaans).
"""

LANGUAGES = [
    {"code": "en", "name": "English",    "mt": "argos", "nllb": "eng_Latn",
     "tts": "piper", "voice": "en_US-lessac-medium",   "voice_path": "en/en_US/lessac/medium"},
    {"code": "es", "name": "Spanish",    "mt": "argos", "nllb": "spa_Latn",
     "tts": "piper", "voice": "es_ES-davefx-medium",   "voice_path": "es/es_ES/davefx/medium"},
    {"code": "fr", "name": "French",     "mt": "argos", "nllb": "fra_Latn",
     "tts": "piper", "voice": "fr_FR-siwis-medium",    "voice_path": "fr/fr_FR/siwis/medium"},
    {"code": "de", "name": "German",     "mt": "argos", "nllb": "deu_Latn",
     "tts": "piper", "voice": "de_DE-thorsten-medium", "voice_path": "de/de_DE/thorsten/medium"},
    {"code": "pt", "name": "Portuguese", "mt": "argos", "nllb": "por_Latn",
     "tts": "piper", "voice": "pt_BR-faber-medium",    "voice_path": "pt/pt_BR/faber/medium"},
    {"code": "af", "name": "Afrikaans",  "mt": "nllb",  "nllb": "afr_Latn",
     "tts": "mms",   "mms_model": "UBC-NLP/Simba-TTS-afr"},
]

CODES = [lang["code"] for lang in LANGUAGES]
DEFAULT_TARGET = "es"


def get(code):
    for lang in LANGUAGES:
        if lang["code"] == code:
            return lang
    return None


def uses(engine_key, engine):
    """True if any language uses this engine, e.g. uses("tts", "mms")."""
    return any(lang.get(engine_key) == engine for lang in LANGUAGES)
