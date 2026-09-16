package com.hapticbelt.app.ui.screens

import android.Manifest
import android.content.Intent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import com.hapticbelt.app.data.BeltRepository
import com.hapticbelt.app.data.training.AudioSampleRecorder
import com.hapticbelt.app.data.training.GoertzelFeatures
import com.hapticbelt.app.data.training.SoundClassSummary
import com.hapticbelt.app.data.training.TrainingExporter
import com.hapticbelt.app.data.training.TrainingStore
import com.hapticbelt.app.data.training.WavUtil
import kotlinx.coroutines.launch

// Below this many samples, a centroid is closer to noise than a class
// average -- matches tools/train_classifier.py's MIN_SAMPLES_PER_CLASS, kept
// in sync by convention, not by shared code (different languages/files).
private const val MIN_SAMPLES_TO_TRAIN = 3

/**
 * Collects labeled sound samples (e.g. "smoke alarm", "doorbell") and trains
 * a real nearest-centroid classifier entirely on-device -- no laptop, no
 * reflash. "Train & Sync" reads this class's recorded WAVs, computes their
 * Goertzel feature centroid right here in Kotlin (GoertzelFeatures.kt, the
 * same spec src/classifier_interface.cpp runs on the belt), and sends it
 * over BLE to the belt's flash-backed classifier_store. "Export" is a
 * separate, optional path for pulling the raw WAVs off the phone (backup,
 * or advanced offline training) -- it is not required for the classifier
 * to work.
 */
@Composable
fun SoundTrainingScreen(repository: BeltRepository) {
    val context = LocalContext.current
    val recorder = remember { AudioSampleRecorder(context) }
    val scope = rememberCoroutineScope()

    var hasPermission by remember { mutableStateOf(recorder.hasPermission()) }
    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted -> hasPermission = granted }

    var classes by remember { mutableStateOf(TrainingStore.listClasses(context)) }
    var newClassName by remember { mutableStateOf("") }
    var recordingClass by remember { mutableStateOf<String?>(null) }
    var syncStatus by remember { mutableStateOf<Pair<String, String>?>(null) } // className to status text

    fun refresh() { classes = TrainingStore.listClasses(context) }

    val totalSamples = classes.sumOf { it.sampleCount }

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("Sound training", style = MaterialTheme.typography.titleMedium)
                    Text(
                        "Record a few samples of an important sound (smoke alarm, doorbell, your dog barking...), " +
                            "then tap Train & Sync to send it straight to the belt -- computed on this phone, " +
                            "no laptop and no reflash needed.",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color.Gray
                    )
                    if (!hasPermission) {
                        Text(
                            "Microphone access is needed to record samples.",
                            style = MaterialTheme.typography.bodySmall,
                            color = Color(0xFFCF6679)
                        )
                        Button(onClick = { permissionLauncher.launch(Manifest.permission.RECORD_AUDIO) }) {
                            Text("Grant microphone access")
                        }
                    }
                }
            }
        }

        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(16.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedTextField(
                        value = newClassName,
                        onValueChange = { newClassName = it },
                        label = { Text("New sound class") },
                        modifier = Modifier.weight(1f),
                        singleLine = true
                    )
                    Button(
                        onClick = {
                            if (newClassName.isNotBlank()) {
                                TrainingStore.classDir(context, newClassName)
                                newClassName = ""
                                refresh()
                            }
                        }
                    ) {
                        Text("Add")
                    }
                }
            }
        }

        if (classes.isEmpty()) {
            item {
                Text(
                    "No sound classes yet -- add one above to start recording samples.",
                    style = MaterialTheme.typography.bodySmall,
                    color = Color.Gray
                )
            }
        }

        items(classes, key = { it.className }) { summary ->
            SoundClassRow(
                summary = summary,
                isRecording = recordingClass == summary.className,
                recordingDisabled = recordingClass != null && recordingClass != summary.className,
                hasPermission = hasPermission,
                syncStatusText = syncStatus?.takeIf { it.first == summary.className }?.second,
                onRecordToggle = {
                    if (recordingClass == summary.className) {
                        recorder.stop()
                    } else if (recordingClass == null) {
                        recordingClass = summary.className
                        val file = TrainingStore.newSampleFile(context, summary.className)
                        recorder.start(file) { success ->
                            recordingClass = null
                            if (!success) file.delete()
                            refresh()
                        }
                    }
                },
                onDelete = {
                    TrainingStore.deleteClass(context, summary.className)
                    refresh()
                },
                onTrainAndSync = {
                    val wavFiles = TrainingStore.classDir(context, summary.className)
                        .listFiles { f -> f.extension == "wav" }
                        ?.toList().orEmpty()
                    val featureVectors = wavFiles.map { GoertzelFeatures.extractFeatures(WavUtil.readSamples(it)) }
                    val centroid = GoertzelFeatures.computeCentroid(featureVectors)
                    syncStatus = summary.className to "Sending..."
                    scope.launch {
                        val sent = repository.uploadTrainedClass(summary.className, centroid)
                        syncStatus = summary.className to if (sent) {
                            "Sent to belt (${featureVectors.size} samples averaged)"
                        } else {
                            "Failed to reach the belt -- check it's connected and try again"
                        }
                    }
                }
            )
        }

        item {
            Button(
                enabled = totalSamples > 0,
                modifier = Modifier.fillMaxWidth(),
                onClick = {
                    val zip = TrainingExporter.exportZip(context)
                    val uri = FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", zip)
                    val intent = Intent(Intent.ACTION_SEND).apply {
                        type = "application/zip"
                        putExtra(Intent.EXTRA_STREAM, uri)
                        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    }
                    context.startActivity(Intent.createChooser(intent, "Export sound training data"))
                }
            ) {
                Text("Export raw samples ($totalSamples) -- optional backup")
            }
        }
    }
}

@Composable
private fun SoundClassRow(
    summary: SoundClassSummary,
    isRecording: Boolean,
    recordingDisabled: Boolean,
    hasPermission: Boolean,
    syncStatusText: String?,
    onRecordToggle: () -> Unit,
    onDelete: () -> Unit,
    onTrainAndSync: () -> Unit
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Column {
                    Text(summary.className, style = MaterialTheme.typography.bodyLarge)
                    Text(
                        "${summary.sampleCount} sample${if (summary.sampleCount == 1) "" else "s"}" +
                            if (isRecording) " -- recording..." else "",
                        style = MaterialTheme.typography.bodySmall,
                        color = if (isRecording) Color(0xFFCF6679) else Color.Gray
                    )
                }
                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    TextButton(onClick = onDelete) { Text("Delete") }
                    Button(enabled = hasPermission && !recordingDisabled, onClick = onRecordToggle) {
                        Text(if (isRecording) "Stop" else "Record")
                    }
                }
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text(
                    syncStatusText ?: if (summary.sampleCount < MIN_SAMPLES_TO_TRAIN) {
                        "Needs >= $MIN_SAMPLES_TO_TRAIN samples to train"
                    } else {
                        ""
                    },
                    style = MaterialTheme.typography.bodySmall,
                    color = if (syncStatusText != null) Color(0xFF2E7D32) else Color.Gray
                )
                Button(
                    enabled = summary.sampleCount >= MIN_SAMPLES_TO_TRAIN,
                    onClick = onTrainAndSync
                ) {
                    Text("Train & Sync to belt")
                }
            }
        }
    }
}
