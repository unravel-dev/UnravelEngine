using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Xml;

namespace Unravel.Core
{
    /// <summary>
    /// Searchable index of the public scripting API: the engine's types and the game's, each member
    /// with its C# signature and the summary of its XML documentation. The editor serves it to tools
    /// (the scripts_api_search and scripts_api_get MCP tools).
    /// </summary>
    /// <remarks>
    /// Built on first use and rebuilt when the set of loaded script assemblies changes. Holds game
    /// types, so it is dropped before a script domain unloads.
    /// </remarks>
    [AutoStaticsCleanup]
    internal static class ApiIndex
    {
        private sealed class Entry
        {
            public string kind;        // type, constructor, property, field, method, event, value
            public Type type;          // the declaring type, or the type itself for a type entry
            public string name;        // member name, or the type name for a type entry
            public string signature;   // C# declaration
            public string summary;     // documentation summary, plain text
        }

        private const int DEFAULT_SEARCH_LIMIT = 20;
        private const int MAX_SEARCH_LIMIT = 200;
        private const int SEARCH_SUMMARY_LENGTH = 160;

        private static List<Entry> entries_;
        private static Dictionary<Type, List<Entry>> entriesByType_;
        private static string indexKey_;

        private static void OnStaticsCleanup()
        {
            entries_ = null;
            entriesByType_ = null;
            indexKey_ = null;
        }

        /// <summary>
        /// Searches type and member names and documentation for every word of
        /// <paramref name="query"/> and returns the best matches, one per line.
        /// </summary>
        /// <param name="query">Words to look for, case-insensitive; all must match.</param>
        /// <param name="limit">Maximum number of results (0 for the default).</param>
        /// <param name="docPaths">The XML documentation files of the script assemblies, ';'-separated.</param>
        internal static string internal_n2m_search(string query, int limit, string docPaths)
        {
            EnsureIndex(docPaths);
            limit = limit <= 0 ? DEFAULT_SEARCH_LIMIT : Math.Min(limit, MAX_SEARCH_LIMIT);
            string[] words = (query ?? "").ToLowerInvariant().Split(new[] { ' ', '\t', '.', ',' }, StringSplitOptions.RemoveEmptyEntries);
            if (words.Length == 0)
            {
                return "Empty query.";
            }
            var matches = new List<(int score, Entry entry)>();
            foreach (Entry entry in entries_)
            {
                int score = Score(entry, words);
                if (score > 0)
                {
                    matches.Add((score, entry));
                }
            }
            matches.Sort((a, b) => b.score != a.score ? b.score.CompareTo(a.score) : string.CompareOrdinal(a.entry.name, b.entry.name));
            var text = new StringBuilder();
            text.Append(matches.Count).Append(" matches").Append(matches.Count > limit ? $", first {limit}" : "").Append('\n');
            foreach (var (_, entry) in matches.Take(limit))
            {
                text.Append(entry.kind == "type" ? FullName(entry.type) : FullName(entry.type) + "." + entry.name)
                    .Append(": ").Append(entry.signature);
                AppendSummary(text, entry.summary, SEARCH_SUMMARY_LENGTH);
                text.Append('\n');
            }
            return text.ToString();
        }

        /// <summary>
        /// Describes one type: its declaration and summary, then every public member with its
        /// signature and summary.
        /// </summary>
        /// <param name="typeName">Full or short type name, case-insensitive.</param>
        /// <param name="docPaths">The XML documentation files of the script assemblies, ';'-separated.</param>
        internal static string internal_n2m_describe_type(string typeName, string docPaths)
        {
            EnsureIndex(docPaths);
            string wanted = (typeName ?? "").Trim();
            var candidates = entriesByType_.Keys
                .Where(t => string.Equals(FullName(t), wanted, StringComparison.OrdinalIgnoreCase) ||
                            string.Equals(TypeName(t), wanted, StringComparison.OrdinalIgnoreCase))
                .ToList();
            if (candidates.Count == 0)
            {
                return $"No type named '{wanted}'. Use scripts_api_search to find it.";
            }
            if (candidates.Count > 1)
            {
                return "Several types match: " + string.Join(", ", candidates.Select(FullName));
            }
            Type type = candidates[0];
            var text = new StringBuilder();
            foreach (Entry entry in entriesByType_[type])
            {
                if (entry.kind != "type")
                {
                    text.Append("  ");
                }
                text.Append(entry.signature);
                AppendSummary(text, entry.summary, int.MaxValue);
                text.Append('\n');
            }
            return text.ToString();
        }

        private static int Score(Entry entry, string[] words)
        {
            string name = entry.name.ToLowerInvariant();
            string typeName = TypeName(entry.type).ToLowerInvariant();
            string summary = (entry.summary ?? "").ToLowerInvariant();
            int score = 0;
            foreach (string word in words)
            {
                if (name == word)
                {
                    score += 20;
                }
                else if (name.Contains(word))
                {
                    score += 8;
                }
                else if (typeName.Contains(word))
                {
                    score += 4;
                }
                else if (summary.Contains(word))
                {
                    score += 1;
                }
                else
                {
                    return 0;
                }
            }
            return entry.kind == "type" ? score + 1 : score;
        }

        private static void AppendSummary(StringBuilder text, string summary, int maxLength)
        {
            if (string.IsNullOrEmpty(summary))
            {
                return;
            }
            text.Append(" -- ").Append(summary.Length > maxLength ? summary.Substring(0, maxLength) + "..." : summary);
        }

        // ---------------------------------------------------------------- index

        private static void EnsureIndex(string docPaths)
        {
            Assembly engine = typeof(ApiIndex).Assembly;
            List<Assembly> assemblies = AppDomain.CurrentDomain.GetAssemblies()
                .Where(a => a == engine || (!a.IsDynamic && a.GetReferencedAssemblies().Any(r => r.Name == engine.GetName().Name)))
                .OrderBy(a => a == engine ? 0 : 1).ThenBy(a => a.GetName().Name)
                .ToList();
            string key = docPaths + "|" + string.Join("|", assemblies.Select(a => a.ManifestModule.ModuleVersionId.ToString()));
            if (entries_ != null && key == indexKey_)
            {
                return;
            }
            Dictionary<string, string> docs = LoadDocs(docPaths);
            entries_ = new List<Entry>();
            entriesByType_ = new Dictionary<Type, List<Entry>>();
            foreach (Assembly assembly in assemblies)
            {
                foreach (Type type in GetExportedTypes(assembly))
                {
                    AddType(type, docs);
                }
            }
            indexKey_ = key;
        }

        private static IEnumerable<Type> GetExportedTypes(Assembly assembly)
        {
            Type[] types;
            try
            {
                types = assembly.GetExportedTypes();
            }
            catch (Exception)
            {
                return Array.Empty<Type>();
            }
            return types.Where(t => !t.IsDefined(typeof(System.Runtime.CompilerServices.CompilerGeneratedAttribute), false));
        }

        private static void AddType(Type type, Dictionary<string, string> docs)
        {
            var list = new List<Entry>();
            entriesByType_[type] = list;
            Add(list, "type", type, TypeName(type), TypeDeclaration(type), Doc(docs, "T:" + DocTypeName(type)));

            const BindingFlags flags = BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;
            if (type.IsEnum)
            {
                foreach (FieldInfo field in type.GetFields(BindingFlags.Public | BindingFlags.Static))
                {
                    object value = Convert.ChangeType(field.GetValue(null), Enum.GetUnderlyingType(type));
                    Add(list, "value", type, field.Name, $"{field.Name} = {value}", Doc(docs, "F:" + DocTypeName(type) + "." + field.Name));
                }
                return;
            }
            if (typeof(Delegate).IsAssignableFrom(type))
            {
                return;
            }
            foreach (ConstructorInfo ctor in type.GetConstructors(flags))
            {
                Add(list, "constructor", type, TypeName(type), Obsolete(ctor) + TypeName(type) + "(" + Parameters(ctor.GetParameters()) + ")",
                    Doc(docs, "M:" + DocTypeName(type) + ".#ctor" + DocParameters(ctor.GetParameters())));
            }
            foreach (PropertyInfo property in type.GetProperties(flags))
            {
                MethodInfo getter = property.GetGetMethod();
                MethodInfo setter = property.GetSetMethod();
                MethodInfo any = getter ?? setter;
                if (any == null)
                {
                    continue;
                }
                ParameterInfo[] indexers = property.GetIndexParameters();
                string name = indexers.Length > 0 ? "this[" + Parameters(indexers) + "]" : property.Name;
                string accessors = "{ " + (getter != null ? "get; " : "") + (setter != null ? "set; " : "") + "}";
                Add(list, "property", type, property.Name,
                    Obsolete(property) + (any.IsStatic ? "static " : "") + FriendlyName(property.PropertyType) + " " + name + " " + accessors,
                    Doc(docs, "P:" + DocTypeName(type) + "." + property.Name + (indexers.Length > 0 ? DocParameters(indexers) : "")));
            }
            foreach (FieldInfo field in type.GetFields(flags))
            {
                if (field.IsSpecialName)
                {
                    continue;
                }
                string modifiers = field.IsLiteral ? "const " : (field.IsStatic ? "static " : "") + (field.IsInitOnly ? "readonly " : "");
                string value = field.IsLiteral ? " = " + FormatValue(field.GetRawConstantValue()) : "";
                Add(list, "field", type, field.Name, Obsolete(field) + modifiers + FriendlyName(field.FieldType) + " " + field.Name + value,
                    Doc(docs, "F:" + DocTypeName(type) + "." + field.Name));
            }
            foreach (EventInfo ev in type.GetEvents(flags))
            {
                Add(list, "event", type, ev.Name, Obsolete(ev) + "event " + FriendlyName(ev.EventHandlerType) + " " + ev.Name,
                    Doc(docs, "E:" + DocTypeName(type) + "." + ev.Name));
            }
            foreach (MethodInfo method in type.GetMethods(flags))
            {
                bool isOperator = method.Name.StartsWith("op_", StringComparison.Ordinal);
                if (method.IsSpecialName && !isOperator)
                {
                    continue;   // property and event accessors
                }
                string name = isOperator ? OperatorName(method.Name) : method.Name;
                string generic = method.IsGenericMethodDefinition
                    ? "<" + string.Join(", ", method.GetGenericArguments().Select(a => a.Name)) + ">"
                    : "";
                string signature = Obsolete(method) + (method.IsStatic ? "static " : "") + (method.IsAbstract && !type.IsInterface ? "abstract " : "") +
                                   (method.IsVirtual && !method.IsAbstract && !method.IsFinal && method.GetBaseDefinition() == method ? "virtual " : "") +
                                   FriendlyName(method.ReturnType) + " " + name + generic + "(" + Parameters(method.GetParameters()) + ")";
                string docId = "M:" + DocTypeName(type) + "." + method.Name +
                               (method.IsGenericMethodDefinition ? "``" + method.GetGenericArguments().Length : "") +
                               DocParameters(method.GetParameters());
                Add(list, "method", type, name, signature, Doc(docs, docId));
            }
        }

        private static void Add(List<Entry> list, string kind, Type type, string name, string signature, string summary)
        {
            var entry = new Entry { kind = kind, type = type, name = name, signature = signature, summary = summary };
            list.Add(entry);
            entries_.Add(entry);
        }

        // ---------------------------------------------------------------- C# rendering

        private static readonly Dictionary<Type, string> Keywords = new Dictionary<Type, string>
        {
            { typeof(void), "void" }, { typeof(bool), "bool" }, { typeof(byte), "byte" }, { typeof(sbyte), "sbyte" },
            { typeof(short), "short" }, { typeof(ushort), "ushort" }, { typeof(int), "int" }, { typeof(uint), "uint" },
            { typeof(long), "long" }, { typeof(ulong), "ulong" }, { typeof(float), "float" }, { typeof(double), "double" },
            { typeof(decimal), "decimal" }, { typeof(char), "char" }, { typeof(string), "string" }, { typeof(object), "object" },
        };

        private static string TypeName(Type type)
        {
            string name = type.Name;
            int tick = name.IndexOf('`');
            return tick >= 0 ? name.Substring(0, tick) : name;
        }

        private static string FullName(Type type)
        {
            string outer = type.IsNested ? FullName(type.DeclaringType) : type.Namespace;
            return string.IsNullOrEmpty(outer) ? TypeName(type) : outer + "." + TypeName(type);
        }

        private static string FriendlyName(Type type)
        {
            if (type.IsByRef)
            {
                return FriendlyName(type.GetElementType());
            }
            if (type.IsArray)
            {
                return FriendlyName(type.GetElementType()) + "[" + new string(',', type.GetArrayRank() - 1) + "]";
            }
            if (Keywords.TryGetValue(type, out string keyword))
            {
                return keyword;
            }
            if (type.IsGenericType)
            {
                Type definition = type.GetGenericTypeDefinition();
                Type[] arguments = type.GetGenericArguments();
                if (definition == typeof(Nullable<>))
                {
                    return FriendlyName(arguments[0]) + "?";
                }
                return TypeName(type) + "<" + string.Join(", ", arguments.Select(FriendlyName)) + ">";
            }
            return TypeName(type);
        }

        private static string TypeDeclaration(Type type)
        {
            string kind = type.IsEnum ? "enum" : type.IsInterface ? "interface" : type.IsValueType ? "struct" :
                          typeof(Delegate).IsAssignableFrom(type) ? "delegate" : "class";
            string modifiers = !type.IsValueType && !type.IsInterface && type.IsAbstract && type.IsSealed ? "static " :
                               !type.IsInterface && type.IsAbstract ? "abstract " :
                               type.IsSealed && kind == "class" ? "sealed " : "";
            if (kind == "delegate")
            {
                MethodInfo invoke = type.GetMethod("Invoke");
                return Obsolete(type) + "delegate " + FriendlyName(invoke.ReturnType) + " " + FriendlyName(type) +
                       "(" + Parameters(invoke.GetParameters()) + ")";
            }
            var bases = new List<string>();
            if (kind == "class" && type.BaseType != null && type.BaseType != typeof(object))
            {
                bases.Add(FriendlyName(type.BaseType));
            }
            bases.AddRange(type.GetInterfaces().Where(i => i.IsPublic && (type.BaseType == null || !i.IsAssignableFrom(type.BaseType))).Select(FriendlyName));
            return Obsolete(type) + modifiers + kind + " " + FullName(type) +
                   (type.IsGenericTypeDefinition ? "<" + string.Join(", ", type.GetGenericArguments().Select(a => a.Name)) + ">" : "") +
                   (bases.Count > 0 ? " : " + string.Join(", ", bases) : "");
        }

        private static string Parameters(ParameterInfo[] parameters)
        {
            return string.Join(", ", parameters.Select(p =>
            {
                string prefix = p.ParameterType.IsByRef ? (p.IsOut ? "out " : p.IsIn ? "in " : "ref ") :
                                p.IsDefined(typeof(ParamArrayAttribute), false) ? "params " : "";
                string value = p.HasDefaultValue ? " = " + FormatValue(p.DefaultValue) : "";
                return prefix + FriendlyName(p.ParameterType) + " " + p.Name + value;
            }));
        }

        private static string FormatValue(object value)
        {
            switch (value)
            {
                case null: return "null";
                case bool b: return b ? "true" : "false";
                case string s: return "\"" + s + "\"";
                case char c: return "'" + c + "'";
                case float f when float.IsPositiveInfinity(f): return "float.PositiveInfinity";
                case float f when float.IsNegativeInfinity(f): return "float.NegativeInfinity";
                case float f when float.IsNaN(f): return "float.NaN";
                case float f: return f.ToString(System.Globalization.CultureInfo.InvariantCulture) + "f";
                case double d when double.IsPositiveInfinity(d): return "double.PositiveInfinity";
                case double d when double.IsNegativeInfinity(d): return "double.NegativeInfinity";
                case double d when double.IsNaN(d): return "double.NaN";
                case double d: return d.ToString(System.Globalization.CultureInfo.InvariantCulture);
                case Enum e: return e.GetType().Name + "." + e;
                default: return Convert.ToString(value, System.Globalization.CultureInfo.InvariantCulture);
            }
        }

        private static string Obsolete(MemberInfo member)
        {
            return member.IsDefined(typeof(ObsoleteAttribute), false) ? "[Obsolete] " : "";
        }

        private static string OperatorName(string name)
        {
            switch (name)
            {
                case "op_Addition": return "operator +";
                case "op_Subtraction": return "operator -";
                case "op_Multiply": return "operator *";
                case "op_Division": return "operator /";
                case "op_UnaryNegation": return "operator -";
                case "op_Equality": return "operator ==";
                case "op_Inequality": return "operator !=";
                case "op_Implicit": return "implicit operator";
                case "op_Explicit": return "explicit operator";
                case "op_LessThan": return "operator <";
                case "op_GreaterThan": return "operator >";
                case "op_True": return "operator true";
                case "op_False": return "operator false";
                default: return name;
            }
        }

        // ---------------------------------------------------------------- XML documentation

        private static string DocTypeName(Type type)
        {
            if (type.IsGenericParameter)
            {
                return (type.DeclaringMethod != null ? "``" : "`") + type.GenericParameterPosition;
            }
            if (type.IsByRef)
            {
                return DocTypeName(type.GetElementType()) + "@";
            }
            if (type.IsArray)
            {
                return DocTypeName(type.GetElementType()) + "[]";
            }
            string outer = type.IsNested ? DocTypeName(type.DeclaringType) : type.Namespace;
            string name = type.IsGenericType && !type.IsGenericTypeDefinition
                ? TypeName(type) + "{" + string.Join(",", type.GetGenericArguments().Select(DocTypeName)) + "}"
                : type.Name;
            return string.IsNullOrEmpty(outer) ? name : outer + "." + name;
        }

        private static string DocParameters(ParameterInfo[] parameters)
        {
            return parameters.Length == 0 ? "" : "(" + string.Join(",", parameters.Select(p => DocTypeName(p.ParameterType))) + ")";
        }

        private static string Doc(Dictionary<string, string> docs, string id)
        {
            return docs.TryGetValue(id, out string summary) ? summary : null;
        }

        private static Dictionary<string, string> LoadDocs(string docPaths)
        {
            var docs = new Dictionary<string, string>();
            foreach (string path in (docPaths ?? "").Split(new[] { ';' }, StringSplitOptions.RemoveEmptyEntries))
            {
                if (!File.Exists(path))
                {
                    continue;
                }
                try
                {
                    using (XmlReader reader = XmlReader.Create(path))
                    {
                        while (reader.ReadToFollowing("member"))
                        {
                            string id = reader.GetAttribute("name");
                            using (XmlReader member = reader.ReadSubtree())
                            {
                                if (id != null && member.ReadToFollowing("summary"))
                                {
                                    docs[id] = PlainText(member.ReadSubtree());
                                }
                            }
                        }
                    }
                }
                catch (Exception ex)
                {
                    Log.Warning($"API index: could not read {path}: {ex.Message}");
                }
            }
            return docs;
        }

        /// <summary>The text of a documentation element with references reduced to their names.</summary>
        private static string PlainText(XmlReader reader)
        {
            var text = new StringBuilder();
            reader.Read();
            while (reader.Read())
            {
                if (reader.NodeType == XmlNodeType.Text || reader.NodeType == XmlNodeType.CDATA)
                {
                    text.Append(reader.Value);
                }
                else if (reader.NodeType == XmlNodeType.Element)
                {
                    string reference = reader.GetAttribute("cref") ?? reader.GetAttribute("name") ?? reader.GetAttribute("langword");
                    if (reference != null)
                    {
                        int colon = reference.IndexOf(':');
                        string name = colon >= 0 ? reference.Substring(colon + 1) : reference;
                        int paren = name.IndexOf('(');
                        name = paren >= 0 ? name.Substring(0, paren) : name;
                        int dot = name.LastIndexOf('.');
                        text.Append(' ').Append(dot >= 0 ? name.Substring(dot + 1) : name).Append(' ');
                    }
                }
            }
            string plain = string.Join(" ", text.ToString().Split(new[] { ' ', '\t', '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries));
            // A reference is padded with spaces; punctuation right after it keeps to the word.
            foreach (string punctuation in new[] { ".", ",", ";", ":", ")", "!", "?" })
            {
                plain = plain.Replace(" " + punctuation, punctuation);
            }
            return plain.Replace("( ", "(");
        }
    }
}
