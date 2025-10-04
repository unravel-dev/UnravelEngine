using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Represents a wrapper around a native RmlUi document with managed lifetime.
    /// The C++ side owns the lifetime and will invalidate this wrapper when the document is destroyed.
    /// </summary>
    public class UIDocument : NativeObject
    {
        // Internal pointer to the C++ Rml::ElementDocument - managed by C++
        private IntPtr nativePtr = IntPtr.Zero;
        
        // Entity that owns this UI document
        private readonly Entity ownerEntity;

        /// <summary>
        /// Gets whether this wrapper still points to a valid native document.
        /// </summary>
        public override bool IsValid()
        {
            return nativePtr != IntPtr.Zero && internal_m2n_ui_document_wrapper_is_valid(nativePtr);
        }
        /// <summary>
        /// Gets the entity that owns this UI document.
        /// </summary>
        public Entity Owner => ownerEntity;

        /// <summary>
        /// Internal constructor - only called from C++ side.
        /// </summary>
        internal UIDocument(IntPtr ptr, Entity owner)
        {
            nativePtr = ptr;
            ownerEntity = owner;
        }

        /// <summary>
        /// Called by C++ when the native document is destroyed to invalidate this wrapper.
        /// </summary>
        internal void Invalidate()
        {
            nativePtr = IntPtr.Zero;
        }

        // ==== Document Properties ====

        /// <summary>
        /// Gets or sets the title of the document.
        /// </summary>
        public string Title
        {
            get
            {
                ValidateAndThrow();
                return internal_m2n_ui_document_wrapper_get_title(nativePtr);
            }
            set
            {
                ValidateAndThrow();
                internal_m2n_ui_document_wrapper_set_title(nativePtr, value ?? "");
            }
        }

        /// <summary>
        /// Gets whether the document is currently visible.
        /// </summary>
        public bool IsVisible
        {
            get
            {
                ValidateAndThrow();
                return internal_m2n_ui_document_wrapper_is_visible(nativePtr);
            }
        }

        // ==== Document Methods ====

        /// <summary>
        /// Shows the document.
        /// </summary>
        public void Show()
        {
            ValidateAndThrow();
            internal_m2n_ui_document_wrapper_show(nativePtr);
        }

        /// <summary>
        /// Hides the document.
        /// </summary>
        public void Hide()
        {
            ValidateAndThrow();
            internal_m2n_ui_document_wrapper_hide(nativePtr);
        }

        /// <summary>
        /// Closes the document and removes it from the context.
        /// After calling this, the wrapper will become invalid.
        /// </summary>
        public void Close()
        {
            ValidateAndThrow();
            internal_m2n_ui_document_wrapper_close(nativePtr);
            // The C++ side will call Invalidate() after closing
        }

        // ==== Element Query Methods ====

        /// <summary>
        /// Gets an element wrapper by its ID.
        /// </summary>
        /// <param name="elementId">The ID of the element to find.</param>
        /// <returns>A UIElement if found; otherwise, null.</returns>
        public UIElement GetElementById(string elementId)
        {
            ValidateAndThrow();
            var elementPtr = internal_m2n_ui_document_wrapper_get_element_by_id(nativePtr, elementId);
            if (elementPtr == IntPtr.Zero)
            {
                return null;
            }
            return new UIElement(elementPtr, ownerEntity);
        }

        /// <summary>
        /// Gets the first element that matches the specified CSS selector.
        /// </summary>
        /// <param name="selector">The CSS selector to match against.</param>
        /// <returns>A UIElement if found; otherwise, null.</returns>
        public UIElement QuerySelector(string selector)
        {
            ValidateAndThrow();
            var elementPtr = internal_m2n_ui_document_wrapper_query_selector(nativePtr, selector);
            if (elementPtr == IntPtr.Zero)
            {
                return null;
            }
            return new UIElement(elementPtr, ownerEntity);
        }

        // ==== Helper Methods ====

        /// <summary>
        /// Validates that this wrapper is still valid and throws an exception if not.
        /// </summary>
        private void ValidateAndThrow()
        {
            if (!IsValid())
            {
                throw new InvalidOperationException($"UIDocument for entity {ownerEntity} is no longer valid. The native document has been destroyed.");
            }
        }

        /// <summary>
        /// Returns a string representation of this UI document wrapper.
        /// </summary>
        public override string ToString()
        {
            if (IsValid())
            {
                return $"UIDocument(Entity: {ownerEntity}, Title: '{Title}')";
            }
            else
            {
                return $"UIDocument(Entity: {ownerEntity}, INVALID)";
            }
        }

        // ==== Internal Calls ====

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_document_wrapper_is_valid(IntPtr documentPtr);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_document_wrapper_get_title(IntPtr documentPtr);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_wrapper_set_title(IntPtr documentPtr, string title);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_document_wrapper_is_visible(IntPtr documentPtr);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_wrapper_show(IntPtr documentPtr);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_wrapper_hide(IntPtr documentPtr);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_wrapper_close(IntPtr documentPtr);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr internal_m2n_ui_document_wrapper_get_element_by_id(IntPtr documentPtr, string elementId);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr internal_m2n_ui_document_wrapper_query_selector(IntPtr documentPtr, string selector);
    }
}
