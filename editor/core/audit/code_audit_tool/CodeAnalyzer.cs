using Mono.Cecil;
using Mono.Cecil.Cil;

namespace Unravel.CodeAudit
{
    /// <summary>An update method the analysis starts from.</summary>
    public sealed class EntryPoint
    {
        public string Method;
        public string File;
        public int Line;
        public int Reachable;
    }

    /// <summary>One call or allocation, in a method an update method reaches.</summary>
    public sealed class Finding
    {
        public string Rule;
        public string Method;
        public string File;
        public int Line;
        /// <summary>What it calls or allocates ("Scene.FindEntityByName", "Int32[]").</summary>
        public string Target;
        /// <summary>More of the finding: what an engine call allocates ("a new string"), the call a params array is for.</summary>
        public string Detail;
        public List<string> Path = new();
    }

    public sealed class AnalysisResult
    {
        public int Methods;
        public List<EntryPoint> EntryPoints = new();
        public List<Finding> Findings = new();
        public List<string> Errors = new();
    }

    /// <summary>
    /// Finds the methods of a script library that the engine calls every frame - the overrides of
    /// ScriptComponent's OnUpdate, OnFixedUpdate and OnLateUpdate, and the coroutines StartCoroutine runs - and
    /// what they reach through the library's own code: direct calls, the library's overrides of the virtual and
    /// interface methods they call, the lambdas they create and the state machines of async and iterator
    /// methods. In those methods it reports the calls and allocations the rules name, and the calls into the
    /// engine's own script library that allocate.
    ///
    /// Code that does not run every frame is left out: the blocks of an error path (those that throw, that a catch
    /// handles, or that report with Log.Warning or Log.Error), the one-time setup of a field left null until first
    /// use, static constructors and [Conditional] methods. Calls from such code are not followed either.
    /// </summary>
    public static class CodeAnalyzer
    {
        private const string ScriptComponentType = "Unravel.Core.ScriptComponent";
        private const string EngineNamespace = "Unravel.";
        private static readonly string[] UpdateMethods = { "OnUpdate", "OnFixedUpdate", "OnLateUpdate" };
        /// <summary>How deep the analysis follows calls inside the engine's library to tell whether one allocates.</summary>
        private const int EngineCallDepth = 4;

        // Rule ids, as the editor's catalog names them (editor/core/audit/audit_catalog.h).
        private const string FindEntity = "code.find_entity";
        private const string CastAll = "code.cast_all";
        private const string Linq = "code.linq";
        private const string Closure = "code.closure";
        private const string Boxing = "code.boxing";
        private const string StringRule = "code.string";
        private const string ArrayRule = "code.array";
        private const string ObjectRule = "code.object";
        private const string Reflection = "code.reflection";
        private const string Log = "code.log";
        private const string EmptyUpdate = "code.empty_update";
        private const string AllocatingCall = "code.allocating_call";
        private const string Instantiate = "code.instantiate";
        private const string AssetLookup = "code.asset_lookup";
        private const string InterfaceEnumeration = "code.interface_enumeration";
        private const string LocalTime = "code.local_time";
        private const string GcCollect = "code.gc_collect";
        private const string BlockingWait = "code.blocking_wait";
        private const string FileAccess = "code.file_access";

        private static readonly HashSet<string> FindEntityMethods = new()
        {
            "FindEntityByName", "FindEntitiesByName", "FindEntityByTag", "FindEntitiesByTag", "FindEntitiesWithComponent",
        };

        /// <summary>Scene calls that make an entity: each loads or copies components, scripts and their assets.</summary>
        private static readonly HashSet<string> CreatingSceneMethods = new() { "Instantiate", "CloneEntity", "CreateEntity" };

        /// <summary>Types whose constructors open a file when the first argument is a path.</summary>
        private static readonly HashSet<string> FileStreamTypes = new()
        {
            "System.IO.FileStream", "System.IO.StreamReader", "System.IO.StreamWriter",
        };

        private static readonly HashSet<string> AllocatingPhysicsMethods = new()
        {
            "RaycastAll", "SphereCastAll", "BoxCastAll", "CapsuleCastAll", "SphereOverlap", "BoxOverlap", "CapsuleOverlap",
        };

        /// <summary>Log calls that report a problem: they run on an error path, never as routine work.</summary>
        private static readonly HashSet<string> ProblemLogMethods = new() { "Warning", "Error", "Critical" };

        private static readonly HashSet<string> StringBuildingMethods = new()
        {
            "Concat", "Format", "Join", "Substring", "Replace", "Split", "ToUpper", "ToLower", "ToUpperInvariant",
            "ToLowerInvariant", "Trim", "TrimStart", "TrimEnd", "PadLeft", "PadRight", "Insert", "Remove",
        };

        private static readonly HashSet<string> ReflectionTypeMethods = new()
        {
            "GetMethod", "GetMethods", "GetField", "GetFields", "GetProperty", "GetProperties", "GetMember", "GetMembers",
            "InvokeMember", "GetCustomAttributes", "GetCustomAttribute", "GetInterfaces", "GetConstructor", "GetConstructors",
        };

        public static AnalysisResult Analyze(string assemblyPath, IReadOnlyList<string> referenceFolders)
        {
            var resolver = new DefaultAssemblyResolver();
            resolver.AddSearchDirectory(Path.GetDirectoryName(Path.GetFullPath(assemblyPath)));
            foreach (var folder in referenceFolders)
            {
                if (Directory.Exists(folder))
                {
                    resolver.AddSearchDirectory(folder);
                }
            }

            var result = new AnalysisResult();
            var module = ReadModule(assemblyPath, resolver, result);
            var context = new Context(module, result);
            context.Run();
            return result;
        }

        private static ModuleDefinition ReadModule(string path, IAssemblyResolver resolver, AnalysisResult result)
        {
            var parameters = new ReaderParameters
            {
                AssemblyResolver = resolver,
                InMemory = true,
                ReadingMode = ReadingMode.Immediate,
                ReadSymbols = File.Exists(Path.ChangeExtension(path, ".pdb")),
            };
            try
            {
                return ModuleDefinition.ReadModule(path, parameters);
            }
            catch (Exception) when (parameters.ReadSymbols)
            {
                result.Errors.Add("The script symbols could not be read, so findings have no line numbers.");
                parameters.ReadSymbols = false;
                return ModuleDefinition.ReadModule(path, parameters);
            }
        }

        private sealed class Context
        {
            private readonly ModuleDefinition module_;
            private readonly AnalysisResult result_;
            private readonly List<MethodDefinition> methods_ = new();
            /// <summary>The virtual methods of the library by name, for the overrides of a virtual call.</summary>
            private readonly Dictionary<string, List<MethodDefinition>> virtuals_ = new();
            /// <summary>Each reached method and the method it was reached from (null for an entry point).</summary>
            private readonly Dictionary<MethodDefinition, MethodDefinition> parents_ = new();
            /// <summary>Each method's instructions on cold paths, worked out once.</summary>
            private readonly Dictionary<MethodDefinition, HashSet<Instruction>> cold_ = new();
            /// <summary>What an engine method allocates when it runs; null when it allocates nothing.</summary>
            private readonly Dictionary<MethodDefinition, EngineAllocation> engineAllocations_ = new();
            /// <summary>The MoveNext of each coroutine StartCoroutine runs: only their loops run again and again.</summary>
            private readonly HashSet<MethodDefinition> coroutines_ = new();

            public Context(ModuleDefinition module, AnalysisResult result)
            {
                module_ = module;
                result_ = result;
            }

            public void Run()
            {
                foreach (var type in module_.GetTypes())
                {
                    foreach (var method in type.Methods)
                    {
                        if (!method.HasBody)
                        {
                            continue;
                        }
                        methods_.Add(method);
                        if (method.IsVirtual)
                        {
                            if (!virtuals_.TryGetValue(method.Name, out var list))
                            {
                                virtuals_[method.Name] = list = new List<MethodDefinition>();
                            }
                            list.Add(method);
                        }
                    }
                }
                result_.Methods = methods_.Count;

                foreach (var root in FindRoots())
                {
                    var reached = Walk(root);
                    var (file, line) = GetFirstLine(root);
                    result_.EntryPoints.Add(new EntryPoint
                    {
                        Method = DisplayName(root), File = file, Line = line, Reachable = reached,
                    });
                    if (IsEmptyUpdate(root))
                    {
                        result_.Findings.Add(new Finding
                        {
                            Rule = EmptyUpdate, Method = DisplayName(root), File = file, Line = line, Target = "",
                            Path = { DisplayName(root) },
                        });
                    }
                }
                foreach (var method in parents_.Keys)
                {
                    if (!IsSetupOrDebugOnly(method))
                    {
                        ScanMethod(method);
                    }
                }
            }

            private List<MethodDefinition> FindRoots()
            {
                var roots = new List<MethodDefinition>();
                foreach (var method in methods_)
                {
                    if (IsUpdateOverride(method))
                    {
                        roots.Add(method);
                    }
                }
                // A coroutine runs one step every frame: the MoveNext of an iterator passed to StartCoroutine.
                foreach (var method in methods_)
                {
                    var instructions = method.Body.Instructions;
                    for (int i = 1; i < instructions.Count; i++)
                    {
                        if (!IsCall(instructions[i]) || instructions[i].Operand is not MethodReference callee ||
                            callee.Name != "StartCoroutine" || callee.DeclaringType.FullName != ScriptComponentType)
                        {
                            continue;
                        }
                        if (IsCall(instructions[i - 1]) && instructions[i - 1].Operand is MethodReference producer)
                        {
                            var definition = ResolveInModule(producer);
                            var moveNext = definition != null ? GetStateMachineMoveNext(definition) : null;
                            if (moveNext != null && !roots.Contains(moveNext))
                            {
                                roots.Add(moveNext);
                                coroutines_.Add(moveNext);
                            }
                        }
                    }
                }
                return roots;
            }

            private bool IsUpdateOverride(MethodDefinition method)
            {
                return !method.IsStatic && method.IsVirtual && !method.IsNewSlot && method.Parameters.Count == 0 &&
                       Array.IndexOf(UpdateMethods, method.Name) >= 0 && DerivesFrom(method.DeclaringType, ScriptComponentType);
            }

            /// <summary>A static constructor runs once; a [Conditional] method is left out of builds without its symbol.</summary>
            private static bool IsSetupOrDebugOnly(MethodDefinition method)
            {
                return method.IsConstructor && method.IsStatic ||
                       method.CustomAttributes.Any(attribute => attribute.AttributeType.FullName == "System.Diagnostics.ConditionalAttribute");
            }

            /// <summary>Walks from an entry point; returns how many methods it reaches.</summary>
            private int Walk(MethodDefinition root)
            {
                var seen = new HashSet<MethodDefinition> { root };
                var queue = new Queue<MethodDefinition>();
                queue.Enqueue(root);
                if (coroutines_.Contains(root))
                {
                    // Its steps run as the coroutine resumes, whoever started it.
                    parents_[root] = null;
                }
                else
                {
                    parents_.TryAdd(root, null);
                }
                while (queue.Count > 0)
                {
                    var method = queue.Dequeue();
                    foreach (var next in GetCallees(method))
                    {
                        if (seen.Add(next))
                        {
                            parents_.TryAdd(next, method);
                            queue.Enqueue(next);
                        }
                    }
                }
                return seen.Count;
            }

            private IEnumerable<MethodDefinition> GetCallees(MethodDefinition method)
            {
                var callees = new List<MethodDefinition>();
                var stateMachine = GetStateMachineMoveNext(method);
                if (stateMachine != null)
                {
                    callees.Add(stateMachine);
                }
                var cold = GetColdInstructions(method);
                foreach (var instruction in method.Body.Instructions)
                {
                    var code = instruction.OpCode.Code;
                    if (instruction.Operand is not MethodReference reference || cold.Contains(instruction))
                    {
                        continue;
                    }
                    var isReference = code == Code.Call || code == Code.Callvirt || code == Code.Newobj ||
                                      code == Code.Ldftn || code == Code.Ldvirtftn;
                    if (!isReference)
                    {
                        continue;
                    }
                    var target = ResolveInModule(reference);
                    if (target == null)
                    {
                        continue;
                    }
                    if (target.HasBody)
                    {
                        callees.Add(target);
                    }
                    if ((code == Code.Callvirt || code == Code.Ldvirtftn) && target.IsVirtual)
                    {
                        callees.AddRange(FindOverrides(target));
                    }
                }
                return callees;
            }

            /// <summary>The library's methods that a virtual call of `target` can run: its overrides, or an interface's implementations.</summary>
            private IEnumerable<MethodDefinition> FindOverrides(MethodDefinition target)
            {
                if (!virtuals_.TryGetValue(target.Name, out var candidates))
                {
                    yield break;
                }
                var owner = target.DeclaringType;
                foreach (var candidate in candidates)
                {
                    if (candidate == target || candidate.Parameters.Count != target.Parameters.Count)
                    {
                        continue;
                    }
                    var matches = owner.IsInterface ? Implements(candidate.DeclaringType, owner.FullName)
                                                    : DerivesFrom(candidate.DeclaringType, owner.FullName);
                    if (matches)
                    {
                        yield return candidate;
                    }
                }
            }

            private MethodDefinition ResolveInModule(MethodReference reference)
            {
                var definition = TryResolve(reference);
                return definition != null && definition.Module == module_ ? definition : null;
            }

            private static MethodDefinition TryResolve(MethodReference reference)
            {
                try
                {
                    return reference.GetElementMethod().Resolve();
                }
                catch (AssemblyResolutionException)
                {
                    return null;
                }
            }

            private static TypeDefinition TryResolve(TypeReference reference)
            {
                try
                {
                    return reference.Resolve();
                }
                catch (AssemblyResolutionException)
                {
                    return null;
                }
            }

            private static MethodDefinition GetStateMachineMoveNext(MethodDefinition method)
            {
                foreach (var attribute in method.CustomAttributes)
                {
                    var name = attribute.AttributeType.FullName;
                    if (name != "System.Runtime.CompilerServices.AsyncStateMachineAttribute" &&
                        name != "System.Runtime.CompilerServices.IteratorStateMachineAttribute" &&
                        name != "System.Runtime.CompilerServices.AsyncIteratorStateMachineAttribute")
                    {
                        continue;
                    }
                    if (attribute.ConstructorArguments.Count == 1 && attribute.ConstructorArguments[0].Value is TypeReference type)
                    {
                        var definition = TryResolve(type);
                        return definition?.Methods.FirstOrDefault(candidate => candidate.Name == "MoveNext" && candidate.HasBody);
                    }
                }
                return null;
            }

            private static bool DerivesFrom(TypeDefinition type, string baseName)
            {
                for (int depth = 0; type != null && depth < 64; depth++)
                {
                    var baseType = type.BaseType;
                    if (baseType == null)
                    {
                        return false;
                    }
                    if (baseType.FullName == baseName || GetOpenName(baseType) == baseName)
                    {
                        return true;
                    }
                    type = TryResolve(baseType);
                }
                return false;
            }

            private static bool Implements(TypeDefinition type, string interfaceName)
            {
                for (int depth = 0; type != null && depth < 64; depth++)
                {
                    if (type.Interfaces.Any(entry => entry.InterfaceType.FullName == interfaceName ||
                                                     GetOpenName(entry.InterfaceType) == interfaceName))
                    {
                        return true;
                    }
                    type = type.BaseType != null ? TryResolve(type.BaseType) : null;
                }
                return false;
            }

            private static string GetOpenName(TypeReference type)
            {
                return type is GenericInstanceType generic ? generic.ElementType.FullName : type.FullName;
            }

            /// <summary>An override whose body does nothing, or only calls ScriptComponent's own (empty) method.</summary>
            private static bool IsEmptyUpdate(MethodDefinition method)
            {
                if (!UpdateMethods.Contains(method.Name))
                {
                    return false;
                }
                var meaningful = method.Body.Instructions.Where(instruction => instruction.OpCode.Code != Code.Nop).ToList();
                if (meaningful.Count == 1 && meaningful[0].OpCode.Code == Code.Ret)
                {
                    return true;
                }
                return meaningful.Count == 3 && meaningful[0].OpCode.Code == Code.Ldarg_0 &&
                       meaningful[1].OpCode.Code == Code.Call && meaningful[1].Operand is MethodReference called &&
                       called.Name == method.Name && called.DeclaringType.FullName == ScriptComponentType &&
                       meaningful[2].OpCode.Code == Code.Ret;
            }

            /// <summary>
            /// The instructions of the method's cold blocks: a block that throws, a catch or filter handler, and a
            /// block that reports a problem with Log.Warning or Log.Error. Such a block is an error path: it does
            /// not run as routine work every frame. The code that fills a cache when it misses is cold too: it runs
            /// once for each key or each time the slot is empty or stale. In a coroutine, the code outside its loops
            /// is cold: each part between two yields runs once each time the coroutine is started, and only a loop
            /// runs it again.
            /// </summary>
            private HashSet<Instruction> GetColdInstructions(MethodDefinition method)
            {
                if (cold_.TryGetValue(method, out var known))
                {
                    return known;
                }
                var cold = new HashSet<Instruction>();
                cold_[method] = cold;
                if (!method.HasBody)
                {
                    return cold;
                }
                var body = method.Body;
                foreach (var block in SplitBlocks(body))
                {
                    if (block.Any(IsColdMarker) || IsInHandler(body, block[0]))
                    {
                        cold.UnionWith(block);
                    }
                }
                cold.UnionWith(FindFillInstructions(body));
                if (coroutines_.Contains(method))
                {
                    var looped = FindLoopInstructions(body);
                    cold.UnionWith(body.Instructions.Where(instruction => !looped.Contains(instruction)));
                }
                return cold;
            }

            /// <summary>The instructions inside a loop: between a backward branch and its target.</summary>
            private static HashSet<Instruction> FindLoopInstructions(MethodBody body)
            {
                var looped = new HashSet<Instruction>();
                foreach (var branch in body.Instructions)
                {
                    if (branch.Operand is not Instruction target || target.Offset > branch.Offset)
                    {
                        continue;
                    }
                    for (var current = target; current != null && current.Offset <= branch.Offset; current = current.Next)
                    {
                        looped.Add(current);
                    }
                }
                return looped;
            }

            private static bool IsColdMarker(Instruction instruction)
            {
                var code = instruction.OpCode.Code;
                if (code == Code.Throw || code == Code.Rethrow)
                {
                    return true;
                }
                return IsCall(instruction) && instruction.Operand is MethodReference called &&
                       called.DeclaringType.FullName == "Unravel.Core.Log" && ProblemLogMethods.Contains(called.Name);
            }

            private static bool IsInHandler(MethodBody body, Instruction instruction)
            {
                foreach (var handler in body.ExceptionHandlers)
                {
                    var isCatching = handler.HandlerType == ExceptionHandlerType.Catch ||
                                     handler.HandlerType == ExceptionHandlerType.Filter;
                    if (isCatching && IsInRange(instruction, handler.HandlerStart, handler.HandlerEnd))
                    {
                        return true;
                    }
                    if (handler.HandlerType == ExceptionHandlerType.Filter && IsInRange(instruction, handler.FilterStart, handler.HandlerStart))
                    {
                        return true;
                    }
                }
                return false;
            }

            private static bool IsInRange(Instruction instruction, Instruction start, Instruction end)
            {
                return start != null && instruction.Offset >= start.Offset && (end == null || instruction.Offset < end.Offset);
            }

            /// <summary>The method's basic blocks: runs of instructions entered only at their first and left only at their last.</summary>
            private static List<List<Instruction>> SplitBlocks(MethodBody body)
            {
                var instructions = body.Instructions;
                var leaders = new HashSet<Instruction>();
                if (instructions.Count > 0)
                {
                    leaders.Add(instructions[0]);
                }
                foreach (var instruction in instructions)
                {
                    switch (instruction.Operand)
                    {
                        case Instruction target:
                            leaders.Add(target);
                            break;
                        case Instruction[] targets:
                            leaders.UnionWith(targets);
                            break;
                    }
                    var flow = instruction.OpCode.FlowControl;
                    var endsBlock = flow == FlowControl.Branch || flow == FlowControl.Cond_Branch || flow == FlowControl.Return ||
                                    flow == FlowControl.Throw;
                    if (endsBlock && instruction.Next != null)
                    {
                        leaders.Add(instruction.Next);
                    }
                }
                foreach (var handler in body.ExceptionHandlers)
                {
                    foreach (var edge in new[] { handler.TryStart, handler.TryEnd, handler.HandlerStart, handler.HandlerEnd, handler.FilterStart })
                    {
                        if (edge != null)
                        {
                            leaders.Add(edge);
                        }
                    }
                }
                var blocks = new List<List<Instruction>>();
                List<Instruction> current = null;
                foreach (var instruction in instructions)
                {
                    if (current == null || leaders.Contains(instruction))
                    {
                        current = new List<Instruction>();
                        blocks.Add(current);
                    }
                    current.Add(instruction);
                }
                return blocks;
            }

            /// <summary>Lookups whose miss is what a cache creates an entry for.</summary>
            private static readonly HashSet<string> CacheLookups = new() { "TryGetValue", "ContainsKey", "Contains", "TryGet" };

            /// <summary>Calls that keep an object in a collection.</summary>
            private static readonly HashSet<string> CacheStores = new() { "set_Item", "Add", "TryAdd", "GetOrAdd", "AddOrUpdate" };

            /// <summary>What a guard tests: a field, a reference, an array element, or a collection's lookup.</summary>
            private enum SlotKind
            {
                Field,
                Reference,
                Element,
                Collection,
            }

            /// <summary>
            /// The instructions that fill a cache when it misses: after a conditional branch on a slot just loaded (a
            /// field, a static, an array element, a reference) or on a collection's lookup, the code up to the store
            /// back into that slot or collection, when the branch skips it for a slot that is set (`slot ??= new T()`,
            /// `if (slot == null || slot.Id != id) slot = new T(...)`, `if (!cache.TryGetValue(key, out item)) {...}`),
            /// or jumps to it after the found path returns (`if (cache.TryGetValue(key, out item)) return item;`).
            /// </summary>
            private static HashSet<Instruction> FindFillInstructions(MethodBody body)
            {
                var fill = new HashSet<Instruction>();
                var instructions = body.Instructions;
                for (int i = 0; i < instructions.Count; i++)
                {
                    var branch = instructions[i];
                    if (branch.OpCode.FlowControl != FlowControl.Cond_Branch || branch.Operand is not Instruction target ||
                        target.Offset <= branch.Offset)
                    {
                        continue;
                    }
                    if (!FindGuardedSlot(instructions, i, out var kind, out var field, out var guard))
                    {
                        continue;
                    }
                    var store = FindSlotStore(instructions, i + 1, kind, field);
                    if (store == null)
                    {
                        continue;
                    }
                    var taken = GetBranchSense(instructions, guard, i);
                    Instruction first = null;
                    if (target.Offset > store.Offset && taken != BranchSense.WhenMissing)
                    {
                        // The branch skips the fill when the slot is set or the key found.
                        first = branch.Next;
                    }
                    else if (target.Offset <= store.Offset && taken == BranchSense.WhenMissing && ExitsBefore(instructions, i + 1, target, store))
                    {
                        // The found path leaves the method; the branch jumps to the fill when the key is missing.
                        first = target;
                    }
                    for (var current = first; current != null && current.Offset <= store.Offset; current = current.Next)
                    {
                        fill.Add(current);
                    }
                }
                return fill;
            }

            /// <summary>
            /// The slot the branch at `index` tests: the nearest load or lookup a few instructions before it. A
            /// debug build puts a comparison and a local between them.
            /// </summary>
            private static bool FindGuardedSlot(IList<Instruction> instructions,
                                                int index,
                                                out SlotKind kind,
                                                out FieldReference field,
                                                out int guard)
            {
                const int GuardWindow = 10;
                kind = SlotKind.Field;
                field = null;
                guard = -1;
                for (int i = index - 1; i >= 0 && i >= index - GuardWindow; i--)
                {
                    var instruction = instructions[i];
                    guard = i;
                    switch (instruction.OpCode.Code)
                    {
                        case Code.Ldfld:
                        case Code.Ldflda:
                        case Code.Ldsfld:
                        case Code.Ldsflda:
                            // A number or an id read from the slot (`slot.Key != key`) is not the slot: look further.
                            kind = SlotKind.Field;
                            field = instruction.Operand as FieldReference;
                            if (IsReferenceSlot(field))
                            {
                                return true;
                            }
                            break;
                        case Code.Ldind_Ref:
                            kind = SlotKind.Reference;
                            return true;
                        case Code.Ldelem_Ref:
                        case Code.Ldelem_Any:
                        case Code.Ldelema:
                            kind = SlotKind.Element;
                            return true;
                        case Code.Call:
                        case Code.Callvirt:
                            if (instruction.Operand is MethodReference called && CacheLookups.Contains(called.Name) && IsCollection(called))
                            {
                                kind = SlotKind.Collection;
                                return true;
                            }
                            break;
                    }
                    if (EndsStatement(instruction))
                    {
                        return false;
                    }
                }
                return false;
            }

            /// <summary>An instruction that ends a statement before the test: the guard is not looked for past it.</summary>
            private static bool EndsStatement(Instruction instruction)
            {
                switch (instruction.OpCode.Code)
                {
                    case Code.Stfld:
                    case Code.Stsfld:
                    case Code.Stelem_Ref:
                    case Code.Stelem_Any:
                    case Code.Stind_Ref:
                    case Code.Pop:
                    case Code.Ret:
                    case Code.Throw:
                        return true;
                    case Code.Call:
                    case Code.Callvirt:
                        return instruction.Operand is MethodReference called && called.ReturnType.FullName == "System.Void";
                    default:
                        return false;
                }
            }

            /// <summary>When a guard's branch is taken: when the slot is set (the key found), when it is missing, or unknown.</summary>
            private enum BranchSense
            {
                Unknown,
                WhenSet,
                WhenMissing,
            }

            /// <summary>
            /// When the branch at `branch` is taken, from what lies between it and the guard's load at `guard`: the
            /// value tested is "set" (a reference that is not null, a lookup that found its key), negated by a
            /// comparison with null or false, through the locals a debug build adds. Unknown for any other test.
            /// </summary>
            private static BranchSense GetBranchSense(IList<Instruction> instructions, int guard, int branch)
            {
                var isSet = true;
                for (int i = guard + 1; i < branch; i++)
                {
                    var code = instructions[i].OpCode.Code;
                    var next = i + 1 < branch ? instructions[i + 1].OpCode.Code : Code.Nop;
                    if (code == Code.Dup || code == Code.Nop || IsLocalCopy(code))
                    {
                        continue;
                    }
                    if ((code == Code.Ldnull || code == Code.Ldc_I4_0) && next == Code.Ceq)
                    {
                        isSet = !isSet;
                        ++i;
                        continue;
                    }
                    if (code == Code.Ldnull && next == Code.Cgt_Un)
                    {
                        ++i;
                        continue;
                    }
                    return BranchSense.Unknown;
                }
                var taken = instructions[branch].OpCode.Code;
                var takenWhenTrue = taken == Code.Brtrue || taken == Code.Brtrue_S;
                var takenWhenFalse = taken == Code.Brfalse || taken == Code.Brfalse_S;
                if (!takenWhenTrue && !takenWhenFalse)
                {
                    return BranchSense.Unknown;
                }
                return takenWhenTrue == isSet ? BranchSense.WhenSet : BranchSense.WhenMissing;
            }

            private static bool IsLocalCopy(Code code)
            {
                switch (code)
                {
                    case Code.Stloc:
                    case Code.Stloc_S:
                    case Code.Stloc_0:
                    case Code.Stloc_1:
                    case Code.Stloc_2:
                    case Code.Stloc_3:
                    case Code.Ldloc:
                    case Code.Ldloc_S:
                    case Code.Ldloc_0:
                    case Code.Ldloc_1:
                    case Code.Ldloc_2:
                    case Code.Ldloc_3:
                        return true;
                    default:
                        return false;
                }
            }

            /// <summary>
            /// A field a cache can be kept in: a class, an array, or a struct the code tests through its address
            /// (`if (!entity.IsValid()) entity = ...`).
            /// </summary>
            private static bool IsReferenceSlot(FieldReference field)
            {
                if (field == null || field.FieldType.IsPrimitive || field.FieldType.FullName == "System.Guid")
                {
                    return false;
                }
                // A state kept in an enum changes; it does not cache an object.
                var type = field.FieldType.IsValueType ? TryResolve(field.FieldType) : null;
                return type == null || !type.IsEnum;
            }

            /// <summary>The first store back into the guarded slot after `start`, within a fill's reach.</summary>
            private static Instruction FindSlotStore(IList<Instruction> instructions, int start, SlotKind kind, FieldReference field)
            {
                const int FillWindow = 64;
                for (int i = start; i < instructions.Count && i <= start + FillWindow; i++)
                {
                    var instruction = instructions[i];
                    if (instruction.Operand is Instruction target && target.Offset <= instruction.Offset)
                    {
                        // A loop is not a fill.
                        return null;
                    }
                    var code = instruction.OpCode.Code;
                    var isStore = kind switch
                    {
                        SlotKind.Field => (code == Code.Stfld || code == Code.Stsfld) && instruction.Operand is FieldReference written &&
                                          written.FullName == field.FullName,
                        SlotKind.Reference => code == Code.Stind_Ref,
                        SlotKind.Element => code == Code.Stelem_Ref || code == Code.Stelem_Any,
                        _ => IsCall(instruction) && instruction.Operand is MethodReference called && CacheStores.Contains(called.Name) &&
                             IsCollection(called),
                    };
                    if (isStore)
                    {
                        return instruction;
                    }
                }
                return null;
            }

            /// <summary>Whether the path from `start` up to `target` leaves before reaching it: a return, a throw, or a jump past the store.</summary>
            private static bool ExitsBefore(IList<Instruction> instructions, int start, Instruction target, Instruction store)
            {
                var last = target.Previous;
                if (last == null || last.Offset < instructions[start].Offset)
                {
                    return false;
                }
                var flow = last.OpCode.FlowControl;
                if (flow == FlowControl.Return || flow == FlowControl.Throw)
                {
                    return true;
                }
                return flow == FlowControl.Branch && last.Operand is Instruction jump && jump.Offset > store.Offset;
            }

            private static bool IsCollection(MethodReference called)
            {
                return called.DeclaringType.Namespace.StartsWith("System.Collections", StringComparison.Ordinal);
            }

            private void ScanMethod(MethodDefinition method)
            {
                var instructions = method.Body.Instructions;
                var cold = GetColdInstructions(method);
                for (int i = 0; i < instructions.Count; i++)
                {
                    var instruction = instructions[i];
                    if (cold.Contains(instruction))
                    {
                        continue;
                    }
                    var (rule, target, detail) = Classify(instructions, i);
                    if (rule == null)
                    {
                        continue;
                    }
                    var (file, line) = GetLine(method, instruction);
                    result_.Findings.Add(new Finding
                    {
                        Rule = rule, Method = DisplayName(method), File = file, Line = line, Target = target, Detail = detail,
                        Path = GetPath(method),
                    });
                }
            }

            /// <summary>The rule an instruction falls under, what it calls or allocates and more of it; no rule for most.</summary>
            private (string rule, string target, string detail) Classify(IList<Instruction> instructions, int index)
            {
                var instruction = instructions[index];
                switch (instruction.OpCode.Code)
                {
                    case Code.Call:
                    case Code.Callvirt:
                        return instruction.Operand is MethodReference called ? ClassifyCall(called) : default;
                    case Code.Newobj:
                        return instruction.Operand is MethodReference constructor ? ClassifyNew(instructions, index, constructor) : default;
                    case Code.Newarr:
                        return instruction.Operand is TypeReference element ? ClassifyArray(instructions, index, element) : default;
                    case Code.Box:
                        // A generic parameter boxes nothing once it is a reference type, and the JIT drops the box of a
                        // null check; a concrete value type always boxes.
                        return instruction.Operand is TypeReference boxed && !boxed.IsGenericParameter && !IsBoxNullCheck(instructions, index)
                                   ? (Boxing, TypeName(boxed), null)
                                   : default;
                    default:
                        return default;
                }
            }

            private static bool IsBoxNullCheck(IList<Instruction> instructions, int index)
            {
                if (index + 1 >= instructions.Count)
                {
                    return false;
                }
                var next = instructions[index + 1].OpCode.Code;
                return next == Code.Brtrue || next == Code.Brtrue_S || next == Code.Brfalse || next == Code.Brfalse_S;
            }

            private (string rule, string target, string detail) ClassifyCall(MethodReference called)
            {
                var type = called.DeclaringType;
                var typeName = GetOpenName(type);
                var display = MemberName(called);
                if (typeName == "Unravel.Core.Scene" && FindEntityMethods.Contains(called.Name))
                {
                    return (FindEntity, display, null);
                }
                if (typeName == "Unravel.Core.Physics" && AllocatingPhysicsMethods.Contains(called.Name))
                {
                    return (CastAll, display, null);
                }
                if (typeName == "Unravel.Core.Log")
                {
                    // A warning or an error reports a problem; routine logging is what costs every frame.
                    return ProblemLogMethods.Contains(called.Name) ? default : (Log, display, null);
                }
                if (typeName == "Unravel.Core.Scene" && CreatingSceneMethods.Contains(called.Name))
                {
                    var byKey = called.Parameters.Count > 0 && called.Parameters[0].ParameterType.FullName == "System.String";
                    return (Instantiate, display, byKey ? "by key" : null);
                }
                if (typeName == "Unravel.Core.Entity" && called.Name == "AddComponent")
                {
                    return (Instantiate, display, null);
                }
                if (typeName == "Unravel.Core.Assets" && called.Name == "GetAsset" && called.Parameters.Count == 1 &&
                    called.Parameters[0].ParameterType.FullName == "System.String")
                {
                    return (AssetLookup, display, null);
                }
                if (type.Namespace == "System.Linq" && called.Name != "Empty")
                {
                    return (Linq, display, null);
                }
                if (typeName == "System.String" && StringBuildingMethods.Contains(called.Name))
                {
                    return (StringRule, display, null);
                }
                if (called.Name == "ToStringAndClear" && type.Name == "DefaultInterpolatedStringHandler")
                {
                    return (StringRule, "string interpolation", null);
                }
                if (called.Name == "ToString" && called.Parameters.Count == 0 && typeName != "System.String")
                {
                    return (StringRule, display, null);
                }
                if (type.Namespace == "System.Reflection" ||
                    (typeName == "System.Type" && ReflectionTypeMethods.Contains(called.Name)) ||
                    (typeName == "System.Activator" && called.Name == "CreateInstance") ||
                    (typeName == "System.AppDomain" && called.Name == "GetAssemblies"))
                {
                    return (Reflection, display, null);
                }
                if (typeName == "System.DateTime" && (called.Name == "get_Now" || called.Name == "get_Today"))
                {
                    return (LocalTime, display, null);
                }
                if (typeName == "System.GC" && called.Name == "Collect")
                {
                    return (GcCollect, display, null);
                }
                if (IsBlockingWait(typeName, called.Name))
                {
                    return (BlockingWait, display, null);
                }
                if (typeName == "System.IO.File" || typeName == "System.IO.Directory")
                {
                    return (FileAccess, display, null);
                }
                if (called.Name == "GetEnumerator" &&
                    (typeName == "System.Collections.Generic.IEnumerable`1" || typeName == "System.Collections.IEnumerable"))
                {
                    return (InterfaceEnumeration, TypeName(type), null);
                }
                if (IsEngineType(type))
                {
                    var definition = TryResolve(called);
                    var allocation = definition != null && definition.Module != module_
                                         ? GetEngineAllocation(definition, 0, new HashSet<MethodDefinition>())
                                         : null;
                    if (allocation != null)
                    {
                        return (AllocatingCall, display, allocation.What + (allocation.IsAlways ? " on every call" : " on some calls"));
                    }
                }
                return default;
            }

            /// <summary>A call that stops the calling thread until a timer or another thread is done.</summary>
            private static bool IsBlockingWait(string typeName, string name)
            {
                switch (typeName)
                {
                    case "System.Threading.Thread":
                        return name == "Sleep" || name == "Join";
                    case "System.Threading.Tasks.Task":
                        return name == "Wait" || name == "WaitAll" || name == "WaitAny";
                    case "System.Threading.Tasks.Task`1":
                        return name == "get_Result" || name == "Wait";
                    default:
                        return false;
                }
            }

            private static bool IsEngineType(TypeReference type)
            {
                return GetOpenName(type).StartsWith(EngineNamespace, StringComparison.Ordinal);
            }

            /// <summary>What an engine method allocates, and whether every call does or only some.</summary>
            private sealed class EngineAllocation
            {
                /// <summary>"a new string", "a new Entity[]".</summary>
                public string What;
                /// <summary>Every path through the method allocates it; false when a branch can return without.</summary>
                public bool IsAlways;
            }

            /// <summary>
            /// What an engine method allocates when it runs, outside its error paths and one-time setup: a native
            /// call that returns a string or an array, an array, a class instance or a box, in it or in the engine
            /// methods it calls. Null when it allocates none of these. A method that returns an array or a string is
            /// said to allocate that. An allocation every call makes wins over one only some calls make.
            /// </summary>
            private EngineAllocation GetEngineAllocation(MethodDefinition method, int depth, HashSet<MethodDefinition> visiting)
            {
                if (engineAllocations_.TryGetValue(method, out var known))
                {
                    return known;
                }
                if (!visiting.Add(method))
                {
                    return null;
                }
                var allocation = FindEngineAllocation(method, depth, visiting);
                var returned = method.ReturnType;
                if (allocation != null && (returned.IsArray || returned.FullName == "System.String"))
                {
                    allocation = new EngineAllocation { What = DescribeAllocation(returned), IsAlways = allocation.IsAlways };
                }
                engineAllocations_[method] = allocation;
                return allocation;
            }

            private EngineAllocation FindEngineAllocation(MethodDefinition method, int depth, HashSet<MethodDefinition> visiting)
            {
                if (IsNativeCall(method))
                {
                    // The native side makes a new managed string or array for each call that returns one.
                    return ReturnsNewObject(method) ? new EngineAllocation { What = DescribeAllocation(method.ReturnType), IsAlways = true } : null;
                }
                var instructions = method.Body.Instructions;
                var cold = GetColdInstructions(method);
                EngineAllocation sometimes = null;
                for (int i = 0; i < instructions.Count; i++)
                {
                    var instruction = instructions[i];
                    if (cold.Contains(instruction))
                    {
                        continue;
                    }
                    var found = FindAllocationAt(instructions, i, depth, visiting);
                    if (found == null)
                    {
                        continue;
                    }
                    if (found.IsAlways && RunsOnEveryCall(method.Body, instruction))
                    {
                        return found;
                    }
                    sometimes ??= new EngineAllocation { What = found.What, IsAlways = false };
                }
                return sometimes;
            }

            /// <summary>What the instruction at `index` of an engine method allocates, or null.</summary>
            private EngineAllocation FindAllocationAt(IList<Instruction> instructions, int index, int depth, HashSet<MethodDefinition> visiting)
            {
                var instruction = instructions[index];
                switch (instruction.OpCode.Code)
                {
                    case Code.Newarr when instruction.Operand is TypeReference element:
                        return Always(element.ContainsGenericParameter ? "a new array" : "a new " + TypeName(element) + "[]");
                    case Code.Newobj when instruction.Operand is MethodReference constructor:
                    {
                        var allocated = ClassifyNew(instructions, index, constructor);
                        if (allocated.rule == null)
                        {
                            return null;
                        }
                        return Always(allocated.rule == StringRule ? "a new string" : DescribeAllocation(constructor.DeclaringType));
                    }
                    case Code.Box when instruction.Operand is TypeReference boxed && !boxed.IsGenericParameter && !IsBoxNullCheck(instructions, index):
                        return Always("a boxed " + TypeName(boxed));
                    case Code.Call:
                    case Code.Callvirt:
                    {
                        if (instruction.Operand is not MethodReference called)
                        {
                            return null;
                        }
                        var classified = IsEngineType(called.DeclaringType) ? null : ClassifyFrameworkAllocation(called);
                        if (classified != null)
                        {
                            return Always(classified);
                        }
                        if (depth < EngineCallDepth && IsEngineType(called.DeclaringType))
                        {
                            var definition = TryResolve(called);
                            return definition != null ? GetEngineAllocation(definition, depth + 1, visiting) : null;
                        }
                        return null;
                    }
                    default:
                        return null;
                }
            }

            private static EngineAllocation Always(string what)
            {
                return new EngineAllocation { What = what, IsAlways = true };
            }

            /// <summary>
            /// Whether every path from the method's start to a return passes the instruction: none of its branches
            /// can return without running it.
            /// </summary>
            private static bool RunsOnEveryCall(MethodBody body, Instruction instruction)
            {
                var blocks = SplitBlocks(body);
                var blockOf = new Dictionary<Instruction, int>();
                for (int index = 0; index < blocks.Count; index++)
                {
                    foreach (var member in blocks[index])
                    {
                        blockOf[member] = index;
                    }
                }
                var avoided = blockOf[instruction];
                if (avoided == 0)
                {
                    return true;
                }
                var seen = new HashSet<int> { 0 };
                var queue = new Queue<int>();
                queue.Enqueue(0);
                while (queue.Count > 0)
                {
                    var block = blocks[queue.Dequeue()];
                    var last = block[block.Count - 1];
                    var flow = last.OpCode.FlowControl;
                    if (flow == FlowControl.Return)
                    {
                        return false;
                    }
                    var next = new List<Instruction>();
                    switch (last.Operand)
                    {
                        case Instruction target:
                            next.Add(target);
                            break;
                        case Instruction[] targets:
                            next.AddRange(targets);
                            break;
                    }
                    if (flow != FlowControl.Branch && flow != FlowControl.Throw && last.Next != null)
                    {
                        next.Add(last.Next);
                    }
                    foreach (var successor in next)
                    {
                        if (blockOf.TryGetValue(successor, out var index) && index != avoided && seen.Add(index))
                        {
                            queue.Enqueue(index);
                        }
                    }
                }
                return true;
            }

            /// <summary>
            /// What a framework call inside an engine method allocates: built strings, LINQ, and the arrays methods
            /// such as ToArray return; null for others.
            /// </summary>
            private static string ClassifyFrameworkAllocation(MethodReference called)
            {
                var typeName = GetOpenName(called.DeclaringType);
                if (typeName == "System.String" && StringBuildingMethods.Contains(called.Name))
                {
                    return "a new string";
                }
                if (called.DeclaringType.Namespace == "System.Linq" && called.Name != "Empty")
                {
                    return "LINQ enumerators";
                }
                if (called.ReturnType.IsArray && called.Name != "Empty")
                {
                    return DescribeAllocation(called.ReturnType);
                }
                return null;
            }

            /// <summary>
            /// A method the native side runs: an extern internal call, or one the icall weaver gave a body that calls
            /// the native function through a pointer (calli).
            /// </summary>
            private static bool IsNativeCall(MethodDefinition method)
            {
                return !method.HasBody || method.Body.Instructions.Any(instruction => instruction.OpCode.Code == Code.Calli);
            }

            /// <summary>Whether a native call returns a managed string or array, which it makes new for each call.</summary>
            private static bool ReturnsNewObject(MethodDefinition method)
            {
                var returned = method.ReturnType;
                return returned.IsArray || returned.FullName == "System.String" || returned.FullName == "System.Array";
            }

            private static string DescribeAllocation(TypeReference type)
            {
                if (type.ContainsGenericParameter)
                {
                    return type.IsArray ? "a new array" : "a new object";
                }
                switch (type.FullName)
                {
                    case "System.String":
                        return "a new string";
                    case "System.Array":
                        return "a new array";
                    default:
                        return "a new " + TypeName(type);
                }
            }

            private (string rule, string target, string detail) ClassifyNew(IList<Instruction> instructions, int index, MethodReference constructor)
            {
                var type = constructor.DeclaringType;
                // An exception thrown on the spot is not a cost of the frames that do not throw.
                if (index + 1 < instructions.Count && instructions[index + 1].OpCode.Code == Code.Throw)
                {
                    return default;
                }
                if (type.Name.StartsWith("<>c__DisplayClass", StringComparison.Ordinal))
                {
                    return (Closure, "captured variables", "closure");
                }
                var definition = TryResolve(type);
                if (definition == null || definition.IsValueType)
                {
                    return default;
                }
                if (definition.BaseType?.FullName == "System.MulticastDelegate")
                {
                    return IsCachedDelegate(instructions, index) ? default : (Closure, TypeName(type), "delegate");
                }
                if (type.FullName == "System.String")
                {
                    return (StringRule, "new String", null);
                }
                if (FileStreamTypes.Contains(type.FullName) && constructor.Parameters.Count > 0 &&
                    constructor.Parameters[0].ParameterType.FullName == "System.String")
                {
                    return (FileAccess, "new " + TypeName(type), null);
                }
                // The state machine of an iterator or async method the code calls: the call is what the source wrote,
                // and a closure has its own rule.
                if (IsGenerated(type.Name))
                {
                    return default;
                }
                return (ObjectRule, TypeName(type), null);
            }

            private (string rule, string target, string detail) ClassifyArray(IList<Instruction> instructions, int index, TypeReference element)
            {
                var paramsCall = FindParamsCall(instructions, index, element);
                // The array a concatenation or a Format passes its parts in is part of the string the string rule
                // reports.
                if (paramsCall != null && paramsCall.DeclaringType.FullName == "System.String" &&
                    StringBuildingMethods.Contains(paramsCall.Name))
                {
                    return default;
                }
                return (ArrayRule, TypeName(element) + "[]", paramsCall != null ? MemberName(paramsCall) : null);
            }

            /// <summary>
            /// The call a new array is the params argument of, or null: the array is followed on the stack, through
            /// the stores of its elements, to the call that takes it as its last argument. An array kept in a local or
            /// passed anywhere else is no call's params argument.
            /// </summary>
            private static MethodReference FindParamsCall(IList<Instruction> instructions, int index, TypeReference element)
            {
                const int Window = 96;
                // The values pushed since the array, bottom first; true for a copy of the array.
                var stack = new List<bool> { true };
                var instruction = instructions[index].Next;
                for (int steps = 0; instruction != null && steps < Window; steps++)
                {
                    var code = instruction.OpCode.Code;
                    if (code == Code.Dup)
                    {
                        if (stack.Count == 0)
                        {
                            return null;
                        }
                        stack.Add(stack[stack.Count - 1]);
                        instruction = instruction.Next;
                        continue;
                    }
                    var (pops, pushes) = GetStackEffect(instruction);
                    if (pops < 0)
                    {
                        return null;
                    }
                    // The values the instruction takes, the last argument first; values from before the array are
                    // below it, so taking one of them takes the array too.
                    var taken = new List<bool>();
                    for (int i = 0; i < pops; i++)
                    {
                        if (stack.Count == 0)
                        {
                            break;
                        }
                        taken.Add(stack[stack.Count - 1]);
                        stack.RemoveAt(stack.Count - 1);
                    }
                    // A copy taken while another stays (an element stored, InitializeArray) fills the array; the
                    // instruction that takes the last copy is where it goes.
                    if (!stack.Contains(true))
                    {
                        var isLastArgument = taken.Count > 0 && taken[0] && IsCallOrNew(code);
                        return isLastArgument && instruction.Operand is MethodReference called && IsParamsOf(called, element) ? called : null;
                    }
                    for (int i = 0; i < pushes; i++)
                    {
                        stack.Add(false);
                    }
                    var flow = instruction.OpCode.FlowControl;
                    if (flow == FlowControl.Branch && instruction.Operand is Instruction target && target.Offset > instruction.Offset)
                    {
                        // The other arm of a conditional element: carry on where the arms join.
                        instruction = target;
                        continue;
                    }
                    if (flow == FlowControl.Return || flow == FlowControl.Throw || flow == FlowControl.Branch)
                    {
                        return null;
                    }
                    instruction = instruction.Next;
                }
                return null;
            }

            private static bool IsCallOrNew(Code code)
            {
                return code == Code.Call || code == Code.Callvirt || code == Code.Newobj;
            }

            private static bool IsParamsOf(MethodReference called, TypeReference element)
            {
                var definition = TryResolve(called);
                var last = definition?.Parameters.LastOrDefault();
                return last != null && last.ParameterType is ArrayType array && array.ElementType.FullName == element.FullName &&
                       last.CustomAttributes.Any(attribute => attribute.AttributeType.Name == "ParamArrayAttribute");
            }

            /// <summary>How many values an instruction takes from the stack and leaves on it; -1 taken when unknown.</summary>
            private static (int pops, int pushes) GetStackEffect(Instruction instruction)
            {
                var opcode = instruction.OpCode;
                int pops;
                switch (opcode.StackBehaviourPop)
                {
                    case StackBehaviour.Pop0:
                        pops = 0;
                        break;
                    case StackBehaviour.Pop1:
                    case StackBehaviour.Popi:
                    case StackBehaviour.Popref:
                        pops = 1;
                        break;
                    case StackBehaviour.Pop1_pop1:
                    case StackBehaviour.Popi_pop1:
                    case StackBehaviour.Popi_popi:
                    case StackBehaviour.Popi_popi8:
                    case StackBehaviour.Popi_popr4:
                    case StackBehaviour.Popi_popr8:
                    case StackBehaviour.Popref_pop1:
                    case StackBehaviour.Popref_popi:
                        pops = 2;
                        break;
                    case StackBehaviour.Popi_popi_popi:
                    case StackBehaviour.Popref_popi_popi:
                    case StackBehaviour.Popref_popi_popi8:
                    case StackBehaviour.Popref_popi_popr4:
                    case StackBehaviour.Popref_popi_popr8:
                    case StackBehaviour.Popref_popi_popref:
                        pops = 3;
                        break;
                    case StackBehaviour.Varpop when instruction.Operand is MethodReference called && opcode.Code != Code.Calli:
                        pops = called.Parameters.Count + (called.HasThis && opcode.Code != Code.Newobj ? 1 : 0);
                        break;
                    default:
                        return (-1, 0);
                }
                int pushes;
                switch (opcode.StackBehaviourPush)
                {
                    case StackBehaviour.Push0:
                        pushes = 0;
                        break;
                    case StackBehaviour.Push1_push1:
                        pushes = 2;
                        break;
                    case StackBehaviour.Varpush when instruction.Operand is MethodReference called:
                        pushes = opcode.Code == Code.Newobj || called.ReturnType.FullName != "System.Void" ? 1 : 0;
                        break;
                    case StackBehaviour.Varpush:
                        return (-1, 0);
                    default:
                        pushes = 1;
                        break;
                }
                return (pops, pushes);
            }

            /// <summary>
            /// A delegate the compiler caches in a static field: created on the first run only (dup, then stsfld into
            /// the field the earlier ldsfld tested).
            /// </summary>
            private static bool IsCachedDelegate(IList<Instruction> instructions, int index)
            {
                return index + 2 < instructions.Count && instructions[index + 1].OpCode.Code == Code.Dup &&
                       instructions[index + 2].OpCode.Code == Code.Stsfld;
            }

            private List<string> GetPath(MethodDefinition method)
            {
                var path = new List<string>();
                for (var step = method; step != null && path.Count < 32; step = parents_.TryGetValue(step, out var parent) ? parent : null)
                {
                    path.Add(DisplayName(step));
                }
                path.Reverse();
                return path;
            }

            private static (string file, int line) GetLine(MethodDefinition method, Instruction instruction)
            {
                if (!method.DebugInformation.HasSequencePoints)
                {
                    return (null, 0);
                }
                // The sequence point that covers an instruction is the nearest one at or before it.
                for (var current = instruction; current != null; current = current.Previous)
                {
                    var point = method.DebugInformation.GetSequencePoint(current);
                    if (point != null && !point.IsHidden)
                    {
                        return (point.Document.Url, point.StartLine);
                    }
                }
                return GetFirstLine(method);
            }

            private static (string file, int line) GetFirstLine(MethodDefinition method)
            {
                if (!method.DebugInformation.HasSequencePoints)
                {
                    return (null, 0);
                }
                var point = method.DebugInformation.SequencePoints.FirstOrDefault(candidate => !candidate.IsHidden);
                return point != null ? (point.Document.Url, point.StartLine) : (null, 0);
            }

            private static bool IsCall(Instruction instruction)
            {
                return instruction.OpCode.Code == Code.Call || instruction.OpCode.Code == Code.Callvirt;
            }

            /// <summary>A called member as the source names it: "Physics.RaycastAll", a property without its get_ ("Entity.name").</summary>
            private static string MemberName(MethodReference called)
            {
                var name = called.Name;
                if (name.StartsWith("get_", StringComparison.Ordinal) || name.StartsWith("set_", StringComparison.Ordinal))
                {
                    name = name.Substring(4);
                }
                if (name == ".ctor")
                {
                    return "new " + TypeName(called.DeclaringType);
                }
                return TypeName(called.DeclaringType) + "." + name;
            }

            /// <summary>"Player.OnUpdate", "Player.OnUpdate (lambda)", "Player.Spawn (async)": as the source names it.</summary>
            private static string DisplayName(MethodDefinition method)
            {
                var type = method.DeclaringType;
                var name = method.Name;
                string kind = null;
                if (IsGenerated(type.Name))
                {
                    if (name == "MoveNext")
                    {
                        name = GetSourceName(type.Name);
                        kind = type.Interfaces.Any(entry => entry.InterfaceType.Name == "IAsyncStateMachine") ? "async" : "iterator";
                    }
                    while (type.DeclaringType != null && IsGenerated(type.Name))
                    {
                        type = type.DeclaringType;
                    }
                }
                if (IsGenerated(name))
                {
                    kind ??= name.Contains(">g__", StringComparison.Ordinal) ? "local function" : "lambda";
                    name = GetSourceName(name);
                }
                // A constructor reads as its type: "Player.Weapon (constructor)".
                if (name == ".ctor" || name == ".cctor")
                {
                    kind ??= name == ".ctor" ? "constructor" : "static constructor";
                    return $"{TypeName(type)} ({kind})";
                }
                var display = TypeName(type) + "." + name;
                return kind != null ? $"{display} ({kind})" : display;
            }

            private static bool IsGenerated(string name)
            {
                return name.StartsWith("<", StringComparison.Ordinal);
            }

            /// <summary>"&lt;OnUpdate&gt;b__3_0" -> "OnUpdate".</summary>
            private static string GetSourceName(string generated)
            {
                var end = generated.IndexOf('>');
                return end > 1 ? generated.Substring(1, end - 1) : generated;
            }

            /// <summary>A type as the source writes it: "List&lt;Int32&gt;", "Player.Weapon".</summary>
            private static string TypeName(TypeReference type)
            {
                if (type is ArrayType array)
                {
                    return TypeName(array.ElementType) + "[]";
                }
                if (type is GenericInstanceType generic)
                {
                    return StripArity(generic.ElementType.Name) + "<" + string.Join(", ", generic.GenericArguments.Select(TypeName)) + ">";
                }
                var name = StripArity(type.Name);
                return type.DeclaringType != null && !IsGenerated(type.DeclaringType.Name) ? TypeName(type.DeclaringType) + "." + name : name;
            }

            private static string StripArity(string name)
            {
                var tick = name.IndexOf('`');
                return tick > 0 ? name.Substring(0, tick) : name;
            }
        }
    }
}
