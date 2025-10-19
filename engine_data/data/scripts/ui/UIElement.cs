using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Unravel.Core
{

    /// <summary>
    /// Represents a wrapper around a native RmlUi element with managed lifetime.
    /// The C++ side owns the lifetime and will invalidate this wrapper when the element is destroyed.
    /// </summary>
    public class UIElement : NativeObject
    {
        // Internal pointer to the C++ Rml::Element - managed by C++
        private IntPtr nativePtr = IntPtr.Zero;

        // Entity that owns this UI element
        private readonly Entity ownerEntity;

        /// <summary>
        /// Gets whether this wrapper still points to a valid native element.
        /// </summary>
        public override bool IsValid()
        {
            return nativePtr != IntPtr.Zero && internal_m2n_ui_element_wrapper_is_valid(nativePtr, ownerEntity);
        }

        /// <summary>
        /// Gets the entity that owns this UI element.
        /// </summary>
        public Entity Owner => ownerEntity;

        /// <summary>
        /// Gets the element ID.
        /// </summary>
        public string ElementId => IsValid() ? internal_m2n_ui_element_wrapper_get_id(nativePtr, ownerEntity) : "";

        /// <summary>
        /// Internal constructor - only called from C++ side.
        /// </summary>
        internal UIElement(IntPtr ptr, Entity owner)
        {
            nativePtr = ptr;
            ownerEntity = owner;
        }

        /// <summary>
        /// Called by C++ when the native element is destroyed to invalidate this wrapper.
        /// </summary>
        internal void Invalidate()
        {
            nativePtr = IntPtr.Zero;
        }

        /// <summary>
        /// Gets the native pointer for internal use by the event system.
        /// </summary>
        internal IntPtr GetNativePointer()
        {
            return nativePtr;
        }

        // ==== Element Properties ====

        /// <summary>
        /// Gets or sets the inner RML content of the element.
        /// </summary>
        public string InnerRml
        {
            get
            {
                ValidateAndThrow();
                return internal_m2n_ui_element_wrapper_get_inner_rml(nativePtr, ownerEntity);
            }
            set
            {
                ValidateAndThrow();
                internal_m2n_ui_element_wrapper_set_inner_rml(nativePtr, ownerEntity, value ?? "");
            }
        }

        /// <summary>
        /// Gets or sets whether the element is visible.
        /// </summary>
        public bool IsVisible
        {
            get
            {
                ValidateAndThrow();
                return internal_m2n_ui_element_wrapper_is_visible(nativePtr, ownerEntity);
            }
            set
            {
                ValidateAndThrow();
                internal_m2n_ui_element_wrapper_set_visible(nativePtr, ownerEntity, value);
            }
        }

        // ==== Element Methods ====

        /// <summary>
        /// Gets the value of an attribute.
        /// </summary>
        /// <param name="attributeName">The name of the attribute.</param>
        /// <returns>The attribute value, or empty string if not found.</returns>
        public string GetAttribute(string attributeName)
        {
            ValidateAndThrow();
            return internal_m2n_ui_element_wrapper_get_attribute(nativePtr, ownerEntity, attributeName);
        }

        /// <summary>
        /// Sets the value of an attribute.
        /// </summary>
        /// <param name="attributeName">The name of the attribute.</param>
        /// <param name="value">The value to set.</param>
        public void SetAttribute(string attributeName, string value)
        {
            ValidateAndThrow();
            internal_m2n_ui_element_wrapper_set_attribute(nativePtr, ownerEntity, attributeName, value ?? "");
        }

        /// <summary>
        /// Removes an attribute from the element.
        /// </summary>
        /// <param name="attributeName">The name of the attribute to remove.</param>
        public void RemoveAttribute(string attributeName)
        {
            ValidateAndThrow();
            internal_m2n_ui_element_wrapper_remove_attribute(nativePtr, ownerEntity, attributeName);
        }

        /// <summary>
        /// Checks if the element has a specific attribute.
        /// </summary>
        /// <param name="attributeName">The name of the attribute to check.</param>
        /// <returns>True if the attribute exists; otherwise, false.</returns>
        public bool HasAttribute(string attributeName)
        {
            ValidateAndThrow();
            return internal_m2n_ui_element_wrapper_has_attribute(nativePtr, ownerEntity, attributeName);
        }

        /// <summary>
        /// Sets or removes a CSS class on the element.
        /// </summary>
        /// <param name="className">The name of the CSS class.</param>
        /// <param name="activate">True to add the class, false to remove it.</param>
        public void SetClass(string className, bool activate)
        {
            ValidateAndThrow();
            internal_m2n_ui_element_wrapper_set_class(nativePtr, ownerEntity, className, activate);
        }

        /// <summary>
        /// Checks if the element has a specific CSS class.
        /// </summary>
        /// <param name="className">The name of the CSS class to check.</param>
        /// <returns>True if the class is set; otherwise, false.</returns>
        public bool IsClassSet(string className)
        {
            ValidateAndThrow();
            return internal_m2n_ui_element_wrapper_is_class_set(nativePtr, ownerEntity, className);
        }

        /// <summary>
        /// Gives focus to this element.
        /// </summary>
        public void Focus()
        {
            ValidateAndThrow();
            internal_m2n_ui_element_wrapper_focus(nativePtr, ownerEntity);
        }

        /// <summary>
        /// Removes focus from this element.
        /// </summary>
        public void Blur()
        {
            ValidateAndThrow();
            internal_m2n_ui_element_wrapper_blur(nativePtr, ownerEntity);
        }

        /// <summary>
        /// Simulates a click on this element.
        /// </summary>
        public void Click()
        {
            ValidateAndThrow();
            internal_m2n_ui_element_wrapper_click(nativePtr, ownerEntity);
        }

        /// <summary>
        /// Scrolls the element into view.
        /// </summary>
        /// <param name="alignWithTop">If true, align with the top of the viewport; otherwise, align with the bottom.</param>
        public void ScrollIntoView(bool alignWithTop = true)
        {
            ValidateAndThrow();
            internal_m2n_ui_element_wrapper_scroll_into_view(nativePtr, ownerEntity, alignWithTop);
        }

        // ==== Event Handling ====

        /// <summary>
        /// Adds an event listener to this element.
        /// </summary>
        /// <param name="eventType">The type of event to listen for.</param>
        /// <param name="callback">The callback to invoke when the event occurs.</param>
        public void RegisterCallback(string eventType, UIEventCallback callback)
        {
            ValidateAndThrow();
            UIEventManager.Subscribe(this, eventType, callback);
        }

        /// <summary>
        /// Removes an event listener from this element.
        /// </summary>
        /// <param name="eventType">The type of event.</param>
        /// <param name="callback">The callback to remove.</param>
        /// <returns>True if the listener was removed successfully.</returns>
        public bool UnregisterCallback(string eventType, UIEventCallback callback)
        {
            ValidateAndThrow();
            return UIEventManager.Unsubscribe(this, eventType, callback);
        }

        /// <summary>
        /// Registers a typed event callback with compile-time type safety and zero runtime casting.
        /// </summary>
        /// <typeparam name="T">The specific event type (e.g., UIKeyEvent, UIPointerEvent)</typeparam>
        /// <param name="eventType">The type of event to listen for.</param>
        /// <param name="callback">The callback to invoke when the event occurs.</param>
        public void RegisterCallback<T>(string eventType, Action<T> callback) where T : UIEventBase
        {
            ValidateAndThrow();
            UIEventManager.Subscribe<T>(this, eventType, callback);
        }

        /// <summary>
        /// Unregisters a typed event callback.
        /// </summary>
        /// <typeparam name="T">The specific event type</typeparam>
        /// <param name="eventType">The type of event.</param>
        /// <param name="callback">The callback to remove.</param>
        /// <returns>True if the callback was found and removed.</returns>
        public bool UnregisterCallback<T>(string eventType, Action<T> callback) where T : UIEventBase
        {
            ValidateAndThrow();
            return UIEventManager.Unsubscribe<T>(this, eventType, callback);
        }

        // ==== Helper Methods ====

        /// <summary>
        /// Validates that this wrapper is still valid and throws an exception if not.
        /// </summary>
        private void ValidateAndThrow()
        {
            if (!IsValid())
            {
                throw new InvalidOperationException($"UIElement for '{ElementId}' is no longer valid. The native element has been destroyed.");
            }
        }

        /// <summary>
        /// Returns a string representation of this UI element wrapper.
        /// </summary>
        public override string ToString()
        {
            if (IsValid())
            {
                return $"UIElement('{ElementId}', Entity: {ownerEntity})";
            }
            else
            {
                return $"UIElement(INVALID)";
            }
        }

        // ==== Internal Calls ====

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_element_wrapper_is_valid(IntPtr elementPtr, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_element_wrapper_get_inner_rml(IntPtr elementPtr, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_set_inner_rml(IntPtr elementPtr, Entity ownerEntity, string rml);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_element_wrapper_is_visible(IntPtr elementPtr, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_set_visible(IntPtr elementPtr, Entity ownerEntity, bool visible);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_element_wrapper_get_attribute(IntPtr elementPtr, Entity ownerEntity, string attributeName);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_set_attribute(IntPtr elementPtr, Entity ownerEntity, string attributeName, string value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_remove_attribute(IntPtr elementPtr, Entity ownerEntity, string attributeName);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_element_wrapper_has_attribute(IntPtr elementPtr, Entity ownerEntity, string attributeName);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_set_class(IntPtr elementPtr, Entity ownerEntity, string className, bool activate);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_element_wrapper_is_class_set(IntPtr elementPtr, Entity ownerEntity, string className);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_focus(IntPtr elementPtr, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_blur(IntPtr elementPtr, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_click(IntPtr elementPtr, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_scroll_into_view(IntPtr elementPtr, Entity ownerEntity, bool alignWithTop);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_element_wrapper_get_id(IntPtr elementPtr, Entity ownerEntity);
    }
}
