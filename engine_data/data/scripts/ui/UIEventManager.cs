using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Unravel.Core
{

    /// <summary>
    /// Phase of UI event dispatch in the capture/target/bubble model.
    /// </summary>
    public enum EventPhase
    {
        None,
        Capture = 1,
        Target = 2,
        Bubble = 4
    }

    /// <summary>
    /// Event classes as native code reports them; the values mirror ui_event_kind in ui_bindings.cpp.
    /// </summary>
    internal enum UIEventKind
    {
        Base,
        Pointer,
        Click,
        Key,
        TextInput,
        Change,
        Slider,
        Focus,
        Scroll,
        Animation,
        Submit,
        Drag,
        Count
    }

    /// <summary>
    /// One UI event as native code sends it; the layout mirrors script_system::ui_event_payload.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    internal struct UIEventPayload
    {
        public long nativeEvent;
        public long targetHandle;
        public long currentHandle;
        public long relatedHandle;
        public int kind;
        public int phase;
        public int isCaptureListener;
        public int button;
        public int keyCode;
        public int modifiers;
        public int clickCount;
        public float x;
        public float y;
        public float deltaX;
        public float deltaY;
        public float value;
        public float minValue;
        public float maxValue;
        public float step;

        public bool ctrlKey => (modifiers & 1) != 0;
        public bool shiftKey => (modifiers & 2) != 0;
        public bool altKey => (modifiers & 4) != 0;
        public bool metaKey => (modifiers & 8) != 0;
    }

    /// <summary>
    /// Base type for UI events dispatched through <see cref="UIEventManager"/>.
    /// Event objects are reused: read what you need inside the callback and do not keep the event.
    /// </summary>
    public class UIEventBase
    {
        private IntPtr nativePtr = IntPtr.Zero;
        private Entity owner;
        internal int poolKind;

        /// <summary>
        /// The ID of the element that triggered the event.
        /// </summary>
        public string targetElementId;

        /// <summary>
        /// Opaque handle of the element that triggered the event (not a pointer); see <see cref="target"/>.
        /// </summary>
        public IntPtr targetElementPtr = IntPtr.Zero;

        /// <summary>
        /// The ID of the element that received the event.
        /// </summary>
        public string currentElementId;

        /// <summary>
        /// Opaque handle of the element that received the event (not a pointer); see <see cref="currentTarget"/>.
        /// </summary>
        public IntPtr currentElementPtr = IntPtr.Zero;

        /// <summary>
        /// Current dispatch phase of the event.
        /// </summary>
        public EventPhase phase = EventPhase.None;

        /// <summary>
        /// The type of event that occurred (e.g., "click", "mousedown").
        /// </summary>
        public string eventType;

        /// <summary>
        /// The element that triggered the event (the innermost one under the pointer, the focused one).
        /// </summary>
        public UIElement target => UIEventManager.GetOrCreateWrapper(targetElementPtr, owner);

        /// <summary>
        /// The element whose callback runs now: the one subscribed to, while the event bubbles up to it.
        /// </summary>
        public UIElement currentTarget => UIEventManager.GetOrCreateWrapper(currentElementPtr, owner);

        /// <summary>
        /// The entity whose document the event happened in.
        /// </summary>
        protected Entity documentOwner => owner;

        /// <summary>
        /// Stops propagation of the event if it is interruptible, but finish all listeners on the current element.
        /// Works only while the event is being dispatched (inside its callback).
        /// </summary>
        public void StopPropagation()
        {
            internal_m2n_ui_stop_propagation(nativePtr);
        }

        /// <summary>
        /// Stops propagation of the event if it is interruptible, including to any other listeners on the current element.
        /// Works only while the event is being dispatched (inside its callback).
        /// </summary>
        public void StopImmediatePropagation()
        {
            internal_m2n_ui_stop_immediate_propagation(nativePtr);
        }

        /// <summary>
        /// Fills the event from what native code sent; classes override it for their own fields.
        /// </summary>
        internal virtual void FillFrom(in UIEventPayload payload, string text)
        {
        }

        internal void FillBase(in UIEventPayload payload, string type, string targetId, string currentId, Entity documentEntity)
        {
            nativePtr = new IntPtr(payload.nativeEvent);
            owner = documentEntity;
            targetElementId = targetId;
            targetElementPtr = new IntPtr(payload.targetHandle);
            currentElementId = currentId;
            currentElementPtr = new IntPtr(payload.currentHandle);
            phase = (EventPhase)payload.phase;
            eventType = type;
        }

        /// <summary>
        /// Copies the fields every event has from @p other.
        /// </summary>
        internal void CopyBaseFrom(UIEventBase other)
        {
            nativePtr = other.nativePtr;
            owner = other.owner;
            targetElementId = other.targetElementId;
            targetElementPtr = other.targetElementPtr;
            currentElementId = other.currentElementId;
            currentElementPtr = other.currentElementPtr;
            phase = other.phase;
            eventType = other.eventType;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_stop_propagation(IntPtr nativePtr);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_stop_immediate_propagation(IntPtr nativePtr);
    }

    /// <summary>
    /// Callback invoked when a UI event is dispatched to a subscribed element.
    /// </summary>
    /// <param name="ev">The event being dispatched.</param>
    public delegate void UIEventCallback(UIEventBase ev);

    /// <summary>
    /// Global UI event manager that handles all UI event dispatching.
    /// Subscriptions are kept per element handle, so every wrapper of one element shares them.
    /// A subscription ends when its element is destroyed or when the script that made it is
    /// destroyed: the script the callback belongs to, else the script that was running.
    /// </summary>
    [AutoStaticsCleanup]
    public static class UIEventManager
    {
        internal sealed class Subscription
        {
            internal IntPtr handle;
            internal string eventType;
            /// <summary>The callback as given, matched by the unsubscribe calls.</summary>
            internal Delegate callback;
            /// <summary>The event class a typed callback takes; null for <see cref="UIEventCallback"/>.</summary>
            internal Type eventClass;
            internal Action<UIEventBase> invoke;
            internal ScriptComponent script;
            /// <summary>Listens in the capture phase (and at the target) instead of the bubble phase.</summary>
            internal bool capture;
            internal bool targetOnly;
            internal bool once;
            /// <summary>Part of a <see cref="UIElement.clicked"/> handler.</summary>
            internal bool isClicked;
            /// <summary>Serves the engine (animation completion), not a script: no script ends it.</summary>
            internal bool ownedByNoScript;
            internal bool removed;
            internal bool warnedMismatch;
        }

        private sealed class ElementEntry
        {
            internal UIElement wrapper;
            /// <summary>In subscription order; removed ones are dropped once no dispatch runs.</summary>
            internal readonly List<Subscription> subscriptions = new List<Subscription>();
            /// <summary>Native listeners attached, by <see cref="NativeKey"/>.</summary>
            internal readonly HashSet<string> nativeEvents = new HashSet<string>();
            /// <summary>The running animation of each property, for its completion.</summary>
            internal Dictionary<string, UIAnimation> animations;
            internal bool hasRemoved;
        }

        private sealed class DocumentWatcher
        {
            internal UIDocumentComponent component;
            internal Action<UIDocument> handler;
            internal ScriptComponent script;
            internal IntPtr documentHandle;
            internal bool removed;
        }

        private const string AnimationEndEvent = "animationend";

        private static readonly Dictionary<IntPtr, ElementEntry> entries = new Dictionary<IntPtr, ElementEntry>();
        private static readonly List<ElementEntry> entriesToCompact = new List<ElementEntry>();
        private static readonly List<DocumentWatcher> documentWatchers = new List<DocumentWatcher>();
        private static readonly Stack<UIEventBase>[] eventPools = CreateEventPools();
        private static int dispatchDepth;
        private static bool isPollingDocuments;

        /// <summary>
        /// Invoked by the runtime before a script domain unloads. Subscribed
        /// callbacks usually target script instances, which would otherwise
        /// keep the unloading domain alive.
        /// </summary>
        private static void OnStaticsCleanup()
        {
            entries.Clear();
            entriesToCompact.Clear();
            documentWatchers.Clear();
            foreach (Stack<UIEventBase> pool in eventPools)
            {
                pool.Clear();
            }
            dispatchDepth = 0;
            isPollingDocuments = false;
            UIDocument.ClearCache();
        }

        /// <summary>
        /// Returns the wrapper of the element @p handle names, creating one if needed.
        /// </summary>
        /// <param name="handle">Native element handle.</param>
        /// <param name="owner">Entity that owns the UI document.</param>
        /// <returns>Canonical <see cref="UIElement"/> for this element, or null for a zero handle.</returns>
        internal static UIElement GetOrCreateWrapper(IntPtr handle, Entity owner)
        {
            if (handle == IntPtr.Zero)
            {
                return null;
            }
            ElementEntry entry = GetOrCreateEntry(handle, owner);
            return entry.wrapper;
        }

        /// <summary>
        /// Wrappers for a list of element handles.
        /// </summary>
        internal static List<UIElement> GetOrCreateWrappers(long[] handles, Entity owner)
        {
            var elements = new List<UIElement>(handles?.Length ?? 0);
            if (handles != null)
            {
                foreach (long handle in handles)
                {
                    elements.Add(GetOrCreateWrapper(new IntPtr(handle), owner));
                }
            }
            return elements;
        }

        /// <summary>
        /// Subscribe to a UI event. This is called by UIElement.RegisterCallback.
        /// </summary>
        /// <param name="elementWrapper">The UIElement instance.</param>
        /// <param name="eventType">The type of event (e.g., "click").</param>
        /// <param name="callback">The callback to invoke.</param>
        public static void Subscribe(UIElement elementWrapper, string eventType, UIEventCallback callback)
        {
            if (callback == null)
            {
                return;
            }
            AddSubscription(elementWrapper, eventType, new Subscription
            {
                callback = callback,
                invoke = ev => callback(ev)
            });
        }

        /// <summary>
        /// Unsubscribe from a UI event.
        /// </summary>
        /// <param name="elementWrapper">The UIElement instance.</param>
        /// <param name="eventType">The type of event.</param>
        /// <param name="callback">The callback to remove.</param>
        /// <returns>True if the callback was found and removed.</returns>
        public static bool Unsubscribe(UIElement elementWrapper, string eventType, UIEventCallback callback)
        {
            return RemoveSubscription(elementWrapper, eventType, callback, null);
        }

        /// <summary>
        /// Subscribe to a typed UI event with compile-time type safety.
        /// A <see cref="UIChangeEvent"/> callback also receives the change events of sliders, with
        /// the value as text; a <see cref="UISliderEvent"/> callback receives the change events of
        /// other elements whose value is a number.
        /// </summary>
        /// <typeparam name="T">Event type (must derive from <see cref="UIEventBase"/>).</typeparam>
        /// <param name="elementWrapper">The UIElement instance.</param>
        /// <param name="eventType">The type of event to listen for.</param>
        /// <param name="callback">The callback to invoke.</param>
        public static void Subscribe<T>(UIElement elementWrapper, string eventType, Action<T> callback) where T : UIEventBase
        {
            SubscribeTyped(elementWrapper, eventType, callback, EventPhase.Bubble, false);
        }

        /// <summary>
        /// Unsubscribe from a typed UI event.
        /// </summary>
        /// <typeparam name="T">Event type (must derive from <see cref="UIEventBase"/>).</typeparam>
        /// <param name="elementWrapper">The UIElement instance.</param>
        /// <param name="eventType">The type of event.</param>
        /// <param name="callback">The callback to remove.</param>
        /// <returns>True if the callback was found and removed.</returns>
        public static bool Unsubscribe<T>(UIElement elementWrapper, string eventType, Action<T> callback) where T : UIEventBase
        {
            return RemoveSubscription(elementWrapper, eventType, callback, typeof(T));
        }

        /// <summary>
        /// Unsubscribe all callbacks from a specific UIElement.
        /// </summary>
        /// <param name="elementWrapper">The UIElement to unsubscribe from.</param>
        /// <returns>True if any subscriptions were removed.</returns>
        public static bool UnsubscribeAll(UIElement elementWrapper)
        {
            if (elementWrapper == null || !entries.TryGetValue(elementWrapper.GetHandle(), out ElementEntry entry))
            {
                return false;
            }
            bool hadAny = ClearEntry(elementWrapper.GetHandle(), entry);
            CompactIfIdle();
            return hadAny;
        }

        /// <summary>
        /// Removes all UI event subscriptions on the elements of the given entity's document.
        /// </summary>
        /// <param name="owner">Entity whose UI element subscriptions should be cleared.</param>
        public static void UnsubscribeAllForOwner(Entity owner)
        {
            UnsubscribeAllForOwner(owner, invalidateWrappers: false);
        }

        /// <summary>
        /// Removes all UI event subscriptions on the elements of the given entity's document.
        /// </summary>
        /// <param name="owner">Entity whose UI element subscriptions should be cleared.</param>
        /// <param name="invalidateWrappers">
        /// When true, also invalidates cached element/document wrappers (use on document Close).
        /// </param>
        public static void UnsubscribeAllForOwner(Entity owner, bool invalidateWrappers)
        {
            List<IntPtr> owned = null;
            foreach (KeyValuePair<IntPtr, ElementEntry> kvp in entries)
            {
                if (kvp.Value.wrapper != null && kvp.Value.wrapper.Owner == owner)
                {
                    if (owned == null)
                    {
                        owned = new List<IntPtr>();
                    }
                    owned.Add(kvp.Key);
                }
            }
            if (owned != null)
            {
                foreach (IntPtr handle in owned)
                {
                    ElementEntry entry = entries[handle];
                    ClearEntry(handle, entry);
                    if (invalidateWrappers)
                    {
                        entry.wrapper.Invalidate();
                        entries.Remove(handle);
                    }
                }
            }
            CompactIfIdle();
            if (invalidateWrappers)
            {
                UIDocument.InvalidateForOwner(owner);
            }
        }

        /// <summary>
        /// Get the number of active subscriptions (for debugging/monitoring).
        /// </summary>
        /// <returns>Total number of registered callbacks.</returns>
        public static int GetSubscriptionCount()
        {
            int count = 0;
            foreach (ElementEntry entry in entries.Values)
            {
                foreach (Subscription subscription in entry.subscriptions)
                {
                    if (!subscription.removed && subscription.callback != null)
                    {
                        count++;
                    }
                }
            }
            return count;
        }

        /// <summary>
        /// Get subscription info for debugging.
        /// </summary>
        /// <returns>Human-readable subscription summary.</returns>
        public static string GetSubscriptionInfo()
        {
            var info = $"Total subscriptions: {GetSubscriptionCount()}\n";
            info += $"Cached wrappers: {entries.Count}\n";
            foreach (ElementEntry entry in entries.Values)
            {
                var byEvent = new Dictionary<string, int>();
                foreach (Subscription subscription in entry.subscriptions)
                {
                    if (!subscription.removed && subscription.callback != null)
                    {
                        byEvent.TryGetValue(subscription.eventType, out int n);
                        byEvent[subscription.eventType] = n + 1;
                    }
                }
                if (byEvent.Count == 0)
                {
                    continue;
                }
                string elementId = entry.wrapper != null && entry.wrapper.IsValid() ? entry.wrapper.ElementId : "[INVALID]";
                info += $"  Element '{elementId}': {byEvent.Count} event types\n";
                foreach (KeyValuePair<string, int> eventKvp in byEvent)
                {
                    info += $"    Event '{eventKvp.Key}': {eventKvp.Value} callbacks\n";
                }
            }
            return info;
        }

        /// <summary>
        /// Adds a typed subscription; the engine of <see cref="UIElement.On{T}(string, Action{T}, EventPhase)"/>.
        /// </summary>
        internal static UISubscription SubscribeTyped<T>(UIElement elementWrapper, string eventType, Action<T> callback,
                                                         EventPhase phase, bool once) where T : UIEventBase
        {
            if (callback == null)
            {
                return UISubscription.Inactive;
            }
            var subscription = new Subscription
            {
                callback = callback,
                eventClass = typeof(T),
                capture = phase == EventPhase.Capture,
                targetOnly = phase == EventPhase.Target,
                once = once
            };
            subscription.invoke = ev =>
            {
                T typed = ev as T ?? ConvertEvent<T>(ev, subscription);
                if (typed != null)
                {
                    callback(typed);
                }
            };
            return AddSubscription(elementWrapper, eventType, subscription);
        }

        /// <summary>
        /// The event a typed subscription without a name listens to.
        /// </summary>
        internal static string GetDefaultEventType<T>() where T : UIEventBase
        {
            Type type = typeof(T);
            if (type == typeof(UIClickEvent))
            {
                return "click";
            }
            if (type == typeof(UIChangeEvent) || type == typeof(UISliderEvent))
            {
                return "change";
            }
            if (type == typeof(UIAnimationEvent))
            {
                return AnimationEndEvent;
            }
            if (type == typeof(UISubmitEvent))
            {
                return "submit";
            }
            if (type == typeof(UIScrollEvent))
            {
                return "scroll";
            }
            if (type == typeof(UITextInputEvent))
            {
                return "textinput";
            }
            throw new ArgumentException(
                $"{type.Name} covers several events; name one, e.g. On<{type.Name}>(\"mousedown\", callback).");
        }

        /// <summary>
        /// Ends a subscription; the engine of <see cref="UISubscription.Dispose"/>.
        /// </summary>
        internal static void Cancel(Subscription subscription)
        {
            if (subscription == null || subscription.removed || !entries.TryGetValue(subscription.handle, out ElementEntry entry))
            {
                return;
            }
            MarkRemoved(entry, subscription);
            ReleaseNativeListenerIfUnused(subscription.handle, entry, subscription.eventType, subscription.capture);
            CompactIfIdle();
        }

        /// <summary>
        /// Adds a <see cref="UIElement.clicked"/> handler.
        /// </summary>
        internal static void AddClicked(UIElement elementWrapper, Action action)
        {
            if (action == null)
            {
                return;
            }
            AddSubscription(elementWrapper, "click", new Subscription
            {
                callback = action,
                isClicked = true,
                invoke = ev => action()
            });
        }

        /// <summary>
        /// Removes a <see cref="UIElement.clicked"/> handler.
        /// </summary>
        internal static void RemoveClicked(UIElement elementWrapper, Action action)
        {
            if (elementWrapper == null || action == null ||
                !entries.TryGetValue(elementWrapper.GetHandle(), out ElementEntry entry))
            {
                return;
            }
            foreach (Subscription subscription in entry.subscriptions)
            {
                if (!subscription.removed && subscription.isClicked && subscription.callback.Equals(action))
                {
                    MarkRemoved(entry, subscription);
                    ReleaseNativeListenerIfUnused(subscription.handle, entry, subscription.eventType, false);
                    break;
                }
            }
            CompactIfIdle();
        }

        /// <summary>
        /// Follows @p animation to its end on @p elementWrapper: completes it on its 'animationend',
        /// ends the one it replaces.
        /// </summary>
        internal static void TrackAnimation(UIElement elementWrapper, UIAnimation animation)
        {
            IntPtr handle = elementWrapper.GetHandle();
            if (handle == IntPtr.Zero)
            {
                return;
            }
            ElementEntry entry = GetOrCreateEntry(handle, elementWrapper.Owner);
            if (entry.animations == null)
            {
                entry.animations = new Dictionary<string, UIAnimation>();
                // Belongs to no script: it serves every animation of the element.
                AddSubscription(elementWrapper, AnimationEndEvent, new Subscription
                {
                    invoke = ev => CompleteAnimation(handle, ev),
                    ownedByNoScript = true
                });
            }
            if (entry.animations.TryGetValue(animation.property, out UIAnimation replaced))
            {
                replaced.End(false);
            }
            entry.animations[animation.property] = animation;
        }

        /// <summary>
        /// Forgets @p animation: it was stopped and no completion follows.
        /// </summary>
        internal static void UntrackAnimation(UIElement elementWrapper, UIAnimation animation)
        {
            if (entries.TryGetValue(elementWrapper.GetHandle(), out ElementEntry entry) && entry.animations != null &&
                entry.animations.TryGetValue(animation.property, out UIAnimation current) && ReferenceEquals(current, animation))
            {
                entry.animations.Remove(animation.property);
            }
        }

        /// <summary>
        /// Ends every subscription @p script owns. Called when the script is destroyed, so its
        /// callbacks neither run afterwards nor keep it alive.
        /// </summary>
        internal static void UnsubscribeAllForScript(ScriptComponent script)
        {
            foreach (KeyValuePair<IntPtr, ElementEntry> kvp in entries)
            {
                ElementEntry entry = kvp.Value;
                List<Subscription> subscriptions = entry.subscriptions;
                for (int i = 0; i < subscriptions.Count; i++)
                {
                    Subscription subscription = subscriptions[i];
                    if (!subscription.removed && ReferenceEquals(subscription.script, script))
                    {
                        MarkRemoved(entry, subscription);
                        ReleaseNativeListenerIfUnused(kvp.Key, entry, subscription.eventType, subscription.capture);
                    }
                }
                if (entry.animations != null)
                {
                    foreach (UIAnimation animation in entry.animations.Values)
                    {
                        animation.ForgetHandlersOf(script);
                    }
                }
            }
            CompactIfIdle();
            foreach (DocumentWatcher watcher in documentWatchers)
            {
                if (ReferenceEquals(watcher.script, script))
                {
                    watcher.removed = true;
                }
            }
            CompactDocumentWatchersIfIdle();
        }

        /// <summary>
        /// Called by native code for every UI event on an element with subscriptions.
        /// </summary>
        internal static void InternalDispatchPayload(UIEventPayload payload, string eventType, string targetId,
                                                     string currentId, string text)
        {
            if (!entries.TryGetValue(new IntPtr(payload.currentHandle), out ElementEntry entry))
            {
                return;
            }
            UIEventBase ev = RentFilled(payload, eventType, targetId, currentId, text, entry.wrapper?.Owner ?? default(Entity));
            try
            {
                Dispatch(entry, ev, payload.isCaptureListener != 0);
            }
            finally
            {
                eventPools[ev.poolKind].Push(ev);
            }
        }

        /// <summary>
        /// Runs @p use with the event object for a payload (data model events, inline handlers).
        /// </summary>
        internal static void WithEvent(in UIEventPayload payload, string eventType, string targetId, string currentId,
                                       string text, Entity owner, Action<UIEventBase> use)
        {
            UIEventBase ev = RentFilled(payload, eventType, targetId, currentId, text, owner);
            try
            {
                use(ev);
            }
            finally
            {
                eventPools[ev.poolKind].Push(ev);
            }
        }

        private static UIEventBase RentFilled(in UIEventPayload payload, string eventType, string targetId, string currentId,
                                              string text, Entity owner)
        {
            int kind = payload.kind >= 0 && payload.kind < (int)UIEventKind.Count ? payload.kind : 0;
            UIEventBase ev = RentEvent(kind);
            ev.poolKind = kind;
            ev.FillBase(payload, eventType, targetId, currentId, owner);
            ev.FillFrom(payload, text);
            return ev;
        }

        /// <summary>
        /// Drops the wrappers and subscriptions of elements destroyed since the last call, then
        /// tells document watchers about new documents. Runs once per frame before the updates.
        /// </summary>
        internal static void ProcessDestroyedElements()
        {
            for (IntPtr handle = internal_m2n_ui_take_destroyed_handle(); handle != IntPtr.Zero;
                 handle = internal_m2n_ui_take_destroyed_handle())
            {
                if (entries.TryGetValue(handle, out ElementEntry entry))
                {
                    // The element took its native listeners with it.
                    foreach (Subscription subscription in entry.subscriptions)
                    {
                        subscription.removed = true;
                    }
                    EndAnimations(entry);
                    entry.wrapper?.Invalidate();
                    entries.Remove(handle);
                }
                UIDocument.InvalidateHandle(handle);
            }
            PollDocumentWatchers();
        }

        /// <summary>
        /// Calls @p handler with the document of @p component each time a document is loaded for
        /// it, starting with the current one, at the start of a frame.
        /// </summary>
        internal static void AddDocumentWatcher(UIDocumentComponent component, Action<UIDocument> handler)
        {
            if (component == null || handler == null)
            {
                return;
            }
            documentWatchers.Add(new DocumentWatcher
            {
                component = component,
                handler = handler,
                script = handler.Target as ScriptComponent ?? ScriptComponent.running
            });
        }

        /// <summary>
        /// Removes a handler added with <see cref="AddDocumentWatcher"/>.
        /// </summary>
        internal static void RemoveDocumentWatcher(UIDocumentComponent component, Action<UIDocument> handler)
        {
            foreach (DocumentWatcher watcher in documentWatchers)
            {
                if (!watcher.removed && watcher.component.owner == component.owner && watcher.handler.Equals(handler))
                {
                    watcher.removed = true;
                    break;
                }
            }
            CompactDocumentWatchersIfIdle();
        }

        private static ElementEntry GetOrCreateEntry(IntPtr handle, Entity owner)
        {
            if (!entries.TryGetValue(handle, out ElementEntry entry))
            {
                entry = new ElementEntry { wrapper = UIElement.Create(handle, owner) };
                entries[handle] = entry;
            }
            return entry;
        }

        private static UISubscription AddSubscription(UIElement elementWrapper, string eventType, Subscription subscription)
        {
            if (elementWrapper == null || string.IsNullOrEmpty(eventType))
            {
                return UISubscription.Inactive;
            }
            IntPtr handle = elementWrapper.GetHandle();
            if (handle == IntPtr.Zero)
            {
                return UISubscription.Inactive;
            }
            ElementEntry entry = GetOrCreateEntry(handle, elementWrapper.Owner);
            subscription.handle = handle;
            subscription.eventType = eventType;
            if (!subscription.ownedByNoScript)
            {
                subscription.script = subscription.callback?.Target as ScriptComponent ?? ScriptComponent.running;
            }
            entry.subscriptions.Add(subscription);
            if (entry.nativeEvents.Add(NativeKey(eventType, subscription.capture)))
            {
                internal_m2n_ui_ensure_native_event_listener(handle, eventType, subscription.capture);
            }
            return new UISubscription(subscription);
        }

        private static bool RemoveSubscription(UIElement elementWrapper, string eventType, Delegate callback, Type eventClass)
        {
            if (elementWrapper == null || callback == null ||
                !entries.TryGetValue(elementWrapper.GetHandle(), out ElementEntry entry))
            {
                return false;
            }
            foreach (Subscription subscription in entry.subscriptions)
            {
                if (!subscription.removed && !subscription.isClicked && subscription.eventType == eventType &&
                    subscription.eventClass == eventClass && callback.Equals(subscription.callback))
                {
                    MarkRemoved(entry, subscription);
                    ReleaseNativeListenerIfUnused(elementWrapper.GetHandle(), entry, eventType, subscription.capture);
                    CompactIfIdle();
                    return true;
                }
            }
            return false;
        }

        private static void Dispatch(ElementEntry entry, UIEventBase ev, bool fromCaptureListener)
        {
            List<Subscription> subscriptions = entry.subscriptions;
            // Callbacks a callback adds start with the next event.
            int count = subscriptions.Count;
            dispatchDepth++;
            try
            {
                for (int i = 0; i < count; i++)
                {
                    Subscription subscription = subscriptions[i];
                    if (!Matches(subscription, ev, fromCaptureListener))
                    {
                        continue;
                    }
                    if (subscription.once)
                    {
                        MarkRemoved(entry, subscription);
                        ReleaseNativeListenerIfUnused(subscription.handle, entry, subscription.eventType, subscription.capture);
                    }
                    Invoke(subscription, ev);
                }
            }
            finally
            {
                dispatchDepth--;
            }
            CompactIfIdle();
        }

        private static bool Matches(Subscription subscription, UIEventBase ev, bool fromCaptureListener)
        {
            return !subscription.removed && subscription.capture == fromCaptureListener &&
                   subscription.eventType == ev.eventType &&
                   (!subscription.targetOnly || ev.phase == EventPhase.Target);
        }

        private static void Invoke(Subscription subscription, UIEventBase ev)
        {
            using (ScriptComponent.Run(subscription.script))
            {
                try
                {
                    subscription.invoke(ev);
                }
                catch (Exception ex)
                {
                    Log.Error($"UI '{ev.eventType}' callback on '{ev.currentElementId}': {ex}");
                }
            }
        }

        private static void CompleteAnimation(IntPtr handle, UIEventBase ev)
        {
            if (ev is UIAnimationEvent animationEvent && entries.TryGetValue(handle, out ElementEntry entry) &&
                entry.animations != null && entry.animations.TryGetValue(animationEvent.property, out UIAnimation animation))
            {
                entry.animations.Remove(animationEvent.property);
                animation.End(true);
            }
        }

        private static void EndAnimations(ElementEntry entry)
        {
            if (entry.animations == null)
            {
                return;
            }
            foreach (UIAnimation animation in entry.animations.Values)
            {
                animation.End(false);
            }
            entry.animations.Clear();
        }

        /// <returns>Whether the entry had any subscription.</returns>
        private static bool ClearEntry(IntPtr handle, ElementEntry entry)
        {
            bool hadAny = false;
            foreach (Subscription subscription in entry.subscriptions)
            {
                if (!subscription.removed)
                {
                    MarkRemoved(entry, subscription);
                    hadAny = true;
                }
            }
            foreach (string key in entry.nativeEvents)
            {
                ParseNativeKey(key, out string eventType, out bool capture);
                internal_m2n_ui_remove_native_event_listener(handle, eventType, capture);
            }
            entry.nativeEvents.Clear();
            if (entry.animations != null)
            {
                // Its completion listener went with the other subscriptions.
                EndAnimations(entry);
                entry.animations = null;
            }
            return hadAny;
        }

        /// <summary>
        /// Marks a subscription removed: it is skipped at once and dropped from the list once no
        /// dispatch iterates it.
        /// </summary>
        private static void MarkRemoved(ElementEntry entry, Subscription subscription)
        {
            subscription.removed = true;
            if (!entry.hasRemoved)
            {
                entry.hasRemoved = true;
                entriesToCompact.Add(entry);
            }
        }

        private static void ReleaseNativeListenerIfUnused(IntPtr handle, ElementEntry entry, string eventType, bool capture)
        {
            foreach (Subscription subscription in entry.subscriptions)
            {
                if (!subscription.removed && subscription.eventType == eventType && subscription.capture == capture)
                {
                    return;
                }
            }
            if (entry.nativeEvents.Remove(NativeKey(eventType, capture)))
            {
                internal_m2n_ui_remove_native_event_listener(handle, eventType, capture);
            }
        }

        private static string NativeKey(string eventType, bool capture)
        {
            return capture ? eventType + "|capture" : eventType;
        }

        private static void ParseNativeKey(string key, out string eventType, out bool capture)
        {
            capture = key.EndsWith("|capture", StringComparison.Ordinal);
            eventType = capture ? key.Substring(0, key.Length - "|capture".Length) : key;
        }

        private static void CompactIfIdle()
        {
            if (dispatchDepth > 0)
            {
                return;
            }
            foreach (ElementEntry entry in entriesToCompact)
            {
                entry.subscriptions.RemoveAll(s => s.removed);
                entry.hasRemoved = false;
            }
            entriesToCompact.Clear();
        }

        private static Stack<UIEventBase>[] CreateEventPools()
        {
            var pools = new Stack<UIEventBase>[(int)UIEventKind.Count];
            for (int i = 0; i < pools.Length; i++)
            {
                pools[i] = new Stack<UIEventBase>();
            }
            return pools;
        }

        /// <summary>
        /// An event object of the kind's class; nested dispatches each get their own.
        /// </summary>
        private static UIEventBase RentEvent(int kind)
        {
            Stack<UIEventBase> pool = eventPools[kind];
            return pool.Count > 0 ? pool.Pop() : CreateEvent((UIEventKind)kind);
        }

        private static UIEventBase CreateEvent(UIEventKind kind)
        {
            switch (kind)
            {
                case UIEventKind.Pointer: return new UIPointerEvent();
                case UIEventKind.Click: return new UIClickEvent();
                case UIEventKind.Key: return new UIKeyEvent();
                case UIEventKind.TextInput: return new UITextInputEvent();
                case UIEventKind.Change: return new UIChangeEvent();
                case UIEventKind.Slider: return new UISliderEvent();
                case UIEventKind.Focus: return new UIFocusEvent();
                case UIEventKind.Scroll: return new UIScrollEvent();
                case UIEventKind.Animation: return new UIAnimationEvent();
                case UIEventKind.Submit: return new UISubmitEvent();
                case UIEventKind.Drag: return new UIDragEvent();
                default: return new UIEventBase();
            }
        }

        private static void PollDocumentWatchers()
        {
            isPollingDocuments = true;
            try
            {
                // Handlers may add watchers (they run from the next poll) or remove them.
                int count = documentWatchers.Count;
                for (int i = 0; i < count; i++)
                {
                    PollDocumentWatcher(documentWatchers[i]);
                }
            }
            finally
            {
                isPollingDocuments = false;
            }
            CompactDocumentWatchersIfIdle();
        }

        private static void PollDocumentWatcher(DocumentWatcher watcher)
        {
            if (watcher.removed)
            {
                return;
            }
            Entity owner = watcher.component.owner;
            UIDocument document = owner.IsValid() && owner.HasComponent<UIDocumentComponent>()
                ? watcher.component.document
                : null;
            IntPtr handle = document?.GetHandle() ?? IntPtr.Zero;
            if (handle == watcher.documentHandle)
            {
                return;
            }
            watcher.documentHandle = handle;
            if (document == null)
            {
                return;
            }
            using (ScriptComponent.Run(watcher.script))
            {
                try
                {
                    watcher.handler(document);
                }
                catch (Exception ex)
                {
                    Log.Error($"UI documentLoaded handler on {owner}: {ex}");
                }
            }
        }

        private static void CompactDocumentWatchersIfIdle()
        {
            if (!isPollingDocuments)
            {
                documentWatchers.RemoveAll(w => w.removed);
            }
        }

        /// <summary>
        /// Converts between the change event classes for a callback that takes the other one
        /// (sliders send <see cref="UISliderEvent"/>, other elements <see cref="UIChangeEvent"/>).
        /// </summary>
        /// <returns>The converted event, or null when the event does not fit the callback.</returns>
        private static T ConvertEvent<T>(UIEventBase ev, Subscription subscription) where T : UIEventBase
        {
            UIEventBase converted = null;
            if (typeof(T) == typeof(UIChangeEvent) && ev is UISliderEvent sliderEvent)
            {
                var asChange = new UIChangeEvent { value = sliderEvent.value.ToString(CultureInfo.InvariantCulture) };
                asChange.CopyBaseFrom(ev);
                converted = asChange;
            }
            else if (typeof(T) == typeof(UISliderEvent) && ev is UIChangeEvent changeEvent &&
                     float.TryParse(changeEvent.value, NumberStyles.Float, CultureInfo.InvariantCulture, out float value))
            {
                var asSlider = new UISliderEvent { value = value };
                asSlider.CopyBaseFrom(ev);
                converted = asSlider;
            }
            if (converted == null && !subscription.warnedMismatch)
            {
                subscription.warnedMismatch = true;
                Log.Warning($"UI: a {typeof(T).Name} callback for '{ev.eventType}' on '{ev.currentElementId}' " +
                            $"does not receive its {ev.GetType().Name} events. Register it for {ev.GetType().Name} or UIEventBase.");
            }
            return converted as T;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_ensure_native_event_listener(IntPtr elementHandle, string eventType, bool capture);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_remove_native_event_listener(IntPtr elementHandle, string eventType, bool capture);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr internal_m2n_ui_take_destroyed_handle();
    }
}
