"""
One-time setup for all supported languages. Needs internet.
  1. installs the Argos translation packs (English <-> each Argos language)
  2. downloads the Piper voice for each Piper language
  3. converts and saves the NLLB-200 translation model (for Afrikaans)
  4. downloads and saves the MMS-TTS voice for each MMS language (Afrikaans)
Run it from this folder:  python setup_languages.py
Steps 3 and 4 need torch and transformers (see requirements.txt) and download
about 2.5 GB the first time; the saved models then work offline.
"""

import os
import urllib.request

import argostranslate.package
import argostranslate.translate

import languages
from languages import LANGUAGES

HERE = os.path.dirname(os.path.abspath(__file__))
MODELS = os.path.join(HERE, "models")
VOICE_URL = "https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/"
NLLB_SOURCE = "facebook/nllb-200-distilled-600M"
NLLB_DIR = os.path.join(MODELS, "nllb-200-distilled-600M-int8")


def install_translation_packs():
    print("\n== Argos translation packs ==")
    argostranslate.package.update_package_index()
    available = argostranslate.package.get_available_packages()
    try:
        installed = {(p.from_code, p.to_code) for p in argostranslate.package.get_installed_packages()}
    except Exception:
        installed = set()

    for lang in LANGUAGES:
        if lang["code"] == "en" or lang["mt"] != "argos":
            continue
        for src, dst in (("en", lang["code"]), (lang["code"], "en")):
            if (src, dst) in installed:
                print(f"  {src} -> {dst}: already installed")
                continue
            pkg = next((p for p in available if p.from_code == src and p.to_code == dst), None)
            if pkg is None:
                print(f"  {src} -> {dst}: NOT AVAILABLE in the package index")
                continue
            print(f"  {src} -> {dst}: downloading ...")
            argostranslate.package.install_from_path(pkg.download())


def download_voices():
    print("\n== Piper voices ==")
    for lang in LANGUAGES:
        if lang["tts"] != "piper":
            continue
        for ext in (".onnx", ".onnx.json"):
            name = lang["voice"] + ext
            path = os.path.join(HERE, name)
            if os.path.exists(path):
                print(f"  {name}: already here")
                continue
            print(f"  {name}: downloading ...")
            urllib.request.urlretrieve(VOICE_URL + lang["voice_path"] + "/" + name, path)


def setup_nllb():
    if not languages.uses("mt", "nllb"):
        return
    print("\n== NLLB-200 translation model ==")
    if os.path.isdir(NLLB_DIR):
        print("  already here")
        return
    import ctranslate2
    from transformers import AutoTokenizer
    print(f"  downloading {NLLB_SOURCE} and converting to int8 (several minutes) ...")
    ctranslate2.converters.TransformersConverter(NLLB_SOURCE).convert(NLLB_DIR, quantization="int8")
    AutoTokenizer.from_pretrained(NLLB_SOURCE).save_pretrained(NLLB_DIR)   # keep the tokenizer offline too
    print(f"  saved to {NLLB_DIR}")


def setup_mms_voices():
    for lang in LANGUAGES:
        if lang["tts"] != "mms":
            continue
        print(f"\n== MMS voice: {lang['name']} ==")
        path = os.path.join(MODELS, lang["mms_model"].split("/")[-1])
        if os.path.isdir(path):
            print("  already here")
            continue
        from transformers import AutoTokenizer, VitsModel
        print(f"  downloading {lang['mms_model']} ...")
        VitsModel.from_pretrained(lang["mms_model"]).save_pretrained(path)
        AutoTokenizer.from_pretrained(lang["mms_model"]).save_pretrained(path)
        print(f"  saved to {path}")


def self_test():
    print("\n== Quick translation test ==")
    from translator import translate
    tests = [("Good morning, how are you?", "en", "es"),
             ("Good morning, how are you?", "en", "fr"),
             ("Good morning, how are you?", "en", "af"),
             ("Goeie môre, hoe gaan dit met jou?", "af", "en"),
             ("Goeie môre, hoe gaan dit met jou?", "af", "es"),
             ("Bom dia, como esta?", "pt", "fr")]
    for text, src, dst in tests:
        try:
            print(f"  {src} -> {dst}: {translate(text, src, dst)}")
        except Exception as error:
            print(f"  {src} -> {dst}: FAILED ({error})")

    print("\n== Quick voice test (writes voice_test_<code>.wav) ==")
    import wave
    import numpy as np
    import tts
    for lang in LANGUAGES:
        if lang["tts"] != "mms":
            continue
        try:
            pcm = np.concatenate(list(tts.synthesize_chunks("Goeie môre, hoe gaan dit?", lang["code"], 16000)))
            name = f"voice_test_{lang['code']}.wav"
            with wave.open(os.path.join(HERE, name), "wb") as w:
                w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000)
                w.writeframes(pcm.tobytes())
            print(f"  {lang['name']}: wrote {name}, play it to check")
        except Exception as error:
            print(f"  {lang['name']}: FAILED ({error})")


if __name__ == "__main__":
    os.makedirs(MODELS, exist_ok=True)
    install_translation_packs()
    download_voices()
    setup_nllb()
    setup_mms_voices()
    self_test()
    print("\nDone.")
