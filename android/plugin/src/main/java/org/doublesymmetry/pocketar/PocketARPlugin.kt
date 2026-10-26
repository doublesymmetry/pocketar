package org.doublesymmetry.pocketar

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

class PocketARPlugin(godot: Godot) : GodotPlugin(godot), DisplayManager.DisplayListener {
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
            System.loadLibrary("pocketar")
        }
    }

    private data class SurfaceSize(val width: Int, val height: Int)

    @Volatile private var initialized = false
    @Volatile private var resumed = false
    @Volatile private var lifecycleGeneration = 0L
    private var installRequested = false
    private var permissionRequested = false
    private var permissionPending = false
    private var setupState = CHECKING
    private val sessionRequest = PocketARSessionRequest(::prepareSession)
    private var waitingForSurface = false
    private var availability: ArCoreApk.Availability? = null
    private var displayManager: DisplayManager? = null
    private var displayListening = false
    @Volatile private var surfaceSize = SurfaceSize(0, 0)
    @Volatile private var renderThread: Thread? = null
    @Volatile private var pauseQueueReturnMs = 0.0
    @Volatile private var pauseQueueReturnMaxMs = 0.0
    @Volatile private var pauseQueueSamples = 0L

    override fun getPluginName() = "PocketARPlugin"

    override fun getPluginGDExtensionLibrariesPaths() =
        setOf("res://addons/pocketar/pocketar.gdextension")

    override fun getCommandLineParams(unmodifiableOriginalCommandLineParams: List<String>): List<String> {
        val host = activity ?: return emptyList()
        if (host.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE == 0) return emptyList()
        val arguments = PocketARLaunchArgs.collect(
            host.intent.getStringArrayExtra("pocketar_harness_args"),
            host.intent.getStringArrayExtra("pocketar_app_args"),
            unmodifiableOriginalCommandLineParams,
            onAppArgumentsRejected = { Log.e(TAG, it) }
        )
        if (arguments == null) {
            Log.e(TAG, "Rejected malformed pocketar_harness_args")
            return emptyList()
        }
        return arguments
    }

    override fun onMainCreate(activity: Activity?): View? {
        val host = activity ?: return null
        runOnRenderThread { nativeInitialize(host.applicationContext) }
        initialized = true
        displayManager = host.getSystemService(DisplayManager::class.java)
        publishStatus(CHECKING, "Checking ARCore availability")
        checkAvailability()
        return null
    }

    override fun onMainResume() {
        resumed = true
        ++lifecycleGeneration
        if (!initialized) return
        if (!displayListening) {
            displayManager?.registerDisplayListener(this, Handler(Looper.getMainLooper()))
            displayListening = true
        }
        updateDisplayGeometry()
        sessionRequest.resume()
    }

    override fun onMainPause() {
        resumed = false
        sessionRequest.pause()
        ++lifecycleGeneration
        stopDisplayListener()
        if (!initialized) return
        val start = SystemClock.elapsedRealtimeNanos()
        runOnRenderThread {
            nativePause()
            pauseQueueReturnMs = (SystemClock.elapsedRealtimeNanos() - start) / 1_000_000.0
            pauseQueueReturnMaxMs = maxOf(pauseQueueReturnMaxMs, pauseQueueReturnMs)
            ++pauseQueueSamples
            Log.i(TAG, "onMainPause queue_to_pause_return_ms=$pauseQueueReturnMs")
        }
    }

    override fun onMainDestroy() {
        resumed = false
        sessionRequest.pause()
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

    @UsedByGodot
    fun requestSession() {
        runOnHostThread {
            sessionRequest.request()
            if (availability?.isSupported == false) checkAvailability()
        }
    }

    private fun checkAvailability() {
        ArCoreApk.getInstance().checkAvailabilityAsync(context) { result ->
            if (initialized) {
                availability = result
                sessionRequest.prepareIfRequested()
            }
        }
    }

    private fun prepareSession(retry: Boolean) {
        if (!initialized || !resumed) return
        if (permissionPending) return
        if (retry && setupState in listOf(NEEDS_INSTALL, PERMISSION_DENIED, FAILED)) {
            installRequested = false
            permissionRequested = false
        }
        val host = activity ?: return
        val support = availability ?: return
        if (!support.isSupported) {
            val state = if (support == ArCoreApk.Availability.UNSUPPORTED_DEVICE_NOT_CAPABLE) UNSUPPORTED else FAILED
            publishStatus(state, "ARCore availability: $support")
            return
        }
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
        setupState = READY
        val generation = lifecycleGeneration
        runOnRenderThread {
            if (generation != lifecycleGeneration || !resumed || !initialized) return@runOnRenderThread
            nativeStatus(READY, version)
            nativeResume()
        }
    }

    override fun onMainRequestPermissionsResult(requestCode: Int, permissions: Array<String>, grantResults: IntArray) {
        if (requestCode != CAMERA_PERMISSION_REQUEST || !initialized) return
        permissionPending = false
        val index = permissions.indexOf(Manifest.permission.CAMERA)
        if (index < 0 || grantResults.getOrNull(index) != PackageManager.PERMISSION_GRANTED) {
            publishStatus(PERMISSION_DENIED, "Camera permission was denied")
            return
        }
        sessionRequest.prepareIfRequested()
    }

    private fun publishStatus(state: Int, detail: String) {
        setupState = state
        val version = installedApkVersion()
        Log.i(TAG, "state=$state apk=$version sdk=1.56.0 $detail")
        runOnRenderThread { nativeStatus(state, version) }
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
        if (!initialized) return false
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
            if (waitingForSurface) sessionRequest.prepareIfRequested()
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

    private external fun nativeInitialize(applicationContext: Context)
    private external fun nativeStatus(state: Int, apkVersion: String)
    private external fun nativeResume()
    private external fun nativePause()
    private external fun nativeDisplayGeometry(rotation: Int, width: Int, height: Int)
    private external fun nativeShutdown()
}
