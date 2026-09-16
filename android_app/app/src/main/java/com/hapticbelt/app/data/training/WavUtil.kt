package com.hapticbelt.app.data.training

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Reads back the 16kHz mono 16-bit PCM WAV files AudioSampleRecorder
 * writes. Trusts the fixed 44-byte header that recorder always produces
 * (this app controls both the writer and reader) rather than parsing RIFF
 * chunks generically.
 */
object WavUtil {
    private const val HEADER_SIZE = 44

    fun readSamples(file: File): ShortArray {
        val bytes = file.readBytes()
        if (bytes.size <= HEADER_SIZE) return ShortArray(0)
        val pcmBytes = bytes.size - HEADER_SIZE
        val buffer = ByteBuffer.wrap(bytes, HEADER_SIZE, pcmBytes).order(ByteOrder.LITTLE_ENDIAN)
        val samples = ShortArray(pcmBytes / 2)
        for (i in samples.indices) samples[i] = buffer.short
        return samples
    }
}
