using System;
using System.Collections.Generic;
using System.Reflection;

namespace Unravel.Core
{
    /// <summary>
    /// A marker on an animation clip's timeline that playback crossed this frame.
    ///
    /// Events are authored per clip (clip inspector, Events tab) and delivered to the scripts on the
    /// entity that has the <see cref="AnimationComponent"/> - or, when that entity has no scripts, on
    /// its nearest ancestor with scripts. Every script receives
    /// <see cref="ScriptComponent.OnAnimationEvent"/>, and a public method named exactly like the
    /// event is called as well, taking nothing, an <see cref="AnimationEvent"/>, or the event's
    /// string, float or int parameter.
    /// </summary>
    public struct AnimationEvent
    {
        /// <summary>Event name as authored on the clip.</summary>
        public string name;

        /// <summary>String parameter authored on the event.</summary>
        public string stringParameter;

        /// <summary>Float parameter authored on the event.</summary>
        public float floatParameter;

        /// <summary>Int parameter authored on the event.</summary>
        public int intParameter;

        /// <summary>Animation layer the clip plays on (0 = base layer).</summary>
        public int layer;

        /// <summary>
        /// The clip's share of the final pose when the event fired: layer weight times crossfade
        /// weight. At least the event's authored minimum weight.
        /// </summary>
        public float weight;

        /// <summary>The clip that carries the event.</summary>
        public AnimationClip clip;

        /// <summary>Name of the clip that carries the event.</summary>
        public string clipName;
    }

    /// <summary>
    /// Resolves the method a script declares for an event name, cached per script type.
    /// </summary>
    [AutoStaticsCleanup]
    internal static class AnimationEventHandlers
    {
        internal enum HandlerKind
        {
            None,
            NoArguments,
            Event,
            String,
            Float,
            Int,
        }

        internal readonly struct Handler
        {
            internal readonly MethodInfo method;
            internal readonly HandlerKind kind;

            internal Handler(MethodInfo method, HandlerKind kind)
            {
                this.method = method;
                this.kind = kind;
            }
        }

        private static readonly Dictionary<(Type, string), Handler> handlers = new Dictionary<(Type, string), Handler>();

        private static void OnStaticsCleanup()
        {
            handlers.Clear();
        }

        internal static Handler Find(Type type, string name)
        {
            if (string.IsNullOrEmpty(name))
            {
                return default;
            }
            if (handlers.TryGetValue((type, name), out Handler cached))
            {
                return cached;
            }

            Handler best = default;
            foreach (MethodInfo method in type.GetMethods(BindingFlags.Public | BindingFlags.Instance))
            {
                // Methods of ScriptComponent and its bases, and their overrides (OnUpdate,
                // OnAnimationEvent...), are never animation event handlers.
                if (method.Name != name || method.IsGenericMethodDefinition ||
                    method.GetBaseDefinition().DeclaringType.IsAssignableFrom(typeof(ScriptComponent)))
                {
                    continue;
                }
                HandlerKind kind = Classify(method.GetParameters());
                if (kind != HandlerKind.None && (best.kind == HandlerKind.None || Rank(kind) < Rank(best.kind)))
                {
                    best = new Handler(method, kind);
                }
            }
            handlers[(type, name)] = best;
            return best;
        }

        internal static void Invoke(ScriptComponent script, Handler handler, AnimationEvent e)
        {
            object[] arguments;
            switch (handler.kind)
            {
                case HandlerKind.NoArguments: arguments = Array.Empty<object>(); break;
                case HandlerKind.Event: arguments = new object[] { e }; break;
                case HandlerKind.String: arguments = new object[] { e.stringParameter }; break;
                case HandlerKind.Float: arguments = new object[] { e.floatParameter }; break;
                case HandlerKind.Int: arguments = new object[] { e.intParameter }; break;
                default: return;
            }
            try
            {
                handler.method.Invoke(script, arguments);
            }
            catch (TargetInvocationException ex)
            {
                Exception inner = ex.InnerException ?? ex;
                Log.Error($"Animation event '{e.name}' handler {script.GetType().Name}.{handler.method.Name} threw {inner.GetType().Name}: {inner.Message}\n{inner.StackTrace}");
            }
        }

        private static HandlerKind Classify(ParameterInfo[] parameters)
        {
            if (parameters.Length == 0)
            {
                return HandlerKind.NoArguments;
            }
            if (parameters.Length != 1)
            {
                return HandlerKind.None;
            }
            Type type = parameters[0].ParameterType;
            if (type == typeof(AnimationEvent)) return HandlerKind.Event;
            if (type == typeof(string)) return HandlerKind.String;
            if (type == typeof(float)) return HandlerKind.Float;
            if (type == typeof(int)) return HandlerKind.Int;
            return HandlerKind.None;
        }

        private static int Rank(HandlerKind kind)
        {
            switch (kind)
            {
                case HandlerKind.Event: return 0;
                case HandlerKind.NoArguments: return 1;
                case HandlerKind.String: return 2;
                case HandlerKind.Float: return 3;
                case HandlerKind.Int: return 4;
                default: return 5;
            }
        }
    }
}
