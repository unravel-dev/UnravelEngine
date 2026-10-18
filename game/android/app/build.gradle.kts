import java.security.MessageDigest

plugins {
    id("com.android.application")
}

val unravelRoot: File = rootDir.resolve("../..").canonicalFile

// The inputs come from Gradle properties (-P<name>=<value>). Without them the build uses this source
// checkout, as run_on_device.py does. The editor's deploy passes its Android player template's
// folders instead (cmake/android_player_template.cmake), so it needs no NDK and no checkout.
fun pathProperty(name: String): File? = providers.gradleProperty(name).orNull?.let { file(it) }
fun textProperty(name: String, default: String): String = providers.gradleProperty(name).orNull ?: default

// unravel.player: the folder holding a template's libmain.so, already stripped. Default: the
// android-arm64 CMake preset's build, stripped here (cmake --preset android-arm64 &&
// cmake --build --preset android-arm64).
val templatePlayerDir: File? = pathProperty("unravel.player")
val isTemplateBuild = templatePlayerDir != null
val playerLibraryDir: File = templatePlayerDir ?: unravelRoot.resolve("develop/build/android-arm64/bin")

// unravel.sdlJava: SDL's Java sources, which must match the SDL linked into libmain.so.
val sdlJavaDir: File = pathProperty("unravel.sdlJava")
    ?: unravelRoot.resolve("deps/ospp/ospp/3rdparty/SDL/SDL/android-project/app/src/main/java")

// The .NET runtime the player starts on the device: this version of the android-arm64 runtime pack.
// unravel.dotnet: a folder holding the pack's native/ and lib/<framework>/ folders. Default: the pack
// in the .NET SDK's global packages folder (NUGET_PACKAGES, or ~/.nuget/packages), restored there.
val dotnetRuntimeVersion = textProperty("unravel.dotnetVersion", "10.0.13")
val dotnetRuntimePackage = "Microsoft.NETCore.App.Runtime.android-arm64"
val dotnetTargetFramework = "net" + dotnetRuntimeVersion.substringBeforeLast('.')
val providedDotnetDir: File? = pathProperty("unravel.dotnet")
val nugetPackagesDir: File = File(System.getenv("NUGET_PACKAGES") ?: System.getProperty("user.home") + "/.nuget/packages")
val dotnetRuntimeDir: File = providedDotnetDir
    ?: nugetPackagesDir.resolve(dotnetRuntimePackage.lowercase() + "/" + dotnetRuntimeVersion + "/runtimes/android-arm64")

// The game's identity on the device, set by the deploy. unravel.orientation is an
// android:screenOrientation value.
val gameApplicationId = textProperty("unravel.applicationId", "com.unravel.player")
val gameName = textProperty("unravel.appName", "Unravel Player")
val gameVersionCode = textProperty("unravel.versionCode", "1").toInt()
val gameVersionName = textProperty("unravel.versionName", "1.0")
val gameOrientation = textProperty("unravel.orientation", "unspecified")

// unravel.keystore and unravel.keyAlias sign release builds; the passwords come from the
// UNRAVEL_KEYSTORE_PASSWORD and UNRAVEL_KEY_PASSWORD environment variables, never from files. Debug
// builds use the Android SDK's debug keystore (~/.android/debug.keystore), created when missing.
val releaseKeystore: File? = pathProperty("unravel.keystore")

val restoreDotnetRuntime = if (providedDotnetDir != null) null else tasks.register<Exec>("restoreDotnetRuntime") {
    val projectFile = layout.buildDirectory.file("dotnet-runtime/restore.csproj").get().asFile
    inputs.property("version", dotnetRuntimeVersion)
    outputs.dir(dotnetRuntimeDir)
    doFirst {
        projectFile.parentFile.mkdirs()
        projectFile.writeText(
            """
            <Project Sdk="Microsoft.NET.Sdk">
              <PropertyGroup>
                <TargetFramework>$dotnetTargetFramework</TargetFramework>
              </PropertyGroup>
              <ItemGroup>
                <PackageDownload Include="$dotnetRuntimePackage" Version="[$dotnetRuntimeVersion]" />
              </ItemGroup>
            </Project>
            """.trimIndent()
        )
    }
    commandLine("dotnet", "restore", projectFile.path)
}

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
    restoreDotnetRuntime?.let { dependsOn(it) }
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
    /** The deploy folder (the one holding data/); without it the APK carries no content. */
    @get:Optional
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
        val deploy = deployDir.orNull?.asFile ?: return
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
    restoreDotnetRuntime?.let { dependsOn(it) }
    // gradlew assembleDebug -Punravel.content=<deploy folder>
    providers.gradleProperty("unravel.content").orNull?.let { deployDir.set(file(it)) }
    runtimeManagedDir.set(dotnetRuntimeDir.resolve("lib/" + dotnetTargetFramework))
    runtimeVersion.set(dotnetRuntimeVersion)
}

android {
    namespace = "com.unravel.player"
    compileSdk = 37
    // A template's library is already stripped, so only the checkout's build needs the NDK.
    if (!isTemplateBuild) {
        ndkVersion = "30.0.16248370"
    }

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
                keyAlias = textProperty("unravel.keyAlias", "")
                keyPassword = System.getenv("UNRAVEL_KEY_PASSWORD") ?: System.getenv("UNRAVEL_KEYSTORE_PASSWORD")
            }
        }
    }

    packaging {
        jniLibs {
            if (isTemplateBuild) {
                keepDebugSymbols += "**/*.so"
            }
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
