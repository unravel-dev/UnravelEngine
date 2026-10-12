using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Managed wrapper around a native RmlUi document (<c>Rml::ElementDocument</c>).
    /// Each <see cref="UIDocumentComponent"/> owns its own RmlUi context and document.
    /// Wrappers are cached by document, so repeated queries return the same instance. A wrapper is
    /// valid while its document is the component's open document: after <see cref="Close"/> or a
    /// reload it reports <see cref="IsValid"/> false and its calls do nothing.
    /// </summary>
    public class UIDocument : NativeObject
    {
        private static readonly Dictionary<IntPtr, UIDocument> s_cache = new Dictionary<IntPtr, UIDocument>();

        private IntPtr handle = IntPtr.Zero;
        private readonly Entity ownerEntity;

        /// <summary>
        /// Gets whether this wrapper's document is still the owner's open document.
        /// </summary>
        public override bool IsValid()
        {
            return handle != IntPtr.Zero && internal_m2n_ui_document_wrapper_is_valid(handle, ownerEntity);
        }

        /// <summary>
        /// Gets the entity that owns this UI document.
        /// </summary>
        public Entity Owner => ownerEntity;

        /// <summary>
        /// Internal constructor - use <see cref="GetOrCreate"/>.
        /// </summary>
        internal UIDocument(IntPtr documentHandle, Entity owner)
        {
            handle = documentHandle;
            ownerEntity = owner;
        }

        /// <summary>
        /// Returns the cached wrapper of the document @p documentHandle names, creating one if needed.
        /// </summary>
        /// <param name="documentHandle">Native document handle.</param>
        /// <param name="owner">Owning entity.</param>
        /// <returns>Canonical wrapper, or null when <paramref name="documentHandle"/> is zero.</returns>
        internal static UIDocument GetOrCreate(IntPtr documentHandle, Entity owner)
        {
            if (documentHandle == IntPtr.Zero)
            {
                return null;
            }
            if (s_cache.TryGetValue(documentHandle, out UIDocument existing))
            {
                return existing;
            }
            UIDocument document = new UIDocument(documentHandle, owner);
            s_cache[documentHandle] = document;
            return document;
        }

        /// <summary>
        /// Invalidates the wrapper of a destroyed document, if there is one.
        /// </summary>
        internal static void InvalidateHandle(IntPtr documentHandle)
        {
            if (s_cache.TryGetValue(documentHandle, out UIDocument document))
            {
                document.Invalidate();
            }
        }

        /// <summary>
        /// Gets the native document handle.
        /// </summary>
        internal IntPtr GetHandle()
        {
            return handle;
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
            if (handle != IntPtr.Zero)
            {
                s_cache.Remove(handle);
            }
            handle = IntPtr.Zero;
        }

        /// <summary>
        /// Gets or sets the title of the document.
        /// </summary>
        public string Title
        {
            get
            {
                return internal_m2n_ui_document_wrapper_get_title(handle, ownerEntity);
            }
            set
            {
                internal_m2n_ui_document_wrapper_set_title(handle, ownerEntity, value ?? "");
            }
        }

        /// <summary>
        /// Gets whether the document is currently visible.
        /// </summary>
        public bool IsVisible
        {
            get
            {
                return internal_m2n_ui_document_wrapper_is_visible(handle, ownerEntity);
            }
        }

        /// <summary>
        /// Shows the document.
        /// </summary>
        public void Show()
        {
            internal_m2n_ui_document_wrapper_show(handle, ownerEntity);
        }

        /// <summary>
        /// Hides the document.
        /// </summary>
        public void Hide()
        {
            internal_m2n_ui_document_wrapper_hide(handle, ownerEntity);
        }

        /// <summary>
        /// Closes the document: it disappears at once and stays closed until
        /// <see cref="UIDocumentComponent.Reload"/>. Invalidates this wrapper and the element
        /// wrappers, and drops their event subscriptions.
        /// </summary>
        public void Close()
        {
            IntPtr documentHandle = handle;
            if (documentHandle == IntPtr.Zero)
            {
                return;
            }
            // Drop managed + native listeners while the document is still alive.
            UIEventManager.UnsubscribeAllForOwner(ownerEntity, invalidateWrappers: true);
            internal_m2n_ui_document_wrapper_close(documentHandle, ownerEntity);
            Invalidate();
        }

        /// <summary>
        /// Gets an element wrapper by its ID.
        /// </summary>
        /// <param name="elementId">The ID of the element to find.</param>
        /// <returns>A UIElement if found; otherwise, null.</returns>
        public UIElement GetElementById(string elementId)
        {
            IntPtr elementHandle = internal_m2n_ui_document_wrapper_get_element_by_id(handle, ownerEntity, elementId);
            return UIEventManager.GetOrCreateWrapper(elementHandle, ownerEntity);
        }

        /// <summary>
        /// Gets the first element that matches the specified CSS selector.
        /// </summary>
        /// <param name="selector">The CSS selector to match against.</param>
        /// <returns>A UIElement if found; otherwise, null.</returns>
        public UIElement QuerySelector(string selector)
        {
            IntPtr elementHandle = internal_m2n_ui_document_wrapper_query_selector(handle, ownerEntity, selector);
            return UIEventManager.GetOrCreateWrapper(elementHandle, ownerEntity);
        }

        /// <summary>
        /// The first element matching an RCSS selector, or null; the same as <see cref="QuerySelector"/>.
        /// </summary>
        /// <param name="selector">For example "#score", ".slot > img", "button.primary".</param>
        public UIElement Q(string selector)
        {
            return QuerySelector(selector ?? "");
        }

        /// <summary>
        /// The first element matching an RCSS selector, as a control class, e.g.
        /// <c>Q&lt;UISlider&gt;("#volume")</c>; null when none matches, or (with a warning) when it is
        /// another kind of element.
        /// </summary>
        public T Q<T>(string selector) where T : UIElement
        {
            return Q(selector)?.As<T>();
        }

        /// <summary>
        /// Every element matching an RCSS selector, in document order.
        /// </summary>
        public List<UIElement> QueryAll(string selector)
        {
            long[] handles = internal_m2n_ui_document_wrapper_query_selector_all(handle, ownerEntity, selector ?? "");
            return UIEventManager.GetOrCreateWrappers(handles, ownerEntity);
        }

        /// <summary>
        /// The document's root element (its body), to add elements at the top level.
        /// </summary>
        public UIElement root => IsValid() ? UIEventManager.GetOrCreateWrapper(handle, ownerEntity) : null;

        /// <summary>
        /// Creates an element in no tree yet; add it with <see cref="UIElement.AppendChild"/> or
        /// <see cref="UIElement.InsertBefore"/>. Set its attributes and classes before or after. An
        /// element never added lives until the document closes.
        /// </summary>
        /// <param name="tag">The tag, e.g. "div", "img", "button".</param>
        public UIElement CreateElement(string tag)
        {
            IntPtr elementHandle = internal_m2n_ui_document_wrapper_create_element(handle, ownerEntity, tag ?? "div");
            return UIEventManager.GetOrCreateWrapper(elementHandle, ownerEntity);
        }

        /// <summary>
        /// The innermost element of this document at a point, or null.
        /// </summary>
        /// <param name="point">In the document's pixels: for a screen-space document, <see cref="Input.mousePosition"/>.</param>
        public UIElement ElementAt(Vector2 point)
        {
            IntPtr elementHandle = internal_m2n_ui_document_wrapper_element_at(handle, ownerEntity, point);
            return UIEventManager.GetOrCreateWrapper(elementHandle, ownerEntity);
        }

        /// <summary>
        /// The data model the document declares with <c>data-model="name"</c>; see
        /// <see cref="UIDocumentComponent.GetModel"/>.
        /// </summary>
        public UIDataModel GetModel(string name)
        {
            return UIDataModels.GetOrCreate(ownerEntity, name);
        }

        /// <summary>
        /// Pixels per dp in this document. Geometry (<see cref="UIElement.rect"/>, screen points)
        /// is in pixels, RCSS lengths in dp: <c>marker.style.left = screenPoint.x / document.pixelsPerDp</c>
        /// places an absolutely positioned element of the body at a screen x.
        /// </summary>
        public float pixelsPerDp => internal_m2n_ui_document_wrapper_get_dp_ratio(handle, ownerEntity);

        /// <summary>
        /// Lays the document out now. Geometry reads (<see cref="UIElement.rect"/>) already do this
        /// when the document changed; call it to pay the cost at a moment of your choosing.
        /// </summary>
        public void UpdateLayout()
        {
            internal_m2n_ui_document_wrapper_update_layout(handle, ownerEntity);
        }

        /// <summary>
        /// Returns a string representation of this UI document wrapper.
        /// </summary>
        public override string ToString()
        {
            return $"UIDocument(Entity: {ownerEntity}, Title: '{Title}')";
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_document_wrapper_is_valid(IntPtr documentHandle, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_document_wrapper_get_title(IntPtr documentHandle, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_wrapper_set_title(IntPtr documentHandle, Entity ownerEntity, string title);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_document_wrapper_is_visible(IntPtr documentHandle, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_wrapper_show(IntPtr documentHandle, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_wrapper_hide(IntPtr documentHandle, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_wrapper_close(IntPtr documentHandle, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr internal_m2n_ui_document_wrapper_get_element_by_id(IntPtr documentHandle, Entity ownerEntity, string elementId);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern long[] internal_m2n_ui_document_wrapper_query_selector_all(IntPtr documentHandle, Entity ownerEntity, string selector);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr internal_m2n_ui_document_wrapper_create_element(IntPtr documentHandle, Entity ownerEntity, string tag);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr internal_m2n_ui_document_wrapper_element_at(IntPtr documentHandle, Entity ownerEntity, Vector2 point);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_wrapper_update_layout(IntPtr documentHandle, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_ui_document_wrapper_get_dp_ratio(IntPtr documentHandle, Entity ownerEntity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr internal_m2n_ui_document_wrapper_query_selector(IntPtr documentHandle, Entity ownerEntity, string selector);
    }
}
