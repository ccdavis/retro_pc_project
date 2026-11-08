# /// script
# requires-python = ">=3.9"
# dependencies = ["faster-whisper", "numpy"]
# ///
"""Transcribe 16-bit mono WAVs with faster-whisper base.en, loading the samples with
the wave module and a plain linear resample to 16 kHz (avoids the PyAV decoder)."""
import sys, wave, numpy as np
from faster_whisper import WhisperModel
m = WhisperModel("base.en", device="cpu", compute_type="int8")
for path in sys.argv[1:]:
    with wave.open(path, "rb") as w:
        sr = w.getframerate(); n = w.getnframes()
        x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float32) / 32768.0
    t_in = np.arange(len(x)) / sr
    t_out = np.arange(0, t_in[-1] if len(t_in) else 0, 1 / 16000.0)
    y = np.interp(t_out, t_in, x).astype(np.float32)
    segs, info = m.transcribe(y, beam_size=5, temperature=0, condition_on_previous_text=False)
    text = " ".join(s.text.strip() for s in segs)
    print(f"{path.split('/')[-1]:26s} {len(x)/sr:5.2f}s  {text}")
