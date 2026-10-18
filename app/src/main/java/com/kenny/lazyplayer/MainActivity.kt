package com.kenny.lazyplayer

import android.app.AlertDialog
import android.content.res.ColorStateList
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.GestureDetector
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.View
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.ListView
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.isVisible
import com.kenny.lazyplayer.databinding.ActivityMainBinding
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : AppCompatActivity(), SurfaceHolder.Callback, NativePlayer.PlayerListener {

    private lateinit var binding: ActivityMainBinding
    private lateinit var dataStore: DataStoreManager
    private val nativePlayer = NativePlayer()

    private val telemetryHandler = Handler(Looper.getMainLooper())
    private val stopwatchHandler = Handler(Looper.getMainLooper())
    private var isRecording = false
    private var isPlaying = false
    private var isTimerRunning = false
    private var timerStartTime = 0L
    private var lastBackPressTime = 0L

    private lateinit var gestureDetector: GestureDetector

    private val telemetryRunnable = object : Runnable {
        override fun run() {
            if (isPlaying) {
                updateTelemetryDisplay()
            }
            telemetryHandler.postDelayed(this, 500)
        }
    }

    private val stopwatchRunnable = object : Runnable {
        override fun run() {
            if (isTimerRunning) {
                val elapsed = SystemClock.elapsedRealtime() - timerStartTime
                val hours = elapsed / 3600000L
                val minutes = (elapsed % 3600000L) / 60000L
                val seconds = (elapsed % 60000L) / 1000L
                val millis = elapsed % 1000L
                binding.tvStopwatch.text = String.format(Locale.US, "%02d:%02d:%02d:%03d", hours, minutes, seconds, millis)
                stopwatchHandler.postDelayed(this, 16)
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        dataStore = DataStoreManager(this)
        nativePlayer.setListener(this)

        binding.videoSurface.holder.addCallback(this)

        // Set default OSD visibility based on BuildConfig (Debug: visible, Release: hidden)
        binding.osdContainer.isVisible = BuildConfig.DEFAULT_OSD_VISIBLE
        updateOsdButtonState(binding.osdContainer.isVisible)

        setupGestures()
        setupControls()

        // Cold-start auto load (PRD 4.8)
        val lastUrl = dataStore.lastActiveUrl
        if (lastUrl.isNotEmpty()) {
            binding.etUrl.setText(lastUrl)
        } else {
            binding.etUrl.setText(getString(R.string.default_rtsp_url))
        }

        telemetryHandler.post(telemetryRunnable)
    }

    private fun setupGestures() {
        gestureDetector = GestureDetector(this, object : GestureDetector.SimpleOnGestureListener() {
            override fun onDoubleTap(e: MotionEvent): Boolean {
                // Double tap toggles OSD display (PRD 4.7)
                toggleOsd()
                return true
            }

            override fun onLongPress(e: MotionEvent) {
                // Long press 1.5s summons settings dialog (PRD 4.9)
                showSettingsDialog()
            }
        })

        binding.videoSurface.setOnTouchListener { _, event ->
            gestureDetector.onTouchEvent(event)
            true
        }
    }

    private fun setupControls() {
        binding.btnPlayStop.setOnClickListener {
            if (isPlaying) {
                stopPlaying()
            } else {
                startPlaying()
            }
        }

        binding.btnHistory.setOnClickListener {
            showHistoryDialog()
        }

        binding.btnRecord.setOnClickListener {
            toggleRecording()
        }

        binding.btnSnapshot.setOnClickListener {
            takeSnapshot()
        }

        binding.btnOsd.setOnClickListener {
            toggleOsd()
        }

        binding.btnTimer.setOnClickListener {
            toggleStopwatch()
        }

        binding.btnSettings.setOnClickListener {
            showSettingsDialog()
        }
    }

    private fun toggleOsd() {
        val visible = !binding.osdContainer.isVisible
        binding.osdContainer.isVisible = visible
        updateOsdButtonState(visible)
        val status = if (visible) "OSD Shown" else "OSD Hidden"
        Toast.makeText(this, status, Toast.LENGTH_SHORT).show()
    }

    private fun updateOsdButtonState(visible: Boolean) {
        val color = if (visible) 0xFF2563EB.toInt() else 0xFF4B5563.toInt()
        binding.btnOsd.backgroundTintList = ColorStateList.valueOf(color)
    }

    private fun toggleStopwatch() {
        if (isTimerRunning) {
            isTimerRunning = false
            stopwatchHandler.removeCallbacks(stopwatchRunnable)
            binding.stopwatchContainer.isVisible = false
            binding.btnTimer.backgroundTintList = ColorStateList.valueOf(0xFF4B5563.toInt())
            Toast.makeText(this, "Timer Stopped", Toast.LENGTH_SHORT).show()
        } else {
            isTimerRunning = true
            timerStartTime = SystemClock.elapsedRealtime()
            binding.tvStopwatch.text = "00:00:00:000"
            binding.stopwatchContainer.isVisible = true
            binding.btnTimer.backgroundTintList = ColorStateList.valueOf(0xFF059669.toInt()) // Emerald when active
            stopwatchHandler.post(stopwatchRunnable)
            Toast.makeText(this, "Timer Started", Toast.LENGTH_SHORT).show()
        }
    }

    private fun startPlaying() {
        val url = binding.etUrl.text.toString().trim()
        if (url.isEmpty() && dataStore.transportType != TransportType.DIRECT_UDP) {
            Toast.makeText(this, "Please enter stream URL", Toast.LENGTH_SHORT).show()
            return
        }

        val transport = dataStore.transportType
        val pipeline = dataStore.pipelineType
        val port = dataStore.directUdpPort

        val success = nativePlayer.start(url, transport, pipeline, port)
        if (success) {
            isPlaying = true
            binding.btnPlayStop.text = getString(R.string.stop)
            binding.btnPlayStop.backgroundTintList = ColorStateList.valueOf(getColor(R.color.accent_red))

            // Save to LRU history (PRD 4.8)
            dataStore.recordSuccessfulUrl(url)
        } else {
            Toast.makeText(this, "Failed to connect", Toast.LENGTH_SHORT).show()
        }
    }

    private fun stopPlaying() {
        if (isRecording) {
            toggleRecording()
        }
        nativePlayer.stop()
        isPlaying = false
        binding.btnPlayStop.text = getString(R.string.connect)
        binding.btnPlayStop.backgroundTintList = ColorStateList.valueOf(getColor(R.color.accent_green))
        binding.tvDisconnectionWarning.isVisible = false
        binding.tvOsdContent.text = "STOPPED"
    }

    private fun toggleRecording() {
        if (!isPlaying) {
            Toast.makeText(this, "Cannot record when not playing", Toast.LENGTH_SHORT).show()
            return
        }

        if (!isRecording) {
            val recordDir = getExternalFilesDir(Environment.DIRECTORY_MOVIES) ?: filesDir
            val fpvDir = File(recordDir, "FPV_Records")
            if (!fpvDir.exists()) fpvDir.mkdirs()

            val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
            val file = File(fpvDir, "record_${timestamp}.mp4")

            val started = nativePlayer.startRecording(file.absolutePath)
            if (started) {
                isRecording = true
                binding.btnRecord.text = getString(R.string.stop_record)
                binding.btnRecord.backgroundTintList = ColorStateList.valueOf(getColor(R.color.accent_red))
                Toast.makeText(this, "Recording started: ${file.name}", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this, "Recording start failed (stream probing?)", Toast.LENGTH_SHORT).show()
            }
        } else {
            nativePlayer.stopRecording()
            isRecording = false
            binding.btnRecord.text = getString(R.string.record)
            binding.btnRecord.backgroundTintList = ColorStateList.valueOf(0xFF4B5563.toInt())
            Toast.makeText(this, "Clean MP4 saved to Movies/FPV_Records", Toast.LENGTH_LONG).show()
        }
    }

    private fun takeSnapshot() {
        if (!isPlaying) {
            Toast.makeText(this, "Cannot take snapshot when not playing", Toast.LENGTH_SHORT).show()
            return
        }

        val picDir = getExternalFilesDir(Environment.DIRECTORY_PICTURES) ?: filesDir
        val snapDir = File(picDir, "FPV_Snapshots")
        if (!snapDir.exists()) snapDir.mkdirs()

        val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val file = File(snapDir, "snapshot_${timestamp}.jpg")

        val ok = nativePlayer.takeSnapshot(file.absolutePath)
        if (ok) {
            Toast.makeText(this, "Clean JPEG saved: ${file.name}", Toast.LENGTH_SHORT).show()
        } else {
            Toast.makeText(this, "Snapshot request failed", Toast.LENGTH_SHORT).show()
        }
    }

    private fun updateTelemetryDisplay() {
        val t = nativePlayer.getTelemetry()
        val text = buildString {
            append("=== OSD TELEMETRY ===\n")
            append(String.format(Locale.US, "PIPELINE:   %s\n", t.pipelineCombination))
            append(String.format(Locale.US, "TRANSPORT:  %s\n", t.transportProtocol))
            append(String.format(Locale.US, "CODEC:      %s\n", t.codecResolution))
            append(String.format(Locale.US, "BITRATE:    %.1f kbps\n", t.bitrateKbps))
            append(String.format(Locale.US, "RENDER FPS: %.1f fps (Src: %d)\n", t.renderFps, t.sourceFps))
            append(String.format(Locale.US, "DECODE TIME:%.1f ms\n", t.decodeTimeMs))
            append(String.format(Locale.US, "DROPPED:    Jitter: %d / Loss: %d\n", t.droppedFramesJitter, t.droppedFramesLoss))
            if (t.cpuRateTotal >= 0.0) {
                append(String.format(Locale.US, "CPU USAGE:  App: %.1f%% / Total: %.1f%%\n", t.cpuRateApp, t.cpuRateTotal))
            } else {
                append(String.format(Locale.US, "CPU USAGE:  App: %.1f%% / Total: N/A\n", t.cpuRateApp))
            }
            append(String.format(Locale.US, "RAM MEMORY: RSS: %.1f MB / PSS: %.1f MB\n", t.ramRssMb, t.ramPssMb))
            append(String.format(Locale.US, "GPU STATE:  %s", t.gpuState))
        }
        binding.tvOsdContent.text = text
    }

    private fun showHistoryDialog() {
        val dialogView = layoutInflater.inflate(R.layout.dialog_history, null)
        val lvHistory = dialogView.findViewById<ListView>(R.id.lvHistory)
        val btnClear = dialogView.findViewById<Button>(R.id.btnClearHistory)
        val btnClose = dialogView.findViewById<Button>(R.id.btnCloseHistory)

        val historyList = dataStore.getHistoryUrls().toMutableList()
        val adapter = object : ArrayAdapter<String>(this, R.layout.item_history, historyList) {
            override fun getView(position: Int, convertView: View?, parent: android.view.ViewGroup): View {
                val view = convertView ?: layoutInflater.inflate(R.layout.item_history, parent, false)
                val item = getItem(position) ?: ""
                val tvUrl = view.findViewById<TextView>(R.id.tvUrl)
                val btnDelete = view.findViewById<Button>(R.id.btnDelete)

                tvUrl.text = item
                tvUrl.setOnClickListener {
                    binding.etUrl.setText(item)
                    (view.tag as? AlertDialog)?.dismiss()
                }
                btnDelete.setOnClickListener {
                    dataStore.removeHistoryUrl(item)
                    remove(item)
                    notifyDataSetChanged()
                }
                return view
            }
        }
        lvHistory.adapter = adapter

        val dialog = AlertDialog.Builder(this)
            .setView(dialogView)
            .create()

        dialogView.tag = dialog
        btnClose.setOnClickListener { dialog.dismiss() }
        btnClear.setOnClickListener {
            dataStore.clearHistory()
            historyList.clear()
            adapter.notifyDataSetChanged()
        }

        dialog.show()
    }

    private fun showSettingsDialog() {
        val dialogView = layoutInflater.inflate(R.layout.dialog_settings, null)
        val spPipeline = dialogView.findViewById<Spinner>(R.id.spPipeline)
        val spTransport = dialogView.findViewById<Spinner>(R.id.spTransport)
        val etUdpPort = dialogView.findViewById<EditText>(R.id.etUdpPort)
        val btnSave = dialogView.findViewById<Button>(R.id.btnSaveSettings)
        val btnCancel = dialogView.findViewById<Button>(R.id.btnCancelSettings)

        val pipelineNames = PipelineCombination.values().map { it.displayName }
        spPipeline.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, pipelineNames)
        spPipeline.setSelection(dataStore.pipelineType.id)

        val transportNames = TransportType.values().map { it.displayName }
        spTransport.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, transportNames)
        spTransport.setSelection(dataStore.transportType.id)

        etUdpPort.setText(dataStore.directUdpPort.toString())

        val dialog = AlertDialog.Builder(this)
            .setView(dialogView)
            .create()

        btnCancel.setOnClickListener { dialog.dismiss() }
        btnSave.setOnClickListener {
            val selectedPipeline = PipelineCombination.fromId(spPipeline.selectedItemPosition)
            val selectedTransport = TransportType.fromId(spTransport.selectedItemPosition)
            val port = etUdpPort.text.toString().toIntOrNull() ?: 5600

            dataStore.pipelineType = selectedPipeline
            dataStore.transportType = selectedTransport
            dataStore.directUdpPort = port

            Toast.makeText(this, "Settings Saved", Toast.LENGTH_SHORT).show()
            dialog.dismiss()

            if (isPlaying) {
                stopPlaying()
                startPlaying()
            }
        }

        dialog.show()
    }

    // SurfaceHolder.Callback
    override fun surfaceCreated(holder: SurfaceHolder) {
        nativePlayer.setSurface(holder.surface)
        // Cold start auto-play (PRD 4.8)
        if (!isPlaying && dataStore.lastActiveUrl.isNotEmpty()) {
            startPlaying()
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        nativePlayer.setSurface(holder.surface)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        nativePlayer.setSurface(null)
    }

    // NativePlayer.PlayerListener
    override fun onStateChanged(state: Int, message: String) {
        runOnUiThread {
            when (state) {
                NativePlayer.STATE_DISCONNECTED -> {
                    // Show 500ms Disconnection warning badge (PRD 4.5)
                    binding.tvDisconnectionWarning.text = message.ifEmpty { getString(R.string.disconnection_warning) }
                    binding.tvDisconnectionWarning.isVisible = true
                }
                NativePlayer.STATE_PLAYING -> {
                    binding.tvDisconnectionWarning.isVisible = false
                }
                NativePlayer.STATE_ERROR -> {
                    binding.tvDisconnectionWarning.text = "⚠️ $message"
                    binding.tvDisconnectionWarning.isVisible = true
                    Toast.makeText(this, "Player error: $message", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    // Android TV remote controller key handling (PRD 4.9)
    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        when (keyCode) {
            KeyEvent.KEYCODE_MENU -> {
                // TV Menu key toggles OSD
                toggleOsd()
                return true
            }
            KeyEvent.KEYCODE_BACK -> {
                // Double click back within 2s to exit
                val now = System.currentTimeMillis()
                if (now - lastBackPressTime < 2000) {
                    finish()
                } else {
                    lastBackPressTime = now
                    Toast.makeText(this, "Press BACK again to exit", Toast.LENGTH_SHORT).show()
                }
                return true
            }
        }
        return super.onKeyDown(keyCode, event)
    }

    override fun onDestroy() {
        super.onDestroy()
        telemetryHandler.removeCallbacks(telemetryRunnable)
        stopwatchHandler.removeCallbacks(stopwatchRunnable)
        nativePlayer.stop()
        nativePlayer.release()
    }
}
