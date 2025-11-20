using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{

    public enum EventPhase
    {
        None,
        Capture = 1,
        Target = 2,
        Bubble = 4
    }
    public class UIEventBase
    {
        private IntPtr nativePtr = IntPtr.Zero;
        //
        // Summary:
        //     The ID of the element that triggered the event.
        public string targetElementId;
        public IntPtr targetElementPtr = IntPtr.Zero;

        //
        // Summary:
        //     The ID of the element that received the event.
        public string currentElementId;
        public IntPtr currentElementPtr = IntPtr.Zero;

        public EventPhase phase = EventPhase.None;
        //
        // Summary:
        //     The type of event that occurred (e.g., "click", "mousedown").
        public string eventType;

        // Note: Mouse and keyboard specific properties have been moved to derived event types
        // for better type safety and cleaner architecture.

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

        // Internal calls to C++ functions
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_stop_propagation(IntPtr nativePtr);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_stop_immediate_propagation(IntPtr nativePtr);
    }
    public delegate void UIEventCallback(UIEventBase ev);

    /// <summary>
    /// Global UI event manager that handles all UI event dispatching.
    /// Similar to ScriptComponentManager but for UI events.
    /// </summary>
    public static class UIEventManager
    {
         // Legacy event subscription storage for backward compatibility
         private static readonly Dictionary<UIElement, Dictionary<string, List<UIEventCallback>>> legacySubscriptions
             = new Dictionary<UIElement, Dictionary<string, List<UIEventCallback>>>();

         // Type-specific callback storage: Element -> EventType -> Type -> List<Delegates>
         private static readonly Dictionary<UIElement, Dictionary<string, Dictionary<Type, List<Delegate>>>> typedSubscriptions
             = new Dictionary<UIElement, Dictionary<string, Dictionary<Type, List<Delegate>>>>();

         private static bool isDispatching = false;
         private static readonly List<PendingSubscription> pendingSubscriptions = new List<PendingSubscription>();
         private static readonly List<PendingUnsubscription> pendingUnsubscriptions = new List<PendingUnsubscription>();
         private static readonly List<PendingTypedSubscription> pendingTypedSubscriptions = new List<PendingTypedSubscription>();
         private static readonly List<PendingTypedUnsubscription> pendingTypedUnsubscriptions = new List<PendingTypedUnsubscription>();

         private struct PendingSubscription
         {
             public UIElement elementWrapper;
             public string eventType;
             public UIEventCallback callback;
         }

         private struct PendingUnsubscription
         {
             public UIElement elementWrapper;
             public string eventType;
             public UIEventCallback callback;
         }

         private struct PendingTypedSubscription
         {
             public UIElement elementWrapper;
             public string eventType;
             public Type callbackType;
             public Delegate callback;
         }

         private struct PendingTypedUnsubscription
         {
             public UIElement elementWrapper;
             public string eventType;
             public Type callbackType;
             public Delegate callback;
         }

        /// <summary>
        /// Subscribe to a UI event. This is called by UIElement.AddEventListener.
        /// </summary>
        /// <param name="elementWrapper">The UIElement instance</param>
        /// <param name="eventType">The type of event (e.g., "click")</param>
        /// <param name="callback">The callback to invoke</param>
        public static void Subscribe(UIElement elementWrapper, string eventType, UIEventCallback callback)
        {
            if (callback == null || elementWrapper == null) return;

            if (isDispatching)
            {
                // Defer subscription during event dispatch
                pendingSubscriptions.Add(new PendingSubscription
                {
                    elementWrapper = elementWrapper,
                    eventType = eventType,
                    callback = callback
                });
                return;
            }

            // Ensure the nested dictionaries exist
            if (!legacySubscriptions.ContainsKey(elementWrapper))
            {
                legacySubscriptions[elementWrapper] = new Dictionary<string, List<UIEventCallback>>();
            }

            if (!legacySubscriptions[elementWrapper].ContainsKey(eventType))
            {
                legacySubscriptions[elementWrapper][eventType] = new List<UIEventCallback>();
            }

            // Add the callback
            legacySubscriptions[elementWrapper][eventType].Add(callback);

            // Ensure the C++ side has an event listener attached to this element
            // Pass the wrapper's native pointer directly
            EnsureNativeEventListener(elementWrapper, eventType);
        }

        /// <summary>
        /// Unsubscribe from a UI event.
        /// </summary>
        /// <param name="elementWrapper">The UIElement instance</param>
        /// <param name="eventType">The type of event</param>
        /// <param name="callback">The callback to remove</param>
        /// <returns>True if the callback was found and removed</returns>
        public static bool Unsubscribe(UIElement elementWrapper, string eventType, UIEventCallback callback)
        {
            if (callback == null || elementWrapper == null) return false;

            if (isDispatching)
            {
                // Defer unsubscription during event dispatch
                pendingUnsubscriptions.Add(new PendingUnsubscription
                {
                    elementWrapper = elementWrapper,
                    eventType = eventType,
                    callback = callback
                });
                return true; // Assume it will be removed
            }

            // Navigate to the callback list
            if (!legacySubscriptions.ContainsKey(elementWrapper) ||
                !legacySubscriptions[elementWrapper].ContainsKey(eventType))
            {
                return false;
            }

            var callbacks = legacySubscriptions[elementWrapper][eventType];
            bool removed = callbacks.Remove(callback);

            // Clean up empty collections
            if (callbacks.Count == 0)
            {
                legacySubscriptions[elementWrapper].Remove(eventType);

                if (legacySubscriptions[elementWrapper].Count == 0)
                {
                    legacySubscriptions.Remove(elementWrapper);
                }
            }

            return removed;
        }

        /// <summary>
        /// Unsubscribe all callbacks from a specific UIElement.
        /// This removes the element from both legacy and typed subscription dictionaries.
        /// </summary>
        /// <param name="elementWrapper">The UIElement to unsubscribe from</param>
        /// <returns>True if any subscriptions were removed</returns>
        public static bool UnsubscribeAll(UIElement elementWrapper)
        {
            if (elementWrapper == null) return false;

            if (isDispatching)
            {
                // Cannot defer this operation safely, so we skip it during dispatch
                Log.Warning("UnsubscribeAll called during event dispatch - operation ignored");
                return false;
            }

            bool hadLegacy = legacySubscriptions.Remove(elementWrapper);
            bool hadTyped = typedSubscriptions.Remove(elementWrapper);

            return hadLegacy || hadTyped;
        }

        /// <summary>
        /// Dispatch a UI event. This is called from C++ when an event occurs.
        /// </summary>
        /// <param name="eventData">The event data</param>
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void DispatchEvent(UIEventBase ev);

        /// <summary>
        /// Internal method called by C++ to dispatch events to managed callbacks.
        /// </summary>
        /// <param name="ev">The event data</param>
        internal static void InternalDispatchEvent(UIEventBase ev)
        {
            isDispatching = true;
            try
            {
                // Find the wrapper that matches the current element ID
                UIElement targetWrapper = FindTargetWrapper(ev);
                
                if (targetWrapper != null)
                {
                    // Dispatch to typed callbacks first (zero casting!)
                    DispatchTypedCallbacks(targetWrapper, ev);
                    
                    // Then dispatch to legacy callbacks for backward compatibility
                    DispatchLegacyCallbacks(targetWrapper, ev);
                }
            }
            finally
            {
                isDispatching = false;
            }

            // Process pending subscriptions and unsubscriptions
            ProcessPendingOperations();
        }

        private static UIElement FindTargetWrapper(UIEventBase ev)
        {
            // Check both subscription dictionaries for the wrapper
            foreach (var wrapper in legacySubscriptions.Keys)
            {
                if (wrapper.GetNativePointer() == ev.currentElementPtr)
                {
                    return wrapper;
                }
            }
            
            foreach (var wrapper in typedSubscriptions.Keys)
            {
                if (wrapper.GetNativePointer() == ev.currentElementPtr)
                {
                    return wrapper;
                }
            }
            
            return null;
        }

        private static void DispatchTypedCallbacks(UIElement targetWrapper, UIEventBase ev)
        {
            if (!typedSubscriptions.ContainsKey(targetWrapper) ||
                !typedSubscriptions[targetWrapper].ContainsKey(ev.eventType))
            {
                return;
            }

            var eventTypeDict = typedSubscriptions[targetWrapper][ev.eventType];
            var actualEventType = ev.GetType();

            // Direct dispatch - no casting because we stored Action<T> directly
            if (eventTypeDict.ContainsKey(actualEventType))
            {
                var callbacks = eventTypeDict[actualEventType];
                foreach (var callback in callbacks)
                {
                    try
                    {
                        // Direct invocation - callback is already Action<T> where T matches ev's actual type
                        callback.DynamicInvoke(ev);
                    }
                    catch (Exception ex)
                    {
                        Log.Error($"Error in typed UI event callback for {ev.eventType} on {ev.currentElementId}: {ex}");
                    }
                }
            }
        }

        private static void DispatchLegacyCallbacks(UIElement targetWrapper, UIEventBase ev)
        {
            if (!legacySubscriptions.ContainsKey(targetWrapper) ||
                !legacySubscriptions[targetWrapper].ContainsKey(ev.eventType))
            {
                return;
            }

            var callbacks = legacySubscriptions[targetWrapper][ev.eventType];

            // Invoke all legacy callbacks for this event
            foreach (var callback in callbacks)
            {
                try
                {
                    callback?.Invoke(ev);
                }
                catch (Exception ex)
                {
                    Log.Error($"Error in legacy UI event callback for {ev.eventType} on {ev.currentElementId}: {ex}");
                }
            }
        }

        private static void ProcessPendingOperations()
        {
            // Process pending legacy subscriptions
            foreach (var pending in pendingSubscriptions)
            {
                Subscribe(pending.elementWrapper, pending.eventType, pending.callback);
            }
            pendingSubscriptions.Clear();

            // Process pending legacy unsubscriptions
            foreach (var pending in pendingUnsubscriptions)
            {
                Unsubscribe(pending.elementWrapper, pending.eventType, pending.callback);
            }
            pendingUnsubscriptions.Clear();

            // Process pending typed subscriptions
            foreach (var pending in pendingTypedSubscriptions)
            {
                SubscribeTypedInternal(pending.elementWrapper, pending.eventType, pending.callbackType, pending.callback);
            }
            pendingTypedSubscriptions.Clear();

            // Process pending typed unsubscriptions
            foreach (var pending in pendingTypedUnsubscriptions)
            {
                UnsubscribeTypedInternal(pending.elementWrapper, pending.eventType, pending.callbackType, pending.callback);
            }
            pendingTypedUnsubscriptions.Clear();
        }

        /// <summary>
        /// Ensure that the C++ side has a native event listener for this event type.
        /// </summary>
        private static void EnsureNativeEventListener(UIElement elementWrapper, string eventType)
        {
            // Call C++ to ensure an event listener is attached to the element
            // Pass the wrapper's native pointer directly for efficiency
            internal_m2n_ui_ensure_native_event_listener(elementWrapper.GetNativePointer(), eventType);
        }

        /// <summary>
        /// Get the number of active subscriptions (for debugging/monitoring).
        /// </summary>
        public static int GetSubscriptionCount()
        {
            int count = 0;
            
            // Count legacy subscriptions
            foreach (var elementSubs in legacySubscriptions.Values)
            {
                foreach (var eventCallbacks in elementSubs.Values)
                {
                    count += eventCallbacks.Count;
                }
            }
            
            // Count typed subscriptions
            foreach (var elementSubs in typedSubscriptions.Values)
            {
                foreach (var eventTypes in elementSubs.Values)
                {
                    foreach (var typeCallbacks in eventTypes.Values)
                    {
                        count += typeCallbacks.Count;
                    }
                }
            }
            
            return count;
        }

        /// <summary>
        /// Subscribe to a typed UI event with compile-time type safety and zero runtime casting.
        /// </summary>
        public static void Subscribe<T>(UIElement elementWrapper, string eventType, Action<T> callback) where T : UIEventBase
        {
            if (callback == null || elementWrapper == null) return;

            if (isDispatching)
            {
                // Defer subscription during event dispatch
                pendingTypedSubscriptions.Add(new PendingTypedSubscription
                {
                    elementWrapper = elementWrapper,
                    eventType = eventType,
                    callbackType = typeof(T),
                    callback = callback
                });
                return;
            }

            SubscribeTypedInternal(elementWrapper, eventType, typeof(T), callback);
        }

        /// <summary>
        /// Unsubscribe from a typed UI event.
        /// </summary>
        public static bool Unsubscribe<T>(UIElement elementWrapper, string eventType, Action<T> callback) where T : UIEventBase
        {
            if (callback == null || elementWrapper == null) return false;

            if (isDispatching)
            {
                // Defer unsubscription during event dispatch
                pendingTypedUnsubscriptions.Add(new PendingTypedUnsubscription
                {
                    elementWrapper = elementWrapper,
                    eventType = eventType,
                    callbackType = typeof(T),
                    callback = callback
                });
                return true; // Assume it will be removed
            }

            return UnsubscribeTypedInternal(elementWrapper, eventType, typeof(T), callback);
        }

        private static void SubscribeTypedInternal(UIElement elementWrapper, string eventType, Type callbackType, Delegate callback)
        {
            // Ensure nested dictionaries exist
            if (!typedSubscriptions.ContainsKey(elementWrapper))
            {
                typedSubscriptions[elementWrapper] = new Dictionary<string, Dictionary<Type, List<Delegate>>>();
            }

            if (!typedSubscriptions[elementWrapper].ContainsKey(eventType))
            {
                typedSubscriptions[elementWrapper][eventType] = new Dictionary<Type, List<Delegate>>();
            }

            var eventTypeDict = typedSubscriptions[elementWrapper][eventType];

            if (!eventTypeDict.ContainsKey(callbackType))
            {
                eventTypeDict[callbackType] = new List<Delegate>();
            }

            // Store the callback directly as Action<T> - no wrapper needed!
            eventTypeDict[callbackType].Add(callback);

            // Ensure the C++ side has an event listener attached to this element
            EnsureNativeEventListener(elementWrapper, eventType);
        }

        private static bool UnsubscribeTypedInternal(UIElement elementWrapper, string eventType, Type callbackType, Delegate callback)
        {
            // Navigate to the callback list
            if (!typedSubscriptions.ContainsKey(elementWrapper) ||
                !typedSubscriptions[elementWrapper].ContainsKey(eventType) ||
                !typedSubscriptions[elementWrapper][eventType].ContainsKey(callbackType))
            {
                return false;
            }

            var callbacks = typedSubscriptions[elementWrapper][eventType][callbackType];
            bool removed = callbacks.Remove(callback);

			Log.Info($"UIEventManager: Unsubscribed from {eventType} event on {elementWrapper.ElementId}");

            // Clean up empty collections
            if (callbacks.Count == 0)
            {
                typedSubscriptions[elementWrapper][eventType].Remove(callbackType);

				Log.Info($"UIEventManager: Removed {callbackType.Name} callback from {eventType} event on {elementWrapper.ElementId}");

                if (typedSubscriptions[elementWrapper][eventType].Count == 0)
                {
					Log.Info($"UIEventManager: Removed {eventType} event from {elementWrapper.ElementId}");

                    typedSubscriptions[elementWrapper].Remove(eventType);

                    if (typedSubscriptions[elementWrapper].Count == 0)
                    {
						Log.Info($"UIEventManager: Removed {elementWrapper.ElementId} from UIEventManager");

                        typedSubscriptions.Remove(elementWrapper);
                    }
                }
            }

            return removed;
        }

        /// <summary>
        /// Get subscription info for debugging.
        /// </summary>
        public static string GetSubscriptionInfo()
        {
            var info = $"Total subscriptions: {GetSubscriptionCount()}\n";
            info += $"Elements with legacy subscriptions: {legacySubscriptions.Count}\n";
            info += $"Elements with typed subscriptions: {typedSubscriptions.Count}\n";

            foreach (var elementKvp in legacySubscriptions)
            {
                var elementWrapper = elementKvp.Key;
                var elementSubs = elementKvp.Value;
                var elementId = elementWrapper.IsValid() ? elementWrapper.ElementId : "[INVALID]";
                info += $"  Legacy Element '{elementId}': {elementSubs.Count} event types\n";

                foreach (var eventKvp in elementSubs)
                {
                    var eventType = eventKvp.Key;
                    var callbacks = eventKvp.Value;
                    info += $"    Event '{eventType}': {callbacks.Count} callbacks\n";
                }
            }

            foreach (var elementKvp in typedSubscriptions)
            {
                var elementWrapper = elementKvp.Key;
                var elementSubs = elementKvp.Value;
                var elementId = elementWrapper.IsValid() ? elementWrapper.ElementId : "[INVALID]";
                info += $"  Typed Element '{elementId}': {elementSubs.Count} event types\n";

                foreach (var eventKvp in elementSubs)
                {
                    var eventType = eventKvp.Key;
                    var typeCallbacks = eventKvp.Value;
                    foreach (var typeKvp in typeCallbacks)
                    {
                        var callbackType = typeKvp.Key;
                        var callbacks = typeKvp.Value;
                        info += $"    Event '{eventType}' ({callbackType.Name}): {callbacks.Count} callbacks\n";
                    }
                }
            }

            return info;
        }

        // Internal call to ensure native event listener
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_ensure_native_event_listener(IntPtr elementPtr, string eventType);
    }
}
