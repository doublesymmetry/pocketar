package org.doublesymmetry.pocketar

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class PocketARSessionRequestTest {
    @Test fun launchAndResumesWithoutArNeverPrepareSetup() {
        val calls = mutableListOf<Boolean>()
        val request = PocketARSessionRequest(calls::add)
        request.prepareIfRequested()
        request.resume()
        request.prepareIfRequested()
        request.pause()
        request.resume()
        assertTrue(calls.isEmpty())
    }

    @Test fun requestWaitsForResumeAndCallbacksDoNotRetryDenial() {
        val calls = mutableListOf<Boolean>()
        val request = PocketARSessionRequest(calls::add)
        request.request()
        request.prepareIfRequested()
        assertTrue(calls.isEmpty())
        request.resume()
        request.prepareIfRequested()
        request.pause()
        request.prepareIfRequested()
        request.resume()
        assertEquals(listOf(true, false, false), calls)
        request.request()
        assertEquals(listOf(true, false, false, true), calls)
    }

    @Test fun explicitRetryWhilePausedSurvivesUntilResume() {
        val calls = mutableListOf<Boolean>()
        val request = PocketARSessionRequest(calls::add)
        request.resume()
        request.request()
        request.pause()
        request.request()
        request.prepareIfRequested()
        assertEquals(listOf(true), calls)
        request.resume()
        assertEquals(listOf(true, true), calls)
    }
}
