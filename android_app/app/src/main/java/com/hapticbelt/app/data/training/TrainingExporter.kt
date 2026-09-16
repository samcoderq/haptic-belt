package com.hapticbelt.app.data.training

import android.content.Context
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * Packages every recorded class/sample into one zip (className/sample.wav
 * per entry) so it can be pulled off the phone for offline training -- see
 * the plan: this app only collects labeled samples, it does not train
 * anything itself.
 */
object TrainingExporter {
    fun exportZip(context: Context): File {
        val exportDir = File(context.cacheDir, "training_export").apply { mkdirs() }
        // Clear previous exports so the cache doesn't grow unbounded across repeated exports.
        exportDir.listFiles()?.forEach { it.delete() }

        val zipFile = File(exportDir, "sound_training_${System.currentTimeMillis()}.zip")
        val root = TrainingStore.rootDir(context)

        ZipOutputStream(FileOutputStream(zipFile)).use { zos ->
            root.walkTopDown().filter { it.isFile }.forEach { file ->
                val entryName = root.toPath().relativize(file.toPath()).toString().replace('\\', '/')
                zos.putNextEntry(ZipEntry(entryName))
                file.inputStream().use { it.copyTo(zos) }
                zos.closeEntry()
            }
        }
        return zipFile
    }
}
