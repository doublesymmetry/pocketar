package org.doublesymmetry.pocketar

internal class PocketARSessionRequest(private val prepare: (Boolean) -> Unit) {
    private var requested = false
    private var resumed = false
    private var retry = false

    fun request() {
        requested = true
        retry = true
        prepareIfRequested()
    }

    fun resume() {
        resumed = true
        prepareIfRequested()
    }

    fun pause() {
        resumed = false
    }

    fun prepareIfRequested() {
        if (!requested || !resumed) return
        val retryRequested = retry
        retry = false
        prepare(retryRequested)
    }
}
