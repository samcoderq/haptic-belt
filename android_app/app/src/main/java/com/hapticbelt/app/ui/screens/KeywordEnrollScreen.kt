package com.hapticbelt.app.ui.screens

import android.Manifest
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
import androidx.compose.material3.LinearProgressIndicator
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
import com.hapticbelt.app.data.BeltRepository
import com.hapticbelt.app.data.training.AudioSampleRecorder
import com.hapticbelt.app.data.training.EnrolledKeywordsStore
import com.hapticbelt.app.data.training.KeywordFeatures
import com.hapticbelt.app.data.training.WavUtil
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import java.io.File

// Must equal BleProtocol.KEYWORD_TEMPLATES_PER_WORD exactly -- a 4th/5th
// recording beyond that limit would make encodeKeywordUploadSegments()
// return null and fail the WHOLE upload (see that constant's history
// comment for why this shrunk from 5 to 3 on 2026-09-16).
private const val ENROLLMENT_REPS = 3
private const val REP_DURATION_MS = 1800L
private const val PAUSE_BETWEEN_REPS_MS = 700L

/**
 * "Say your word 5 times" enrollment for name/keyword detection -- a
 * different technique from SoundTrainingScreen's sound classes (spectral
 * shape): a keyword's identity is in its temporal/phonetic pattern, so this
 * records several repetitions, extracts a multi-segment template from each
 * (KeywordFeatures.kt), averages them, and uploads the result to the belt
 * over BLE -- same no-laptop, no-reflash architecture as sound classes,
 * just a different upload characteristic and a bigger payload.
 *
 * Real, not simulated: recording is AudioSampleRecorder over AudioRecord,
 * matching. What's unverified as of this writing: whether the belt's
 * on-device Euclidean-distance matching (keyword_detector.cpp, a documented
 * simplification of full DTW) actually recognizes speech reliably -- no
 * belt has been connected to test against.
 */
@Composable
fun KeywordEnrollScreen(repository: BeltRepository) {
    val context = LocalContext.current
    val recorder = remember { AudioSampleRecorder(context) }
    val scope = rememberCoroutineScope()

    var hasPermission by remember { mutableStateOf(recorder.hasPermission()) }
    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted -> hasPermission = granted }

    var keywordName by remember { mutableStateOf("") }
    var isEnrolling by remember { mutableStateOf(false) }
    var currentRep by remember { mutableStateOf(0) } // 0 = not recording, 1..ENROLLMENT_REPS while recording that rep
    var statusText by remember { mutableStateOf<String?>(null) }
    var statusSucceeded by remember { mutableStateOf(true) }
    var enrolledKeywords by remember { mutableStateOf(EnrolledKeywordsStore.getAll(context)) }
    var removingKeyword by remember { mutableStateOf<String?>(null) }
    var removeError by remember { mutableStateOf<String?>(null) }

    suspend fun recordOneRep(outputFile: File): Boolean = suspendCancellableCoroutine { cont ->
        recorder.start(outputFile, REP_DURATION_MS) { success ->
            if (cont.isActive) cont.resumeWith(Result.success(success))
        }
        cont.invokeOnCancellation { recorder.stop() }
    }

    fun startEnrollment() {
        if (keywordName.isBlank() || isEnrolling) return
        isEnrolling = true
        statusText = null
        scope.launch {
            val tempDir = File(context.cacheDir, "keyword_enroll").apply { mkdirs() }
            val templates = mutableListOf<KeywordFeatures.KeywordTemplate>()
            for (rep in 1..ENROLLMENT_REPS) {
                currentRep = rep
                delay(PAUSE_BETWEEN_REPS_MS)
                val file = File(tempDir, "rep_$rep.wav")
                val success = recordOneRep(file)
                if (success) {
                    val samples = WavUtil.readSamples(file)
                    templates.add(KeywordFeatures.extractTemplate(samples))
                    file.delete()
                }
            }
            currentRep = 0
            isEnrolling = false

            if (templates.size < 3) {
                statusSucceeded = false
                statusText = "Only captured ${templates.size}/${ENROLLMENT_REPS} usable recordings -- try again"
                return@launch
            }
            // Every successfully-captured recording is uploaded and stored
            // as its own separate template now, not reduced to one
            // "representative" recording first -- see BeltRepository.kt's
            // uploadKeyword doc and keyword_store.h's history comment for
            // why (discarding the other recordings' pronunciation
            // variability was flagged as the highest-priority fix after
            // live testing kept showing one-template-per-word struggling
            // with near-duplicate keywords).
            val sent = repository.uploadKeyword(keywordName, templates.map { it.mfcc to it.frameCount })
            if (sent) {
                // Only recorded locally on a confirmed write -- EnrolledKeywordsStore's
                // insertion order is used to map a belt-reported keyword slot back to a
                // name (see its nameForSlot()), so adding an entry for a write that never
                // actually reached the belt would desync every slot after it.
                EnrolledKeywordsStore.add(context, keywordName)
                enrolledKeywords = EnrolledKeywordsStore.getAll(context)
                statusSucceeded = true
                statusText = "Sent '$keywordName' to belt (${templates.size}/${ENROLLMENT_REPS} recordings, ${templates.joinToString(", ") { "${it.frameCount}f" }})"
            } else {
                statusSucceeded = false
                statusText = "Failed to reach the belt -- check it's connected and try again"
            }
        }
    }

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text("Keyword / name detection", style = MaterialTheme.typography.titleMedium)
                    Text(
                        "Say a name or word 5 times to enroll it. This is a different technique from sound " +
                            "training (it matches the word's spoken pattern, not a sound's timbre) -- computed " +
                            "on this phone and sent to the belt, no laptop needed.",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color.Gray
                    )
                    Text(
                        "A match triggers a real LED flash on the belt (not yet real motors) and shows up on the " +
                            "Dashboard's Keyword card -- but matching quality against real speech is still unproven.",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color(0xFFCF6679)
                    )
                    if (!hasPermission) {
                        Button(onClick = { permissionLauncher.launch(Manifest.permission.RECORD_AUDIO) }) {
                            Text("Grant microphone access")
                        }
                    }
                }
            }
        }

        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    OutlinedTextField(
                        value = keywordName,
                        onValueChange = { keywordName = it },
                        label = { Text("Word or name to detect") },
                        singleLine = true,
                        enabled = !isEnrolling,
                        modifier = Modifier.fillMaxWidth()
                    )

                    if (isEnrolling) {
                        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                            Text(
                                "Say \"$keywordName\" now! ($currentRep / $ENROLLMENT_REPS)",
                                style = MaterialTheme.typography.titleMedium,
                                color = Color(0xFFCF6679)
                            )
                            LinearProgressIndicator(
                                progress = { currentRep / ENROLLMENT_REPS.toFloat() },
                                modifier = Modifier.fillMaxWidth()
                            )
                        }
                    } else {
                        Button(
                            enabled = hasPermission && keywordName.isNotBlank(),
                            onClick = { startEnrollment() },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text("Start enrollment (say it $ENROLLMENT_REPS times)")
                        }
                    }

                    statusText?.let {
                        Text(
                            it,
                            style = MaterialTheme.typography.bodySmall,
                            color = if (statusSucceeded) Color(0xFF2E7D32) else Color(0xFFCF6679)
                        )
                    }
                }
            }
        }

        item {
            Text(
                "Enrolled keywords (${enrolledKeywords.size})",
                style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.padding(top = 4.dp)
            )
        }

        if (enrolledKeywords.isEmpty()) {
            item {
                Text(
                    "None enrolled yet -- finish an enrollment above to see it listed here.",
                    style = MaterialTheme.typography.bodySmall,
                    color = Color.Gray
                )
            }
        } else {
            item {
                Text(
                    "This is this phone's own record of what it has sent -- \"sent\" isn't the same as " +
                        "\"confirmed stored\" (uploads aren't acknowledged). Removing one does send a real " +
                        "delete to the belt (not just this list) and waits for it to confirm before updating " +
                        "this list.",
                    style = MaterialTheme.typography.bodySmall,
                    color = Color.Gray
                )
            }
            removeError?.let {
                item {
                    Text(it, style = MaterialTheme.typography.bodySmall, color = Color(0xFFCF6679))
                }
            }
            items(enrolledKeywords, key = { it }) { name ->
                Card(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(12.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(name, style = MaterialTheme.typography.bodyLarge)
                        TextButton(
                            enabled = removingKeyword == null,
                            onClick = {
                                removingKeyword = name
                                removeError = null
                                scope.launch {
                                    val deleted = repository.deleteKeyword(name)
                                    if (deleted) {
                                        EnrolledKeywordsStore.remove(context, name)
                                        enrolledKeywords = EnrolledKeywordsStore.getAll(context)
                                    } else {
                                        removeError = "Failed to remove '$name' from the belt -- check it's connected and try again"
                                    }
                                    removingKeyword = null
                                }
                            }
                        ) {
                            Text(if (removingKeyword == name) "Removing..." else "Remove from list")
                        }
                    }
                }
            }
        }
    }
}
