"""Normalize newly recognized speech for the Pocket draft, on the PC only."""
import threading

NORMALIZER = 'opencc-python-reimplemented/0.1.7:t2s'
_lock = threading.Lock()
_converter = None


def simplified_transcript(text):
    """Convert Chinese orthography without editing the original transcript."""
    if not isinstance(text, str):
        raise TypeError('speech transcription must be text')
    if not text or text.isascii():
        return text
    global _converter
    with _lock:
        if _converter is None:
            from vendor.opencc import OpenCC
            _converter = OpenCC('t2s')
        return _converter.convert(text)
