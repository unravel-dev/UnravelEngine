/*
 * Validation suite for the agent docs: the root and directory AGENTS.md files and the skills
 * under .agents/skills. Agents trust a path a doc names; a doc that names a moved file sends
 * them to read code that is not there.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "agent docs"
 *
 * This pins, for every doc:
 *   - each repository path (engine/, editor/, game/, deps/, tests/, cmake/, the data folders,
 *     .agents/, .github/) in a backticked token exists, also as one word of a command, and each
 *     scripts/ path a skill names exists in its folder; words with wildcards or <placeholders>
 *     and example files named my_* are not paths;
 *   - each relative markdown link resolves from the doc's folder;
 *   - no doc names a file under tasks/, which is local and not in git;
 *   - the text is ASCII, and so are the skills' helper scripts (.py, .sh, .ps1 under .agents/).
 * And for every skill folder: SKILL.md opens with frontmatter whose name is the folder's name,
 * a description of at most 1024 characters and no paths key (Claude Code lists a skill with
 * paths only after a matching file was read), and stays within 500 lines.
 */

#include "../tests.h"

#include <filesystem/filesystem.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace unravel;

namespace
{

/// Path prefixes that name a file of the repository. .claude/ is left out: its skills link is
/// made per checkout and is not in git.
constexpr std::array<const char*, 11> REPOSITORY_ROOTS = {"engine/",  "editor/",      "game/",        "deps/",
                                                          "tests/",   "cmake/",       ".agents/",     ".github/",
                                                          "engine_data/", "editor_data/", "CMakeLists.txt"};
/// Folders the doc search never enters.
constexpr std::array<const char*, 9> SKIPPED_FOLDERS = {".git",    "build",     "3rdparty", "compiled",    ".claude",
                                                        ".cursor", "tasks",     "node_modules", ".fetchcontent-cache"};
/// Characters that make a backticked token a pattern or a placeholder rather than a path.
constexpr const char* PATTERN_CHARACTERS = "*?<>{}$| ";
/// The skill limits of the Agent Skills format.
constexpr size_t MAX_DESCRIPTION_LENGTH = 1024;
constexpr size_t MAX_SKILL_LINES = 500;

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto get_repository_dir() -> fs::path
{
    return fs::path(REPOSITORY_DIR).lexically_normal();
}

auto read_lines(const fs::path& file) -> std::vector<std::string>
{
    std::ifstream stream(file, std::ios::binary);
    std::vector<std::string> lines;
    std::string line;
    while(std::getline(stream, line))
    {
        if(!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

auto is_skipped_folder(const fs::path& folder) -> bool
{
    const std::string name = folder.filename().string();
    return std::find(SKIPPED_FOLDERS.begin(), SKIPPED_FOLDERS.end(), name) != SKIPPED_FOLDERS.end();
}

/// Every AGENTS.md of the repository and every markdown file under .agents/.
auto find_docs(const fs::path& repository) -> std::vector<fs::path>
{
    std::vector<fs::path> docs;
    for(auto it = fs::recursive_directory_iterator(repository); it != fs::recursive_directory_iterator(); ++it)
    {
        if(it->is_directory() && is_skipped_folder(it->path()))
        {
            it.disable_recursion_pending();
            continue;
        }
        if(it->is_regular_file() && it->path().filename() == "AGENTS.md")
        {
            docs.push_back(it->path());
        }
    }
    for(const auto& entry : fs::recursive_directory_iterator(repository / ".agents"))
    {
        if(entry.is_regular_file() && entry.path().extension() == ".md")
        {
            docs.push_back(entry.path());
        }
    }
    return docs;
}

auto is_repository_path(const std::string& token) -> bool
{
    const bool has_root = std::any_of(REPOSITORY_ROOTS.begin(),
                                      REPOSITORY_ROOTS.end(),
                                      [&](const char* root) { return token.rfind(root, 0) == 0; });
    const bool is_pattern =
        token.find_first_of(PATTERN_CHARACTERS) != std::string::npos || token.find("...") != std::string::npos;
    const bool is_example = fs::path(token).filename().string().rfind("my_", 0) == 0;
    return has_root && !is_pattern && !is_example;
}

auto describe(const fs::path& doc, size_t line_index, const fs::path& repository) -> std::string
{
    return doc.lexically_relative(repository).generic_string() + ":" + std::to_string(line_index + 1);
}

/// The words of a backticked token: a command such as `python .agents/.../check.py` names its
/// script as one of them.
auto split_words(const std::string& token) -> std::vector<std::string>
{
    std::istringstream stream(token);
    std::vector<std::string> words;
    for(std::string word; stream >> word;)
    {
        words.push_back(word);
    }
    return words;
}

void check_word(const fs::path& doc, size_t line_index, const std::string& word, const fs::path& repository)
{
    const bool names_task_file = word.rfind("tasks/", 0) == 0 && word.size() > 6;
    check(!names_task_file, describe(doc, line_index, repository) + " names " + word + ", which is not in git");
    if(is_repository_path(word))
    {
        check(fs::exists(repository / word), describe(doc, line_index, repository) + " names a missing path: " + word);
    }
    // A skill names its own helper scripts relative to its folder.
    const bool is_skill_script = word.rfind("scripts/", 0) == 0 && word.size() > std::string("scripts/").size() &&
                                 word.find_first_of(PATTERN_CHARACTERS) == std::string::npos;
    if(is_skill_script)
    {
        check(fs::exists(doc.parent_path() / word), describe(doc, line_index, repository) + " names a missing script: " + word);
    }
}

void check_backticked_paths(const fs::path& doc, const std::vector<std::string>& lines, const fs::path& repository)
{
    const std::regex backticked("`([^`]+)`");
    for(size_t i = 0; i < lines.size(); ++i)
    {
        for(std::sregex_iterator it(lines[i].begin(), lines[i].end(), backticked), end; it != end; ++it)
        {
            for(const std::string& word : split_words((*it)[1].str()))
            {
                check_word(doc, i, word, repository);
            }
        }
    }
}

void check_relative_links(const fs::path& doc, const std::vector<std::string>& lines, const fs::path& repository)
{
    const std::regex link(R"(\]\(([^)#\s]+)(#[^)]*)?\))");
    for(size_t i = 0; i < lines.size(); ++i)
    {
        for(std::sregex_iterator it(lines[i].begin(), lines[i].end(), link), end; it != end; ++it)
        {
            const std::string target = (*it)[1].str();
            if(target.find("://") != std::string::npos)
            {
                continue;
            }
            check(fs::exists(doc.parent_path() / target), describe(doc, i, repository) + " links to a missing file: " + target);
        }
    }
}

void check_ascii(const fs::path& doc, const std::vector<std::string>& lines, const fs::path& repository)
{
    for(size_t i = 0; i < lines.size(); ++i)
    {
        const bool is_ascii = std::all_of(lines[i].begin(),
                                          lines[i].end(),
                                          [](char c) { return static_cast<unsigned char>(c) < 128; });
        check(is_ascii, describe(doc, i, repository) + " has a non-ASCII character");
    }
}

void test_docs_name_existing_paths()
{
    std::printf("test_docs_name_existing_paths\n");
    const fs::path repository = get_repository_dir();
    const std::vector<fs::path> docs = find_docs(repository);
    check(docs.size() > 1, "the root AGENTS.md and the skills are found");
    for(const fs::path& doc : docs)
    {
        const std::vector<std::string> lines = read_lines(doc);
        check_backticked_paths(doc, lines, repository);
        check_relative_links(doc, lines, repository);
        check_ascii(doc, lines, repository);
    }
}

/// Skill helper scripts are repository text too: ASCII like the docs.
void test_skill_scripts_are_ascii()
{
    std::printf("test_skill_scripts_are_ascii\n");
    const fs::path repository = get_repository_dir();
    size_t script_count = 0;
    for(const auto& entry : fs::recursive_directory_iterator(repository / ".agents"))
    {
        const std::string extension = entry.path().extension().string();
        if(entry.is_regular_file() && (extension == ".py" || extension == ".sh" || extension == ".ps1"))
        {
            check_ascii(entry.path(), read_lines(entry.path()), repository);
            ++script_count;
        }
    }
    check(script_count > 0, "the skill scripts are found");
}

/// The value of a frontmatter key, or empty. Folded values (>-) join their indented lines.
auto get_frontmatter_value(const std::vector<std::string>& lines, size_t end, const std::string& key) -> std::string
{
    const std::string prefix = key + ":";
    for(size_t i = 1; i < end; ++i)
    {
        if(lines[i].rfind(prefix, 0) != 0)
        {
            continue;
        }
        std::string value = lines[i].substr(prefix.size());
        value.erase(0, value.find_first_not_of(' '));
        if(value != ">-" && value != ">" && value != "|")
        {
            return value;
        }
        std::ostringstream folded;
        for(size_t j = i + 1; j < end && !lines[j].empty() && lines[j].front() == ' '; ++j)
        {
            folded << (j > i + 1 ? " " : "") << lines[j].substr(lines[j].find_first_not_of(' '));
        }
        return folded.str();
    }
    return {};
}

/// Whether the frontmatter between line 1 and @p end has @p key, whatever its value.
auto has_frontmatter_key(const std::vector<std::string>& lines, size_t end, const std::string& key) -> bool
{
    const std::string prefix = key + ":";
    return std::any_of(lines.begin() + 1,
                       lines.begin() + static_cast<std::ptrdiff_t>(end),
                       [&](const std::string& line) { return line.rfind(prefix, 0) == 0; });
}

void check_skill(const fs::path& folder, const fs::path& repository)
{
    const fs::path skill_file = folder / "SKILL.md";
    const std::string name = folder.filename().string();
    check(fs::exists(skill_file), name + " has a SKILL.md");
    if(!fs::exists(skill_file))
    {
        return;
    }
    const std::vector<std::string> lines = read_lines(skill_file);
    const auto closing = std::find(lines.begin() + std::min<size_t>(1, lines.size()), lines.end(), "---");
    const size_t end = static_cast<size_t>(closing - lines.begin());
    check(!lines.empty() && lines.front() == "---" && closing != lines.end(), name + " opens with frontmatter");
    check(get_frontmatter_value(lines, end, "name") == name, name + ": the frontmatter name is the folder name");
    const std::string description = get_frontmatter_value(lines, end, "description");
    check(!description.empty(), name + " has a description");
    check(description.size() <= MAX_DESCRIPTION_LENGTH, name + ": the description fits 1024 characters");
    check(!has_frontmatter_key(lines, end, "paths"),
          name + " has no paths key: Claude Code hides such a skill until a matching file is read");
    check(lines.size() <= MAX_SKILL_LINES, name + ": SKILL.md stays within 500 lines");
}

void test_skills_have_valid_frontmatter()
{
    std::printf("test_skills_have_valid_frontmatter\n");
    const fs::path repository = get_repository_dir();
    size_t skill_count = 0;
    for(const auto& entry : fs::directory_iterator(repository / ".agents" / "skills"))
    {
        if(entry.is_directory())
        {
            check_skill(entry.path(), repository);
            ++skill_count;
        }
    }
    check(skill_count > 0, "the skills folder holds skills");
}

auto run_agent_docs_suite(rtti::context& /*ctx*/) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_docs_name_existing_paths();
    test_skill_scripts_are_ascii();
    test_skills_have_valid_frontmatter();
    std::printf("agent docs: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("agent docs", run_agent_docs_suite)
