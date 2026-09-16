"""
Python reference/verification for the belt's keyword-spotting pipeline
(MFCC feature extraction + DTW matching) -- mirrors src/mfcc_dsp.cpp,
src/dtw.cpp, and KeywordEnrollScreen's KeywordFeatures.kt exactly (same
frame size/hop, same mel filterbank recipe, same DCT-II with C0 dropped,
same cepstral mean normalization, same DTW formulation). Written offline
(no belt/phone connected) specifically to verify the hand-rolled radix-2
FFT and DTW in those three independent implementations are actually
mathematically correct, not just "compiles and doesn't crash" -- that had
never been checked before this file, only live-tested against real speech
once, which is a much weaker signal for "is the algorithm itself right."

Run directly (`python tools/mfcc_dtw.py`) to execute the self-tests at the
bottom: FFT-vs-numpy cross-check, and DTW sanity checks (identical/shifted/
different sequences). All use synthetic signals -- no recorded audio needed.
"""
import numpy as np

SAMPLE_RATE = 16000
FRAME_SIZE = 512   # 32ms @ 16kHz, power-of-two for the FFT
FRAME_HOP = 384    # 24ms @ 16kHz
MEL_FILTERS = 40
MFCC_COUNT = 13
FREQ_MIN = 80.0
FREQ_MAX = 7600.0  # must stay under Nyquist (8000 @ 16kHz)


def fft_radix2(x: np.ndarray) -> np.ndarray:
    """Hand-rolled iterative radix-2 Cooley-Tukey FFT, same structure as
    src/mfcc_dsp.cpp's fft() and KeywordFeatures.kt's fft() -- bit-reversal
    permutation, then log2(n) combine stages, precomputed-twiddle style
    (computed inline here since Python has no reason to cache them the way
    an embedded target does). Exists specifically so its output can be
    diffed against numpy.fft.fft() as ground truth (see verify_fft() below)
    -- the two firmware/app implementations can't be unit-tested directly,
    but they share this exact algorithm structure, so validating it here
    validates the approach both of them use.
    """
    n = len(x)
    assert n & (n - 1) == 0, "FFT size must be a power of two"
    bits = n.bit_length() - 1

    def bit_reverse(i, nbits):
        r = 0
        for _ in range(nbits):
            r = (r << 1) | (i & 1)
            i >>= 1
        return r

    re = np.real(x).astype(np.float64).copy()
    im = np.imag(x).astype(np.float64).copy()
    for i in range(n):
        j = bit_reverse(i, bits)
        if j > i:
            re[i], re[j] = re[j], re[i]
            im[i], im[j] = im[j], im[i]

    size = 2
    while size <= n:
        half = size // 2
        step = n // size
        for start in range(0, n, size):
            for k in range(half):
                theta = 2.0 * np.pi * (k * step) / n
                wr, wi = np.cos(theta), -np.sin(theta)  # forward transform: e^-j*theta
                a, b = start + k, start + k + half
                br = re[b] * wr - im[b] * wi
                bi = re[b] * wi + im[b] * wr
                re[b] = re[a] - br
                im[b] = im[a] - bi
                re[a] = re[a] + br
                im[a] = im[a] + bi
        size <<= 1

    return re + 1j * im


def _hz_to_mel(hz):
    return 2595.0 * np.log10(1.0 + hz / 700.0)


def _mel_to_hz(mel):
    return 700.0 * (10.0 ** (mel / 2595.0) - 1.0)


def _mel_filter_bins(sample_rate=SAMPLE_RATE):
    """(left, center, right) FFT bin index per filter -- same recipe as
    mfcc_dsp.cpp's mfccInit()."""
    mel_min, mel_max = _hz_to_mel(FREQ_MIN), _hz_to_mel(FREQ_MAX)
    mel_step = (mel_max - mel_min) / (MEL_FILTERS + 1)
    points = []
    for i in range(MEL_FILTERS + 2):
        hz = _mel_to_hz(mel_min + mel_step * i)
        bin_idx = int(round(hz * FRAME_SIZE / sample_rate))
        points.append(max(0, min(FRAME_SIZE // 2, bin_idx)))
    return [(points[m], points[m + 1], points[m + 2]) for m in range(MEL_FILTERS)]


_MEL_BINS = _mel_filter_bins()


def extract_frame(samples: np.ndarray, offset: int) -> np.ndarray:
    """One MFCC_COUNT-length feature vector (C1..C(MFCC_COUNT), C0 dropped)
    from exactly FRAME_SIZE samples starting at offset (zero-padded past
    the end of `samples`) -- mirrors mfcc_dsp.cpp's mfccExtractFrame()."""
    frame = np.zeros(FRAME_SIZE)
    end = min(offset + FRAME_SIZE, len(samples))
    if end > offset:
        frame[: end - offset] = samples[offset:end]

    window = 0.54 - 0.46 * np.cos(2.0 * np.pi * np.arange(FRAME_SIZE) / (FRAME_SIZE - 1))
    spectrum = fft_radix2((frame * window).astype(np.complex128))
    magnitude = np.abs(spectrum[: FRAME_SIZE // 2 + 1])

    mel_energy = np.zeros(MEL_FILTERS)
    for m, (left, center, right) in enumerate(_MEL_BINS):
        s = 0.0
        for k in range(left, center):
            w = (k - left) / (center - left) if center > left else 0.0
            s += magnitude[k] * w
        for k in range(center, right):
            w = (right - k) / (right - center) if right > center else 0.0
            s += magnitude[k] * w
        mel_energy[m] = np.log10(s + 1e-6)

    mfcc = np.zeros(MFCC_COUNT)
    for k in range(1, MFCC_COUNT + 1):  # C1..C(MFCC_COUNT), C0 dropped -- see mfcc_dsp.cpp
        m_idx = np.arange(MEL_FILTERS)
        mfcc[k - 1] = np.sum(mel_energy * np.cos(np.pi / MEL_FILTERS * (m_idx + 0.5) * k))
    return mfcc


def extract_sequence(samples: np.ndarray) -> np.ndarray:
    """All frames across `samples` at FRAME_HOP spacing, no endpointing --
    a simple full-signal helper for the self-tests below. Real onset/offset
    detection lives in keyword_detector.cpp / KeywordFeatures.kt, not here."""
    frames = []
    offset = 0
    while offset + FRAME_SIZE <= len(samples):
        frames.append(extract_frame(samples, offset))
        offset += FRAME_HOP
    return np.array(frames) if frames else np.zeros((0, MFCC_COUNT))


def extract_template_with_endpointing(samples: np.ndarray, max_frames: int = 60) -> np.ndarray:
    """Mirrors KeywordFeatures.kt's extractTemplate() (post-fix): a
    15th-percentile-over-the-whole-clip baseline (not seeded from frame 0
    and EMA-adapted, which turned out to be fragile -- see that file's own
    comment) plus the same ON/OFF energy-ratio state machine. Exists here
    so the fix can be sanity-checked against a synthetic worst case
    (see _verify_endpointing_fix below) without needing the phone or belt.
    """
    energy_on_ratio, energy_off_ratio, hang_frames, min_frames = 2.5, 1.5, 8, 6

    energies = []
    offset = 0
    while offset + FRAME_SIZE <= len(samples):
        frame = samples[offset:offset + FRAME_SIZE]
        energies.append(float(np.mean(frame.astype(np.float64) ** 2)))
        offset += FRAME_HOP
    if not energies:
        return np.zeros((0, MFCC_COUNT))
    baseline = max(np.percentile(energies, 15), 1e5)

    frames = []
    state = 0
    low_energy_streak = 0
    offset = 0
    i = 0
    while offset + FRAME_SIZE <= len(samples) and len(frames) < max_frames:
        energy = energies[i]
        if state == 0:
            if energy > baseline * energy_on_ratio:
                state = 1
                low_energy_streak = 0
                frames.append(extract_frame(samples, offset))
        else:
            frames.append(extract_frame(samples, offset))
            low_energy_streak = low_energy_streak + 1 if energy < baseline * energy_off_ratio else 0
            if low_energy_streak >= hang_frames:
                break
        offset += FRAME_HOP
        i += 1

    if len(frames) < min_frames:
        frames = []
        offset = 0
        while offset + FRAME_SIZE <= len(samples) and len(frames) < max_frames:
            frames.append(extract_frame(samples, offset))
            offset += FRAME_HOP

    return np.array(frames) if frames else np.zeros((0, MFCC_COUNT))


def apply_cmn(frames: np.ndarray) -> np.ndarray:
    """Cepstral mean normalization -- subtracts the sequence's own mean
    vector from every frame. Mirrors both firmware/app implementations."""
    if len(frames) == 0:
        return frames
    return frames - frames.mean(axis=0, keepdims=True)


def dtw_distance(a: np.ndarray, b: np.ndarray) -> float:
    """Length-normalized DTW cost between two (frames x MFCC_COUNT)
    sequences -- same DP formulation as src/dtw.cpp's dtwDistance()."""
    a_len, b_len = len(a), len(b)
    if a_len == 0 or b_len == 0:
        return float("inf")
    inf = float("inf")
    cost = np.full((a_len + 1, b_len + 1), inf)
    cost[0, 0] = 0.0
    for i in range(1, a_len + 1):
        for j in range(1, b_len + 1):
            local = np.linalg.norm(a[i - 1] - b[j - 1])
            cost[i, j] = local + min(cost[i - 1, j], cost[i, j - 1], cost[i - 1, j - 1])
    return cost[a_len, b_len] / (a_len + b_len)


# ---------------------------------------------------------------------
# Self-tests -- synthetic signals only, no recorded audio needed. Run with
# `python tools/mfcc_dtw.py`.
# ---------------------------------------------------------------------

def _verify_fft():
    rng = np.random.default_rng(42)
    max_err = 0.0
    for _ in range(20):
        x = rng.normal(size=FRAME_SIZE) + 1j * rng.normal(size=FRAME_SIZE) * 0  # real input, like real audio
        mine = fft_radix2(x)
        ref = np.fft.fft(x)
        err = np.max(np.abs(mine - ref)) / np.max(np.abs(ref))
        max_err = max(max_err, err)
    ok = max_err < 1e-9
    print(f"[{'PASS' if ok else 'FAIL'}] FFT vs numpy.fft.fft: max relative error = {max_err:.2e}")
    return ok


def _tone(freq, duration_s, amplitude=8000.0, sample_rate=SAMPLE_RATE):
    t = np.arange(int(duration_s * sample_rate)) / sample_rate
    return amplitude * np.sin(2 * np.pi * freq * t)


def _verify_dtw_sanity():
    results = []

    # Identical sequences -> ~0 distance.
    a = extract_sequence(_tone(440, 0.4))
    a_cmn = apply_cmn(a)
    d_identical = dtw_distance(a_cmn, a_cmn)
    ok1 = d_identical < 1e-3
    results.append(ok1)
    print(f"[{'PASS' if ok1 else 'FAIL'}] identical sequence DTW distance ~0: {d_identical:.4f}")

    # A slightly time-stretched version of the same tone -- DTW should
    # still call these close, since that's the entire point of DTW over
    # fixed-window Euclidean distance.
    b = extract_sequence(_tone(440, 0.5))  # 25% longer
    b_cmn = apply_cmn(b)
    d_stretched = dtw_distance(a_cmn, b_cmn)

    # A genuinely different tone (different pitch => different spectral
    # shape) should score clearly higher than the stretched-but-same-pitch
    # case.
    c = extract_sequence(_tone(1200, 0.4))
    c_cmn = apply_cmn(c)
    d_different = dtw_distance(a_cmn, c_cmn)

    ok2 = d_stretched < d_different
    results.append(ok2)
    print(f"[{'PASS' if ok2 else 'FAIL'}] time-stretched-same-tone ({d_stretched:.4f}) "
          f"< different-tone ({d_different:.4f})")

    return all(results)


def _rough_threshold_estimate():
    """NOT a substitute for the real silence-vs-speech test against actual
    hardware/microphones/room noise (see keyword_detector.cpp's
    MAX_MATCH_DISTANCE comment -- that number needs real data, and this
    can't provide it). This only sanity-checks the ORDER OF MAGNITUDE of
    the current guess using synthetic "speech-like" (a multi-tone burst,
    two independently-generated renditions to mimic two utterances of the
    same word) versus "silence-like" (low-amplitude noise) signals -- if
    the current guess were off by 10x or 100x, this would likely catch it;
    it cannot confirm the guess is actually right.
    """
    rng = np.random.default_rng(7)

    def word_like(seed_offset=0.0):
        # A short burst of a few overlapping tones with a slight amplitude
        # envelope, meant to loosely resemble a spoken word's mix of
        # formants + onset/decay shape far more than a pure sine does.
        duration = 0.45
        t = np.arange(int(duration * SAMPLE_RATE)) / SAMPLE_RATE
        envelope = np.sin(np.pi * t / duration) ** 2
        sig = np.zeros_like(t)
        for f in (280 + seed_offset, 850 + seed_offset * 2, 2200 + seed_offset * 3):
            sig += np.sin(2 * np.pi * f * t)
        return (sig * envelope * 3000).astype(np.float64)

    def silence_like():
        duration = 0.45
        n = int(duration * SAMPLE_RATE)
        return rng.normal(scale=300.0, size=n)  # quiet room noise floor, not true zero

    word_a = apply_cmn(extract_sequence(word_like(0.0)))
    word_b = apply_cmn(extract_sequence(word_like(15.0)))  # a "different utterance" of the same word-like signal
    silence_a = apply_cmn(extract_sequence(silence_like()))
    silence_b = apply_cmn(extract_sequence(silence_like()))

    d_same_word = dtw_distance(word_a, word_b)
    d_word_vs_silence = dtw_distance(word_a, silence_a)
    d_silence_vs_silence = dtw_distance(silence_a, silence_b)

    print()
    print("Rough order-of-magnitude estimate (synthetic signals, NOT a real threshold):")
    print(f"  same word-like signal, two renditions : {d_same_word:.3f}")
    print(f"  word-like vs silence-like              : {d_word_vs_silence:.3f}")
    print(f"  silence-like vs silence-like            : {d_silence_vs_silence:.3f}")
    print(f"  current MAX_MATCH_DISTANCE in firmware  : 2.0 (src/keyword_detector.cpp)")


def _verify_endpointing_fix():
    """The exact worst case that motivated switching from a frame-0-seeded
    EMA baseline to a whole-clip percentile baseline: a recording where the
    word starts immediately at sample 0 (no leading silence at all -- see
    KeywordFeatures.kt's comment on why KeywordEnrollScreen's timing makes
    this realistic, not a contrived edge case) followed by a quiet tail. If
    the fix works, this should end well before MAX_TEMPLATE_FRAMES; if it
    doesn't, this reproduces the "every enrollment silently hits the cap"
    symptom that was observed live before this fix existed.
    """
    rng = np.random.default_rng(3)
    max_frames = 60
    word_samples = _tone(500, 0.35, amplitude=6000.0) + _tone(1400, 0.35, amplitude=3000.0)
    quiet_tail = rng.normal(scale=250.0, size=int(1.0 * SAMPLE_RATE))  # ~1s of quiet room noise
    signal = np.concatenate([word_samples, quiet_tail])  # speech at sample 0, no leading silence

    template = extract_template_with_endpointing(signal, max_frames=max_frames)
    ok = 0 < len(template) < max_frames
    print(f"[{'PASS' if ok else 'FAIL'}] no-leading-silence recording ends before the "
          f"{max_frames}-frame cap: got {len(template)} frames")
    return ok


if __name__ == "__main__":
    fft_ok = _verify_fft()
    dtw_ok = _verify_dtw_sanity()
    endpointing_ok = _verify_endpointing_fix()
    _rough_threshold_estimate()
    print()
    print("All checks passed." if (fft_ok and dtw_ok and endpointing_ok) else "SOME CHECKS FAILED -- see above.")
