#include "audit_catalog.h"

#include <algorithm>

namespace unravel::audit
{
namespace
{

constexpr std::int64_t MIB = 1024 * 1024;

auto make_rules() -> std::vector<rule>
{
    std::vector<rule> list;

    // Content: what the game loads and what keeps a profile from deploying.
    list.push_back({rules::MISSING_ASSET,
                    "Missing Asset Reference",
                    severity::error,
                    area::correctness | area::quality,
                    rule_source::scenes,
                    "A scene, prefab, material or animation controller refers to an asset that is not in the project. "
                    "When the document loads, the field is empty: a model draws without its material, a sound does "
                    "not play.",
                    "Choose another asset for the field, or bring the asset back together with its .meta file, which "
                    "keeps its id.",
                    {},
                    false});
    list.push_back({rules::UNREADABLE_DOCUMENT,
                    "Unreadable Scene or Prefab",
                    severity::error,
                    to_mask(area::correctness),
                    rule_source::scenes,
                    "A scene or prefab this engine cannot load: a newer engine wrote it, it is empty, or it is not a "
                    "valid document.",
                    "Open it in the engine version that wrote it, or restore it from version control.",
                    {},
                    false});
    list.push_back({rules::INVALID_CONTROLLER,
                    "Invalid Animation Controller",
                    severity::error,
                    to_mask(area::correctness),
                    rule_source::scenes,
                    "An animation controller has a mistake its player cannot work around, such as two parameters of "
                    "one name or a transition to a state that does not exist.",
                    "Open the controller in the Animation Controller window and correct what the issue names.",
                    {},
                    false});
    list.push_back({rules::JOINT_PROBLEM,
                    "Joint Cannot Be Made",
                    severity::error,
                    to_mask(area::correctness),
                    rule_source::scenes,
                    "A joint whose bodies are missing or the same: play mode and the built game do not make it, so "
                    "what it holds together falls apart.",
                    "Select the entity and set the joint's bodies as the issue says.",
                    {},
                    false});
    list.push_back({rules::FEATURE_OFF,
                    "Component Turned Off by Profile",
                    severity::info,
                    to_mask(area::quality),
                    rule_source::scenes,
                    "A component asks for a rendering feature that a profile's Graphics > Allowed Features turn off, so "
                    "it has no effect in that profile's builds. The editor draws it as the project's settings allow, so the "
                    "difference shows only in a build or a profile preview.",
                    "Keep it when the profile turns the feature off on purpose. Otherwise allow the feature in the "
                    "profile's Graphics > Allowed Features, or give that platform another way to show the effect.",
                    {},
                    true});
    list.push_back({rules::STEREO_SPATIAL,
                    "Stereo Clip on 3D Sources Only",
                    severity::info,
                    to_mask(area::memory),
                    rule_source::scenes,
                    "A stereo clip that only spatial (3D) audio sources play. A spatial source places the sound at "
                    "one point in the world, and the clip's second channel doubles its memory.",
                    "Turn on Force To Mono in the clip's import settings, unless a script also plays it as 2D sound.",
                    {},
                    false});
    list.push_back({rules::DEPLOY_PROBLEM,
                    "Profile Cannot Deploy",
                    severity::error,
                    to_mask(area::correctness),
                    rule_source::content,
                    "Something keeps the profile from deploying, as the Deploy window reports it. It is an error for "
                    "the profile the editor deploys and a warning for the others.",
                    "Correct what the issue names, in the Deploy window or the profile's settings.",
                    {},
                    true});
    list.push_back({rules::MISSING_SCENE,
                    "Deploy Scene Missing",
                    severity::error,
                    to_mask(area::correctness),
                    rule_source::content,
                    "The profile's list of scenes to deploy names a scene that is no longer in the project.",
                    "Remove it from the profile's scenes in the Deploy window, or bring the scene back.",
                    {},
                    true});
    list.push_back({rules::EXCLUDED_BUT_USED,
                    "Used Asset Left Out",
                    severity::warning,
                    to_mask(area::correctness),
                    rule_source::content,
                    "An exclude rule leaves out an asset that the deploy's scenes use, so the built game finds "
                    "nothing where it loads it.",
                    "Remove the exclude rule in the Deploy window's Content tab, or stop using the asset.",
                    {},
                    true});
    list.push_back({rules::ORPHAN_RULE,
                    "Content Rule Names Nothing",
                    severity::info,
                    to_mask(area::correctness),
                    rule_source::content,
                    "An include or exclude rule names no asset or folder of the project, usually one that was moved or "
                    "deleted.",
                    "Remove the rule, or point it at the asset's new place.",
                    {},
                    true});
    list.push_back({rules::NAMED_FOLDER,
                    "Scripts Load From a Folder at Run Time",
                    severity::info,
                    to_mask(area::correctness),
                    rule_source::content,
                    "Scripts build asset paths in this folder at run time. The deploy cannot follow such a path, so "
                    "what the scripts load from the folder ships only when something else uses it.",
                    "Add an include rule for the folder in the Deploy window's Content tab, so everything in it ships.",
                    {},
                    true});
    list.push_back({rules::BUILD_GREW,
                    "Build Grew",
                    severity::info,
                    to_mask(area::download),
                    rule_source::content,
                    "The profile's last deploy is larger than the one before it. The issue names the kinds of content "
                    "and the files that grew most, from the deploy reports the profile's cache keeps.",
                    "Check that the growth is what the change meant: an asset left uncompressed, a texture imported "
                    "larger, or content a scene now uses by mistake.",
                    {{thresholds::MIN_GROWTH_BYTES,
                      "Smallest reported growth",
                      "bytes",
                      "Builds that grew less than this are not reported.",
                      {MIB, MIB, MIB}}},
                    true});
    list.push_back({rules::COMPILE_ERROR,
                    "Script Compile Error",
                    severity::error,
                    to_mask(area::correctness),
                    rule_source::scripts,
                    "The project's scripts do not compile: play mode and deploys use the last build that did.",
                    "Correct the error at the file and line the issue names.",
                    {},
                    false});
    list.push_back({rules::COMPILE_WARNING,
                    "Script Compile Warning",
                    severity::warning,
                    to_mask(area::correctness),
                    rule_source::scripts,
                    "The C# compiler warns about the code at this line: it compiles, but it often does not do what "
                    "it seems to.",
                    "Read the compiler's message and change the code, or ignore the issue with the reason it is fine.",
                    {},
                    false});

    // Assets, judged with each profile's settings.
    list.push_back({rules::TEXTURE_UNCOMPRESSED,
                    "Uncompressed Texture",
                    severity::warning,
                    area::memory | area::download,
                    rule_source::assets,
                    "A texture stored uncompressed in a profile's build. Compressed formats keep it in a quarter to an "
                    "eighth of the memory and the download, or less.",
                    "Set the texture's Compression to Normal for that platform; keep None only for textures whose "
                    "exact pixels matter, such as pixel art or lookup tables.",
                    {{thresholds::MIN_BYTES,
                      "Smallest reported size",
                      "bytes",
                      "Uncompressed textures smaller than this in memory, mipmaps included, are not reported.",
                      {4 * MIB, 1 * MIB, 1 * MIB}}},
                    true});
    list.push_back({rules::TEXTURE_OVERSIZED,
                    "Texture Larger Than the Platform Guideline",
                    severity::info,
                    area::memory | area::download | area::load_time,
                    rule_source::assets,
                    "A texture whose largest side in a profile's build is over the platform's guideline. Memory, "
                    "download and load time grow with the square of the side.",
                    "Lower the texture's Max Size for that platform, unless it covers much of the screen up close.",
                    {{thresholds::MAX_SIDE,
                      "Largest side",
                      "pixels",
                      "Textures whose largest side in the build is over this are reported.",
                      {4096, 2048, 2048}}},
                    true});
    list.push_back({rules::TEXTURE_NO_MIPS,
                    "Material Texture Without Mipmaps",
                    severity::info,
                    area::quality | area::gpu,
                    rule_source::assets,
                    "A texture that a material samples, imported without mipmaps. Drawn smaller than its size it "
                    "shimmers, and the graphics processor reads more memory for each pixel.",
                    "Turn on Generate Mipmaps in its import settings, unless the material draws it at its own size.",
                    {},
                    false});
    list.push_back({rules::AUDIO_LARGE_DECODED,
                    "Large Decoded Audio Clip",
                    severity::warning,
                    area::memory | area::load_time,
                    rule_source::assets,
                    "An audio clip whose decoded sound is large. The engine decodes a clip whole into 16-bit samples "
                    "when it loads, so its memory is its length times its sample rate times its channels times two "
                    "bytes, whatever its compression.",
                    "Turn on Force To Mono when the clip need not be stereo, lower the source's sample rate, or "
                    "shorten the clip, such as music cut into a loop.",
                    {{thresholds::MAX_DECODED_BYTES,
                      "Largest decoded size",
                      "bytes",
                      "Clips whose decoded sound is larger than this are reported.",
                      {64 * MIB, 16 * MIB, 16 * MIB}}},
                    true});
    list.push_back({rules::AUDIO_UNCOMPRESSED,
                    "Uncompressed Audio Clip",
                    severity::info,
                    to_mask(area::download),
                    rule_source::assets,
                    "A lossless clip (WAV or FLAC) that a profile ships as its own bytes. Ogg Vorbis at Normal "
                    "quality is usually about a tenth of a WAV's size.",
                    "Set the clip's Compression to Normal for that platform.",
                    {{thresholds::MIN_FILE_BYTES,
                      "Smallest reported file",
                      "bytes",
                      "Uncompressed clips whose file is smaller than this are not reported.",
                      {4 * MIB, 1 * MIB, 1 * MIB}}},
                    true});
    list.push_back({rules::MESH_NO_LODS,
                    "Detailed Mesh Without LODs",
                    severity::info,
                    to_mask(area::gpu),
                    rule_source::assets,
                    "A mesh over the platform's triangle guideline, imported without generated LODs: it draws every "
                    "triangle however small it is on screen.",
                    "Turn on Generate LODs in the model's import settings, so distant copies draw simpler meshes.",
                    {{thresholds::TRIANGLES,
                      "Triangles",
                      "triangles",
                      "Meshes with more triangles than this and no LODs are reported.",
                      {100000, 20000, 20000}}},
                    true});
    list.push_back({rules::DUPLICATE_FILES,
                    "Duplicate Asset Files",
                    severity::info,
                    area::download | area::memory,
                    rule_source::assets,
                    "Asset files with the same bytes. Each copy is an asset of its own: each compiles, and each that "
                    "the game uses ships and loads on its own.",
                    "Keep one copy, point what uses the others at it, and delete the others.",
                    {{thresholds::MIN_FILE_BYTES,
                      "Smallest reported file",
                      "bytes",
                      "Duplicates smaller than this are not reported.",
                      {64 * 1024, 64 * 1024, 64 * 1024}}},
                    false});

    // Settings, judged per profile.
    list.push_back({rules::COSTLIER_THAN_START,
                    "Setting Costlier Than the Platform's Start",
                    severity::warning,
                    area::gpu | area::memory,
                    rule_source::settings,
                    "A profile allows a more expensive rendering setting than a new profile of its platform starts "
                    "with. The engine starts phone and browser profiles without the features those devices cannot "
                    "afford at frame rate.",
                    "Use the platform's starting value, unless the game needs the feature and measures fine on its "
                    "slowest target device.",
                    {},
                    true});
    list.push_back({rules::TEXTURE_DEFAULT_UNCOMPRESSED,
                    "Textures Uncompressed by Default",
                    severity::warning,
                    area::memory | area::download,
                    rule_source::settings,
                    "The profile's default texture compression is None, so every texture left on Project Default is "
                    "stored uncompressed.",
                    "Set Assets > Textures > Default Compression to Normal, and set None only on the textures that need "
                    "it.",
                    {},
                    true});
    list.push_back({rules::AUDIO_DEFAULT_UNCOMPRESSED,
                    "Audio Uncompressed by Default",
                    severity::info,
                    to_mask(area::download),
                    rule_source::settings,
                    "The profile's default audio compression is None, so every lossless clip left on Project Default "
                    "ships as its own bytes.",
                    "Set Assets > Audio > Default Compression to Normal.",
                    {},
                    true});
    list.push_back({rules::PHYSICS_RATE,
                    "High Physics Rate",
                    severity::info,
                    to_mask(area::cpu),
                    rule_source::settings,
                    "The physics steps more often than the guideline. Each step simulates every body again, so at 60 "
                    "frames a second a 240 Hz rate costs four steps a frame.",
                    "Raise Physics > Fixed Timestep (0.02 s steps 50 times a second), unless fast bodies tunnel at it.",
                    {{thresholds::MAX_RATE,
                      "Steps a second",
                      "Hz",
                      "Fixed timesteps that step more often than this are reported.",
                      {120, 120, 120}}},
                    true});

    // Scripts: what code reachable from the update methods does every time it runs.
    // What every script rule's finding means and does not: static analysis sees what code can run, not how often.
    const std::string reach_note =
        " The analysis follows the project's own calls from the update methods and the coroutines. It leaves out "
        "error paths (code that throws, a catch, or a branch that reports with Log.Warning or Log.Error) and the "
        "one-time setup of a field that starts empty. It cannot tell how often a call runs: code behind a "
        "condition, such as a level-up, may run rarely.";
    const auto code_rule = [&list, &reach_note](const char* id,
                                                const char* title,
                                                severity level,
                                                area_mask areas,
                                                const char* description,
                                                const char* advice)
    {
        list.push_back({id, title, level, areas, rule_source::code, description + reach_note, advice, {}, false});
    };
    code_rule(rules::CODE_FIND_ENTITY,
              "Entity Search in Update",
              severity::warning,
              area::cpu | area::memory,
              "Code reachable from an update method finds entities by name, tag or component (Scene.FindEntityByName, "
              "FindEntitiesByTag, FindEntitiesWithComponent). Each call walks every entity of the scene, and the forms "
              "that return several allocate an array.",
              "Find the entity once in OnStart and keep it in a field.");
    code_rule(rules::CODE_CAST_ALL,
              "Allocating Physics Query in Update",
              severity::warning,
              area::memory | area::cpu,
              "Code reachable from an update method calls a physics query that returns every hit (RaycastAll, "
              "SphereCastAll, BoxCastAll, CapsuleCastAll) or every overlap. Each call allocates a new array, which "
              "the garbage collector must later free.",
              "Call its NonAlloc form with an array the script keeps: RaycastNonAlloc, SphereOverlapNonAlloc.");
    code_rule(rules::CODE_LINQ,
              "LINQ in Update",
              severity::warning,
              area::memory | area::cpu,
              "Code reachable from an update method uses LINQ (System.Linq). Its queries allocate enumerators and "
              "delegates on every call.",
              "Write the query as a loop, or compute it outside the update and keep the result.");
    code_rule(rules::CODE_CLOSURE,
              "Closure Allocation in Update",
              severity::info,
              to_mask(area::memory),
              "Code reachable from an update method creates a lambda or delegate that is not cached: each run "
              "allocates the delegate, and the object holding the variables it captures.",
              "Create the delegate once and keep it in a field, or pass what it needs as arguments.");
    code_rule(rules::CODE_BOXING,
              "Boxing in Update",
              severity::info,
              to_mask(area::memory),
              "Code reachable from an update method converts a value type (a number, a vector, a struct) to "
              "object or an interface, which allocates a box each time.",
              "Use a generic or typed overload, or convert the value once outside the update.");
    code_rule(rules::CODE_STRING,
              "String Building in Update",
              severity::info,
              to_mask(area::memory),
              "Code reachable from an update method builds strings (concatenation, interpolation, Format). Each "
              "result is a new string to collect.",
              "Build the string only when what it shows changes, and keep it.");
    code_rule(rules::CODE_ARRAY,
              "Array Allocation in Update",
              severity::info,
              to_mask(area::memory),
              "Code reachable from an update method allocates a new array each time it runs, including the array "
              "a params argument is passed in.",
              "Allocate the array once and reuse it.");
    code_rule(rules::CODE_OBJECT,
              "Object Allocation in Update",
              severity::info,
              to_mask(area::memory),
              "Code reachable from an update method creates a class instance (new) each time it runs; it becomes "
              "garbage to collect.",
              "Create it once and reuse it, or make it a struct when it is small and short-lived.");
    code_rule(rules::CODE_REFLECTION,
              "Reflection in Update",
              severity::warning,
              area::cpu | area::memory,
              "Code reachable from an update method uses reflection (System.Reflection, Type.GetMethod, "
              "Activator.CreateInstance). Each lookup searches metadata and allocates.",
              "Look the member up once and keep it, or call it directly.");
    code_rule(rules::CODE_LOG,
              "Logging in Update",
              severity::info,
              area::cpu | area::memory,
              "Code reachable from an update method writes routine lines to the log with Log.Trace or Log.Info. Each "
              "line formats a string and goes through the log's sinks, in the built game too. Log.Warning and "
              "Log.Error are not reported: they report problems, and their branches count as error paths.",
              "Log only when something changes, or remove the line once the code works.");
    code_rule(rules::CODE_ALLOCATING_CALL,
              "Allocating Engine Call in Update",
              severity::info,
              to_mask(area::memory),
              "Code reachable from an update method calls an engine member that makes a new object: a property that "
              "returns a new string or array (Entity.name, TransformComponent.children) or a method that returns its "
              "results in a new array (Entity.GetComponents). The analysis reads the engine's script library to tell, "
              "and says whether every call allocates or only calls that take some of its paths; what the engine "
              "makes once and keeps is not reported.",
              "Read the value once and keep it while it stays the same, or use a form that fills a list the script "
              "keeps.");
    code_rule(rules::CODE_INSTANTIATE,
              "Instantiation in Update",
              severity::info,
              area::cpu | area::memory,
              "Code reachable from an update method makes entities (Scene.Instantiate, CloneEntity, CreateEntity) "
              "or adds components (Entity.AddComponent). Each one creates components and scripts, loads what they "
              "reference, and later costs a destroy; done in bursts it shows as frame spikes.",
              "Make a pool of the entities ahead of time, in OnStart or a loading screen, and enable and disable "
              "them instead of making and destroying them.");
    code_rule(rules::CODE_ASSET_LOOKUP,
              "Asset Lookup by Key in Update",
              severity::warning,
              to_mask(area::cpu),
              "Code reachable from an update method looks up an asset by its key (Assets.GetAsset with a string). "
              "Each call finds the key in the asset database, and the first call may load the asset on the spot.",
              "Look the asset up once in OnStart, or make it a field the Inspector sets, and keep the handle.");
    code_rule(rules::CODE_INTERFACE_ENUMERATION,
              "Enumeration Through an Interface in Update",
              severity::info,
              to_mask(area::memory),
              "Code reachable from an update method loops over a collection through IEnumerable (a foreach over a "
              "parameter or field typed as an interface). The enumerator of a List or Dictionary is a struct when "
              "the loop sees the concrete type, but through the interface it is boxed: each loop allocates.",
              "Type the variable as the concrete collection (List, array), or loop with an index.");
    code_rule(rules::CODE_LOCAL_TIME,
              "Local Time in Update",
              severity::info,
              to_mask(area::cpu),
              "Code reachable from an update method reads DateTime.Now or DateTime.Today. Each read converts the "
              "clock to the local time zone, which costs far more than reading the clock.",
              "Use Time for game time, or DateTime.UtcNow for wall-clock time.");
    code_rule(rules::CODE_GC_COLLECT,
              "Forced Garbage Collection in Update",
              severity::warning,
              area::cpu | area::memory,
              "Code reachable from an update method calls GC.Collect. A full collection pauses the game while it "
              "walks every managed object, which is a frame spike each time it runs.",
              "Remove the call, or make it only at a loading screen or another moment a pause cannot be seen.");
    code_rule(rules::CODE_BLOCKING_WAIT,
              "Blocking Wait in Update",
              severity::warning,
              to_mask(area::cpu),
              "Code reachable from an update method waits on the main thread: Thread.Sleep, Thread.Join, Task.Wait "
              "or Task.Result. The frame cannot finish until the wait ends, so each one stalls the game.",
              "Await the task in an async method or a coroutine, or check IsCompleted each frame and read the "
              "result once it is done.");
    code_rule(rules::CODE_FILE_ACCESS,
              "File Access in Update",
              severity::warning,
              area::cpu | area::load_time,
              "Code reachable from an update method reads or writes files (System.IO.File, Directory, a FileStream "
              "or a stream reader or writer opened on a path). Each access waits on the storage device, which "
              "takes far longer on a phone than on a desktop.",
              "Read the file once when the scene loads and keep what it holds, and write saves at checkpoints or "
              "from a background task.");
    list.push_back({rules::CODE_EMPTY_UPDATE,
                    "Empty Update Method",
                    severity::info,
                    to_mask(area::cpu),
                    rule_source::code,
                    "A script overrides an update method with an empty body, or one that only calls the base method. "
                    "The engine calls every overridden update method once a frame for each enabled instance.",
                    "Delete the empty method.",
                    {},
                    false});
    return list;
}

} // namespace

auto get_rules() -> const std::vector<rule>&
{
    static const std::vector<rule> all = make_rules();
    return all;
}

auto find_rule(const std::string& id) -> const rule*
{
    const auto& all = get_rules();
    const auto found = std::find_if(all.begin(),
                                    all.end(),
                                    [&id](const rule& item)
                                    {
                                        return item.id == id;
                                    });
    return found != all.end() ? &*found : nullptr;
}

auto get_fix_label(const std::string& fix_id) -> const char*
{
    if(fix_id == fixes::COMPRESS_TEXTURE)
    {
        return "Compress the texture";
    }
    if(fix_id == fixes::LIMIT_TEXTURE_SIZE)
    {
        return "Lower the texture's Max Size";
    }
    if(fix_id == fixes::GENERATE_MIPS)
    {
        return "Generate mipmaps";
    }
    if(fix_id == fixes::COMPRESS_AUDIO)
    {
        return "Compress the clip";
    }
    if(fix_id == fixes::FORCE_MONO)
    {
        return "Force the clip to mono";
    }
    if(fix_id == fixes::GENERATE_LODS)
    {
        return "Generate LODs";
    }
    if(fix_id == fixes::USE_PLATFORM_START)
    {
        return "Use the platform's starting value";
    }
    if(fix_id == fixes::REMOVE_CONTENT_RULE)
    {
        return "Remove the rule";
    }
    return "";
}

} // namespace unravel::audit
