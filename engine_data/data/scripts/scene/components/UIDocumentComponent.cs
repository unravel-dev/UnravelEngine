using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Component that manages an RmlUi document for rendering HTML/CSS-based user interfaces.
    /// Each component owns its own RmlUi context and document (context-per-document).
    /// </summary>
    public class UIDocumentComponent : Component
    {
        private VisualTree asset_;

        /// <summary>
        /// Gets or sets the visual tree asset (HTML/RML document) for this UI component.
        /// </summary>
        /// <value>The visual tree asset that defines the UI document structure and content.</value>
        public VisualTree asset
        {
            get
            {
                var uid = internal_m2n_ui_document_get_asset(owner);

                if (uid == Guid.Empty)
                {
                    asset_ = null;
                }
                else if (asset_ == null || asset_.uid != uid)
                {
                    asset_ = new VisualTree { uid = uid };
                }

                return asset_;
            }
            set
            {
                asset_ = value;
                internal_m2n_ui_document_set_asset(owner, asset_?.uid ?? Guid.Empty);
            }
        }

        /// <summary>
        /// Gets a value indicating whether the UI document is currently loaded and ready for use.
        /// </summary>
        /// <value>True if the document is loaded; otherwise, false.</value>
        public bool loaded
        {
            get => internal_m2n_ui_document_is_loaded(owner);
        }

        /// <summary>
        /// Gets a value indicating whether the UI document is currently enabled.
        /// </summary>
        /// <value>True if the document is enabled; otherwise, false.</value>
        public bool enabled
        {
            get => internal_m2n_ui_document_is_enabled(owner);
            set => internal_m2n_ui_document_set_enabled(owner, value);
        }

        /// <summary>
        /// Gets or sets the title of the UI document.
        /// </summary>
        /// <value>The document title as a string.</value>
        public string title
        {
            get => internal_m2n_ui_document_get_title(owner);
            set => internal_m2n_ui_document_set_title(owner, value);
        }

        /// <summary>
        /// Closes the UI document, unloading it from memory.
        /// Invalidates cached wrappers and clears UI event subscriptions for this entity.
        /// The document will need to be reloaded before it can be used again.
        /// </summary>
        public void Close()
        {
            UIEventManager.UnsubscribeAllForOwner(owner, invalidateWrappers: true);
            internal_m2n_ui_document_close(owner);
        }

        // ==== Wrapper Object Creation ====

        /// <summary>
        /// Gets a cached wrapper for this UI document for direct access.
        /// Repeated calls return the same instance while the native document remains valid.
        /// </summary>
        /// <returns>A UIDocument if the document is loaded; otherwise, null.</returns>
        public UIDocument GetDocument()
        {
            if (!loaded)
            {
                return null;
            }
            IntPtr documentPtr = internal_m2n_ui_document_get_wrapper(owner);
            return UIDocument.GetOrCreate(documentPtr, owner);
        }

        // ==== Internal Calls ====

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Guid internal_m2n_ui_document_get_asset(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_set_asset(Entity eid, Guid uid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_document_is_loaded(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_document_is_enabled(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_set_enabled(Entity eid, bool enabled);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_close(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_document_get_title(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_set_title(Entity eid, string title);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr internal_m2n_ui_document_get_wrapper(Entity eid);
    }
}
