import java.security.MessageDigest

plugins {
    id("com.android.application")
}

// The editor's deploy builds this project from its Android player template
// (cmake/android_player_template.cmake) and passes every input as a Gradle property
// (-P<name>=<value>): the template's folders, the game's data and the game's identity on the device.
fun requireProperty(name: String): String = providers.gradleProperty(name).orNull
    ?: throw GradleException(
        "The Gradle property $name is missing. The editor's deploy builds this project from its Android player template."
    )

fun pathProperty(name: String): File = file(requireProperty(name))

// unravel.player: the folder holding the template's libmain.so, already stripped.
val playerLibraryDir: File = pathProperty("unravel.player")

// unravel.sdlJava: SDL's Java sources, which must match the SDL linked into libmain.so.
val sdlJavaDir: File = pathProperty("unravel.sdlJava")

// unravel.dotnet: the android-arm64 .NET runtime pack the player starts on the device, a folder
// holding its native/ and lib/<framework>/ folders. unravel.dotnetVersion: the pack's version.
val dotnetRuntimeDir: File = pathProperty("unravel.dotnet")
val dotnetRuntimeVersion = requireProperty("unravel.dotnetVersion")
val dotnetTargetFramework = "net" + dotnetRuntimeVersion.substringBeforeLast('.')

// unravel.content: the deploy folder whose data/ the APK carries.
val contentDir: File = pathProperty("unravel.content")

// The game's identity on the device. unravel.orientation is an android:screenOrientation value.
val gameApplicationId = requireProperty("unravel.applicationId")
val gameName = requireProperty("unravel.appName")
val gameVersionCode = requireProperty("unravel.versionCode").toInt()
val gameVersionName = requireProperty("unravel.versionName")
val gameOrientation = requireProperty("unravel.orientation")

// unravel.keystore and unravel.keyAlias sign release builds; the passwords come from the
// UNRAVEL_KEYSTORE_PASSWORD and UNRAVEL_KEY_PASSWORD environment variables, never from files. Debug
// builds use the Android SDK's debug keystore (~/.android/debug.keystore), created when missing.
val releaseKeystore: File? = providers.gradleProperty("unravel.keystore").orNull?.let { file(it) }

/**
 * Copies the native libraries into the <dir>/<abi>/ layout Gradle packages them from: libmain.so and
 * the .NET runtime's. They load from the APK, as Android runs no code from the app's writable storage.
 */
abstract class StagePlayerLibrary : DefaultTask() {
    @get:InputFile
    abstract val library: RegularFileProperty

    /** The runtime pack's native folder: libcoreclr.so, libclrjit.so and the BCL's native libraries. */
    @get:InputDirectory
    abstract val runtimeNativeDir: DirectoryProperty

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun stage() {
        val abiDir = outputDir.get().dir("arm64-v8a").asFile
        abiDir.deleteRecursively()
        abiDir.mkdirs()
        library.get().asFile.copyTo(abiDir.resolve("libmain.so"))
        runtimeNativeDir.get().asFile.listFiles().orEmpty()
            .filter { isRuntimeLibrary(it.name) }
            .forEach { it.copyTo(abiDir.resolve(it.name)) }
    }

    // The pack's static archives are for linking it into an executable; the debugger's libraries
    // (mscordaccore, mscordbi) are left out.
    private fun isRuntimeLibrary(name: String): Boolean = name.endsWith(".so") && !name.startsWith("libmscord")
}

val stagePlayerLibrary = tasks.register<StagePlayerLibrary>("stagePlayerLibrary") {
    library.set(playerLibraryDir.resolve("libmain.so"))
    runtimeNativeDir.set(dotnetRuntimeDir.resolve("native"))
}

/**
 * Stages the APK's assets: a deploy folder's data (packs, mounted in place on the device, and the
 * loose files the player extracts on its first start) and the .NET runtime's managed framework, a
 * loose folder of the bundled dotnet root (data/engine/dotnet). Writes bundle_files.txt (the build id
 * on the first line, then the loose files), which game/game/bundled_files.h reads.
 */
abstract class StagePlayerContent : DefaultTask() {
    /** The deploy folder, the one holding data/. */
    @get:InputDirectory
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val deployDir: DirectoryProperty

    /** The runtime pack's managed framework, System.Private.CoreLib.dll included. */
    @get:InputDirectory
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val runtimeManagedDir: DirectoryProperty

    @get:Input
    abstract val runtimeVersion: Property<String>

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun stage() {
        val output = outputDir.get().asFile
        output.deleteRecursively()
        output.mkdirs()
        val deploy = deployDir.get().asFile
        val deployFiles = deploy.resolve("data").walkTopDown()
            .filter { it.isFile }
            .map { it.relativeTo(deploy).invariantSeparatorsPath to it }
            .filter { (name, _) -> !isLeftOut(name) }
        val frameworkFolder = "data/engine/dotnet/shared/Microsoft.NETCore.App/" + runtimeVersion.get()
        val runtimeFiles = runtimeManagedDir.get().asFile.listFiles().orEmpty().asSequence()
            .filter { it.isFile }
            .map { frameworkFolder + "/" + it.name to it }
        val digest = MessageDigest.getInstance("SHA-256")
        val looseFiles = mutableListOf<String>()
        (deployFiles + runtimeFiles)
            .sortedBy { (name, _) -> name }
            .forEach { (name, file) ->
                file.copyTo(output.resolve(name))
                digest.update(name.toByteArray())
                digest.update(file.readBytes())
                if (!name.endsWith(".pak")) {
                    looseFiles += name
                }
            }
        val buildId = digest.digest().joinToString("") { "%02x".format(it) }
        val list = listOf(buildId) + looseFiles
        output.resolve("bundle_files.txt").writeText(list.joinToString("\n", postfix = "\n"))
    }

    // The desktop .NET runtime a deploy bundles does not run on Android, and debug symbols do not ship.
    private fun isLeftOut(name: String): Boolean =
        name.startsWith("data/engine/dotnet/") || name.endsWith(".pdb")
}

val stagePlayerContent = tasks.register<StagePlayerContent>("stagePlayerContent") {
    deployDir.set(contentDir)
    runtimeManagedDir.set(dotnetRuntimeDir.resolve("lib/" + dotnetTargetFramework))
    runtimeVersion.set(dotnetRuntimeVersion)
}

android {
    namespace = "com.unravel.player"
    compileSdk = 37

    defaultConfig {
        applicationId = gameApplicationId
        minSdk = 29
        targetSdk = 36
        versionCode = gameVersionCode
        versionName = gameVersionName
        manifestPlaceholders["appName"] = gameName
        manifestPlaceholders["screenOrientation"] = gameOrientation
        ndk {
            abiFilters += "arm64-v8a"
        }
    }

    if (releaseKeystore != null) {
        signingConfigs {
            create("release") {
                storeFile = releaseKeystore
                storePassword = System.getenv("UNRAVEL_KEYSTORE_PASSWORD")
                keyAlias = requireProperty("unravel.keyAlias")
                keyPassword = System.getenv("UNRAVEL_KEY_PASSWORD") ?: System.getenv("UNRAVEL_KEYSTORE_PASSWORD")
            }
        }
    }

    // The template's libmain.so is already stripped and the .NET runtime's libraries ship as they
    // are, so packaging leaves every library unchanged and needs no NDK.
    packaging {
        jniLibs {
            keepDebugSymbols += "**/*.so"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            java.directories.add(sdlJavaDir.path)
        }
    }

    // Packs are read in place, so they stay uncompressed in the APK.
    androidResources {
        noCompress += "pak"
    }

    buildTypes {
        getByName("release") {
            isMinifyEnabled = false
            if (releaseKeystore != null) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
    }
}

androidComponents {
    onVariants { variant ->
        variant.sources.jniLibs?.addGeneratedSourceDirectory(stagePlayerLibrary, StagePlayerLibrary::outputDir)
        variant.sources.assets?.addGeneratedSourceDirectory(stagePlayerContent, StagePlayerContent::outputDir)
    }
}
