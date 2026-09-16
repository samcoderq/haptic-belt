package com.hapticbelt.app.data.training

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream

/**
 * Records a short labeled audio sample as a 16kHz mono 16-bit PCM WAV file --
 * 16kHz matches the belt firmware's AUDIO_SAMPLE_RATE (config.h), so samples
 * collected here reflect the same audio characteristics the ESP32 mic
 * pipeline actually captures. Used by both SoundTrainingScreen (environmental
 * sounds, longer clips) and KeywordEnrollScreen (spoken words, shorter clips).
 *
 * This class only records and writes a file -- no training or inference
 * happens here; that runs on-device elsewhere (GoertzelFeatures.kt /
 * KeywordFeatures.kt) once recording finishes.
 */
class AudioSampleRecorder(private val context: Context) {
    companion object {
        const val SAMPLE_RATE = 16000
        const val DEFAULT_MAX_DURATION_MS = 3000L
    }

    private var recordJob: Job? = null
    @Volatile private var recording = false

    fun hasPermission(): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED

    /**
     * Records until [maxDurationMs] elapses or [stop] is called, then writes
     * a WAV file to [outputFile] and invokes [onFinished] on the main thread
     * with whether it actually captured anything.
     */
    fun start(outputFile: File, maxDurationMs: Long = DEFAULT_MAX_DURATION_MS, onFinished: (success: Boolean) -> Unit) {
        if (!hasPermission()) {
            onFinished(false)
            return
        }
        val minBufSize = AudioRecord.getMinBufferSize(
            SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT
        )
        if (minBufSize <= 0) {
            onFinished(false)
            return
        }

        val record = try {
            AudioRecord(
                MediaRecorder.AudioSource.MIC,
                SAMPLE_RATE,
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
                minBufSize * 2
            )
        } catch (e: SecurityException) {
            onFinished(false)
            return
        }
        if (record.state != AudioRecord.STATE_INITIALIZED) {
            record.release()
            onFinished(false)
            return
        }

        recording = true
        recordJob = CoroutineScope(Dispatchers.IO).launch {
            val pcm = ByteArrayOutputStream()
            val buffer = ByteArray(minBufSize)
            record.startRecording()
            val startTime = System.currentTimeMillis()
            while (recording && (System.currentTimeMillis() - startTime) < maxDurationMs) {
                val read = record.read(buffer, 0, buffer.size)
                if (read > 0) pcm.write(buffer, 0, read)
            }
            record.stop()
            record.release()

            val pcmBytes = pcm.toByteArray()
            val success = pcmBytes.isNotEmpty() && try {
                writeWavFile(outputFile, pcmBytes)
                true
            } catch (e: Exception) {
                false
            }
            withContext(Dispatchers.Main) { onFinished(success) }
        }
    }

    /** Stops early -- the natural way to end a sample shorter than MAX_DURATION_MS. */
    fun stop() {
        recording = false
    }

    private fun writeWavFile(file: File, pcmData: ByteArray) {
        FileOutputStream(file).use { out ->
            val byteRate = SAMPLE_RATE * 2 // 16-bit mono
            val header = ByteArray(44)

            fun writeString(offset: Int, s: String) {
                for (i in s.indices) header[offset + i] = s[i].code.toByte()
            }
            fun writeIntLE(offset: Int, v: Int) {
                header[offset] = (v and 0xff).toByte()
                header[offset + 1] = ((v shr 8) and 0xff).toByte()
                header[offset + 2] = ((v shr 16) and 0xff).toByte()
                header[offset + 3] = ((v shr 24) and 0xff).toByte()
            }
            fun writeShortLE(offset: Int, v: Int) {
                header[offset] = (v and 0xff).toByte()
                header[offset + 1] = ((v shr 8) and 0xff).toByte()
            }

            writeString(0, "RIFF")
            writeIntLE(4, pcmData.size + 36)
            writeString(8, "WAVE")
            writeString(12, "fmt ")
            writeIntLE(16, 16)   // PCM fmt chunk size
            writeShortLE(20, 1)  // PCM format tag
            writeShortLE(22, 1)  // mono
            writeIntLE(24, SAMPLE_RATE)
            writeIntLE(28, byteRate)
            writeShortLE(32, 2)  // block align (16-bit mono)
            writeShortLE(34, 16) // bits per sample
            writeString(36, "data")
            writeIntLE(40, pcmData.size)

            out.write(header)
            out.write(pcmData)
        }
    }
}
