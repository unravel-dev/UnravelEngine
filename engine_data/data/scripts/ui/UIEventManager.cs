using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

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
    /// Base type for UI events dispatched through <see cref="UIEventManager"/>.
    /// </summary>
    public class UIEventBase
    {
        private IntPtr nativePtr = IntPtr.Zero;

        /// <summary>
        /// The ID of the element that triggered the event.
        /// </summary>
        public string targetElementId;

        /// <summary>
        /// Native pointer to the element that triggered the event.
        /// </summary>
        public IntPtr targetElementPtr = IntPtr.Zero;

        /// <summary>
        /// The ID of the element that received the event.
        /// </summary>
        public string currentElementId;

        /// <summary>
        /// Native pointer to the element that received the event.
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
        /// Stops propagation of the event if it is interruptible, but finish all listeners on the current element.
        /// </summary>
        public void StopPropagation()
        {
            internal_m2n_ui_stop_propagation(nativePtr);
        }

        /// <summary>
        /// Stops propagation of the event if it is interruptible, including to any other listeners on the current element.
        /// </summary>
        public void StopImmediatePropagation()
        {
            internal_m2n_ui_stop_immediate_propagation(nativePtr);
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
    /// Subscriptions are keyed by native element pointer so multiple managed wrappers
    /// for the same element share one subscription set.
    /// </summary>
    [AutoStaticsCleanup]
    public static class UIEventManager
    {
        private static readonly Dictionary<IntPtr, UIElement> wrappersByPtr
            = new Dictionary<IntPtr, UIElement>();

        private static readonly Dictionary<IntPtr, Dictionary<string, List<UIEventCallback>>> baseSubscriptions
            = new Dictionary<IntPtr, Dictionary<string, List<UIEventCallback>>>();

        private static readonly Dictionary<IntPtr, Dictionary<string, Dictionary<Type, List<Delegate>>>> typedSubscriptions
            = new Dictionary<IntPtr, Dictionary<string, Dictionary<Type, List<Delegate>>>>();

        private static readonly Dictionary<IntPtr, HashSet<string>> attachedNativeEvents
            = new Dictionary<IntPtr, HashSet<string>>();

        private static bool isDispatching = false;
        private static readonly List<PendingOperation> pendingOperations = new List<PendingOperation>();

        private enum OperationType
        {
            Subscribe,
            Unsubscribe,
            SubscribeTyped,
            UnsubscribeTyped,
            UnsubscribeAll,
            UnsubscribeAllForOwner,
            UnsubscribeAllForOwnerInvalidate
        }

        private struct PendingOperation
        {
            public OperationType type;
            public UIElement elementWrapper;
            public Entity owner;
            public string eventType;
            public UIEventCallback baseCallback;
            public Type callbackType;
            public Delegate typedCallback;
        }

        /// <summary>
        /// Invoked by the runtime before a script domain unloads. Subscribed
        /// callbacks usually target script instances, which would otherwise
        /// keep the unloading domain alive.
        /// </summary>
        private static void OnStaticsCleanup()
        {
            baseSubscriptions.Clear();
            typedSubscriptions.Clear();
            attachedNativeEvents.Clear();
            wrappersByPtr.Clear();
            pendingOperations.Clear();
            isDispatching = false;
            UIDocument.ClearCache();
        }

        /// <summary>
        /// Returns a cached wrapper for the native element pointer, creating one if needed.
        /// </summary>
        /// <param name="ptr">Native <c>Rml::Element*</c>.</param>
        /// <param name="owner">Entity that owns the UI document.</param>
        /// <returns>Canonical <see cref="UIElement"/> for this pointer, or null.</returns>
        internal static UIElement GetOrCreateWrapper(IntPtr ptr, Entity owner)
        {
            if (ptr == IntPtr.Zero)
            {
                return null;
            }
            if (wrappersByPtr.TryGetValue(ptr, out UIElement existing))
            {
                if (existing.GetNativePointer() == ptr && existing.Owner == owner)
                {
                    return existing;
                }
                existing.Invalidate();
                wrappersByPtr.Remove(ptr);
            }
            UIElement wrapper = new UIElement(ptr, owner);
            wrappersByPtr[ptr] = wrapper;
            return wrapper;
        }

        /// <summary>
        /// Subscribe to a UI event. This is called by UIElement.RegisterCallback.
        /// </summary>
        /// <param name="elementWrapper">The UIElement instance.</param>
        /// <param name="eventType">The type of event (e.g., "click").</param>
        /// <param name="callback">The callback to invoke.</param>
        public static void Subscribe(UIElement elementWrapper, string eventType, UIEventCallback callback)
        {
            if (callback == null || elementWrapper == null)
            {
                return;
            }
            IntPtr ptr = elementWrapper.GetNativePointer();
            if (ptr == IntPtr.Zero)
            {
                return;
            }
            if (isDispatching)
            {
                pendingOperations.Add(new PendingOperation
                {
                    type = OperationType.Subscribe,
                    elementWrapper = elementWrapper,
                    eventType = eventType,
                    baseCallback = callback
                });
                return;
            }
            RegisterCanonicalWrapper(elementWrapper, ptr);
            if (!baseSubscriptions.TryGetValue(ptr, out Dictionary<string, List<UIEventCallback>> byEvent))
            {
                byEvent = new Dictionary<string, List<UIEventCallback>>();
                baseSubscriptions[ptr] = byEvent;
            }
            if (!byEvent.TryGetValue(eventType, out List<UIEventCallback> callbacks))
            {
                callbacks = new List<UIEventCallback>();
                byEvent[eventType] = callbacks;
            }
            callbacks.Add(callback);
            EnsureNativeEventListener(ptr, eventType);
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
            if (callback == null || elementWrapper == null)
            {
                return false;
            }
            IntPtr ptr = elementWrapper.GetNativePointer();
            if (ptr == IntPtr.Zero)
            {
                return false;
            }
            if (isDispatching)
            {
                pendingOperations.Add(new PendingOperation
                {
                    type = OperationType.Unsubscribe,
                    elementWrapper = elementWrapper,
                    eventType = eventType,
                    baseCallback = callback
                });
                return true;
            }
            if (!baseSubscriptions.TryGetValue(ptr, out Dictionary<string, List<UIEventCallback>> byEvent) ||
                !byEvent.TryGetValue(eventType, out List<UIEventCallback> callbacks))
            {
                return false;
            }
            bool removed = callbacks.Remove(callback);
            if (callbacks.Count == 0)
            {
                byEvent.Remove(eventType);
                if (byEvent.Count == 0)
                {
                    baseSubscriptions.Remove(ptr);
                }
                MaybeRemoveNativeEventListener(ptr, eventType);
            }
            return removed;
        }

        /// <summary>
        /// Unsubscribe all callbacks from a specific UIElement.
        /// </summary>
        /// <param name="elementWrapper">The UIElement to unsubscribe from.</param>
        /// <returns>True if any subscriptions were removed (or will be removed if deferred).</returns>
        public static bool UnsubscribeAll(UIElement elementWrapper)
        {
            if (elementWrapper == null)
            {
                return false;
            }
            IntPtr ptr = elementWrapper.GetNativePointer();
            if (ptr == IntPtr.Zero)
            {
                return false;
            }
            if (isDispatching)
            {
                pendingOperations.Add(new PendingOperation
                {
                    type = OperationType.UnsubscribeAll,
                    elementWrapper = elementWrapper
                });
                return true;
            }
            return ClearSubscriptionsForPtr(ptr);
        }

        /// <summary>
        /// Removes all UI event subscriptions for wrappers owned by the given entity.
        /// Called automatically when a <see cref="ScriptComponent"/> is destroyed so
        /// callbacks cannot pin destroyed script instances.
        /// </summary>
        /// <param name="owner">Entity whose UI element subscriptions should be cleared.</param>
        public static void UnsubscribeAllForOwner(Entity owner)
        {
            UnsubscribeAllForOwner(owner, invalidateWrappers: false);
        }

        /// <summary>
        /// Removes all UI event subscriptions for wrappers owned by the given entity.
        /// </summary>
        /// <param name="owner">Entity whose UI element subscriptions should be cleared.</param>
        /// <param name="invalidateWrappers">
        /// When true, also invalidates cached element/document wrappers (use on document Close).
        /// </param>
        public static void UnsubscribeAllForOwner(Entity owner, bool invalidateWrappers)
        {
            if (isDispatching)
            {
                pendingOperations.Add(new PendingOperation
                {
                    type = invalidateWrappers
                        ? OperationType.UnsubscribeAllForOwnerInvalidate
                        : OperationType.UnsubscribeAllForOwner,
                    owner = owner
                });
                return;
            }
            List<IntPtr> ownedPtrs = null;
            foreach (KeyValuePair<IntPtr, UIElement> kvp in wrappersByPtr)
            {
                if (kvp.Value.Owner == owner)
                {
                    if (ownedPtrs == null)
                    {
                        ownedPtrs = new List<IntPtr>();
                    }
                    ownedPtrs.Add(kvp.Key);
                }
            }
            if (ownedPtrs != null)
            {
                foreach (IntPtr ptr in ownedPtrs)
                {
                    ClearSubscriptionsForPtr(ptr);
                    if (invalidateWrappers && wrappersByPtr.TryGetValue(ptr, out UIElement wrapper))
                    {
                        wrapper.Invalidate();
                        wrappersByPtr.Remove(ptr);
                    }
                }
            }
            if (invalidateWrappers)
            {
                UIDocument.InvalidateForOwner(owner);
            }
        }

        /// <summary>
        /// Invalidates a cached element wrapper and clears its subscriptions.
        /// </summary>
        /// <param name="ptr">Native element pointer.</param>
        internal static void InvalidateElement(IntPtr ptr)
        {
            if (ptr == IntPtr.Zero)
            {
                return;
            }
            ClearSubscriptionsForPtr(ptr);
            if (wrappersByPtr.TryGetValue(ptr, out UIElement wrapper))
            {
                wrapper.Invalidate();
                wrappersByPtr.Remove(ptr);
            }
        }

        /// <summary>
        /// Internal method called by C++ to dispatch events to managed callbacks.
        /// </summary>
        /// <param name="ev">The event data.</param>
        internal static void InternalDispatchEvent(UIEventBase ev)
        {
            isDispatching = true;
            try
            {
                if (wrappersByPtr.TryGetValue(ev.currentElementPtr, out UIElement targetWrapper))
                {
                    DispatchTypedCallbacks(ev.currentElementPtr, ev);
                    DispatchBaseCallbacks(ev.currentElementPtr, ev);
                }
            }
            finally
            {
                isDispatching = false;
            }
            ProcessPendingOperations();
        }

        /// <summary>
        /// Subscribe to a typed UI event with compile-time type safety.
        /// </summary>
        /// <typeparam name="T">Event type (must derive from <see cref="UIEventBase"/>).</typeparam>
        /// <param name="elementWrapper">The UIElement instance.</param>
        /// <param name="eventType">The type of event to listen for.</param>
        /// <param name="callback">The callback to invoke.</param>
        public static void Subscribe<T>(UIElement elementWrapper, string eventType, Action<T> callback) where T : UIEventBase
        {
            if (callback == null || elementWrapper == null)
            {
                return;
            }
            if (isDispatching)
            {
                pendingOperations.Add(new PendingOperation
                {
                    type = OperationType.SubscribeTyped,
                    elementWrapper = elementWrapper,
                    eventType = eventType,
                    callbackType = typeof(T),
                    typedCallback = callback
                });
                return;
            }
            SubscribeTypedInternal(elementWrapper, eventType, typeof(T), callback);
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
            if (callback == null || elementWrapper == null)
            {
                return false;
            }
            if (isDispatching)
            {
                pendingOperations.Add(new PendingOperation
                {
                    type = OperationType.UnsubscribeTyped,
                    elementWrapper = elementWrapper,
                    eventType = eventType,
                    callbackType = typeof(T),
                    typedCallback = callback
                });
                return true;
            }
            return UnsubscribeTypedInternal(elementWrapper, eventType, typeof(T), callback);
        }

        /// <summary>
        /// Get the number of active subscriptions (for debugging/monitoring).
        /// </summary>
        /// <returns>Total number of registered callbacks.</returns>
        public static int GetSubscriptionCount()
        {
            int count = 0;
            foreach (Dictionary<string, List<UIEventCallback>> elementSubs in baseSubscriptions.Values)
            {
                foreach (List<UIEventCallback> eventCallbacks in elementSubs.Values)
                {
                    count += eventCallbacks.Count;
                }
            }
            foreach (Dictionary<string, Dictionary<Type, List<Delegate>>> elementSubs in typedSubscriptions.Values)
            {
                foreach (Dictionary<Type, List<Delegate>> eventTypes in elementSubs.Values)
                {
                    foreach (List<Delegate> typeCallbacks in eventTypes.Values)
                    {
                        count += typeCallbacks.Count;
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
            info += $"Cached wrappers: {wrappersByPtr.Count}\n";
            info += $"Elements with base subscriptions: {baseSubscriptions.Count}\n";
            info += $"Elements with typed subscriptions: {typedSubscriptions.Count}\n";
            foreach (KeyValuePair<IntPtr, Dictionary<string, List<UIEventCallback>>> elementKvp in baseSubscriptions)
            {
                string elementId = ResolveElementId(elementKvp.Key);
                info += $"  Base Element '{elementId}': {elementKvp.Value.Count} event types\n";
                foreach (KeyValuePair<string, List<UIEventCallback>> eventKvp in elementKvp.Value)
                {
                    info += $"    Event '{eventKvp.Key}': {eventKvp.Value.Count} callbacks\n";
                }
            }
            foreach (KeyValuePair<IntPtr, Dictionary<string, Dictionary<Type, List<Delegate>>>> elementKvp in typedSubscriptions)
            {
                string elementId = ResolveElementId(elementKvp.Key);
                info += $"  Typed Element '{elementId}': {elementKvp.Value.Count} event types\n";
                foreach (KeyValuePair<string, Dictionary<Type, List<Delegate>>> eventKvp in elementKvp.Value)
                {
                    foreach (KeyValuePair<Type, List<Delegate>> typeKvp in eventKvp.Value)
                    {
                        info += $"    Event '{eventKvp.Key}' ({typeKvp.Key.Name}): {typeKvp.Value.Count} callbacks\n";
                    }
                }
            }
            return info;
        }

        private static string ResolveElementId(IntPtr ptr)
        {
            if (wrappersByPtr.TryGetValue(ptr, out UIElement wrapper) && wrapper.IsValid())
            {
                return wrapper.ElementId;
            }
            return "[INVALID]";
        }

        private static void RegisterCanonicalWrapper(UIElement elementWrapper, IntPtr ptr)
        {
            if (wrappersByPtr.TryGetValue(ptr, out UIElement existing))
            {
                if (ReferenceEquals(existing, elementWrapper))
                {
                    return;
                }
                if (existing.GetNativePointer() == ptr && existing.Owner == elementWrapper.Owner)
                {
                    return;
                }
                existing.Invalidate();
            }
            wrappersByPtr[ptr] = elementWrapper;
        }

        private static void SubscribeTypedInternal(UIElement elementWrapper, string eventType, Type callbackType, Delegate callback)
        {
            IntPtr ptr = elementWrapper.GetNativePointer();
            if (ptr == IntPtr.Zero)
            {
                return;
            }
            RegisterCanonicalWrapper(elementWrapper, ptr);
            if (!typedSubscriptions.TryGetValue(ptr, out Dictionary<string, Dictionary<Type, List<Delegate>>> byEvent))
            {
                byEvent = new Dictionary<string, Dictionary<Type, List<Delegate>>>();
                typedSubscriptions[ptr] = byEvent;
            }
            if (!byEvent.TryGetValue(eventType, out Dictionary<Type, List<Delegate>> byType))
            {
                byType = new Dictionary<Type, List<Delegate>>();
                byEvent[eventType] = byType;
            }
            if (!byType.TryGetValue(callbackType, out List<Delegate> callbacks))
            {
                callbacks = new List<Delegate>();
                byType[callbackType] = callbacks;
            }
            callbacks.Add(callback);
            EnsureNativeEventListener(ptr, eventType);
        }

        private static bool UnsubscribeTypedInternal(UIElement elementWrapper, string eventType, Type callbackType, Delegate callback)
        {
            IntPtr ptr = elementWrapper.GetNativePointer();
            if (ptr == IntPtr.Zero)
            {
                return false;
            }
            if (!typedSubscriptions.TryGetValue(ptr, out Dictionary<string, Dictionary<Type, List<Delegate>>> byEvent) ||
                !byEvent.TryGetValue(eventType, out Dictionary<Type, List<Delegate>> byType) ||
                !byType.TryGetValue(callbackType, out List<Delegate> callbacks))
            {
                return false;
            }
            bool removed = callbacks.Remove(callback);
            if (callbacks.Count == 0)
            {
                byType.Remove(callbackType);
                if (byType.Count == 0)
                {
                    byEvent.Remove(eventType);
                    if (byEvent.Count == 0)
                    {
                        typedSubscriptions.Remove(ptr);
                    }
                    MaybeRemoveNativeEventListener(ptr, eventType);
                }
            }
            return removed;
        }

        private static bool ClearSubscriptionsForPtr(IntPtr ptr)
        {
            bool hadBase = baseSubscriptions.Remove(ptr);
            bool hadTyped = typedSubscriptions.Remove(ptr);
            if (attachedNativeEvents.TryGetValue(ptr, out HashSet<string> events))
            {
                foreach (string eventType in events)
                {
                    internal_m2n_ui_remove_native_event_listener(ptr, eventType);
                }
                attachedNativeEvents.Remove(ptr);
            }
            return hadBase || hadTyped;
        }

        private static void DispatchTypedCallbacks(IntPtr ptr, UIEventBase ev)
        {
            if (!typedSubscriptions.TryGetValue(ptr, out Dictionary<string, Dictionary<Type, List<Delegate>>> byEvent) ||
                !byEvent.TryGetValue(ev.eventType, out Dictionary<Type, List<Delegate>> byType))
            {
                return;
            }
            Type actualEventType = ev.GetType();
            foreach (KeyValuePair<Type, List<Delegate>> typeKvp in byType)
            {
                Type callbackType = typeKvp.Key;
                if (!callbackType.IsAssignableFrom(actualEventType))
                {
                    continue;
                }
                List<Delegate> callbacks = typeKvp.Value;
                for (int i = 0; i < callbacks.Count; i++)
                {
                    Delegate callback = callbacks[i];
                    try
                    {
                        if (callbackType == actualEventType)
                        {
                            callback.DynamicInvoke(ev);
                        }
                        else
                        {
                            // Assignable base (e.g. Action<UIEventBase> receiving UIChangeEvent).
                            callback.DynamicInvoke(ev);
                        }
                    }
                    catch (Exception ex)
                    {
                        Log.Error($"Error in typed UI event callback for {ev.eventType} on {ev.currentElementId}: {ex}");
                    }
                }
            }
        }

        private static void DispatchBaseCallbacks(IntPtr ptr, UIEventBase ev)
        {
            if (!baseSubscriptions.TryGetValue(ptr, out Dictionary<string, List<UIEventCallback>> byEvent) ||
                !byEvent.TryGetValue(ev.eventType, out List<UIEventCallback> callbacks))
            {
                return;
            }
            for (int i = 0; i < callbacks.Count; i++)
            {
                UIEventCallback callback = callbacks[i];
                try
                {
                    callback?.Invoke(ev);
                }
                catch (Exception ex)
                {
                    Log.Error($"Error in Base UI event callback for {ev.eventType} on {ev.currentElementId}: {ex}");
                }
            }
        }

        private static void ProcessPendingOperations()
        {
            for (int i = 0; i < pendingOperations.Count; i++)
            {
                PendingOperation op = pendingOperations[i];
                switch (op.type)
                {
                    case OperationType.Subscribe:
                        Subscribe(op.elementWrapper, op.eventType, op.baseCallback);
                        break;
                    case OperationType.Unsubscribe:
                        Unsubscribe(op.elementWrapper, op.eventType, op.baseCallback);
                        break;
                    case OperationType.SubscribeTyped:
                        SubscribeTypedInternal(op.elementWrapper, op.eventType, op.callbackType, op.typedCallback);
                        break;
                    case OperationType.UnsubscribeTyped:
                        UnsubscribeTypedInternal(op.elementWrapper, op.eventType, op.callbackType, op.typedCallback);
                        break;
                    case OperationType.UnsubscribeAll:
                        if (op.elementWrapper != null)
                        {
                            IntPtr ptr = op.elementWrapper.GetNativePointer();
                            if (ptr != IntPtr.Zero)
                            {
                                ClearSubscriptionsForPtr(ptr);
                            }
                        }
                        break;
                    case OperationType.UnsubscribeAllForOwner:
                        UnsubscribeAllForOwner(op.owner, invalidateWrappers: false);
                        break;
                    case OperationType.UnsubscribeAllForOwnerInvalidate:
                        UnsubscribeAllForOwner(op.owner, invalidateWrappers: true);
                        break;
                }
            }
            pendingOperations.Clear();
        }

        private static void EnsureNativeEventListener(IntPtr ptr, string eventType)
        {
            if (!attachedNativeEvents.TryGetValue(ptr, out HashSet<string> events))
            {
                events = new HashSet<string>();
                attachedNativeEvents[ptr] = events;
            }
            if (events.Add(eventType))
            {
                internal_m2n_ui_ensure_native_event_listener(ptr, eventType);
            }
        }

        private static void MaybeRemoveNativeEventListener(IntPtr ptr, string eventType)
        {
            if (HasAnySubscription(ptr, eventType))
            {
                return;
            }
            if (attachedNativeEvents.TryGetValue(ptr, out HashSet<string> events) && events.Remove(eventType))
            {
                internal_m2n_ui_remove_native_event_listener(ptr, eventType);
                if (events.Count == 0)
                {
                    attachedNativeEvents.Remove(ptr);
                }
            }
        }

        private static bool HasAnySubscription(IntPtr ptr, string eventType)
        {
            if (baseSubscriptions.TryGetValue(ptr, out Dictionary<string, List<UIEventCallback>> baseByEvent) &&
                baseByEvent.TryGetValue(eventType, out List<UIEventCallback> baseCallbacks) &&
                baseCallbacks.Count > 0)
            {
                return true;
            }
            if (typedSubscriptions.TryGetValue(ptr, out Dictionary<string, Dictionary<Type, List<Delegate>>> typedByEvent) &&
                typedByEvent.TryGetValue(eventType, out Dictionary<Type, List<Delegate>> byType))
            {
                foreach (List<Delegate> callbacks in byType.Values)
                {
                    if (callbacks.Count > 0)
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_ensure_native_event_listener(IntPtr elementPtr, string eventType);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_remove_native_event_listener(IntPtr elementPtr, string eventType);
    }
}
