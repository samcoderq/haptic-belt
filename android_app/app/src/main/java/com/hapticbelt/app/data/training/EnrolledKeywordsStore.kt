package com.hapticbelt.app.data.training

import android.content.Context

/**
 * The app's own local record of which keywords it has sent to the belt --
 * NOT a read-back of the belt's actual flash contents. There is no BLE
 * mechanism to query keyword_store.cpp's contents (Keyword Upload is
 * write-only, no read/list characteristic), and the upload itself is a
 * plain Write, not Write-with-response, so this records "enrolled and sent"
 * rather than "confirmed stored on the belt." SharedPreferences-backed so
 * the list survives app restarts.
 */
object EnrolledKeywordsStore {
    private const val PREFS_NAME = "enrolled_keywords_prefs"
    private const val KEY_NAMES = "names"
    // Append-only, insertion-ordered, separate from KEY_NAMES (which is a
    // Set and whose getAll() is alphabetically sorted for display). This one
    // exists only to best-effort map a belt-reported keyword slot index back
    // to a name (see Dashboard's Keyword card / BleBeltRepository), since
    // keyword_store.cpp's keywordStoreAdd() also appends sequentially by
    // upload order -- index 0 here should be the belt's slot 0, and so on.
    // Deliberately never filtered by remove(): the belt has no delete
    // capability at all, so a locally-removed name may still really be
    // sitting in the belt's flash at its original slot, and shifting this
    // list on remove() would desync every slot after it. Best-effort, not
    // authoritative -- there is still no read-back from the belt itself.
    private const val KEY_ORDER = "order_csv"
    private const val ORDER_DELIMITER = ""

    fun getAll(context: Context): List<String> =
        prefs(context).getStringSet(KEY_NAMES, emptySet())!!.sorted()

    fun add(context: Context, name: String) {
        val current = prefs(context).getStringSet(KEY_NAMES, emptySet())!!.toMutableSet()
        current.add(name)
        val order = getInsertionOrder(context).toMutableList()
        if (name !in order) order.add(name)
        prefs(context).edit()
            .putStringSet(KEY_NAMES, current)
            .putString(KEY_ORDER, order.joinToString(ORDER_DELIMITER))
            .apply()
    }

    /** Removes it from this local list only -- has no effect on the belt's own flash storage. */
    fun remove(context: Context, name: String) {
        val current = prefs(context).getStringSet(KEY_NAMES, emptySet())!!.toMutableSet()
        current.remove(name)
        prefs(context).edit().putStringSet(KEY_NAMES, current).apply()
    }

    private fun getInsertionOrder(context: Context): List<String> {
        val raw = prefs(context).getString(KEY_ORDER, null) ?: return emptyList()
        return if (raw.isEmpty()) emptyList() else raw.split(ORDER_DELIMITER)
    }

    /** Best-effort name for a 0-based keyword_store slot index; see KEY_ORDER above. */
    fun nameForSlot(context: Context, slotIndex: Int): String? =
        getInsertionOrder(context).getOrNull(slotIndex)

    private fun prefs(context: Context) =
        context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
}
