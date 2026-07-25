using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Managed wrapper around a native RmlUi document (<c>Rml::ElementDocument</c>).
    /// Each <see cref="UIDocumentComponent"/> owns its own RmlUi context and document.
    /// Wrappers are cached by native pointer so repeated queries return the same instance.
    /// </summary>
    public class UIDocument : NativeObject
    {
        private static readonly Dictionary<IntPtr, UIDocument> s_cache = new Dictionary<IntPtr, UIDocument>();

        private IntPtr nativePtr = IntPtr.Zero;
        private readonly Entity ownerEntity;

        /// <summary>
        /// Gets whether this wrapper still points to a valid native document.
        /// </summary>
        public override bool IsValid()
        {
            return nativePtr != IntPtr.Zero && internal_m2n_ui_document_wrapper_is_valid(nativePtr, ownerEntity);
        }

        /// <summary>
        /// Gets the entity that owns this UI document.
        /// </summary>
        public Entity Owner => ownerEntity;

        /// <summary>
        /// Internal constructor - use <see cref="GetOrCreate"/>.
        /// </summary>
        internal UIDocument(IntPtr ptr, Entity owner)
        {
            nativePtr = ptr;
            ownerEntity = owner;
        }

        /// <summary>
        /// Returns a cached document wrapper for the native pointer, creating one if needed.
        /// </summary>
        /// <param name="ptr">Native document pointer.</param>
        /// <param name="owner">Owning entity.</param>
        /// <returns>Canonical wrapper, or null when <paramref name="ptr"/> is zero.</returns>
        internal static UIDocument GetOrCreate(IntPtr ptr, Entity owner)
        {
            if (ptr == IntPtr.Zero)
            {
                return null;
            }
            if (s_cache.TryGetValue(ptr, out UIDocument existing))
            {
                if (existing.nativePtr == ptr && existing.ownerEntity == owner)
                {
                    return existing;
                }
                existing.Invalidate();
                s_cache.Remove(ptr);
            }
            UIDocument document = new UIDocument(ptr, owner);
            s_cache[ptr] = document;
            return document;
        }

        /// <summary>
        /// Invalidates and removes all cached document wrappers for the given owner entity.
        /// </summary>
        /// <param name="owner">Entity whose document wrappers should be invalidated.</param>
        internal static void InvalidateForOwner(Entity owner)
        {
            List<IntPtr> toRemove = null;
            foreach (KeyValuePair<IntPtr, UIDocument> kvp in s_cache)
            {
                if (kvp.Value.ownerEntity == owner)
                {
                    if (toRemove == null)
                    {
                        toRemove = new List<IntPtr>();
                    }
                    toRemove.Add(kvp.Key);
                }
            }
            if (toRemove == null)
            {
                return;
            }
            foreach (IntPtr ptr in toRemove)
            {
                if (s_cache.TryGetValue(ptr, out UIDocument document))
                {
                    document.Invalidate();
                    s_cache.Remove(ptr);
                }
            }
        }

        /// <summary>
        /// Clears the document wrapper cache (domain unload).
        /// </summary>
        internal static void ClearCache()
        {
            s_cache.Clear();
        }

        /// <summary>
        /// Called when the native document is closed or destroyed.
        /// </summary>
        internal void Invalidate()
        {
            if (nativePtr != IntPtr.Zero)
            {
                s_cache.Remove(nativePtr);
            }
            nativePtr = IntPtr.Zero;
        }

        /// <summary>
        /// Gets or sets the title of the document.
        /// </summary>
        public string Title
        {
            get
            {
                return internal_m2n_ui_document_wrapper_get_title(nativePtr, ownerEntity);
            }
            set
            {
                internal_m2n_ui_document_wrapper_set_title(nativePtr, ownerEntity, value ?? "");
            }
        }

        /// <summary>
        /// Gets whether the document is currently visible.
        /// </summary>
        public bool IsVisible
        {
            get
            {
                return internal_m2n_ui_document_wrapper_is_visible(nativePtr, ownerEntity);
            }
        }

        /// <summary>
        /// Shows the document.
        /// </summary>
        public void Show()
        {
            internal_m2n_ui_document_wrapper_show(nativePtr, ownerEntity);
        }

        /// <summary>
        /// Hides the document.
        /// </summary>
        public void Hide()
        {
            internal_m2n_ui_document_wrapper_hide(nativePtr, ownerEntity);
        }

        /// <summary>
        /// Closes the document and removes it from the context.
        /// Invalidates this wrapper, clears related element wrappers, and drops event subscriptions.
        /// </summary>
        public void Close()
        {
            IntPtr ptr = nativePtr;
            if (ptr == IntPtr.Zero)
            {
                return;
            }
            // Drop managed + native listeners while the document is still alive.
            UIEventManager.UnsubscribeAllForOwner(ownerEntity, invalidateWrappers: true);
            internal_m2n_ui_document_wrapper_close(ptr, ownerEntity);
            Invalidate();
        }

        /// <summary>
        /// Gets an element wrapper by its ID.
        /// </summary>
        /// <param name="elementId">The ID of the element to find.</param>
        /// <returns>A UIElement if found; otherwise, null.</returns>
        public UIElement GetElementById(string elementId)
        {
            IntPtr elementPtr = internal_m2n_ui_document_wrapper_get_element_by_id(nativePtr, ownerEntity, elementId);
            return UIEventManager.GetOrCreateWrapper(elementPtr, ownerEntity);
        }

        /// <summary>
        /// Gets the first element that matches the specified CSS selector.
        /// </summary>
        /// <param name="selector">The CSS selector to match against.</param>
        /// <returns>A UIElement if found; otherwise, null.</returns>
        public UIElement QuerySelector(string selector)
        {
            IntPtr elementPtr = internal_m2n_ui_document_wrapper_query_selector(nativePtr, ownerEntity, selector);
            return UIEventManager.GetOrCreateWrapper(elementPtr, ownerEntity);
        }

        /// <summary>
        /// Returns a string representation of this UI document wrapper.
        /// </summary>
        public override string ToString()
        {
            return $"UIDocument(Entity: {ownerEntity}, Title: '{Title}')";
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_document_wrapper_is_valid(IntPtr documentPtr, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_document_wrapper_get_title(IntPtr documentPtr, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_wrapper_set_title(IntPtr documentPtr, Entity ownerEntity, string title);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_document_wrapper_is_visible(IntPtr documentPtr, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_wrapper_show(IntPtr documentPtr, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_wrapper_hide(IntPtr documentPtr, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_wrapper_close(IntPtr documentPtr, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr internal_m2n_ui_document_wrapper_get_element_by_id(IntPtr documentPtr, Entity ownerEntity, string elementId);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr internal_m2n_ui_document_wrapper_query_selector(IntPtr documentPtr, Entity ownerEntity, string selector);
    }
}
