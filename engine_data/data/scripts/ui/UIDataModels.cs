using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;

namespace Unravel.Core
{
    /// <summary>
    /// The script side of UI data models: every value native code reads or writes is a node here
    /// (a bound variable, or a member or element below one), addressed by id. Values cross as
    /// text with a type tag; the tags mirror ui_data_model_bindings.cpp.
    /// </summary>
    [AutoStaticsCleanup]
    internal static class UIDataModels
    {
        internal enum NodeKind
        {
            Missing,
            Scalar,
            Struct,
            Array
        }

        internal sealed class Node
        {
            internal long id;
            internal UIDataModel model;
            internal Node parent;
            internal string name;
            internal int index = -1;
            internal Type declaredType;
            internal Func<object> rootGetter;
            internal Action<object> rootSetter;
            internal ScriptComponent script;
            internal Dictionary<string, Node> namedChildren;
            internal List<Node> indexChildren;
            internal bool warned;
        }

        internal sealed class MemberAccessor
        {
            internal string name;
            internal Type type;
            internal bool canWrite;
            internal FieldInfo field;
            internal PropertyInfo property;

            internal object Get(object target)
            {
                return field != null ? field.GetValue(target) : property.GetValue(target);
            }

            internal void Set(object target, object value)
            {
                object converted = ConvertTo(value, type);
                if (field != null)
                {
                    field.SetValue(target, converted);
                }
                else
                {
                    property.SetValue(target, converted);
                }
            }
        }

        private sealed class TransformBinding
        {
            internal Func<object, object[], object> transform;
            internal ScriptComponent script;
        }

        private const int KindBits = 2;
        private const char ValueMissing = 'n';
        private const char ValueBool = 'b';
        private const char ValueInteger = 'i';
        private const char ValueNumber = 'd';
        private const char ValueString = 's';
        private const char ValueSeparator = '\x1e';

        private static readonly Dictionary<(Entity, string), UIDataModel> models = new Dictionary<(Entity, string), UIDataModel>();
        private static readonly List<Node> nodes = new List<Node>();
        private static readonly Dictionary<(Entity, string), TransformBinding> transforms = new Dictionary<(Entity, string), TransformBinding>();
        private static readonly Dictionary<Type, Dictionary<string, MemberAccessor>> membersByType = new Dictionary<Type, Dictionary<string, MemberAccessor>>();
        private static readonly Dictionary<(Type, string), MethodInfo> inlineHandlers = new Dictionary<(Type, string), MethodInfo>();
        private static readonly HashSet<string> warnings = new HashSet<string>();

        private static void OnStaticsCleanup()
        {
            models.Clear();
            nodes.Clear();
            transforms.Clear();
            membersByType.Clear();
            inlineHandlers.Clear();
            warnings.Clear();
        }

        internal static UIDataModel GetOrCreate(Entity owner, string name)
        {
            if (string.IsNullOrEmpty(name))
            {
                throw new ArgumentException("A data model needs the name the document's data-model attribute gives it.");
            }
            if (!models.TryGetValue((owner, name), out UIDataModel model))
            {
                model = new UIDataModel(owner, name);
                models[(owner, name)] = model;
                Sync(model);
            }
            return model;
        }

        internal static Node CreateRoot(UIDataModel model, string name, Type type)
        {
            var node = new Node { model = model, name = name, declaredType = type };
            return Register(node);
        }

        /// <summary>
        /// Makes the model's bindings in the document's current context; the native side skips the
        /// ones it has. Without a context yet, <see cref="Poll"/> retries once there is one.
        /// </summary>
        internal static void Sync(UIDataModel model)
        {
            ulong generation = model.GetGeneration();
            if (generation == 0)
            {
                return;
            }
            model.syncedGeneration = generation;
            if (!model.Exists())
            {
                Warn($"UI: the document of entity {model.owner} declares no data-model '{model.name}' (add data-model=\"{model.name}\" to an element).");
                return;
            }
            foreach (KeyValuePair<string, Node> kvp in model.variables)
            {
                if (kvp.Value.rootGetter != null)
                {
                    model.BindNative(kvp.Key, Encode(kvp.Value));
                }
            }
            foreach (string eventName in model.events.Keys)
            {
                model.BindNativeEvent(eventName);
            }
            foreach ((Entity owner, string name) key in transforms.Keys)
            {
                if (key.owner == model.owner)
                {
                    model.AddNativeTransform(key.name);
                }
            }
        }

        /// <summary>
        /// Once per frame before the updates: binds models into new document contexts and applies
        /// <see cref="UIDataModel.autoDirty"/>.
        /// </summary>
        internal static void Poll()
        {
            List<(Entity, string)> dead = null;
            foreach (KeyValuePair<(Entity, string), UIDataModel> kvp in models)
            {
                UIDataModel model = kvp.Value;
                if (!model.owner.IsValid())
                {
                    if (dead == null)
                    {
                        dead = new List<(Entity, string)>();
                    }
                    dead.Add(kvp.Key);
                    continue;
                }
                if (model.GetGeneration() != model.syncedGeneration)
                {
                    Sync(model);
                }
                if (model.autoDirty)
                {
                    DirtyChangedScalars(model);
                }
            }
            if (dead != null)
            {
                foreach ((Entity, string) key in dead)
                {
                    models.Remove(key);
                }
            }
        }

        /// <summary>
        /// Ends the bindings, events and transforms @p script made; called when it is destroyed.
        /// </summary>
        internal static void ForgetScript(ScriptComponent script)
        {
            foreach (UIDataModel model in models.Values)
            {
                var variables = new List<string>();
                foreach (KeyValuePair<string, Node> kvp in model.variables)
                {
                    if (ReferenceEquals(kvp.Value.script, script))
                    {
                        variables.Add(kvp.Key);
                    }
                }
                foreach (string variable in variables)
                {
                    model.ClearVariable(variable);
                }
                var events = new List<string>();
                foreach (KeyValuePair<string, UIDataModel.EventBinding> kvp in model.events)
                {
                    if (ReferenceEquals(kvp.Value.script, script))
                    {
                        events.Add(kvp.Key);
                    }
                }
                foreach (string eventName in events)
                {
                    model.events.Remove(eventName);
                }
            }
            var ended = new List<(Entity, string)>();
            foreach (KeyValuePair<(Entity, string), TransformBinding> kvp in transforms)
            {
                if (ReferenceEquals(kvp.Value.script, script))
                {
                    ended.Add(kvp.Key);
                }
            }
            foreach ((Entity, string) key in ended)
            {
                transforms.Remove(key);
            }
        }

        internal static void SetTransform(Entity owner, string name, Func<object, object[], object> transform, ScriptComponent script)
        {
            if (string.IsNullOrEmpty(name) || transform == null)
            {
                throw new ArgumentException("A transform needs a name and a function.");
            }
            transforms[(owner, name)] = new TransformBinding { transform = transform, script = script };
        }

        // ==== Called by native code ====

        internal static string internal_n2m_get(long id)
        {
            Node node = GetNode(id);
            if (node == null)
            {
                return ValueMissing.ToString();
            }
            try
            {
                return EncodeValue(Resolve(node));
            }
            catch (Exception ex)
            {
                WarnOnce(node, $"UI: reading data model variable '{PathOf(node)}' failed: {ex.Message}");
                return ValueMissing.ToString();
            }
        }

        internal static void internal_n2m_set(long id, string value)
        {
            Node node = GetNode(id);
            if (node == null)
            {
                return;
            }
            try
            {
                using (ScriptComponent.Run(RootOf(node).script))
                {
                    Write(node, Decode(value));
                }
            }
            catch (Exception ex)
            {
                WarnOnce(node, $"UI: writing data model variable '{PathOf(node)}' failed: {ex.Message}");
            }
        }

        internal static int internal_n2m_size(long id)
        {
            Node node = GetNode(id);
            try
            {
                return node != null && Resolve(node) is IList list ? list.Count : 0;
            }
            catch (Exception ex)
            {
                WarnOnce(node, $"UI: reading data model list '{PathOf(node)}' failed: {ex.Message}");
                return 0;
            }
        }

        internal static long internal_n2m_child(long id, string name, int index)
        {
            Node parent = GetNode(id);
            if (parent == null)
            {
                return 0;
            }
            try
            {
                Node child = index >= 0 ? GetIndexChild(parent, index) : GetNamedChild(parent, name);
                return child == null ? 0 : Encode(child);
            }
            catch (Exception ex)
            {
                WarnOnce(parent, $"UI: reading data model member '{PathOf(parent)}.{name}' failed: {ex.Message}");
                return 0;
            }
        }

        internal static void internal_n2m_event(Entity owner, string model, string name, string args, UIEventPayload payload,
                                                string eventType, string targetId, string currentId, string text)
        {
            if (!models.TryGetValue((owner, model), out UIDataModel dataModel) ||
                !dataModel.events.TryGetValue(name, out UIDataModel.EventBinding binding))
            {
                Warn($"UI: data model '{model}' has no event '{name}' (bind an object with a public method {name}, or call BindEvent).");
                return;
            }
            object[] arguments = DecodeList(args);
            UIEventManager.WithEvent(payload, eventType, targetId, currentId, text, owner, ev => InvokeEvent(binding, ev, arguments));
            // Handlers change what the views show; the model updates without a Dirty call.
            dataModel.DirtyAll();
        }

        internal static string internal_n2m_transform(Entity owner, string name, string args)
        {
            if (!transforms.TryGetValue((owner, name), out TransformBinding binding))
            {
                return ValueMissing.ToString();
            }
            object[] arguments = DecodeList(args);
            object value = arguments.Length > 0 ? arguments[0] : null;
            var rest = new object[Math.Max(0, arguments.Length - 1)];
            Array.Copy(arguments, 1, rest, 0, rest.Length);
            using (ScriptComponent.Run(binding.script))
            {
                try
                {
                    return EncodeValue(binding.transform(value, rest));
                }
                catch (Exception ex)
                {
                    Warn($"UI: transform '{name}' failed: {ex.Message}");
                    return ValueMissing.ToString();
                }
            }
        }

        internal static void internal_n2m_inline_event(Entity owner, string handler, UIEventPayload payload,
                                                       string eventType, string targetId, string currentId, string text)
        {
            if (!owner.IsValid())
            {
                return;
            }
            string methodName = ParseHandlerName(handler);
            foreach (ScriptComponent script in owner.GetComponents<ScriptComponent>())
            {
                MethodInfo method = FindInlineHandler(script.GetType(), methodName);
                if (method == null)
                {
                    continue;
                }
                var binding = new UIDataModel.EventBinding { method = method, target = script, script = script };
                UIEventManager.WithEvent(payload, eventType, targetId, currentId, text, owner,
                                         ev => InvokeEvent(binding, ev, System.Array.Empty<object>()));
                return;
            }
            Warn($"UI: no script on entity {owner} has a public method {methodName}() or {methodName}(event) for on{eventType}=\"{handler}\".");
        }

        // ==== Nodes ====

        private static Node Register(Node node)
        {
            nodes.Add(node);
            node.id = nodes.Count;
            return node;
        }

        private static Node GetNode(long id)
        {
            return id >= 1 && id <= nodes.Count ? nodes[(int)id - 1] : null;
        }

        private static Node RootOf(Node node)
        {
            while (node.parent != null)
            {
                node = node.parent;
            }
            return node;
        }

        private static string PathOf(Node node)
        {
            if (node == null)
            {
                return "";
            }
            if (node.parent == null)
            {
                return node.name;
            }
            return node.index >= 0 ? $"{PathOf(node.parent)}[{node.index}]" : $"{PathOf(node.parent)}.{node.name}";
        }

        /// <summary>
        /// The node's value now: read through its parents from the bound getter.
        /// </summary>
        private static object Resolve(Node node)
        {
            if (node.parent == null)
            {
                return node.rootGetter?.Invoke();
            }
            object parent = Resolve(node.parent);
            if (parent == null)
            {
                return null;
            }
            if (node.index >= 0)
            {
                return parent is IList list && node.index < list.Count ? list[node.index] : null;
            }
            return GetAccessor(parent.GetType(), node.name)?.Get(parent);
        }

        /// <summary>
        /// Writes a value, and a struct holding it back into its own parent (structs are copies).
        /// </summary>
        private static void Write(Node node, object value)
        {
            if (node.parent == null)
            {
                node.rootSetter?.Invoke(value);
                return;
            }
            object parent = Resolve(node.parent);
            if (parent == null)
            {
                return;
            }
            if (node.index >= 0)
            {
                if (parent is IList list && node.index < list.Count)
                {
                    list[node.index] = ConvertTo(value, ElementTypeOf(parent.GetType()));
                }
            }
            else
            {
                MemberAccessor accessor = GetAccessor(parent.GetType(), node.name);
                if (accessor == null || !accessor.canWrite)
                {
                    return;
                }
                accessor.Set(parent, value);
            }
            if (parent.GetType().IsValueType)
            {
                Write(node.parent, parent);
            }
        }

        private static Node GetNamedChild(Node parent, string name)
        {
            if (parent.namedChildren != null && parent.namedChildren.TryGetValue(name, out Node existing))
            {
                return existing;
            }
            Type parentType = EffectiveType(parent);
            MemberAccessor accessor = parentType != null ? GetAccessor(parentType, name) : null;
            if (accessor == null)
            {
                return null;
            }
            if (parent.namedChildren == null)
            {
                parent.namedChildren = new Dictionary<string, Node>();
            }
            Node child = Register(new Node { model = parent.model, parent = parent, name = name, declaredType = accessor.type });
            parent.namedChildren[name] = child;
            return child;
        }

        private static Node GetIndexChild(Node parent, int index)
        {
            if (parent.indexChildren == null)
            {
                parent.indexChildren = new List<Node>();
            }
            Type elementType = ElementTypeOf(EffectiveType(parent));
            while (parent.indexChildren.Count <= index)
            {
                int childIndex = parent.indexChildren.Count;
                parent.indexChildren.Add(Register(new Node
                {
                    model = parent.model,
                    parent = parent,
                    name = parent.name,
                    index = childIndex,
                    declaredType = elementType
                }));
            }
            return parent.indexChildren[index];
        }

        /// <summary>
        /// The declared type, or the value's type when the declaration says only object or an interface.
        /// </summary>
        private static Type EffectiveType(Node node)
        {
            Type type = node.declaredType;
            if (type == null || type == typeof(object) || (type.IsInterface && !typeof(IList).IsAssignableFrom(type)))
            {
                return Resolve(node)?.GetType() ?? type;
            }
            return type;
        }

        /// <summary>
        /// The node id with its kind in the low bits, as native code expects it.
        /// </summary>
        private static long Encode(Node node)
        {
            NodeKind kind = KindOf(EffectiveType(node));
            return kind == NodeKind.Missing ? 0 : (node.id << KindBits) | (long)kind;
        }

        private static NodeKind KindOf(Type type)
        {
            if (type == null || type == typeof(object))
            {
                return NodeKind.Scalar;
            }
            type = Nullable.GetUnderlyingType(type) ?? type;
            if (type.IsPrimitive || type.IsEnum || type == typeof(string) || type == typeof(decimal) || type == typeof(Color))
            {
                return NodeKind.Scalar;
            }
            return typeof(IList).IsAssignableFrom(type) ? NodeKind.Array : NodeKind.Struct;
        }

        private static Type ElementTypeOf(Type listType)
        {
            if (listType == null)
            {
                return typeof(object);
            }
            if (listType.IsArray)
            {
                return listType.GetElementType();
            }
            foreach (Type contract in listType.GetInterfaces())
            {
                if (contract.IsGenericType && contract.GetGenericTypeDefinition() == typeof(IList<>))
                {
                    return contract.GetGenericArguments()[0];
                }
            }
            return typeof(object);
        }

        private static void DirtyChangedScalars(UIDataModel model)
        {
            foreach (KeyValuePair<string, Node> kvp in model.variables)
            {
                Node root = kvp.Value;
                if (root.rootGetter == null || KindOf(EffectiveType(root)) != NodeKind.Scalar)
                {
                    continue;
                }
                string value = internal_n2m_get(root.id);
                if (!model.lastScalarValues.TryGetValue(kvp.Key, out string last) || last != value)
                {
                    model.lastScalarValues[kvp.Key] = value;
                    model.Dirty(kvp.Key);
                }
            }
        }

        // ==== Members and methods ====

        /// <summary>
        /// The members that become variables when an object is bound: the object's own, not those it
        /// inherits from engine classes (a script's owner, transform...).
        /// </summary>
        internal static IEnumerable<MemberAccessor> GetBindableMembers(Type type)
        {
            Assembly engine = typeof(UIDataModels).Assembly;
            bool isEngineType = type.Assembly == engine;
            foreach (MemberAccessor member in GetMemberTable(type).Values)
            {
                Type declaring = member.field != null ? member.field.DeclaringType : member.property.DeclaringType;
                if (isEngineType || declaring.Assembly != engine)
                {
                    yield return member;
                }
            }
        }

        private static MemberAccessor GetAccessor(Type type, string name)
        {
            return name != null && GetMemberTable(type).TryGetValue(name, out MemberAccessor accessor) ? accessor : null;
        }

        private static Dictionary<string, MemberAccessor> GetMemberTable(Type type)
        {
            if (membersByType.TryGetValue(type, out Dictionary<string, MemberAccessor> table))
            {
                return table;
            }
            table = new Dictionary<string, MemberAccessor>();
            foreach (FieldInfo field in type.GetFields(BindingFlags.Public | BindingFlags.Instance))
            {
                table[field.Name] = new MemberAccessor
                {
                    name = field.Name,
                    type = field.FieldType,
                    canWrite = !field.IsInitOnly && !field.IsLiteral,
                    field = field
                };
            }
            foreach (PropertyInfo property in type.GetProperties(BindingFlags.Public | BindingFlags.Instance))
            {
                if (property.GetIndexParameters().Length > 0 || property.GetGetMethod() == null || table.ContainsKey(property.Name))
                {
                    continue;
                }
                table[property.Name] = new MemberAccessor
                {
                    name = property.Name,
                    type = property.PropertyType,
                    canWrite = property.GetSetMethod() != null,
                    property = property
                };
            }
            membersByType[type] = table;
            return table;
        }

        /// <summary>
        /// The public methods that become events when an object is bound: the object's own, not
        /// those it inherits from engine classes (a script's GetComponent, StartCoroutine...).
        /// </summary>
        internal static IEnumerable<MethodInfo> GetEventMethods(Type type)
        {
            Assembly engine = typeof(UIDataModels).Assembly;
            bool isEngineType = type.Assembly == engine;
            foreach (MethodInfo method in type.GetMethods(BindingFlags.Public | BindingFlags.Instance))
            {
                if (method.IsSpecialName || method.IsGenericMethodDefinition || method.DeclaringType == typeof(object))
                {
                    continue;
                }
                if (!isEngineType && method.DeclaringType.Assembly == engine)
                {
                    continue;
                }
                yield return method;
            }
        }

        private static string ParseHandlerName(string handler)
        {
            string name = (handler ?? "").Trim();
            if (name.EndsWith("()", StringComparison.Ordinal))
            {
                name = name.Substring(0, name.Length - 2).TrimEnd();
            }
            return name;
        }

        /// <summary>
        /// A public method taking nothing, or only an event.
        /// </summary>
        private static MethodInfo FindInlineHandler(Type type, string name)
        {
            if (inlineHandlers.TryGetValue((type, name), out MethodInfo cached))
            {
                return cached;
            }
            MethodInfo found = null;
            foreach (MethodInfo method in type.GetMethods(BindingFlags.Public | BindingFlags.Instance))
            {
                if (method.Name != name || method.IsGenericMethodDefinition)
                {
                    continue;
                }
                ParameterInfo[] parameters = method.GetParameters();
                if (parameters.Length == 0 ||
                    (parameters.Length == 1 && typeof(UIEventBase).IsAssignableFrom(parameters[0].ParameterType)))
                {
                    found = method;
                    break;
                }
            }
            inlineHandlers[(type, name)] = found;
            return found;
        }

        private static void InvokeEvent(UIDataModel.EventBinding binding, UIEventBase ev, object[] arguments)
        {
            using (ScriptComponent.Run(binding.script))
            {
                try
                {
                    if (binding.handler != null)
                    {
                        binding.handler(ev, arguments);
                    }
                    else
                    {
                        binding.method.Invoke(binding.target, BuildArguments(binding.method, ev, arguments));
                    }
                }
                catch (TargetInvocationException ex)
                {
                    Log.Error($"UI event handler {binding.method?.Name}: {ex.InnerException ?? ex}");
                }
                catch (Exception ex)
                {
                    Log.Error($"UI event handler {binding.method?.Name}: {ex}");
                }
            }
        }

        /// <summary>
        /// Arguments for a bound method: the event for a first parameter of an event class, then
        /// the expression's arguments converted to the parameter types, then defaults.
        /// </summary>
        private static object[] BuildArguments(MethodInfo method, UIEventBase ev, object[] arguments)
        {
            ParameterInfo[] parameters = method.GetParameters();
            var values = new object[parameters.Length];
            int next = 0;
            for (int i = 0; i < parameters.Length; i++)
            {
                Type type = parameters[i].ParameterType;
                if (i == 0 && typeof(UIEventBase).IsAssignableFrom(type))
                {
                    values[i] = type.IsInstanceOfType(ev) ? ev : null;
                }
                else if (next < arguments.Length)
                {
                    values[i] = ConvertTo(arguments[next++], type);
                }
                else if (parameters[i].HasDefaultValue)
                {
                    values[i] = parameters[i].DefaultValue;
                }
                else
                {
                    values[i] = type.IsValueType ? Activator.CreateInstance(type) : null;
                }
            }
            return values;
        }

        // ==== Values ====

        private static string EncodeValue(object value)
        {
            switch (value)
            {
                case null:
                    return ValueMissing.ToString();
                case bool flag:
                    return flag ? "b1" : "b0";
                case string text:
                    return ValueString + text;
                case Enum enumValue:
                    return ValueString + enumValue.ToString();
                case float _:
                case double _:
                case decimal _:
                    return ValueNumber + Convert.ToDouble(value, CultureInfo.InvariantCulture).ToString("R", CultureInfo.InvariantCulture);
                case ulong unsigned:
                    return ValueInteger + unsigned.ToString(CultureInfo.InvariantCulture);
                case Color color:
                    return ValueString + FormatColor(color);
                default:
                    if (value.GetType().IsPrimitive)
                    {
                        return ValueInteger + Convert.ToInt64(value, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture);
                    }
                    return ValueString + value.ToString();
            }
        }

        /// <summary>
        /// An RCSS colour: rgba() with every channel 0-255.
        /// </summary>
        private static string FormatColor(Color color)
        {
            int Channel(float value) => (int)Math.Round(Math.Max(0f, Math.Min(1f, value)) * 255f);
            return $"rgba({Channel(color.r)},{Channel(color.g)},{Channel(color.b)},{Channel(color.a)})";
        }

        private static object Decode(string text)
        {
            if (string.IsNullOrEmpty(text))
            {
                return null;
            }
            string value = text.Substring(1);
            switch (text[0])
            {
                case ValueBool:
                    return value == "1";
                case ValueInteger:
                    return long.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out long integer) ? integer : 0L;
                case ValueNumber:
                    return double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out double number) ? number : 0.0;
                case ValueString:
                    return value;
                default:
                    return null;
            }
        }

        private static object[] DecodeList(string text)
        {
            if (string.IsNullOrEmpty(text))
            {
                return System.Array.Empty<object>();
            }
            string[] parts = text.Split(ValueSeparator);
            var values = new object[parts.Length];
            for (int i = 0; i < parts.Length; i++)
            {
                values[i] = Decode(parts[i]);
            }
            return values;
        }

        /// <summary>
        /// Converts a decoded value (bool, long, double, string) to a member or parameter type.
        /// </summary>
        internal static object ConvertTo(object value, Type type)
        {
            if (type == null || type == typeof(object))
            {
                return value;
            }
            Type target = Nullable.GetUnderlyingType(type) ?? type;
            if (value == null)
            {
                return type.IsValueType && Nullable.GetUnderlyingType(type) == null ? Activator.CreateInstance(type) : null;
            }
            if (target.IsInstanceOfType(value))
            {
                return value;
            }
            if (target.IsEnum)
            {
                return value is string name
                    ? Enum.Parse(target, name, true)
                    : Enum.ToObject(target, Convert.ToInt64(value, CultureInfo.InvariantCulture));
            }
            if (target == typeof(bool) && value is string flag)
            {
                return flag == "1" || flag.Equals("true", StringComparison.OrdinalIgnoreCase);
            }
            return Convert.ChangeType(value, target, CultureInfo.InvariantCulture);
        }

        // ==== Diagnostics ====

        private static void Warn(string message)
        {
            if (warnings.Add(message))
            {
                Log.Warning(message);
            }
        }

        private static void WarnOnce(Node node, string message)
        {
            if (node != null && !node.warned)
            {
                node.warned = true;
                Log.Warning(message);
            }
        }
    }
}
