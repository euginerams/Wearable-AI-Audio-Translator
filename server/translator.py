"""
Offline translation.

  Argos Translate  for pairs where both languages use "argos". Argos goes
                   through English for two non-English languages, so the
                   English-to-X and X-to-English packs give every pair.
  NLLB-200         for any pair that involves an "nllb" language (Afrikaans).
                   NLLB translates directly between any two of its languages,
                   so Afrikaans -> Spanish does not need to go through English.

The NLLB model is converted and saved by setup_languages.py into
models/nllb-200-distilled-600M-int8 (about 600 MB). It is only loaded the
first time it is needed.
"""

import os
import threading

import argostranslate.translate

import languages

HERE = os.path.dirname(os.path.abspath(__file__))
NLLB_DIR = os.path.join(HERE, "models", "nllb-200-distilled-600M-int8")

_nllb = None            # (translator, tokenizer) once loaded
_nllb_lock = threading.Lock()


def _load_nllb():
    global _nllb
    with _nllb_lock:
        if _nllb is None:
            if not os.path.isdir(NLLB_DIR):
                raise RuntimeError("NLLB model missing: run setup_languages.py")
            import ctranslate2
            from transformers import AutoTokenizer
            translator = ctranslate2.Translator(NLLB_DIR, device="cpu", compute_type="int8")
            tokenizer = AutoTokenizer.from_pretrained(NLLB_DIR)
            _nllb = (translator, tokenizer)
    return _nllb


def _translate_nllb(text, source, target):
    translator, tokenizer = _load_nllb()
    src_code = languages.get(source)["nllb"]
    tgt_code = languages.get(target)["nllb"]
    with _nllb_lock:                       # the tokenizer's src_lang is shared state
        tokenizer.src_lang = src_code
        tokens = tokenizer.convert_ids_to_tokens(tokenizer.encode(text))
    result = translator.translate_batch(
        [tokens], target_prefix=[[tgt_code]], beam_size=2, max_decoding_length=256)
    out_tokens = result[0].hypotheses[0][1:]   # drop the target-language token
    return tokenizer.decode(tokenizer.convert_tokens_to_ids(out_tokens), skip_special_tokens=True)


def translate(text, source, target):
    if source == target:
        return text
    if languages.get(source)["mt"] == "nllb" or languages.get(target)["mt"] == "nllb":
        return _translate_nllb(text, source, target)
    return argostranslate.translate.translate(text, source, target)
