#pragma once

#include <filesystem/filesystem.h>

#include <string>

namespace unravel
{

/// The list of loose files in the application's package: the build id on the first line, then one
/// '/' separated name per line, relative to the package root and to the binary folder. Written by the
/// Android build (game/android/app/build.gradle.kts).
constexpr const char* BUNDLED_FILES_LIST_NAME = "bundle_files.txt";

/**
 * @brief Copies the loose files packaged with the application into the binary folder (binary:/),
 * where the engine reads them as files: settings, script assemblies, the managed bridge. Packs stay
 * in the package and are mounted in place.
 *
 * A copy of the list is written after the files, so a later start of the same build finds it equal
 * and skips the copy, and an interrupted copy is redone. Does nothing where the application's files
 * lie in folders.
 * @return False when the package has no list or a listed file cannot be read or written (logged).
 */
auto extract_bundled_files() -> bool;

/// A file's name in the application's package: its path relative to the binary folder, '/' separated.
auto get_bundled_name(const fs::path& file) -> std::string;

} // namespace unravel
