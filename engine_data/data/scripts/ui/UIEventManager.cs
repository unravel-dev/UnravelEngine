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

        //
        // Summary:
        //     Mouse X coordinate (for mouse events).
        public float mouseX = 0.0f;

        //
        // Summary:
        //     Mouse Y coordinate (for mouse events).
        public float mouseY = 0.0f;

        //
        // Summary:
        //     Key code (for keyboard events).
        public int keyCode = 0;

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
         // Event subscription storage: ElementWrapper -> EventType -> List<Callbacks>
         private static readonly Dictionary<UIElement, Dictionary<string, List<UIEventCallback>>> subscriptions
             = new Dictionary<UIElement, Dictionary<string, List<UIEventCallback>>>();

         private static bool isDispatching = false;
         private static readonly List<PendingSubscription> pendingSubscriptions = new List<PendingSubscription>();
         private static readonly List<PendingUnsubscription> pendingUnsubscriptions = new List<PendingUnsubscription>();

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
            if (!subscriptions.ContainsKey(elementWrapper))
            {
                subscriptions[elementWrapper] = new Dictionary<string, List<UIEventCallback>>();
            }

            if (!subscriptions[elementWrapper].ContainsKey(eventType))
            {
                subscriptions[elementWrapper][eventType] = new List<UIEventCallback>();
            }

            // Add the callback
            subscriptions[elementWrapper][eventType].Add(callback);

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
            if (!subscriptions.ContainsKey(elementWrapper) ||
                !subscriptions[elementWrapper].ContainsKey(eventType))
            {
                return false;
            }

            var callbacks = subscriptions[elementWrapper][eventType];
            bool removed = callbacks.Remove(callback);

            // Clean up empty collections
            if (callbacks.Count == 0)
            {
                subscriptions[elementWrapper].Remove(eventType);

                if (subscriptions[elementWrapper].Count == 0)
                {
                    subscriptions.Remove(elementWrapper);
                }
            }

            return removed;
        }

        /// <summary>
        /// Remove all event subscriptions for a specific element.
        /// </summary>
        /// <param name="elementWrapper">The UIElement to clean up</param>
        public static void ClearElement(UIElement elementWrapper)
        {
            if (elementWrapper == null) return;

            if (isDispatching)
            {
                return; // Defer cleanup
            }

            subscriptions.Remove(elementWrapper);
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
                UIElement targetWrapper = null;
                foreach (var wrapper in subscriptions.Keys)
                {
                    if (wrapper.GetNativePointer() == ev.currentElementPtr)
                    {
                        targetWrapper = wrapper;
                        break;
                    }
                }
                // Find callbacks for this event using the wrapper
                if (targetWrapper != null &&
                    subscriptions[targetWrapper].ContainsKey(ev.eventType))
                {
                    var callbacks = subscriptions[targetWrapper][ev.eventType];

                    // Invoke all callbacks for this event
                    foreach (var callback in callbacks)
                    {
                        try
                        {
                            callback?.Invoke(ev);
                        }
                        catch (Exception ex)
                        {
                            Log.Error($"Error in UI event callback for {ev.eventType} on {ev.currentElementId}: {ex}");
                        }
                    }
                }
            }
            finally
            {
                isDispatching = false;
            }

            // Process pending subscriptions and unsubscriptions
            ProcessPendingOperations();
        }

        private static void ProcessPendingOperations()
        {
            // Process pending subscriptions
            foreach (var pending in pendingSubscriptions)
            {
                Subscribe(pending.elementWrapper, pending.eventType, pending.callback);
            }
            pendingSubscriptions.Clear();

            // Process pending unsubscriptions
            foreach (var pending in pendingUnsubscriptions)
            {
                Unsubscribe(pending.elementWrapper, pending.eventType, pending.callback);
            }
            pendingUnsubscriptions.Clear();
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
            foreach (var elementSubs in subscriptions.Values)
            {
                foreach (var eventCallbacks in elementSubs.Values)
                {
                    count += eventCallbacks.Count;
                }
            }
            return count;
        }

        /// <summary>
        /// Get subscription info for debugging.
        /// </summary>
        public static string GetSubscriptionInfo()
        {
            var info = $"Total subscriptions: {GetSubscriptionCount()}\n";
            info += $"Elements with subscriptions: {subscriptions.Count}\n";

            foreach (var elementKvp in subscriptions)
            {
                var elementWrapper = elementKvp.Key;
                var elementSubs = elementKvp.Value;
                var elementId = elementWrapper.IsValid() ? elementWrapper.ElementId : "[INVALID]";
                info += $"  Element '{elementId}': {elementSubs.Count} event types\n";

                foreach (var eventKvp in elementSubs)
                {
                    var eventType = eventKvp.Key;
                    var callbacks = eventKvp.Value;
                    info += $"    Event '{eventType}': {callbacks.Count} callbacks\n";
                }
            }

            return info;
        }

        // Internal call to ensure native event listener
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_ensure_native_event_listener(IntPtr elementPtr, string eventType);
    }
}
