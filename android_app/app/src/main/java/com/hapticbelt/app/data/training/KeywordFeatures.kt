package com.hapticbelt.app.data.training

import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.log10
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sqrt

/**
 * MFCC feature extraction for spoken keyword/name enrollment -- rewritten
 * (2026-09-15) from the original Goertzel-power-ratio-bins + fixed-7-segment
 * approach after a controlled real-hardware test showed that approach
 * couldn't separate real speech from silence at all (their distance
 * distributions almost entirely overlapped -- see keyword_detector.h's
 * history comment for the full story). This must stay a close mirror of
 * src/mfcc_dsp.cpp's algorithm (same frame size/hop, same mel filterbank,
 * same DCT, same C0-drop, same cepstral mean normalization) since a
 * template computed here is later compared, on the belt, against live
 * audio processed by that C++ implementation -- the two don't need
 * bit-identical output, but the same recipe, or the DTW distance between
 * them is meaningless. Matching itself (DTW) only happens on the belt now
 * (dtw.h/.cpp) -- this file used to also do DTW for medoid template
 * selection, but that was removed once every enrollment recording started
 * being uploaded and matched as its own template instead of picking one
 * "representative" recording (see BeltRepository.kt's uploadKeyword and
 * keyword_store.h's history comment for why).
 *
 * MFCC borrows the feature-extraction idea (not the trained-CNN classifier,
 * which needs a labeled dataset this project doesn't have) from
 * https://github.com/lbalic21/keyword_spotting; MFCC+DTW together is the
 * standard technique for few-shot custom keyword spotting with no training
 * pipeline, per current research (e.g. "Multi-Sample Dynamic Time Warping
 * for Few-Shot Keyword Spotting", EUSIPCO 2024).
 */
object KeywordFeatures {
    const val SAMPLE_RATE = GoertzelFeatures.SAMPLE_RATE // 16000
    const val FRAME_SIZE = 512  // 32ms @ 16kHz, power-of-two for the FFT
    const val FRAME_HOP = 384   // 24ms @ 16kHz
    const val MEL_FILTERS = 40
    const val MFCC_COUNT = 13
    const val FREQ_MIN = 80.0
    const val FREQ_MAX = 7600.0

    // Longest utterance this will extract -- matches src/keyword_store.h's
    // KEYWORD_MAX_TEMPLATE_FRAMES exactly (the belt clamps to the same
    // limit on upload; keeping them equal avoids ever silently truncating
    // what was enrolled). Shrunk 60->40 2026-09-16 -- see that file's
    // history comment: the belt's NVS partition couldn't actually hold
    // KEYWORD_MAX_COUNT keywords at the old size without silently failing
    // to persist.
    const val MAX_TEMPLATE_FRAMES = 40

    data class KeywordTemplate(val mfcc: FloatArray, val frameCount: Int)

    // ---- FFT (radix-2 Cooley-Tukey, iterative, in-place) ----
    // Mirrors src/mfcc_dsp.cpp's fft() exactly in structure; Kotlin/JVM has
    // no reason to hand-optimize this the way an embedded target might, so
    // it's the same straightforward textbook implementation on both sides.

    private fun bitReverse(i: Int, bits: Int): Int {
        var x = i
        var rev = 0
        for (b in 0 until bits) {
            rev = (rev shl 1) or (x and 1)
            x = x shr 1
        }
        return rev
    }

    private val log2FrameSize = (0 until 32).first { (1 shl it) == FRAME_SIZE }

    private fun fft(re: DoubleArray, im: DoubleArray) {
        val n = FRAME_SIZE
        for (i in 0 until n) {
            val j = bitReverse(i, log2FrameSize)
            if (j > i) {
                val tr = re[i]; re[i] = re[j]; re[j] = tr
                val ti = im[i]; im[i] = im[j]; im[j] = ti
            }
        }
        var size = 2
        while (size <= n) {
            val half = size / 2
            val step = n / size
            var start = 0
            while (start < n) {
                for (k in 0 until half) {
                    val theta = 2.0 * PI * (k * step) / n
                    val wr = cos(theta)
                    val wi = -kotlin.math.sin(theta)
                    val a = start + k
                    val b = a + half
                    val br = re[b] * wr - im[b] * wi
                    val bi = re[b] * wi + im[b] * wr
                    re[b] = re[a] - br
                    im[b] = im[a] - bi
                    re[a] = re[a] + br
                    im[a] = im[a] + bi
                }
                start += size
            }
            size = size shl 1
        }
    }

    // ---- Mel filterbank (computed once, same triangular-filter recipe as mfcc_dsp.cpp) ----

    private fun hzToMel(hz: Double) = 2595.0 * log10(1.0 + hz / 700.0)
    private fun melToHz(mel: Double) = 700.0 * (10.0.pow(mel / 2595.0) - 1.0)

    private data class MelFilter(val left: Int, val center: Int, val right: Int)

    private val melFilters: List<MelFilter> by lazy {
        val melMin = hzToMel(FREQ_MIN)
        val melMax = hzToMel(FREQ_MAX)
        val melStep = (melMax - melMin) / (MEL_FILTERS + 1)
        val points = (0 until MEL_FILTERS + 2).map { i ->
            val hz = melToHz(melMin + melStep * i)
            (hz * FRAME_SIZE / SAMPLE_RATE).roundToInt().coerceIn(0, FRAME_SIZE / 2)
        }
        (0 until MEL_FILTERS).map { m -> MelFilter(points[m], points[m + 1], points[m + 2]) }
    }

    /** One MFCC_COUNT-length feature vector (C1..C(MFCC_COUNT), C0 dropped -- see mfcc_dsp.cpp) from exactly FRAME_SIZE samples. */
    private fun extractFrame(samples: ShortArray, offset: Int): FloatArray {
        val re = DoubleArray(FRAME_SIZE)
        val im = DoubleArray(FRAME_SIZE)
        for (i in 0 until FRAME_SIZE) {
            val w = 0.54 - 0.46 * cos(2.0 * PI * i / (FRAME_SIZE - 1))
            val idx = offset + i
            val sample = if (idx < samples.size) samples[idx].toDouble() else 0.0
            re[i] = sample * w
            im[i] = 0.0
        }
        fft(re, im)

        val magnitude = DoubleArray(FRAME_SIZE / 2 + 1) { k -> sqrt(re[k] * re[k] + im[k] * im[k]) }

        val melEnergy = DoubleArray(MEL_FILTERS) { m ->
            val (left, center, right) = melFilters[m]
            var sum = 0.0
            for (k in left until center) {
                val w = if (center > left) (k - left).toDouble() / (center - left) else 0.0
                sum += magnitude[k] * w
            }
            for (k in center until right) {
                val w = if (right > center) (right - k).toDouble() / (right - center) else 0.0
                sum += magnitude[k] * w
            }
            log10(sum + 1e-6)
        }

        return FloatArray(MFCC_COUNT) { kIdx ->
            val k = kIdx + 1 // C1..C(MFCC_COUNT), C0 dropped -- see mfcc_dsp.cpp's DCT loop comment
            var sum = 0.0
            for (m in 0 until MEL_FILTERS) {
                sum += melEnergy[m] * cos(PI / MEL_FILTERS * (m + 0.5) * k)
            }
            sum.toFloat()
        }
    }

    private fun frameEnergy(samples: ShortArray, offset: Int): Double {
        var sum = 0.0
        val end = minOf(offset + FRAME_SIZE, samples.size)
        for (i in offset until end) sum += samples[i].toDouble() * samples[i]
        return sum / FRAME_SIZE
    }

    /**
     * Extracts a variable-length MFCC sequence spanning the detected speech
     * utterance within [samples] (a full recording, e.g. ~1.8s from
     * AudioSampleRecorder) -- energy-based onset AND offset detection this
     * time, not just onset: the old fixed-length approach always took
     * exactly WINDOW_SAMPLES regardless of how long the word actually took,
     * which is part of what made matching unreliable. Mirrors
     * src/keyword_detector.cpp's own endpointing logic (baseline EMA,
     * ON/OFF energy ratios, hang time) closely enough to produce comparable
     * templates, though it runs once over a whole recording here instead of
     * streaming.
     */
    fun extractTemplate(samples: ShortArray): KeywordTemplate {
        val energyOnRatio = 2.5
        val energyOffRatio = 1.5
        val hangFrames = 8
        val minFrames = 6

        // Baseline is a robust (15th-percentile) estimate over the WHOLE
        // recording, computed upfront, rather than seeded from frame 0 and
        // adaptively EMA-updated the way the firmware's streaming version
        // has to be (keyword_detector.cpp genuinely can't look ahead).
        // Frame 0 turned out to be a fragile seed: KeywordEnrollScreen's
        // enrollment flow delays (PAUSE_BETWEEN_REPS_MS) BEFORE starting
        // the recorder, not while it's already running, so the recording
        // buffer itself has little to no guaranteed leading silence -- if
        // frame 0 happened to already be mid-speech, the old EMA baseline
        // would start biased high, weakening onset detection and making it
        // and every subsequent enrollment fall through to the
        // always-exactly-MAX_TEMPLATE_FRAMES fallback below. A percentile
        // over every frame in the clip doesn't depend on any one frame
        // being representative.
        val allEnergies = ArrayList<Double>()
        var scanOffset = 0
        while (scanOffset + FRAME_SIZE <= samples.size) {
            allEnergies.add(frameEnergy(samples, scanOffset))
            scanOffset += FRAME_HOP
        }
        val baseline = if (allEnergies.isEmpty()) 1e5 else {
            allEnergies.sorted()[(allEnergies.size * 0.15).toInt().coerceIn(0, allEnergies.size - 1)]
        }.coerceAtLeast(1e5)

        var state = 0 // 0 = idle, 1 = speaking
        var lowEnergyStreak = 0
        val frames = mutableListOf<FloatArray>()

        var offset = 0
        while (offset + FRAME_SIZE <= samples.size && frames.size < MAX_TEMPLATE_FRAMES) {
            val energy = frameEnergy(samples, offset)
            if (state == 0) {
                if (energy > baseline * energyOnRatio) {
                    state = 1
                    lowEnergyStreak = 0
                    frames.add(extractFrame(samples, offset))
                }
            } else {
                frames.add(extractFrame(samples, offset))
                lowEnergyStreak = if (energy < baseline * energyOffRatio) lowEnergyStreak + 1 else 0
                if (lowEnergyStreak >= hangFrames) break
            }
            offset += FRAME_HOP
        }

        if (frames.size < minFrames) {
            // Nothing clearly detected (e.g. a very quiet recording) --
            // fall back to the first MAX_TEMPLATE_FRAMES worth of audio
            // rather than returning an empty/too-short template that would
            // just get rejected as unusable by the caller.
            frames.clear()
            offset = 0
            while (offset + FRAME_SIZE <= samples.size && frames.size < MAX_TEMPLATE_FRAMES) {
                frames.add(extractFrame(samples, offset))
                offset += FRAME_HOP
            }
        }

        val frameCount = frames.size
        val mfcc = FloatArray(frameCount * MFCC_COUNT)
        for (f in 0 until frameCount) frames[f].copyInto(mfcc, f * MFCC_COUNT)
        applyCepstralMeanNormalization(mfcc, frameCount)
        return KeywordTemplate(mfcc, frameCount)
    }

    /** Subtracts the utterance's own mean MFCC vector from every frame -- see mfcc_dsp.cpp's mirror for why. */
    private fun applyCepstralMeanNormalization(frames: FloatArray, frameCount: Int) {
        if (frameCount == 0) return
        val mean = FloatArray(MFCC_COUNT)
        for (f in 0 until frameCount) {
            for (c in 0 until MFCC_COUNT) mean[c] += frames[f * MFCC_COUNT + c]
        }
        for (c in 0 until MFCC_COUNT) mean[c] = mean[c] / frameCount
        for (f in 0 until frameCount) {
            for (c in 0 until MFCC_COUNT) frames[f * MFCC_COUNT + c] -= mean[c]
        }
    }

}
