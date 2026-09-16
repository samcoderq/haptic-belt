package com.hapticbelt.app.data.training

import android.content.Context
import java.io.File

data class SoundClassSummary(val className: String, val sampleCount: Int)

/**
 * Recorded samples live under filesDir/sound_training/<className>/, one WAV
 * file per sample -- the directory structure IS the label, so there's no
 * separate manifest to keep in sync; listing files on disk is the source
 * of truth.
 */
object TrainingStore {
    private const val ROOT_DIR = "sound_training"

    fun rootDir(context: Context): File =
        File(context.filesDir, ROOT_DIR).apply { mkdirs() }

    fun classDir(context: Context, className: String): File =
        File(rootDir(context), sanitize(className)).apply { mkdirs() }

    fun newSampleFile(context: Context, className: String): File =
        File(classDir(context, className), "sample_${System.currentTimeMillis()}.wav")

    fun listClasses(context: Context): List<SoundClassSummary> {
        return rootDir(context).listFiles { f -> f.isDirectory }
            ?.map { dir ->
                val count = dir.listFiles { f -> f.extension == "wav" }?.size ?: 0
                SoundClassSummary(dir.name, count)
            }
            ?.sortedBy { it.className }
            ?: emptyList()
    }

    fun deleteClass(context: Context, className: String) {
        classDir(context, className).deleteRecursively()
    }

    /** Directory names can't contain arbitrary characters -- keep it to what's readable and safe. */
    fun sanitize(name: String): String =
        name.trim().replace(Regex("[^A-Za-z0-9 _-]"), "").ifBlank { "unnamed" }
}
