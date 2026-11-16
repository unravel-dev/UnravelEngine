#pragma once

/// Where the engine lives on GitHub, as string literals that addresses are built from: the account that owns the
/// engine and sample repositories, the engine's repository and its latest release's downloads. The start page, the
/// About window, the update check and the sample catalog all take their addresses from here.
#define UNRAVEL_GITHUB_ACCOUNT_URL "https://github.com/unravel-dev"
/// Where GitHub serves the files of the account's repositories as they are; a repository, a commit and a path follow.
#define UNRAVEL_GITHUB_RAW_FILES_URL "https://raw.githubusercontent.com/unravel-dev/"
#define UNRAVEL_GITHUB_REPOSITORY_URL UNRAVEL_GITHUB_ACCOUNT_URL "/UnravelEngine"
/// The latest release's attached files; a file name follows. A release download, not a request to the GitHub API,
/// so the API's limit per address does not apply.
#define UNRAVEL_GITHUB_LATEST_DOWNLOAD_URL UNRAVEL_GITHUB_REPOSITORY_URL "/releases/latest/download/"
