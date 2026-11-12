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

// unravel.symbols: the folder holding <abi>/libmain.so.sym, the player's symbol table.
val playerSymbolsDir: File = pathProperty("unravel.symbols")

// unravel.sdlJava: SDL's Java sources, which must match the SDL linked into libmain.so.
val sdlJavaDir: File = pathProperty("unravel.sdlJava")

// unravel.dotnet: the android-arm64 .NET runtime pack the player starts on the device, a folder
// holding its native/ and lib/<framework>/ folders. unravel.dotnetVersion: the pack's version.
val dotnetRuntimeDir: File = pathProperty("unravel.dotnet")
val dotnetRuntimeVersion = requireProperty("unravel.dotnetVersion")
val dotnetTargetFramework = "net" + dotnetRuntimeVersion.substringBeforeLast('.')

// unravel.content: the folder whose files the APK carries: the game's data/, and the profile's extra files.
val contentDir: File = pathProperty("unravel.content")

// The game's identity on the device. unravel.orientation is an android:screenOrientation value.
val gameApplicationId = requireProperty("unravel.applicationId")
val gameName = requireProperty("unravel.appName")
val gameVersionCode = requireProperty("unravel.versionCode").toInt()
val gameVersionName = requireProperty("unravel.versionName")
val gameOrientation = requireProperty("unravel.orientation")

// unravel.icon: the launcher icon's resources, an adaptive icon the deploy makes from the game's icon image.
val iconResourceDir: File = pathProperty("unravel.icon")

// unravel.permissions: a manifest holding only the game's uses-permission elements, which the deploy writes
// from the profile. Each build type's manifest is merged over the main one, so the permissions join it.
val permissionsManifest: File = pathProperty("unravel.permissions")

// unravel.additions: a folder of the project with parts the package takes as they are, when the profile names one:
// java/ (Java sources), res/ (resources), libs/ (.jar and .aar libraries), jniLibs/<abi>/ (native libraries) and
// dependencies.txt (libraries Gradle downloads, one Maven coordinate per line; a line that starts with # is a
// comment). Its AndroidManifest.xml reaches this build inside unravel.permissions, where the deploy merged the
// permissions into it.
val additionsDir: File? = providers.gradleProperty("unravel.additions").orNull?.let { file(it) }

// unravel.trimmedFramework: a file naming, one a line, the files of the runtime's managed framework the
// package leaves out: the assemblies nothing in the game references, when the deploy trims the framework.
val trimmedFrameworkList: File = pathProperty("unravel.trimmedFramework")

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
 * Stages the APK's assets: the content folder's files (packs, mounted in place on the device, and the
 * loose files the player extracts on its first start: the rest of the game's data and the profile's
 * extra files) and the .NET runtime's managed framework, a loose folder of the bundled dotnet root
 * (data/engine/dotnet). Writes bundle_files.txt (the build id on the first line, then the loose
 * files), which game/game/bundled_files.h reads.
 */
abstract class StagePlayerContent : DefaultTask() {
    /** The content folder: data/ and whatever else ships beside it. */
    @get:InputDirectory
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val deployDir: DirectoryProperty

    /** The runtime pack's managed framework, System.Private.CoreLib.dll included. */
    @get:InputDirectory
    @get:PathSensitive(PathSensitivity.RELATIVE)
    abstract val runtimeManagedDir: DirectoryProperty

    /** Names, one a line, the framework's files the package leaves out; a file without names leaves none out. */
    @get:InputFile
    @get:PathSensitive(PathSensitivity.NONE)
    abstract val trimmedFramework: RegularFileProperty

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
        val deployFiles = deploy.walkTopDown()
            .filter { it.isFile }
            .map { it.relativeTo(deploy).invariantSeparatorsPath to it }
            .filter { (name, _) -> !isLeftOut(name) }
        val frameworkFolder = "data/engine/dotnet/shared/Microsoft.NETCore.App/" + runtimeVersion.get()
        val trimmed = trimmedFramework.get().asFile.readLines().filter { it.isNotBlank() }.toSet()
        val runtimeFiles = runtimeManagedDir.get().asFile.listFiles().orEmpty().asSequence()
            .filter { it.isFile && it.name !in trimmed }
            .map { frameworkFolder + "/" + it.name to it }
        val digest = MessageDigest.getInstance("SHA-256")
        val looseFiles = mutableListOf<String>()
        (deployFiles + runtimeFiles)
            .sortedBy { (name, _) -> name }
            .forEach { (name, file) ->
                digest.update(name.toByteArray())
                copyAndDigest(file, output.resolve(name), digest)
                if (!name.endsWith(".pak")) {
                    looseFiles += name
                }
            }
        val buildId = digest.digest().joinToString("") { "%02x".format(it) }
        val list = listOf(buildId) + looseFiles
        output.resolve("bundle_files.txt").writeText(list.joinToString("\n", postfix = "\n"))
    }

    // Copies a file block by block and adds its bytes to the digest on the way: a pack of any size passes through
    // a megabyte of memory.
    private fun copyAndDigest(source: File, destination: File, digest: MessageDigest) {
        destination.parentFile.mkdirs()
        val block = ByteArray(COPY_BLOCK_SIZE)
        source.inputStream().use { input ->
            destination.outputStream().use { output ->
                while (true) {
                    val read = input.read(block)
                    if (read < 0) {
                        break
                    }
                    output.write(block, 0, read)
                    digest.update(block, 0, read)
                }
            }
        }
    }

    // The desktop .NET runtime a deploy bundles does not run on Android. Debug symbols ship when the deploy put
    // them there, which its Ship Debug Symbols decides.
    private fun isLeftOut(name: String): Boolean = name.startsWith("data/engine/dotnet/")

    private companion object {
        const val COPY_BLOCK_SIZE = 1 shl 20
    }
}

val stagePlayerContent = tasks.register<StagePlayerContent>("stagePlayerContent") {
    deployDir.set(contentDir)
    runtimeManagedDir.set(dotnetRuntimeDir.resolve("lib/" + dotnetTargetFramework))
    trimmedFramework.set(trimmedFrameworkList)
    runtimeVersion.set(dotnetRuntimeVersion)
}

// The file Google Play takes beside a bundle to name functions in its crash reports. Gradle's own
// ndk.debugSymbolLevel would need the NDK, which an editor's users do not have.
tasks.register<Zip>("packageNativeDebugSymbols") {
    from(playerSymbolsDir)
    archiveFileName.set("native-debug-symbols.zip")
    destinationDirectory.set(layout.buildDirectory.dir("outputs/native-debug-symbols"))
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
            res.directories.add(iconResourceDir.path)
            if (additionsDir != null) {
                java.directories.add(additionsDir.resolve("java").path)
                res.directories.add(additionsDir.resolve("res").path)
                jniLibs.directories.add(additionsDir.resolve("jniLibs").path)
            }
        }
        getByName("debug") {
            manifest.srcFile(permissionsManifest)
        }
        getByName("release") {
            manifest.srcFile(permissionsManifest)
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

// The Java half of the .NET runtime's cryptography library (libSystem.Security.Cryptography.Native.Android.so),
// which reaches Android's hashing, certificates and TLS through these classes: without them a script's first hash
// or https:// request ends the game.
dependencies {
    implementation(fileTree(dotnetRuntimeDir.resolve("native")) { include("*.jar") })
    // The project's own libraries: the files in its additions folder, and the ones its list has Gradle download.
    if (additionsDir != null) {
        implementation(fileTree(additionsDir.resolve("libs")) { include("*.jar", "*.aar") })
        val listed = additionsDir.resolve("dependencies.txt")
        if (listed.isFile) {
            listed.readLines().map { it.trim() }.filter { it.isNotEmpty() && !it.startsWith("#") }.forEach { implementation(it) }
        }
    }
}

androidComponents {
    onVariants { variant ->
        variant.sources.jniLibs?.addGeneratedSourceDirectory(stagePlayerLibrary, StagePlayerLibrary::outputDir)
        variant.sources.assets?.addGeneratedSourceDirectory(stagePlayerContent, StagePlayerContent::outputDir)
    }
}
