using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// A data model of a UI document: the values and events its markup binds to. The document
    /// declares it with <c>data-model="name"</c> on an element; inside that element
    /// <c>{{ credit }}</c>, <c>data-if</c>, <c>data-for</c>, <c>data-class-*</c>, <c>data-style-*</c>,
    /// <c>data-attr-*</c>, <c>data-value</c>, <c>data-checked</c> and <c>data-event-*</c> read the model.
    /// </summary>
    /// <remarks>
    /// Get it with <see cref="UIDocumentComponent.GetModel"/>; it exists before and across document
    /// (re)loads. Changes are pushed: after changing a bound value call <see cref="Dirty"/> (or
    /// <see cref="DirtyAll"/>); views using it update at the next frame. Controls bound with
    /// data-value / data-checked write back and update by themselves. Values: bool, integers and
    /// floating point numbers, string, enums (as their name), <see cref="Color"/> (as an RCSS
    /// colour); classes and structs become nested variables (<c>{{ row.name }}</c>), lists and
    /// arrays become arrays (<c>data-for="row : rows"</c>, <c>rows.size</c>). Bindings end with the
    /// script that made them.
    /// </remarks>
    /// <example><code>
    /// // &lt;body data-model="hud"&gt; {{ credit | money }} &lt;button data-event-click="Spin()"&gt;
    /// var model = ui.GetModel("hud");
    /// model.Bind(hud);                                  // HudModel's public fields, properties, methods
    /// model.AddTransform&lt;long&gt;("money", v =&gt; $"{v:N0}");
    /// hud.credit += win;
    /// model.Dirty(nameof(HudModel.credit));
    /// </code></example>
    public sealed class UIDataModel
    {
        internal sealed class EventBinding
        {
            internal MethodInfo method;
            internal object target;
            internal Action<UIEventBase, object[]> handler;
            internal ScriptComponent script;
            internal bool isFromSource;
        }

        internal readonly Dictionary<string, UIDataModels.Node> variables = new Dictionary<string, UIDataModels.Node>();
        internal readonly Dictionary<string, EventBinding> events = new Dictionary<string, EventBinding>();
        internal readonly Dictionary<string, string> lastScalarValues = new Dictionary<string, string>();
        internal ulong syncedGeneration;
        internal bool warnedMissing;
        private readonly HashSet<string> sourceVariables = new HashSet<string>();

        internal UIDataModel(Entity owner, string name)
        {
            this.owner = owner;
            this.name = name;
        }

        /// <summary>
        /// The model's name, as in the document's data-model attribute.
        /// </summary>
        public string name { get; }

        /// <summary>
        /// The entity whose UI document has the model.
        /// </summary>
        public Entity owner { get; }

        /// <summary>
        /// Compares the top-level scalar variables each frame and dirties the changed ones, so no
        /// <see cref="Dirty"/> calls are needed. Costs a read of every such variable every frame:
        /// for prototypes and small models.
        /// </summary>
        public bool autoDirty { get; set; }

        /// <summary>
        /// Binds an object: its public fields and properties become variables (writable ones accept
        /// data-value writes), its public methods become events (<c>data-event-click="Spin()"</c>;
        /// a first parameter of an event class receives the event, the others the expression's
        /// arguments; the model is marked dirty after it runs). Binding another object replaces
        /// this one; names it lacks become empty.
        /// </summary>
        /// <param name="source">The object; it is read at every update of a view that uses it.</param>
        public void Bind(object source)
        {
            if (source == null)
            {
                throw new ArgumentNullException(nameof(source));
            }
            ScriptComponent script = source as ScriptComponent ?? ScriptComponent.running;
            var bound = new HashSet<string>();
            foreach (UIDataModels.MemberAccessor member in UIDataModels.GetBindableMembers(source.GetType()))
            {
                UIDataModels.MemberAccessor accessor = member;
                SetVariable(member.name, member.type, () => accessor.Get(source),
                            accessor.canWrite ? (Action<object>)(value => accessor.Set(source, value)) : null, script);
                bound.Add(member.name);
            }
            foreach (string stale in sourceVariables)
            {
                if (!bound.Contains(stale))
                {
                    ClearVariable(stale);
                }
            }
            sourceVariables.Clear();
            sourceVariables.UnionWith(bound);
            BindSourceMethods(source, script);
            UIDataModels.Sync(this);
            DirtyAll();
        }

        /// <summary>
        /// Binds one variable to a getter, and to a setter when data-value / data-checked controls
        /// may write it.
        /// </summary>
        /// <example><code>
        /// model.Bind("credit", () =&gt; credit);
        /// model.Bind("volume", () =&gt; volume, v =&gt; volume = v);
        /// </code></example>
        public void Bind<T>(string variable, Func<T> getter, Action<T> setter = null)
        {
            if (string.IsNullOrEmpty(variable) || getter == null)
            {
                throw new ArgumentException("A variable needs a name and a getter.");
            }
            Action<object> write = null;
            if (setter != null)
            {
                write = value => setter((T)UIDataModels.ConvertTo(value, typeof(T)));
            }
            SetVariable(variable, typeof(T), () => getter(), write, getter.Target as ScriptComponent ?? ScriptComponent.running);
            sourceVariables.Remove(variable);
            UIDataModels.Sync(this);
            Dirty(variable);
        }

        /// <summary>
        /// Binds an event for <c>data-event-*="name(args)"</c>.
        /// </summary>
        /// <param name="eventName">The name used in the expression.</param>
        /// <param name="handler">Receives the UI event and the expression's arguments (integers
        /// as long, numbers as double, strings, bools).</param>
        public void BindEvent(string eventName, Action<UIEventBase, object[]> handler)
        {
            if (string.IsNullOrEmpty(eventName) || handler == null)
            {
                throw new ArgumentException("An event needs a name and a handler.");
            }
            events[eventName] = new EventBinding
            {
                handler = handler,
                script = handler.Target as ScriptComponent ?? ScriptComponent.running
            };
            UIDataModels.Sync(this);
        }

        /// <summary>
        /// Adds a transform for <c>{{ value | name }}</c> or <c>{{ value | name(arg) }}</c>. It serves
        /// every model of the document. Integers arrive as long and numbers as double; use
        /// <see cref="AddTransform{T}"/> to receive a converted value.
        /// </summary>
        /// <param name="transformName">The name used after the pipe.</param>
        /// <param name="transform">Gets the value and the arguments; returns the new value.</param>
        public void AddTransform(string transformName, Func<object, object[], object> transform)
        {
            UIDataModels.SetTransform(owner, transformName, transform,
                                      transform?.Target as ScriptComponent ?? ScriptComponent.running);
            UIDataModels.Sync(this);
        }

        /// <summary>
        /// Adds a transform receiving the value converted to <typeparamref name="T"/>, e.g.
        /// <c>AddTransform&lt;long&gt;("money", v =&gt; $"{v:N0} $")</c>.
        /// </summary>
        public void AddTransform<T>(string transformName, Func<T, object> transform)
        {
            if (transform == null)
            {
                throw new ArgumentNullException(nameof(transform));
            }
            UIDataModels.SetTransform(owner, transformName,
                                      (value, args) => transform((T)UIDataModels.ConvertTo(value, typeof(T))),
                                      transform.Target as ScriptComponent ?? ScriptComponent.running);
            UIDataModels.Sync(this);
        }

        /// <summary>
        /// Marks a top-level variable changed: the views that use it update at the next frame.
        /// </summary>
        public void Dirty(string variable)
        {
            if (!string.IsNullOrEmpty(variable))
            {
                internal_m2n_ui_model_dirty(owner, name, variable);
            }
        }

        /// <summary>
        /// Marks every variable changed.
        /// </summary>
        public void DirtyAll()
        {
            internal_m2n_ui_model_dirty(owner, name, "");
        }

        private void SetVariable(string variable, Type type, Func<object> getter, Action<object> setter, ScriptComponent script)
        {
            if (!variables.TryGetValue(variable, out UIDataModels.Node root))
            {
                root = UIDataModels.CreateRoot(this, variable, type);
                variables[variable] = root;
            }
            root.declaredType = type;
            root.rootGetter = getter;
            root.rootSetter = setter;
            root.script = script;
        }

        internal void ClearVariable(string variable)
        {
            if (variables.TryGetValue(variable, out UIDataModels.Node root))
            {
                root.rootGetter = null;
                root.rootSetter = null;
                root.script = null;
                Dirty(variable);
            }
        }

        private void BindSourceMethods(object source, ScriptComponent script)
        {
            var stale = new List<string>();
            foreach (KeyValuePair<string, EventBinding> kvp in events)
            {
                if (kvp.Value.isFromSource)
                {
                    stale.Add(kvp.Key);
                }
            }
            foreach (string eventName in stale)
            {
                events.Remove(eventName);
            }
            foreach (MethodInfo method in UIDataModels.GetEventMethods(source.GetType()))
            {
                events[method.Name] = new EventBinding
                {
                    method = method,
                    target = source,
                    script = script,
                    isFromSource = true
                };
            }
        }

        internal bool BindNative(string variable, long encodedNode)
        {
            return internal_m2n_ui_model_bind_variable(owner, name, variable, encodedNode);
        }

        internal bool BindNativeEvent(string eventName)
        {
            return internal_m2n_ui_model_bind_event(owner, name, eventName);
        }

        internal bool AddNativeTransform(string transformName)
        {
            return internal_m2n_ui_model_add_transform(owner, name, transformName);
        }

        internal ulong GetGeneration()
        {
            return internal_m2n_ui_model_get_generation(owner);
        }

        internal bool Exists()
        {
            return internal_m2n_ui_model_exists(owner, name);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern ulong internal_m2n_ui_model_get_generation(Entity owner);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_model_exists(Entity owner, string model);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_model_bind_variable(Entity owner, string model, string name, long encodedNode);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_model_bind_event(Entity owner, string model, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_model_add_transform(Entity owner, string model, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_model_dirty(Entity owner, string model, string name);
    }
}
