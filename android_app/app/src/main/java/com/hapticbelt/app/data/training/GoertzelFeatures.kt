package com.hapticbelt.app.data.training

import kotlin.math.cos
import kotlin.math.PI

/**
 * Kotlin port of tools/goertzel.py's feature extraction -- the third of
 * three implementations (Python for the offline trainer, C in
 * src/classifier_interface.cpp for on-device inference, this one for
 * computing a centroid directly on the phone from recorded samples). All
 * three must compute the same feature vector shape for the same audio, or a
 * centroid trained on one doesn't mean anything compared against features
 * extracted on another.
 *
 * Goertzel, not a full FFT: detecting energy at a small fixed set of known
 * frequencies (not a full spectrum) is exactly what Goertzel is for -- the
 * same technique DTMF touch-tone decoders use. See tools/goertzel.py for
 * the full rationale.
 */
object GoertzelFeatures {
    const val SAMPLE_RATE = 16000
    const val BLOCK_SIZE = 1024 // 64ms @ 16kHz

    // Must match tools/goertzel.py's TARGET_FREQS and
    // src/classifier_store.cpp's CLASSIFIER_TARGET_FREQS exactly.
    val TARGET_FREQS = floatArrayOf(
        200f, 300f, 400f, 550f, 700f, 900f, 1100f, 1400f,
        1700f, 2100f, 2600f, 3200f, 3900f, 4700f, 5700f, 7000f
    )

    private fun goertzelPower(block: DoubleArray, freq: Float, sampleRate: Int = SAMPLE_RATE): Double {
        val n = block.size
        val k = Math.round(n * freq / sampleRate)
        val w = 2.0 * PI * k / n
        val coeff = 2.0 * cos(w)
        var sPrev = 0.0
        var sPrev2 = 0.0
        for (x in block) {
            val s = x + coeff * sPrev - sPrev2
            sPrev2 = sPrev
            sPrev = s
        }
        return sPrev2 * sPrev2 + sPrev * sPrev - coeff * sPrev * sPrev2
    }

    /**
     * samples: raw PCM (16-bit signed range, but any numeric magnitude is
     * fine since the result is L1-normalized). Returns a
     * TARGET_FREQS.size-length normalized vector (relative spectral shape,
     * amplitude/loudness-invariant) -- an all-zero vector for silence,
     * matching the same convention in the Python and C implementations.
     */
    fun extractFeatures(samples: ShortArray): FloatArray {
        val block = DoubleArray(BLOCK_SIZE) { i -> if (i < samples.size) samples[i].toDouble() else 0.0 }
        val powers = DoubleArray(TARGET_FREQS.size) { i -> goertzelPower(block, TARGET_FREQS[i]) }
        val total = powers.sum()
        return if (total <= 1e-6) {
            FloatArray(TARGET_FREQS.size)
        } else {
            FloatArray(TARGET_FREQS.size) { i -> (powers[i] / total).toFloat() }
        }
    }

    /** Mean feature vector across multiple recordings of the same class. */
    fun computeCentroid(sampleVectors: List<FloatArray>): FloatArray {
        val n = TARGET_FREQS.size
        val sum = FloatArray(n)
        for (vec in sampleVectors) {
            for (i in 0 until n) sum[i] += vec[i]
        }
        val count = sampleVectors.size.coerceAtLeast(1)
        return FloatArray(n) { i -> sum[i] / count }
    }
}
