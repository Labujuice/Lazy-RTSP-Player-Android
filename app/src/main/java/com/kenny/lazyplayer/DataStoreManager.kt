package com.kenny.lazyplayer

import android.content.Context
import android.content.SharedPreferences

class DataStoreManager(context: Context) {

    companion object {
        private const val PREFS_NAME = "ultra_low_latency_player_prefs"
        private const val KEY_LAST_URL = "last_active_url"
        private const val KEY_HISTORY_URLS = "history_urls"
        private const val KEY_TRANSPORT_TYPE = "transport_type"
        private const val KEY_PIPELINE_TYPE = "pipeline_type"
        private const val KEY_DIRECT_UDP_PORT = "direct_udp_port"
        private const val MAX_LRU_SIZE = 5
        private const val DELIMITER = "\u001F" // Unit separator for safe string joining
    }

    private val prefs: SharedPreferences =
        context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    var lastActiveUrl: String
        get() = prefs.getString(KEY_LAST_URL, "") ?: ""
        set(value) = prefs.edit().putString(KEY_LAST_URL, value).apply()

    var transportType: TransportType
        get() = TransportType.fromId(prefs.getInt(KEY_TRANSPORT_TYPE, TransportType.RTSP_UDP.id))
        set(value) = prefs.edit().putInt(KEY_TRANSPORT_TYPE, value.id).apply()

    var pipelineType: PipelineCombination
        get() = PipelineCombination.fromId(prefs.getInt(KEY_PIPELINE_TYPE, PipelineCombination.CPP_HW.id))
        set(value) = prefs.edit().putInt(KEY_PIPELINE_TYPE, value.id).apply()

    var directUdpPort: Int
        get() = prefs.getInt(KEY_DIRECT_UDP_PORT, 5600)
        set(value) = prefs.edit().putInt(KEY_DIRECT_UDP_PORT, value).apply()

    fun getHistoryUrls(): List<String> {
        val raw = prefs.getString(KEY_HISTORY_URLS, "") ?: ""
        if (raw.isEmpty()) return emptyList()
        return raw.split(DELIMITER).filter { it.isNotEmpty() }
    }

    /**
     * Updates LRU queue with a newly connected URL.
     * Moves existing URL to index 0 or inserts at index 0 and trims to max 5.
     */
    fun recordSuccessfulUrl(url: String) {
        val cleanUrl = url.trim()
        if (cleanUrl.isEmpty()) return

        lastActiveUrl = cleanUrl

        val history = getHistoryUrls().toMutableList()
        history.remove(cleanUrl) // Remove duplicate if already present
        history.add(0, cleanUrl) // Insert at top

        // Trim to max 5 items
        while (history.size > MAX_LRU_SIZE) {
            history.removeAt(history.size - 1)
        }

        val raw = history.joinToString(DELIMITER)
        prefs.edit().putString(KEY_HISTORY_URLS, raw).apply()
    }

    fun removeHistoryUrl(url: String) {
        val history = getHistoryUrls().toMutableList()
        if (history.remove(url)) {
            val raw = history.joinToString(DELIMITER)
            prefs.edit().putString(KEY_HISTORY_URLS, raw).apply()
        }
    }

    fun clearHistory() {
        prefs.edit().remove(KEY_HISTORY_URLS).apply()
    }
}
