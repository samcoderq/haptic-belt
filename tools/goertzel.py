"""
Feature-extraction spec for the belt's sound classifier -- this exact
algorithm (block size, target frequencies, Goertzel formula, normalization)
is re-implemented in plain C in src/classifier_interface.cpp so training
(here) and on-device inference compute the identical feature vector for the
same audio. See tools/verify_goertzel_match.py for the numeric check that
the two implementations actually agree.

Goertzel, not a full FFT: detecting energy at a small fixed set of known
frequencies (not a full spectrum) is exactly what Goertzel is for -- it's
the same technique DTMF touch-tone decoders use. Chosen over FFT because it
needs no library on the firmware side (a straightforward recursive filter,
~10 lines of C, no windowing/bit-reversal/complex-buffer machinery) and
costs less compute for a "energy at N specific bins" answer, which is all
this classifier needs.
"""
import numpy as np

SAMPLE_RATE = 16000
BLOCK_SIZE = 1024  # 64ms @ 16kHz -- fits within one mic_array frame (1600 samples)

# 16 bins, roughly log-spaced 200Hz-7000Hz (Nyquist at 16kHz is 8000Hz).
# Not derived from any measurement of real target sounds -- a reasonable
# generic spread covering low tones (horns, some sirens) through mid
# (doorbell chimes, speech) to high (glass break, some alarm chirps).
TARGET_FREQS = [
    200, 300, 400, 550, 700, 900, 1100, 1400,
    1700, 2100, 2600, 3200, 3900, 4700, 5700, 7000,
]


def goertzel_power(block: np.ndarray, freq: float, sample_rate: int = SAMPLE_RATE) -> float:
    """block: 1D float64 array of exactly BLOCK_SIZE samples."""
    n = len(block)
    k = round(n * freq / sample_rate)
    w = 2.0 * np.pi * k / n
    coeff = 2.0 * np.cos(w)
    s_prev = 0.0
    s_prev2 = 0.0
    for x in block:
        s = x + coeff * s_prev - s_prev2
        s_prev2 = s_prev
        s_prev = s
    return s_prev2 * s_prev2 + s_prev * s_prev - coeff * s_prev * s_prev2


def extract_features(samples: np.ndarray) -> np.ndarray:
    """samples: 1D array of raw PCM samples (any length >= 1, any numeric
    dtype). Returns a CLASSIFIER_FEATURE_COUNT-length L1-normalized vector
    (relative spectral shape, amplitude/loudness-invariant) -- an all-zero
    vector for silence/near-silence, matched by the same convention in the
    C implementation.
    """
    block = np.asarray(samples, dtype=np.float64)[:BLOCK_SIZE]
    if len(block) < BLOCK_SIZE:
        block = np.pad(block, (0, BLOCK_SIZE - len(block)))
    powers = np.array([goertzel_power(block, f) for f in TARGET_FREQS])
    total = powers.sum()
    if total <= 1e-6:
        return np.zeros(len(TARGET_FREQS))
    return powers / total
