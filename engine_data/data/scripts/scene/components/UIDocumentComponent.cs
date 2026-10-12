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
        /// Same as <see cref="visible"/>.
        /// </summary>
        /// <value>True if the document is enabled; otherwise, false.</value>
        public bool enabled
        {
            get => internal_m2n_ui_document_is_enabled(owner);
            set => internal_m2n_ui_document_set_enabled(owner, value);
        }

        /// <summary>
        /// Gets or sets whether the document is shown. A hidden document keeps its state, its
        /// elements and its event subscriptions.
        /// </summary>
        public bool visible
        {
            get => internal_m2n_ui_document_is_enabled(owner);
            set => internal_m2n_ui_document_set_enabled(owner, value);
        }

        /// <summary>
        /// The loaded document, or null while none is loaded (or after <see cref="Close"/>).
        /// </summary>
        public UIDocument document => GetDocument();

        /// <summary>
        /// Raised at the start of a frame after a document is loaded: for the current document
        /// right after subscribing, then after every reload (<see cref="Reload"/>, an edit of the
        /// document asset). Element references from an earlier document are invalid by then;
        /// fetch them again here. Ends with the subscribing script.
        /// </summary>
        public event Action<UIDocument> documentLoaded
        {
            add => UIEventManager.AddDocumentWatcher(this, value);
            remove => UIEventManager.RemoveDocumentWatcher(this, value);
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
        /// Closes the UI document: it disappears at once and stays closed until <see cref="Reload"/>.
        /// Invalidates cached wrappers and clears UI event subscriptions for this entity.
        /// </summary>
        public void Close()
        {
            UIEventManager.UnsubscribeAllForOwner(owner, invalidateWrappers: true);
            internal_m2n_ui_document_close(owner);
        }

        /// <summary>
        /// The data model the document declares with <c>data-model="name"</c>. It exists before the
        /// document loads and lives across reloads, so bind it once (in OnCreate or OnStart).
        /// </summary>
        /// <param name="name">The model name in the markup.</param>
        public UIDataModel GetModel(string name)
        {
            return UIDataModels.GetOrCreate(owner, name);
        }

        /// <summary>
        /// Loads the document again from its asset, now: a fresh document in its initial state.
        /// Also opens a closed document. Wrappers and subscriptions of the old document end.
        /// </summary>
        /// <returns>True if the document loaded.</returns>
        public bool Reload()
        {
            UIEventManager.UnsubscribeAllForOwner(owner, invalidateWrappers: true);
            return internal_m2n_ui_document_reload(owner);
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
            IntPtr documentHandle = internal_m2n_ui_document_get_wrapper(owner);
            return UIDocument.GetOrCreate(documentHandle, owner);
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
        private static extern bool internal_m2n_ui_document_reload(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_document_get_title(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_document_set_title(Entity eid, string title);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr internal_m2n_ui_document_get_wrapper(Entity eid);
    }
}
