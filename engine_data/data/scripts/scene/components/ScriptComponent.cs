using System;
using System.Collections;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Reflection;
using System.Text;

namespace Unravel.Core
{

    /// <summary>
    /// Records the source file path of a script class for editor and tooling use.
    /// </summary>
    [AttributeUsage(AttributeTargets.Class, Inherited = false, AllowMultiple = false)]
    public sealed class ScriptSourceFileAttribute : Attribute
    {
        /// <summary>
        /// Absolute or compiler-provided path to the script source file.
        /// </summary>
        public string Path { get; }

        /// <summary>
        /// Creates the attribute, capturing the caller source path by default.
        /// </summary>
        /// <param name="path">Source file path; filled by the compiler when omitted.</param>
        public ScriptSourceFileAttribute([CallerFilePath] string path = "") => Path = path;
    }
    /// <summary>
    /// Represents a script component that provides lifecycle hooks and event handling for an entity.
    /// </summary>
    public abstract class ScriptComponent : Component
    {
        /// <summary>Levels of nested objects and entries of a list a tool sees in an invoked method's result.</summary>
        private const int InvokeResultDepth = 2;
        private const int InvokeResultItems = 32;

        private bool m_started = false;

        /// <summary>
        /// The script whose code runs now (a lifecycle call, an update, a coroutine step, a UI
        /// callback), or null. What it subscribes to is released when it is destroyed.
        /// </summary>
        internal static ScriptComponent running { get; private set; }

        /// <summary>
        /// Makes @p script the running script until the returned scope is disposed.
        /// </summary>
        internal static RunningScope Run(ScriptComponent script)
        {
            return new RunningScope(script);
        }

        /// <summary>
        /// Restores the previously running script when disposed; scopes nest.
        /// </summary>
        internal readonly struct RunningScope : IDisposable
        {
            private readonly ScriptComponent previous;

            internal RunningScope(ScriptComponent script)
            {
                previous = running;
                running = script;
            }

            public void Dispose()
            {
                running = previous;
            }
        }
        protected string SourceFilePath { get; set; }
        protected ScriptComponent([CallerFilePath] string file = "")
        {
            SourceFilePath = file;
        }        

        /// <summary>
        /// Called when the script is created. Override this method to initialize resources or data.
        /// OnCreate is called when the script is first loaded, or when an object it is attached to is instantiated
        /// It only gets called once on each script, and only after other objects are initialised.
        /// This means that it is safe to create references to other game objects and components in OnCreate.
        /// </summary>
        public virtual void OnCreate()
        {
        }

        /// <summary>
        /// Called when the script or entity is enabled. Override this method to initialize resources or data.
        /// </summary>
        public virtual void OnEnable()
        {
        }

        /// <summary>
        /// Called when the script or entity is disabled. Override this method to clean up resources or data.
        /// </summary>
        public virtual void OnDisable()
        {
        }

        /// <summary>
        /// Called when the script starts execution. Override this method to set up logic at the start.
        /// Start is called once, before any Update methods and after OnCreate.
        /// It works in much the same way as OnCreate, with a few key differences.
        /// Unlike OnCreate, Start will not be called if the script is disabled.
        /// </summary>
        public virtual void OnStart()
        {
        }

        /// <summary>
        /// Called when the script is destroyed. Override this method to clean up resources or data.
        /// </summary>
        public virtual void OnDestroy()
        {
        }

        /// <summary>
        /// Called when another entity enters a sensor attached to this entity.
        /// </summary>
        /// <param name="collision">Details of the collision, including the other entity and contact points.</param>
        public virtual void OnSensorEnter(Collision collision)
        {
        }

        /// <summary>
        /// Called when another entity exits a sensor attached to this entity.
        /// </summary>
        /// <param name="collision">Details of the collision, including the other entity and contact points.</param>
        public virtual void OnSensorExit(Collision collision)
        {
        }

        /// <summary>
        /// Called when this entity begins a collision with another entity.
        /// </summary>
        /// <param name="collision">Details of the collision, including the other entity and contact points.</param>
        public virtual void OnCollisionEnter(Collision collision)
        {
        }

        /// <summary>
        /// Called when this entity ends a collision with another entity.
        /// </summary>
        /// <param name="collision">Details of the collision, including the other entity and contact points.</param>
        public virtual void OnCollisionExit(Collision collision)
        {
        }

        /// <summary>
        /// Called on every frame update. Override this method to implement frame-based logic.
        /// </summary>
        public virtual void OnUpdate()
        {
        }

        /// <summary>
        /// A framerate-idependent interval update when physics calculations are performed. Override this method to implement frame-based logic.
        /// </summary>
        public virtual void OnFixedUpdate()
        {
        }

        /// <summary>
        /// Called on every frame update after other updates are finished. Override this method to implement frame-based logic.
        /// </summary>
        public virtual void OnLateUpdate()
        {
        }

        /// <summary>
        /// Starts a coroutine owned by this component: it runs up to its first <c>yield return</c>
        /// now, then one step per frame after the scripts' updates. It stops when this component is
        /// disabled or destroyed. See <see cref="Coroutine"/> for what a coroutine can yield.
        /// </summary>
        /// <example><code>
        /// IEnumerator Blink(ModelComponent model)
        /// {
        ///     for (int i = 0; i &lt; 6; i++)
        ///     {
        ///         model.enabled = !model.enabled;
        ///         yield return new WaitForSeconds(0.2f);
        ///     }
        /// }
        /// StartCoroutine(Blink(owner.GetComponent&lt;ModelComponent&gt;()));
        /// </code></example>
        /// <param name="routine">The iterator to run.</param>
        /// <returns>The coroutine, to stop it or to wait for it from another coroutine.</returns>
        public Coroutine StartCoroutine(IEnumerator routine)
        {
            return CoroutineScheduler.Start(this, routine);
        }

        /// <summary>
        /// Stops a coroutine started with <see cref="StartCoroutine"/>.
        /// </summary>
        /// <param name="coroutine">The coroutine to stop; null is ignored.</param>
        public void StopCoroutine(Coroutine coroutine)
        {
            coroutine?.Stop();
        }

        /// <summary>
        /// Stops every coroutine this component started.
        /// </summary>
        public void StopAllCoroutines()
        {
            CoroutineScheduler.StopAll(this);
        }

        /// <summary>
        /// Calls an action after a delay of game time. Runs as a coroutine, so it is cancelled
        /// with <see cref="StopCoroutine"/> or when this component is disabled or destroyed.
        /// </summary>
        /// <param name="delay">Seconds of game time before the call; 0 calls at the next coroutine step, after the scripts' updates.</param>
        /// <param name="action">The action to call.</param>
        public Coroutine Invoke(float delay, Action action)
        {
            if (action == null)
            {
                throw new ArgumentNullException(nameof(action));
            }
            return StartCoroutine(InvokeRoutine(delay, action));
        }

        /// <summary>
        /// Calls an action after a delay, then again every interval of game time until stopped.
        /// </summary>
        /// <param name="delay">Seconds of game time before the first call.</param>
        /// <param name="interval">Seconds of game time between calls; 0 or less calls every frame.</param>
        /// <param name="action">The action to call.</param>
        public Coroutine InvokeRepeating(float delay, float interval, Action action)
        {
            if (action == null)
            {
                throw new ArgumentNullException(nameof(action));
            }
            return StartCoroutine(InvokeRepeatingRoutine(delay, interval, action));
        }

        private static IEnumerator InvokeRoutine(float delay, Action action)
        {
            yield return new WaitForSeconds(delay);
            action();
        }

        private static IEnumerator InvokeRepeatingRoutine(float delay, float interval, Action action)
        {
            yield return new WaitForSeconds(delay);
            var wait = interval > 0.0f ? new WaitForSeconds(interval) : null;
            while (true)
            {
                action();
                yield return wait;
            }
        }

        /// <summary>
        /// Internal method invoked when the script is created. Calls <see cref="OnCreate"/> and subscribes <see cref="OnUpdate"/> to the update system.
        /// </summary>
        private void internal_n2m_on_create()
        {
            using (Run(this))
            {
                OnCreate();
            }
        }

        /// <summary>
        /// Internal method invoked when the script is enabled. Calls <see cref="OnEnable"/>.
        /// </summary>
        private void internal_n2m_on_enable()
        {
            if(m_started)
            {
                SystemManager.ScriptManager.Add(this);
            }
            using (Run(this))
            {
                OnEnable();
            }
        }

        /// <summary>
        /// Internal method invoked when the script is disabled. Calls <see cref="OnDisable"/>.
        /// </summary>
        private void internal_n2m_on_disable()
        {
            SystemManager.ScriptManager.Remove(this);
            CoroutineScheduler.StopAll(this);
            using (Run(this))
            {
                OnDisable();
            }
        }

        /// <summary>
        /// Internal method invoked when the script starts. Calls <see cref="OnStart"/>.
        /// </summary>
        private void internal_n2m_on_start()
        {
            m_started = true;
            SystemManager.ScriptManager.Add(this);
            using (Run(this))
            {
                OnStart();
            }
        }

        /// <summary>
        /// Internal method invoked when the script is destroyed. Calls <see cref="OnDestroy"/> and unsubscribes <see cref="OnUpdate"/> from the update system.
        /// Also ends the UI event subscriptions this script made, so callbacks cannot pin destroyed scripts.
        /// </summary>
        private void internal_n2m_on_destroy()
        {
            SystemManager.ScriptManager.Remove(this);
            CoroutineScheduler.StopAll(this);
            UIEventManager.UnsubscribeAllForScript(this);
            Screen.ForgetScript(this);
            UIDataModels.ForgetScript(this);
            using (Run(this))
            {
                OnDestroy();
            }
        }

        /// <summary>
        /// Internal method invoked when another entity enters a sensor attached to this entity. Calls <see cref="OnSensorEnter"/>.
        /// </summary>
        /// <param name="entity">The entity that entered the sensor.</param>
        /// <param name="contactData">The serialized contact data for the sensor.</param>
        private void internal_n2m_on_sensor_enter(Entity entity, byte[] contactData)
        {
            Collision collision = new Collision
            {
                entity = entity,
                contacts = contactData.ToStructArray<ContactPoint>()
            };

            using (Run(this))
            {
                OnSensorEnter(collision);
            }
        }

        /// <summary>
        /// Internal method invoked when another entity exits a sensor attached to this entity. Calls <see cref="OnSensorExit"/>.
        /// </summary>
        /// <param name="entity">The entity that exited the sensor.</param>
        private void internal_n2m_on_sensor_exit(Entity entity, byte[] contactData, byte reason)
        {
            Collision collision = new Collision
            {
                entity = entity,
                contacts = contactData.ToStructArray<ContactPoint>(),
                reason = (ContactEndReason)reason
            };

            using (Run(this))
            {
                OnSensorExit(collision);
            }
        }

        /// <summary>
        /// Internal method invoked when this entity begins a collision. Converts contact data and calls <see cref="OnCollisionEnter"/>.
        /// </summary>
        /// <param name="entity">The other entity involved in the collision.</param>
        /// <param name="contactData">The serialized contact data for the collision.</param>
        private void internal_n2m_on_collision_enter(Entity entity, byte[] contactData)
        {
            Collision collision = new Collision
            {
                entity = entity,
                contacts = contactData.ToStructArray<ContactPoint>()
            };

            using (Run(this))
            {
                OnCollisionEnter(collision);
            }
        }

        /// <summary>
        /// Internal method invoked when this entity ends a collision. Converts contact data and calls <see cref="OnCollisionExit"/>.
        /// </summary>
        /// <param name="entity">The other entity involved in the collision.</param>
        /// <param name="contactData">The serialized contact data for the collision.</param>
        private void internal_n2m_on_collision_exit(Entity entity, byte[] contactData, byte reason)
        {
            Collision collision = new Collision
            {
                entity = entity,
                contacts = contactData.ToStructArray<ContactPoint>(),
                reason = (ContactEndReason)reason
            };

            using (Run(this))
            {
                OnCollisionExit(collision);
            }
        }

        /// <summary>
        /// Internal method invoked by editor tools to call a public instance method of this script by
        /// name. The arguments arrive encoded and separated as UIDataModels encodes values; a number
        /// list ("x,y,z") fills a vector or colour parameter. Missing trailing arguments take their
        /// defaults, and the first overload whose parameters the arguments convert to runs.
        /// </summary>
        /// <returns>{"result":value} (null for a void method) or {"error":"message"} as JSON.</returns>
        private string internal_n2m_invoke(string name, string arguments)
        {
            object[] values = UIDataModels.DecodeList(arguments);
            foreach (MethodInfo method in GetType().GetMethods(BindingFlags.Public | BindingFlags.Instance))
            {
                if (method.Name != name || method.IsGenericMethodDefinition ||
                    !TryBuildInvokeArguments(method.GetParameters(), values, out object[] parameters))
                {
                    continue;
                }
                object result;
                using (Run(this))
                {
                    try
                    {
                        result = method.Invoke(this, parameters);
                    }
                    catch (TargetInvocationException ex)
                    {
                        Exception inner = ex.InnerException ?? ex;
                        return MakeInvokeError($"{name} threw {inner.GetType().Name}: {inner.Message}");
                    }
                }
                var json = new StringBuilder("{\"result\":");
                UIDataModels.AppendJsonValue(json, result, InvokeResultDepth, InvokeResultItems);
                return json.Append('}').ToString();
            }
            return MakeInvokeError($"{GetType().FullName} has no public method {name} taking {values.Length} " +
                                   "argument(s) of types they convert to");
        }

        /// <summary>
        /// The arguments for @p parameters from the decoded @p values, or false when they do not fit.
        /// </summary>
        private static bool TryBuildInvokeArguments(ParameterInfo[] parameters, object[] values, out object[] arguments)
        {
            arguments = null;
            if (values.Length > parameters.Length)
            {
                return false;
            }
            var converted = new object[parameters.Length];
            for (int i = 0; i < parameters.Length; i++)
            {
                if (i >= values.Length)
                {
                    if (!parameters[i].HasDefaultValue)
                    {
                        return false;
                    }
                    converted[i] = parameters[i].DefaultValue;
                    continue;
                }
                try
                {
                    converted[i] = ConvertInvokeArgument(values[i], parameters[i].ParameterType);
                }
                catch (Exception)
                {
                    return false;
                }
            }
            arguments = converted;
            return true;
        }

        /// <summary>
        /// A value for a parameter of @p type: a "x,y,..." number list fills a struct through its
        /// constructor taking that many floats (Vector2, Vector3, Vector4, Color, Quaternion); anything
        /// else converts as a UI event argument does.
        /// </summary>
        private static object ConvertInvokeArgument(object value, Type type)
        {
            if (value is string text && type.IsValueType && !type.IsPrimitive && !type.IsEnum)
            {
                string[] parts = text.Split(',');
                var components = new object[parts.Length];
                var componentTypes = new Type[parts.Length];
                for (int i = 0; i < parts.Length; i++)
                {
                    components[i] = float.Parse(parts[i], NumberStyles.Float, CultureInfo.InvariantCulture);
                    componentTypes[i] = typeof(float);
                }
                ConstructorInfo constructor = type.GetConstructor(componentTypes);
                if (constructor != null)
                {
                    return constructor.Invoke(components);
                }
            }
            return UIDataModels.ConvertTo(value, type);
        }

        private static string MakeInvokeError(string message)
        {
            var json = new StringBuilder("{\"error\":");
            UIDataModels.AppendJsonString(json, message);
            return json.Append('}').ToString();
        }
    }
}

