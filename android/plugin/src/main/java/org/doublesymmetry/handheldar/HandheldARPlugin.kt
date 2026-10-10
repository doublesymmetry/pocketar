package org.doublesymmetry.handheldar

import android.Manifest
import android.app.Activity
import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.hardware.display.DisplayManager
import android.os.Build
import android.os.BatteryManager
import android.os.Debug
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.os.SystemClock
import android.util.Log
import android.view.Display
import android.view.Surface
import android.view.View
import com.google.ar.core.ArCoreApk
import com.google.ar.core.exceptions.UnavailableDeviceNotCompatibleException
import com.google.ar.core.exceptions.UnavailableUserDeclinedInstallationException
import org.godotengine.godot.Godot
import org.godotengine.godot.plugin.GodotPlugin
import org.godotengine.godot.plugin.UsedByGodot
import org.json.JSONObject
import javax.microedition.khronos.opengles.GL10

class HandheldARPlugin(godot: Godot) : GodotPlugin(godot), DisplayManager.DisplayListener {
    companion object {
        private const val TAG = "godot"
        private const val ARCORE_PACKAGE = "com.google.ar.core"
        private const val CAMERA_PERMISSION_REQUEST = 0x4841
        private const val UNSUPPORTED = 0
        private const val CHECKING = 1
        private const val NEEDS_INSTALL = 2
        private const val INSTALLING = 3
        private const val PERMISSION_DENIED = 4
        private const val READY = 5
        private const val FAILED = 9

        init {
            System.loadLibrary("handheld_ar")
        }
    }

    private data class SurfaceSize(val width: Int, val height: Int)

    private var initialized = false
    @Volatile private var destroyed = false
    @Volatile private var resumed = false
    @Volatile private var lifecycleGeneration = 0L
    private var installRequested = false
    private var permissionRequested = false
    private var permissionPending = false
    private var waitingForSurface = false
    private var harnessPaused = false
    private var availability: ArCoreApk.Availability? = null
    private var displayManager: DisplayManager? = null
    private var displayListening = false
    @Volatile private var surfaceSize = SurfaceSize(0, 0)
    @Volatile private var renderThread: Thread? = null
    @Volatile private var pauseQueueReturnMs = 0.0
    @Volatile private var pauseQueueReturnMaxMs = 0.0
    @Volatile private var pauseQueueSamples = 0L

    override fun getPluginName() = "HandheldARPlugin"

    override fun getPluginGDExtensionLibrariesPaths() =
        setOf("res://addons/handheld_ar/handheld_ar.gdextension")

    override fun getCommandLineParams(unmodifiableOriginalCommandLineParams: List<String>): List<String> {
        val host = activity ?: return emptyList()
        if (host.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE == 0) return emptyList()
        val arguments = host.intent.getStringArrayExtra("handheld_ar_harness_args")?.map(::unescapeHarnessArgument)
            ?: return emptyList()
        if (!validHarnessArguments(arguments)) {
            Log.e(TAG, "Rejected malformed handheld_ar_harness_args")
            return emptyList()
        }
        return listOf("--") + arguments
    }

    private fun unescapeHarnessArgument(argument: String): String = buildString {
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

    override fun onMainCreate(activity: Activity?): View? {
        val host = activity ?: return null
        runOnRenderThread { nativeInitialize(host, host.applicationContext) }
        initialized = true
        displayManager = host.getSystemService(DisplayManager::class.java)
        publishStatus(CHECKING, "Checking ARCore availability")
        ArCoreApk.getInstance().checkAvailabilityAsync(host.applicationContext) { result ->
            if (!destroyed) {
                availability = result
                if (resumed) prepareSession()
            }
        }
        return null
    }

    override fun onMainResume() {
        resumed = true
        ++lifecycleGeneration
        if (!initialized || destroyed) return
        if (!displayListening) {
            displayManager?.registerDisplayListener(this, Handler(Looper.getMainLooper()))
            displayListening = true
        }
        updateDisplayGeometry()
        prepareSession()
    }

    override fun onMainPause() {
        resumed = false
        ++lifecycleGeneration
        stopDisplayListener()
        if (initialized && !destroyed) enqueuePause("onMainPause")
    }

    override fun onMainDestroy() {
        destroyed = true
        resumed = false
        ++lifecycleGeneration
        stopDisplayListener()
        if (initialized) runOnRenderThread { nativeShutdown() }
        initialized = false
    }

    override fun onGodotSetupCompleted() {
        renderThread = Thread.currentThread()
    }

    override fun onGodotTerminating() {
        if (Thread.currentThread() === renderThread) nativeShutdown()
    }

    private fun stopDisplayListener() {
        if (displayListening) displayManager?.unregisterDisplayListener(this)
        displayListening = false
    }

    private fun enqueuePause(origin: String) {
        val start = SystemClock.elapsedRealtimeNanos()
        runOnRenderThread {
            nativePause()
            pauseQueueReturnMs = (SystemClock.elapsedRealtimeNanos() - start) / 1_000_000.0
            pauseQueueReturnMaxMs = maxOf(pauseQueueReturnMaxMs, pauseQueueReturnMs)
            ++pauseQueueSamples
            Log.i(TAG, "$origin queue_to_pause_return_ms=$pauseQueueReturnMs")
        }
    }

    private fun prepareSession() {
        if (!initialized || destroyed || !resumed || harnessPaused) return
        val host = activity ?: return
        val support = availability ?: return
        if (!support.isSupported) {
            val state = if (support == ArCoreApk.Availability.UNSUPPORTED_DEVICE_NOT_CAPABLE) UNSUPPORTED else FAILED
            publishStatus(state, "ARCore availability: $support")
            return
        }
        if (permissionPending) return
        try {
            when (ArCoreApk.getInstance().requestInstall(host, !installRequested)) {
                ArCoreApk.InstallStatus.INSTALL_REQUESTED -> {
                    installRequested = true
                    publishStatus(INSTALLING, "Waiting for Google Play Services for AR installation")
                    return
                }
                ArCoreApk.InstallStatus.INSTALLED -> Unit
            }
        } catch (error: UnavailableDeviceNotCompatibleException) {
            publishStatus(UNSUPPORTED, "Device does not support ARCore")
            return
        } catch (error: UnavailableUserDeclinedInstallationException) {
            publishStatus(NEEDS_INSTALL, "Google Play Services for AR installation was declined")
            return
        } catch (error: Exception) {
            publishStatus(FAILED, "ARCore installation: ${error.message ?: error.javaClass.simpleName}")
            return
        }
        val version = installedApkVersion()
        if (!supportsHardwareBufferPlayback(version)) {
            publishStatus(NEEDS_INSTALL, "Google Play Services for AR 1.48 or newer is required (installed: $version)")
            return
        }
        if (host.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            if (permissionRequested) {
                publishStatus(PERMISSION_DENIED, "Camera permission is required; allow it in Android app settings")
            } else {
                permissionRequested = true
                permissionPending = true
                publishStatus(CHECKING, "Waiting for camera permission")
                host.requestPermissions(arrayOf(Manifest.permission.CAMERA), CAMERA_PERMISSION_REQUEST)
            }
            return
        }
        if (!updateDisplayGeometry()) {
            waitingForSurface = true
            return
        }
        waitingForSurface = false
        val generation = lifecycleGeneration
        runOnRenderThread {
            if (generation != lifecycleGeneration || !resumed || destroyed) return@runOnRenderThread
            nativeStatus(READY, version, "ARCore SDK 1.56.0; Google Play Services for AR $version")
            nativeResume()
        }
    }

    override fun onMainRequestPermissionsResult(requestCode: Int, permissions: Array<String>, grantResults: IntArray) {
        if (requestCode != CAMERA_PERMISSION_REQUEST || destroyed) return
        permissionPending = false
        val index = permissions.indexOf(Manifest.permission.CAMERA)
        if (index < 0 || grantResults.getOrNull(index) != PackageManager.PERMISSION_GRANTED) {
            publishStatus(PERMISSION_DENIED, "Camera permission was denied")
            return
        }
        prepareSession()
    }

    private fun publishStatus(state: Int, detail: String) {
        val version = installedApkVersion()
        Log.i(TAG, "state=$state apk=$version sdk=1.56.0 $detail")
        runOnRenderThread { nativeStatus(state, version, detail) }
    }

    @Suppress("DEPRECATION")
    private fun installedApkVersion(): String = try {
        context.packageManager.getPackageInfo(ARCORE_PACKAGE, 0).versionName ?: "unknown"
    } catch (error: PackageManager.NameNotFoundException) {
        "not-installed"
    }

    private fun supportsHardwareBufferPlayback(version: String): Boolean {
        val parts = version.split('.')
        val major = parts.getOrNull(0)?.toIntOrNull() ?: return false
        val minor = parts.getOrNull(1)?.toIntOrNull() ?: return false
        return major > 1 || (major == 1 && minor >= 48)
    }

    @Suppress("DEPRECATION")
    private fun currentDisplay(): Display? {
        val host = activity ?: return null
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) host.display else host.windowManager.defaultDisplay
    }

    private fun updateDisplayGeometry(): Boolean {
        if (!initialized || destroyed) return false
        val rotation = currentDisplay()?.rotation ?: return false
        val size = surfaceSize
        if (size.width <= 0 || size.height <= 0) return false
        runOnRenderThread { nativeDisplayGeometry(rotation, size.width, size.height) }
        return true
    }

    override fun onDisplayAdded(displayId: Int) = Unit

    override fun onDisplayRemoved(displayId: Int) = Unit

    override fun onDisplayChanged(displayId: Int) {
        if (currentDisplay()?.displayId == displayId) updateDisplayGeometry()
    }

    override fun onVkSurfaceChanged(surface: Surface, width: Int, height: Int) {
        surfaceSize = SurfaceSize(width, height)
        runOnHostThread {
            updateDisplayGeometry()
            if (waitingForSurface) prepareSession()
        }
    }

    override fun onGLSurfaceChanged(gl: GL10, width: Int, height: Int) {
        surfaceSize = SurfaceSize(width, height)
        runOnHostThread {
            updateDisplayGeometry()
            if (waitingForSurface) prepareSession()
        }
    }

    @UsedByGodot
    fun setHarnessPaused(paused: Boolean) {
        if (context.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE == 0) return
        runOnHostThread {
            if (!initialized || destroyed) return@runOnHostThread
            harnessPaused = paused
            ++lifecycleGeneration
            if (paused) enqueuePause("harnessPause") else prepareSession()
        }
    }

    @UsedByGodot
    fun getDiagnostics(): String {
        val power = context.getSystemService(PowerManager::class.java)
        val battery = context.getSystemService(BatteryManager::class.java)
        val batteryState = context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        val current = battery?.getIntProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_NOW)
        val energy = battery?.getLongProperty(BatteryManager.BATTERY_PROPERTY_ENERGY_COUNTER)
        val memory = ActivityManager.MemoryInfo()
        context.getSystemService(ActivityManager::class.java)?.getMemoryInfo(memory)
        return JSONObject()
            .put("arcore_sdk", "1.56.0")
            .put("arcore_apk", installedApkVersion())
            .put("android_sdk", Build.VERSION.SDK_INT)
            .put("device", "${Build.MANUFACTURER} ${Build.MODEL}")
            .put("thermal_status", power?.currentThermalStatus ?: -1)
            .put("pause_queue_to_return_ms", pauseQueueReturnMs)
            .put("pause_queue_to_return_max_ms", pauseQueueReturnMaxMs)
            .put("pause_queue_samples", pauseQueueSamples)
            .put("battery_current_microamps", if (current == null || current == Int.MIN_VALUE) JSONObject.NULL else current)
            .put("battery_energy_nanowatt_hours", if (energy == null || energy == Long.MIN_VALUE) JSONObject.NULL else energy)
            .put("battery_voltage_millivolts", batteryState?.getIntExtra(BatteryManager.EXTRA_VOLTAGE, -1))
            .put("battery_temperature_tenths_c", batteryState?.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, -1))
            .put("battery_plugged", batteryState?.getIntExtra(BatteryManager.EXTRA_PLUGGED, -1))
            .put("process_pss_kib", Debug.getPss())
            .put("available_memory_bytes", memory.availMem)
            .put("low_memory", memory.lowMemory)
            .toString()
    }

    @UsedByGodot
    fun getExternalFilesDir(): String = context.getExternalFilesDir(null)?.absolutePath ?: ""

    private external fun nativeInitialize(activity: Activity, applicationContext: Context)
    private external fun nativeStatus(state: Int, apkVersion: String, detail: String)
    private external fun nativeResume()
    private external fun nativePause()
    private external fun nativeDisplayGeometry(rotation: Int, width: Int, height: Int)
    private external fun nativeShutdown()
}
