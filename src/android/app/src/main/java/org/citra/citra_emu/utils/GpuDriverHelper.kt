// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.widget.Toast
import androidx.documentfile.provider.DocumentFile
import java.io.BufferedInputStream
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.lang.IllegalStateException
import java.security.MessageDigest
import java.util.zip.ZipEntry
import java.util.zip.ZipException
import java.util.zip.ZipInputStream
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.DriverSettings
import org.citra.citra_emu.utils.FileUtil.asDocumentFile
import org.citra.citra_emu.utils.FileUtil.inputStream

object GpuDriverHelper {
    private const val META_JSON_FILENAME = "meta.json"
    private var fileRedirectionPath: String? = null
    var driverInstallationPath: String? = null
    private var hookLibPath: String? = null

    val driverStoragePath: DocumentFile
        get() {
            // Bypass directory initialization checks
            val root = DocumentFile.fromTreeUri(
                CitraApplication.appContext,
                Uri.parse(DirectoryInitialization.userPath)
            )!!
            var driverDirectory = root.findFile("gpu_drivers")
            if (driverDirectory == null) {
                driverDirectory = FileUtil.createDir(root.uri.toString(), "gpu_drivers")
            }
            return driverDirectory!!
        }

    fun initializeDriverParameters() {
        try {
            // Initialize the file redirection directory.
            fileRedirectionPath =
                DirectoryInitialization.internalUserPath + "/gpu/vk_file_redirect/"

            // Initialize the driver installation directory.
            driverInstallationPath = CitraApplication.appContext
                .filesDir.canonicalPath + "/gpu_driver/"
        } catch (e: IOException) {
            throw RuntimeException(e)
        }

        // Initialize directories.
        initializeDirectories()

        // Initialize hook libraries directory.
        hookLibPath = CitraApplication.appContext.applicationInfo.nativeLibraryDir + "/"

        // Initialize GPU driver.
        NativeLibrary.initializeGpuDriver(
            hookLibPath,
            driverInstallationPath,
            customDriverData.libraryName,
            fileRedirectionPath
        )
    }

    // CodexAstraLocal: Resolve a title's driver before native creation without overwriting the global install.
    @Synchronized
    fun prepareDriverForGame(titleId: String): Boolean {
        val started = SystemClock.elapsedRealtime()
        val requested = DriverSettings.load(titleId)
        val globalDirectory = driverInstallationPath ?: return false
        val globalMetadata = customDriverData
        var directory = globalDirectory
        var metadata = globalMetadata
        var fallback = false
        if (requested?.driver == DriverSettings.SYSTEM) {
            metadata = GpuDriverMetadata()
        } else if (requested != null) {
            try {
                val archive = driverStoragePath.listFiles().firstOrNull {
                    it.uri.toString() == requested.driver && it.isFile
                } ?: throw IOException("Driver archive was removed")
                val prepared = prepareImmutableDriver(archive)
                directory = prepared.first.canonicalPath + "/"
                metadata = prepared.second
            } catch (error: Exception) {
                fallback = true
                Log.warning("GPU driver preparation fallback: title=$titleId requested=${requested.label} reason=${error.message}")
            }
        }
        var outcome = NativeLibrary.initializeGpuDriver(
            hookLibPath, directory, metadata.libraryName, fileRedirectionPath
        )
        // CodexAstraLocal: Native reports the actual library outcome. A title driver that fails
        // to load retries the saved global choice before settling on the system driver.
        if (outcome == 1 && metadata.libraryName != null) {
            fallback = true
            if (requested != null && requested.driver != DriverSettings.SYSTEM && directory != globalDirectory) {
                directory = globalDirectory
                metadata = globalMetadata
                outcome = NativeLibrary.initializeGpuDriver(
                    hookLibPath, directory, metadata.libraryName, fileRedirectionPath
                )
            }
        }
        val ready = outcome != 0
        val effective = when (outcome) {
            2 -> metadata.name ?: metadata.libraryName ?: "Custom"
            1 -> "System"
            else -> "Unavailable"
        }
        if (fallback) {
            Handler(Looper.getMainLooper()).post {
                Toast.makeText(CitraApplication.appContext, R.string.uberhar_game_driver_failed, Toast.LENGTH_LONG).show()
            }
        }
        Log.info("GPU driver selection: title=$titleId requested=${requested?.label ?: "Global"} effective=$effective fallback=$fallback ready=$ready prepare_ms=${SystemClock.elapsedRealtime() - started}")
        // CodexAstraLocal: The accepted native lifetime gate proves prior renderer/worker users drained.
        // Keep eight recent immutable installations and 512 MiB total, never deleting this selection.
        if (ready) pruneDriverInstallations(directory)
        return ready
    }

    private fun prepareImmutableDriver(archive: DocumentFile): Pair<File, GpuDriverMetadata> {
        val cacheRoot = File(CitraApplication.appContext.filesDir, "gpu_driver_titles")
        check(cacheRoot.mkdirs() || cacheRoot.isDirectory)
        // CodexAstraLocal: Only incomplete private staging files are removed here; live immutable installs stay untouched.
        cacheRoot.listFiles()?.filter {
            it.name.endsWith(".pending") || (it.name.startsWith("driver-") && it.name.endsWith(".zip"))
        }?.forEach { it.deleteRecursively() }
        val snapshot = File.createTempFile("driver-", ".zip", cacheRoot)
        try {
            // CodexAstraLocal: Hash, metadata and extraction consume one bounded private snapshot,
            // so external archive replacement cannot poison its content-addressed installation.
            val digest = MessageDigest.getInstance("SHA-256")
            archive.inputStream().use { input ->
                snapshot.outputStream().use { output ->
                    val chunk = ByteArray(64 * 1024)
                    var total = 0L
                    var count = input.read(chunk)
                    while (count >= 0) {
                        if (count > 0) {
                            total += count
                            require(total <= 128L * 1024 * 1024) { "Driver archive exceeds 128 MiB" }
                            digest.update(chunk, 0, count)
                            output.write(chunk, 0, count)
                        }
                        count = input.read(chunk)
                    }
                    require(total > 0) { "Empty driver archive" }
                }
            }
            val identity = digest.digest().joinToString("") { "%02x".format(it) }
            val installed = File(cacheRoot, identity)
            if (!installed.isDirectory) {
                val staging = File(cacheRoot, "$identity.pending")
                staging.deleteRecursively()
                check(staging.mkdirs())
                try {
                    extractBoundedDriver(snapshot, staging)
                    val candidate = GpuDriverMetadata(File(staging, META_JSON_FILENAME))
                    requireValidLibrary(staging, candidate)
                    check(staging.renameTo(installed)) { "Could not publish driver files" }
                } finally {
                    staging.deleteRecursively()
                }
            }
            val metadata = GpuDriverMetadata(File(installed, META_JSON_FILENAME))
            requireValidLibrary(installed, metadata)
            installed.setLastModified(System.currentTimeMillis())
            return Pair(installed, metadata)
        } finally {
            snapshot.delete()
        }
    }

    private fun extractBoundedDriver(snapshot: File, directory: File) {
        // CodexAstraLocal: Limit expanded bytes and entries as well as compressed input; never follow ZIP paths outside staging.
        val prefix = directory.canonicalPath + "/"
        ZipInputStream(BufferedInputStream(snapshot.inputStream())).use { zip ->
            var entries = 0
            var total = 0L
            val chunk = ByteArray(64 * 1024)
            var entry = zip.nextEntry
            while (entry != null) {
                require(++entries <= 128) { "Driver archive exceeds 128 entries" }
                val output = File(directory, entry.name)
                require(output.canonicalPath.startsWith(prefix)) { "Driver archive path escapes installation" }
                if (entry.isDirectory) {
                    require(zip.read() == -1) { "Driver directory entry contains data" }
                    check(output.mkdirs() || output.isDirectory)
                } else {
                    check(output.parentFile!!.mkdirs() || output.parentFile!!.isDirectory)
                    output.outputStream().use { destination ->
                        var fileBytes = 0L
                        var count = zip.read(chunk)
                        while (count >= 0) {
                            if (count > 0) {
                                fileBytes += count
                                total += count
                                require(fileBytes <= 128L * 1024 * 1024 && total <= 256L * 1024 * 1024) {
                                    "Expanded driver exceeds file/total byte limit"
                                }
                                destination.write(chunk, 0, count)
                            }
                            count = zip.read(chunk)
                        }
                    }
                }
                entry = zip.nextEntry
            }
        }
    }

    private fun requireValidLibrary(directory: File, metadata: GpuDriverMetadata) {
        require(metadata.name != null && metadata.minApi <= Build.VERSION.SDK_INT) { "Unsupported driver metadata/API" }
        val name = metadata.libraryName ?: throw IOException("Missing driver library")
        require(name.isNotEmpty() && File(name).name == name) { "Invalid driver library name" }
        val library = File(directory, name)
        require(library.isFile && library.canonicalPath.startsWith(directory.canonicalPath + "/")) {
            "Driver library is missing or outside installation"
        }
    }

    private fun pruneDriverInstallations(selectedDirectory: String) {
        val root = File(CitraApplication.appContext.filesDir, "gpu_driver_titles")
        val installations = root.listFiles()?.filter { it.isDirectory && !it.name.endsWith(".pending") }
            ?.sortedByDescending { it.lastModified() } ?: return
        var bytes = 0L
        var retained = 0
        // CodexAstraLocal: Account for the selected directory first so older entries cannot displace it.
        val selected = installations.filter { it.canonicalPath + "/" == selectedDirectory }
        val ordered = selected + installations.filterNot { it in selected }
        ordered.forEach { directory ->
            val size = directory.walkTopDown().filter { it.isFile }.sumOf { it.length() }
            if (directory in selected || (retained < 8 && bytes + size <= 512L * 1024 * 1024)) {
                ++retained
                bytes += size
            } else directory.deleteRecursively()
        }
    }

    fun getDrivers(): MutableList<Pair<Uri, GpuDriverMetadata>> {
        val driverZips = driverStoragePath.listFiles()
        val drivers: MutableList<Pair<Uri, GpuDriverMetadata>> =
            driverZips
                .mapNotNull {
                    val metadata = getMetadataFromZip(it.inputStream())
                    metadata.name?.let { _ -> Pair(it.uri, metadata) }
                }
                .sortedByDescending { it: Pair<Uri, GpuDriverMetadata> -> it.second.name }
                .distinct()
                .toMutableList()

        // TODO: Get system driver information
        drivers.add(0, Pair(Uri.EMPTY, GpuDriverMetadata()))
        return drivers
    }

    fun installDefaultDriver() {
        // Removing the installed driver will result in the backend using the default system driver.
        File(driverInstallationPath!!).deleteRecursively()
        initializeDriverParameters()
    }

    fun copyDriverToExternalStorage(driverUri: Uri): DocumentFile? {
        // Ensure we have directories.
        initializeDirectories()

        // Copy the zip file URI to user data
        val copiedFile =
            FileUtil.copyToExternalStorage(driverUri, driverStoragePath) ?: return null

        // Validate driver
        val metadata = getMetadataFromZip(copiedFile.inputStream())
        if (metadata.name == null) {
            copiedFile.delete()
            return null
        }

        if (metadata.minApi > Build.VERSION.SDK_INT) {
            copiedFile.delete()
            return null
        }
        return copiedFile
    }

    /**
     * Copies driver zip into user data directory so that it can be exported along with
     * other user data and also unzipped into the installation directory
     */
    fun installCustomDriverComplete(driverUri: Uri): Boolean {
        // Revert to system default in the event the specified driver is bad.
        installDefaultDriver()

        // Ensure we have directories.
        initializeDirectories()

        // Copy the zip file URI to user data
        val copiedFile =
            FileUtil.copyToExternalStorage(driverUri, driverStoragePath) ?: return false

        // Validate driver
        val metadata = getMetadataFromZip(copiedFile.inputStream())
        if (metadata.name == null) {
            copiedFile.delete()
            return false
        }

        if (metadata.minApi > Build.VERSION.SDK_INT) {
            copiedFile.delete()
            return false
        }

        // Unzip the driver.
        try {
            FileUtil.unzipToInternalStorage(
                BufferedInputStream(copiedFile.inputStream()),
                File(driverInstallationPath!!)
            )
        } catch (e: SecurityException) {
            return false
        }

        // Initialize the driver parameters.
        initializeDriverParameters()

        return true
    }

    /**
     * Unzips driver into private installation directory
     */
    fun installCustomDriverPartial(driver: Uri): Boolean {
        // Revert to system default in the event the specified driver is bad.
        installDefaultDriver()

        // Ensure we have directories.
        initializeDirectories()

        // Validate driver
        val metadata = getMetadataFromZip(driver.inputStream())
        if (metadata.name == null) {
            driver.asDocumentFile()?.delete()
            return false
        }

        // Unzip the driver to the private installation directory
        try {
            FileUtil.unzipToInternalStorage(
                BufferedInputStream(driver.inputStream()),
                File(driverInstallationPath!!)
            )
        } catch (e: SecurityException) {
            return false
        }

        // Initialize the driver parameters.
        initializeDriverParameters()

        return true
    }

    /**
     * Takes in a zip file and reads the meta.json file for presentation to the UI
     *
     * @param driver Zip containing driver and meta.json file
     * @return A non-null [GpuDriverMetadata] instance that may have null members
     */
    fun getMetadataFromZip(driver: InputStream): GpuDriverMetadata {
        try {
            ZipInputStream(driver).use { zis ->
                var entry: ZipEntry? = zis.nextEntry
                while (entry != null) {
                    if (!entry.isDirectory && entry.name.lowercase().contains(".json")) {
                        val size = if (entry.size == -1L) 0L else entry.size
                        return GpuDriverMetadata(zis, size)
                    }
                    entry = zis.nextEntry
                }
            }
        } catch (_: ZipException) {
        }
        return GpuDriverMetadata()
    }

    external fun supportsCustomDriverLoading(): Boolean

    // Parse the custom driver metadata to retrieve the name.
    val customDriverData: GpuDriverMetadata
        get() = GpuDriverMetadata(File(driverInstallationPath + META_JSON_FILENAME))

    fun initializeDirectories() {
        // Ensure the file redirection directory exists.
        val fileRedirectionDir = File(fileRedirectionPath!!)
        if (!fileRedirectionDir.exists()) {
            fileRedirectionDir.mkdirs()
        }
        // Ensure the driver installation directory exists.
        val driverInstallationDir = File(driverInstallationPath!!)
        if (!driverInstallationDir.exists()) {
            driverInstallationDir.mkdirs()
        }
        // Ensure the driver storage directory exists
        if (!driverStoragePath.exists()) {
            throw IllegalStateException("Driver storage directory couldn't be created!")
        }
    }
}
