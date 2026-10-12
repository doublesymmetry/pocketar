package org.doublesymmetry.pocketar

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class PocketARLaunchArgsTest {
    @Test fun compositionIsValidatedBeforeLaunch() {
        for (value in listOf("in-scene", "isolated-camera")) {
            val arguments = arrayOf("-harness-ar", "-harness-composition", value)
            assertEquals(listOf("--") + arguments, PocketARLaunchArgs.collect(arguments, null))
        }
        assertNull(PocketARLaunchArgs.collect(arrayOf("-harness-ar", "-harness-composition"), null))
        assertNull(PocketARLaunchArgs.collect(arrayOf("-harness-ar", "-harness-composition", "unknown"), null))
    }

    @Test fun existingUserSeparatorIsPreserved() {
        for (separator in listOf("--", "++")) {
            assertEquals(
                listOf("-game-seed", "42"),
                PocketARLaunchArgs.collect(null, arrayOf("-game-seed", "42"), listOf(separator, "-existing", "x"))
            )
        }
    }

    @Test fun appOnlyArgumentsReachGodotUserArguments() {
        assertEquals(
            listOf("--", "-game-level", "snow field", "-custom", "comma,value"),
            PocketARLaunchArgs.collect(null, arrayOf("-game-level", "snow field", "-custom", "comma,value"))
        )
    }

    @Test fun malformedAppExtraIsRejectedWithoutDroppingHarnessArguments() {
        for (invalid in listOf("", " ", " value", "value ", "\tvalue", "value\n", "--custom", "--main-pack")) {
            val errors = mutableListOf<String>()
            val app = arrayOf("-valid", "42", invalid)
            assertEquals(emptyList<String>(), PocketARLaunchArgs.collect(null, app, onAppArgumentsRejected = { errors.add(it) }))
            assertEquals(1, errors.size)
            assertEquals(
                listOf("--", "-harness-ar"),
                PocketARLaunchArgs.collect(arrayOf("-harness-ar"), app, onAppArgumentsRejected = { errors.add(it) })
            )
            assertEquals(2, errors.size)
        }
    }

    @Test fun harnessAndAppArgumentsShareOneSeparator() {
        assertEquals(
            listOf("--", "-harness-ar", "-harness-duration", "60", "-game-seed", "42"),
            PocketARLaunchArgs.collect(arrayOf("-harness-ar", "-harness-duration", "60"), arrayOf("-game-seed", "42"))
        )
    }

    @Test fun bothChannelsDecodeCommaAndBackslashEscapes() {
        assertEquals(
            listOf("--", "-harness-ar", "-harness-steps", "tracking,lifecycle", "-app-value", "comma,value", "C:\\room", "literal\\q"),
            PocketARLaunchArgs.collect(
                arrayOf("-harness-ar", "-harness-steps", "tracking\\,lifecycle"),
                arrayOf("-app-value", "comma\\,value", "C:\\\\room", "literal\\q")
            )
        )
    }

    @Test fun absentArgumentsDoNotAddASeparator() {
        assertEquals(emptyList<String>(), PocketARLaunchArgs.collect(null, null))
        assertEquals(emptyList<String>(), PocketARLaunchArgs.collect(null, emptyArray()))
    }

    @Test fun malformedHarnessArgumentsCannotBeRescuedByAppArguments() {
        for (arguments in listOf(
            emptyArray(),
            arrayOf("-harness-ar", "-harness-duration"),
            arrayOf("-harness-ar", "-harness-duration", "NaN"),
            arrayOf("-harness-ar", "-unknown"),
            arrayOf("-harness-ar", "-harness-camera-path", "cpu", "-harness-gpu-validate")
        )) {
            assertNull(PocketARLaunchArgs.collect(arguments, arrayOf("-app-value", "anything")))
        }
    }

    @Test fun harnessOnlyLaunchesKeepTheirArguments() {
        assertEquals(
            listOf("--", "-harness-ar", "-harness-duration", "60", "-harness-camera-config", "0"),
            PocketARLaunchArgs.collect(arrayOf("-harness-ar", "-harness-duration", "60", "-harness-camera-config", "0"), null)
        )
    }
}
