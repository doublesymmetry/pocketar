package org.doublesymmetry.pocketar

internal object PocketARLaunchArgs {
    fun collect(harnessArguments: Array<String>?, appArguments: Array<String>?, originalArguments: List<String> = emptyList(), onAppArgumentsRejected: (String) -> Unit = {}): List<String>? {
        val harness = harnessArguments?.map(::unescapeArgument)
        if (harness != null && !validHarnessArguments(harness)) return null
        val app = appArguments?.map(::unescapeArgument).orEmpty()
        val validApp = app.all { it.isNotBlank() && it == it.trim() && !it.startsWith("--") }
        if (!validApp) onAppArgumentsRejected("Rejected malformed pocketar_app_args: entries must be nonblank, trimmed and use single-dash game flags")
        val arguments = harness.orEmpty() + if (validApp) app else emptyList()
        if (arguments.isEmpty()) return emptyList()
        return if (originalArguments.any { it == "--" || it == "++" }) arguments else listOf("--") + arguments
    }

    private fun unescapeArgument(argument: String): String = buildString {
        var index = 0
        while (index < argument.length) {
            val character = argument[index++]
            if (character == '\\' && argument.getOrNull(index) in listOf('\\', ',')) {
                append(argument[index++])
            } else {
                append(character)
            }
        }
    }

    private fun validHarnessArguments(arguments: List<String>): Boolean {
        if (arguments.firstOrNull() !in listOf("-harness", "-harness-ar", "-ar-demo")) return false
        var index = 1
        var datasetSelected = false
        while (index < arguments.size) {
            when (arguments[index++]) {
                "-harness-lifecycle", "-harness-probes", "-harness-gpu-validate" -> Unit
                "-harness-camera-path" -> {
                    val value = arguments.getOrNull(index++) ?: return false
                    if (value !in listOf("auto", "cpu")) return false
                    if (value == "cpu" && "-harness-gpu-validate" in arguments) return false
                }
                "-harness-camera-config" -> {
                    val value = arguments.getOrNull(index++)?.toIntOrNull() ?: return false
                    if (value < 0) return false
                }
                "-harness-record", "-harness-playback" -> {
                    val value = arguments.getOrNull(index++) ?: return false
                    if (datasetSelected || !Regex("[A-Za-z0-9_-][A-Za-z0-9_.-]*\\.mp4").matches(value)) return false
                    datasetSelected = true
                }
                "-harness-duration", "-harness-timeout" -> {
                    val value = arguments.getOrNull(index++)?.toDoubleOrNull() ?: return false
                    if (!value.isFinite() || value <= 0.0) return false
                }
                "-harness-out", "-harness-steps" -> {
                    val value = arguments.getOrNull(index++) ?: return false
                    if (value.isBlank() || value.startsWith("-")) return false
                }
                else -> return false
            }
        }
        return true
    }
}
