using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Unravel.Core
{

    /// <summary>
    /// Managed wrapper around a native RmlUi element (<c>Rml::Element</c>).
    /// Obtain instances via <see cref="UIDocument.GetElementById"/> /
    /// <see cref="UIDocument.QuerySelector"/>; each element has one wrapper.
    /// A wrapper outliving its element (after an <see cref="InnerRml"/> rebuild of an ancestor, or
    /// a document close or reload) is safe: it reports <see cref="IsValid"/> false, its setters do
    /// nothing and its getters return defaults. Fetch the element again from the document.
    /// </summary>
    public class UIElement : NativeObject
    {
        // Box areas of the rect reads; the values mirror ui_box_area in ui_bindings.cpp.
        private const int AreaBorder = 0;
        private const int AreaPadding = 1;
        private const int AreaContent = 2;

        private IntPtr handle = IntPtr.Zero;
        private readonly Entity ownerEntity;
        private UIStyle style_;

        /// <summary>
        /// Gets whether the element still exists.
        /// </summary>
        public override bool IsValid()
        {
            return handle != IntPtr.Zero && internal_m2n_ui_element_wrapper_is_valid(handle);
        }

        /// <summary>
        /// Gets the entity that owns this UI element.
        /// </summary>
        public Entity Owner => ownerEntity;

        /// <summary>
        /// Gets the element ID.
        /// </summary>
        public string ElementId => internal_m2n_ui_element_wrapper_get_id(handle);

        // Control classes; the values mirror ui_control_kind in ui_bindings.cpp.
        private const int KindButton = 1;
        private const int KindInput = 2;
        private const int KindSlider = 3;
        private const int KindToggle = 4;
        private const int KindSelect = 5;
        private const int KindProgress = 6;
        private const int KindTabSet = 7;

        /// <summary>
        /// Internal constructor - use <see cref="UIEventManager.GetOrCreateWrapper"/>.
        /// </summary>
        internal UIElement(IntPtr elementHandle, Entity owner)
        {
            handle = elementHandle;
            ownerEntity = owner;
        }

        /// <summary>
        /// The wrapper for an element: a control class (<see cref="UISlider"/> for
        /// &lt;input type="range"&gt;...) or a plain <see cref="UIElement"/>.
        /// </summary>
        internal static UIElement Create(IntPtr elementHandle, Entity owner)
        {
            switch (internal_m2n_ui_element_wrapper_get_control_kind(elementHandle))
            {
                case KindButton: return new UIButton(elementHandle, owner);
                case KindInput: return new UIInput(elementHandle, owner);
                case KindSlider: return new UISlider(elementHandle, owner);
                case KindToggle: return new UIToggle(elementHandle, owner);
                case KindSelect: return new UISelect(elementHandle, owner);
                case KindProgress: return new UIProgress(elementHandle, owner);
                case KindTabSet: return new UITabSet(elementHandle, owner);
                default: return new UIElement(elementHandle, owner);
            }
        }

        /// <summary>
        /// Invalidates this wrapper after the native element is closed or destroyed.
        /// </summary>
        internal void Invalidate()
        {
            handle = IntPtr.Zero;
        }

        /// <summary>
        /// Gets the native element handle for internal use by the event system.
        /// </summary>
        internal IntPtr GetHandle()
        {
            return handle;
        }

        // ==== Element Properties ====

        /// <summary>
        /// Gets or sets the inner RML content of the element. Setting parses the value as markup and
        /// replaces every child (wrappers of the old children become invalid); setting the current
        /// content does nothing. Use <see cref="SetText"/> for plain text.
        /// </summary>
        public string InnerRml
        {
            get
            {
                return internal_m2n_ui_element_wrapper_get_inner_rml(handle);
            }
            set
            {
                internal_m2n_ui_element_wrapper_set_inner_rml(handle, value ?? "");
            }
        }

        /// <summary>
        /// Gets or sets whether the element is visible by its own style ('display' not 'none',
        /// 'visibility' visible). Setting false writes an inline 'display: none'; setting true
        /// removes it, so the stylesheet's display (flex, inline-block...) applies again, and writes
        /// 'display: block' only when the stylesheet itself hides the element.
        /// </summary>
        public bool IsVisible
        {
            get
            {
                return internal_m2n_ui_element_wrapper_is_visible(handle);
            }
            set
            {
                internal_m2n_ui_element_wrapper_set_visible(handle, value);
            }
        }

        /// <summary>
        /// Same as <see cref="IsVisible"/>.
        /// </summary>
        public bool visible
        {
            get => IsVisible;
            set => IsVisible = value;
        }

        /// <summary>
        /// The element's text: the text of all its descendants, without markup. Setting replaces
        /// every child with plain text, like <see cref="SetText"/>.
        /// </summary>
        public string text
        {
            get => internal_m2n_ui_element_wrapper_get_text(handle);
            set => SetText(value);
        }

        /// <summary>
        /// The element's tag, e.g. "div".
        /// </summary>
        public string tagName => internal_m2n_ui_element_wrapper_get_tag_name(handle);

        // ==== Controls (any element) ====

        /// <summary>
        /// The value of a form control (input, select, textarea) as text, or the 'value' attribute
        /// of any other element. Setting it sends one "change" event, so change callbacks and
        /// data bindings react as to a user edit; see <see cref="SetValueWithoutNotify"/>.
        /// The control classes (<see cref="UISlider"/>...) add typed views of the same value.
        /// </summary>
        public string value
        {
            get => internal_m2n_ui_element_wrapper_get_value(handle);
            set => internal_m2n_ui_element_wrapper_set_value(handle, value ?? "", true);
        }

        /// <summary>
        /// Sets <see cref="value"/> without sending "change" to scripts (data bindings still update).
        /// </summary>
        public void SetValueWithoutNotify(string newValue)
        {
            internal_m2n_ui_element_wrapper_set_value(handle, newValue ?? "", false);
        }

        /// <summary>
        /// Whether the element is disabled: form controls ignore input and match :disabled rules.
        /// </summary>
        public bool disabled
        {
            get => internal_m2n_ui_element_wrapper_is_disabled(handle);
            set => internal_m2n_ui_element_wrapper_set_disabled(handle, value);
        }

        /// <summary>
        /// This element as a control class, e.g. <c>el.As&lt;UISlider&gt;()</c>; null (with a warning)
        /// when it is another kind of element. <c>el is UISlider slider</c> tests without the warning.
        /// </summary>
        public T As<T>() where T : UIElement
        {
            if (this is T typed)
            {
                return typed;
            }
            Log.Warning($"UI: <{tagName} id='{ElementId}'> is a {GetType().Name}, not a {typeof(T).Name}.");
            return null;
        }

        /// <summary>
        /// Whether this element is of a control class, e.g. <c>el.Is&lt;UIButton&gt;()</c>.
        /// </summary>
        public bool Is<T>() where T : UIElement
        {
            return this is T;
        }

        // ==== Element Methods ====

        /// <summary>
        /// Replaces the element's content with plain text: markup characters show as written.
        /// </summary>
        /// <param name="text">The text to show.</param>
        public void SetText(string text)
        {
            internal_m2n_ui_element_wrapper_set_text(handle, text ?? "");
        }

        /// <summary>
        /// Gets the value of an attribute.
        /// </summary>
        /// <param name="attributeName">The name of the attribute.</param>
        /// <returns>The attribute value, or empty string if not found.</returns>
        public string GetAttribute(string attributeName)
        {
            return internal_m2n_ui_element_wrapper_get_attribute(handle, attributeName);
        }

        /// <summary>
        /// Sets the value of an attribute.
        /// </summary>
        /// <param name="attributeName">The name of the attribute.</param>
        /// <param name="value">The value to set.</param>
        public void SetAttribute(string attributeName, string value)
        {
            internal_m2n_ui_element_wrapper_set_attribute(handle, attributeName, value ?? "");
        }

        /// <summary>
        /// Removes an attribute from the element.
        /// </summary>
        /// <param name="attributeName">The name of the attribute to remove.</param>
        public void RemoveAttribute(string attributeName)
        {
            internal_m2n_ui_element_wrapper_remove_attribute(handle, attributeName);
        }

        /// <summary>
        /// Checks if the element has a specific attribute.
        /// </summary>
        /// <param name="attributeName">The name of the attribute to check.</param>
        /// <returns>True if the attribute exists; otherwise, false.</returns>
        public bool HasAttribute(string attributeName)
        {
            return internal_m2n_ui_element_wrapper_has_attribute(handle, attributeName);
        }

        /// <summary>
        /// Sets or removes a CSS class on the element.
        /// </summary>
        /// <param name="className">The name of the CSS class.</param>
        /// <param name="activate">True to add the class, false to remove it.</param>
        public void SetClass(string className, bool activate)
        {
            internal_m2n_ui_element_wrapper_set_class(handle, className, activate);
        }

        /// <summary>
        /// Checks if the element has a specific CSS class.
        /// </summary>
        /// <param name="className">The name of the CSS class to check.</param>
        /// <returns>True if the class is set; otherwise, false.</returns>
        public bool IsClassSet(string className)
        {
            return internal_m2n_ui_element_wrapper_is_class_set(handle, className);
        }


        /// <summary>
        /// Synchronizes this element's transform to follow the given entity.
        /// </summary>
        /// <param name="transformEntity">The entity whose transform to sync from.</param>
        public void SyncTransformToEntity(Entity transformEntity)
        {
            internal_m2n_ui_element_wrapper_sync_transform_to_entity(handle, transformEntity);
        }
        /// <summary>
        /// Gives focus to this element.
        /// </summary>
        public void Focus()
        {
            internal_m2n_ui_element_wrapper_focus(handle);
        }

        /// <summary>
        /// Removes focus from this element.
        /// </summary>
        public void Blur()
        {
            internal_m2n_ui_element_wrapper_blur(handle);
        }

        /// <summary>
        /// Simulates a click on this element.
        /// </summary>
        public void Click()
        {
            internal_m2n_ui_element_wrapper_click(handle);
        }

        /// <summary>
        /// Scrolls the element into view.
        /// </summary>
        /// <param name="alignWithTop">If true, align with the top of the viewport; otherwise, align with the bottom.</param>
        public void ScrollIntoView(bool alignWithTop = true)
        {
            internal_m2n_ui_element_wrapper_scroll_into_view(handle, alignWithTop);
        }

        // ==== DOM ====

        /// <summary>
        /// The parent element; null for a document's root element and for an element in no tree.
        /// </summary>
        public UIElement parent =>
            UIEventManager.GetOrCreateWrapper(internal_m2n_ui_element_wrapper_get_parent(handle), ownerEntity);

        /// <summary>
        /// The child elements in order, as a new list.
        /// </summary>
        public IReadOnlyList<UIElement> children =>
            UIEventManager.GetOrCreateWrappers(internal_m2n_ui_element_wrapper_get_children(handle), ownerEntity);

        /// <summary>
        /// The first descendant matching an RCSS selector, or null.
        /// </summary>
        /// <param name="selector">For example "#score", ".slot > img", "button.primary".</param>
        public UIElement Q(string selector)
        {
            return UIEventManager.GetOrCreateWrapper(internal_m2n_ui_element_wrapper_query_selector(handle, selector ?? ""),
                                                     ownerEntity);
        }

        /// <summary>
        /// The first descendant matching an RCSS selector, as a control class, e.g.
        /// <c>Q&lt;UISlider&gt;("#volume")</c>; null when none matches, or (with a warning) when it is
        /// another kind of element.
        /// </summary>
        public T Q<T>(string selector) where T : UIElement
        {
            return Q(selector)?.As<T>();
        }

        /// <summary>
        /// Every descendant matching an RCSS selector, in document order.
        /// </summary>
        public List<UIElement> QueryAll(string selector)
        {
            return UIEventManager.GetOrCreateWrappers(internal_m2n_ui_element_wrapper_query_selector_all(handle, selector ?? ""),
                                                      ownerEntity);
        }

        /// <summary>
        /// The nearest ancestor matching an RCSS selector, or null. The element itself is not considered.
        /// </summary>
        public UIElement Closest(string selector)
        {
            return UIEventManager.GetOrCreateWrapper(internal_m2n_ui_element_wrapper_closest(handle, selector ?? ""),
                                                     ownerEntity);
        }

        /// <summary>
        /// Adds @p child as this element's last child, moving it from where it was. Data bindings and
        /// classes of the child take effect from here.
        /// </summary>
        /// <returns>The child, or null when it cannot move here (a document, this element or one of its ancestors).</returns>
        public UIElement AppendChild(UIElement child)
        {
            return child != null && internal_m2n_ui_element_wrapper_append_child(handle, child.handle) ? child : null;
        }

        /// <summary>
        /// Adds @p child before @p before, one of this element's children, moving it from where it
        /// was; with a null @p before (or one that is not a child here) it is added last.
        /// </summary>
        /// <returns>The child, or null when it cannot move here.</returns>
        public UIElement InsertBefore(UIElement child, UIElement before)
        {
            IntPtr beforeHandle = before?.handle ?? IntPtr.Zero;
            return child != null && internal_m2n_ui_element_wrapper_insert_before(handle, child.handle, beforeHandle) ? child : null;
        }

        /// <summary>
        /// Removes the element from its tree and destroys it with its children, at once: their
        /// wrappers become invalid and their subscriptions end. A document's root cannot be removed.
        /// </summary>
        public void Remove()
        {
            internal_m2n_ui_element_wrapper_remove(handle);
        }

        /// <summary>
        /// A deep copy of the element, in no tree yet: add it with <see cref="AppendChild"/> or
        /// <see cref="InsertBefore"/>. A copy never added lives until its document closes. Event
        /// subscriptions are not copied.
        /// </summary>
        public UIElement Clone()
        {
            return UIEventManager.GetOrCreateWrapper(internal_m2n_ui_element_wrapper_clone(handle), ownerEntity);
        }

        // ==== Style ====

        /// <summary>
        /// The element's inline style: <c>el.style["opacity"] = "0.5"</c>, <c>el.style.opacity = 0.5f</c>.
        /// Setting a property to null removes the inline value, so the stylesheet applies again.
        /// </summary>
        public UIStyle style => style_ ?? (style_ = new UIStyle(this));

        /// <summary>
        /// Adds or removes a class; the same as <see cref="SetClass"/>.
        /// </summary>
        public void EnableClass(string className, bool enable)
        {
            SetClass(className, enable);
        }

        /// <summary>
        /// Adds the class when it is missing, removes it when it is set.
        /// </summary>
        /// <returns>Whether the class is set now.</returns>
        public bool ToggleClass(string className)
        {
            bool enable = !HasClass(className);
            SetClass(className, enable);
            return enable;
        }

        /// <summary>
        /// Whether the class is set; the same as <see cref="IsClassSet"/>.
        /// </summary>
        public bool HasClass(string className)
        {
            return IsClassSet(className);
        }

        /// <summary>
        /// Sets or clears a pseudo class such as "checked" or "selected", for :checked rules.
        /// </summary>
        public void SetPseudoClass(string pseudoClass, bool active)
        {
            internal_m2n_ui_element_wrapper_set_pseudo_class(handle, pseudoClass ?? "", active);
        }

        /// <summary>
        /// Whether a pseudo class is set, including the ones RmlUi sets (:hover, :focus, :active).
        /// </summary>
        public bool HasPseudoClass(string pseudoClass)
        {
            return internal_m2n_ui_element_wrapper_is_pseudo_class_set(handle, pseudoClass ?? "");
        }

        /// <summary>
        /// The value a property resolves to on this element, from its inline style, the stylesheet,
        /// its parent or the default, as RCSS text; null for an unknown property or a shorthand.
        /// For laid-out sizes and positions read <see cref="rect"/>.
        /// </summary>
        public string GetComputed(string property)
        {
            string value = internal_m2n_ui_element_wrapper_get_computed_style(handle, property ?? "");
            return string.IsNullOrEmpty(value) ? null : value;
        }

        internal string GetInlineStyle(string property)
        {
            return internal_m2n_ui_element_wrapper_get_inline_style(handle, property ?? "");
        }

        internal bool SetInlineStyle(string property, string value)
        {
            return internal_m2n_ui_element_wrapper_set_inline_style(handle, property ?? "", value ?? "");
        }

        internal void RemoveInlineStyle(string property)
        {
            internal_m2n_ui_element_wrapper_remove_inline_style(handle, property ?? "");
        }

        internal float GetStyleNumber(string property)
        {
            return internal_m2n_ui_element_wrapper_get_style_number(handle, property);
        }

        internal void SetStyleNumber(string property, float value, int unit)
        {
            internal_m2n_ui_element_wrapper_set_style_number(handle, property, value, unit);
        }

        // ==== Geometry and scrolling ====
        // Reads are current: a document changed since its last layout is laid out first.

        /// <summary>
        /// The border box in the document's pixels, y down. For a screen-space document that is the
        /// space of <see cref="Input.mousePosition"/>. CSS transforms are not applied.
        /// </summary>
        public Rect rect => ReadRect(AreaBorder);

        /// <summary>
        /// The padding box (inside the border) in the document's pixels.
        /// </summary>
        public Rect paddingRect => ReadRect(AreaPadding);

        /// <summary>
        /// The content box (inside the padding) in the document's pixels.
        /// </summary>
        public Rect contentRect => ReadRect(AreaContent);

        /// <summary>
        /// Horizontal scroll position of the content in pixels; setting it scrolls at once.
        /// </summary>
        public float scrollLeft
        {
            get => internal_m2n_ui_element_wrapper_get_scroll(handle).x;
            set => ScrollTo(new Vector2(value, scrollTop), false);
        }

        /// <summary>
        /// Vertical scroll position of the content in pixels; setting it scrolls at once.
        /// </summary>
        public float scrollTop
        {
            get => internal_m2n_ui_element_wrapper_get_scroll(handle).y;
            set => ScrollTo(new Vector2(scrollLeft, value), false);
        }

        /// <summary>
        /// Width of the scrollable content in pixels.
        /// </summary>
        public float scrollWidth => internal_m2n_ui_element_wrapper_get_scroll(handle).z;

        /// <summary>
        /// Height of the scrollable content in pixels.
        /// </summary>
        public float scrollHeight => internal_m2n_ui_element_wrapper_get_scroll(handle).w;

        /// <summary>
        /// Scrolls the content to @p offset (left, top) in pixels.
        /// </summary>
        /// <param name="offset">The scroll position.</param>
        /// <param name="smooth">Glide there instead of jumping.</param>
        public void ScrollTo(Vector2 offset, bool smooth = true)
        {
            internal_m2n_ui_element_wrapper_scroll_to(handle, offset, smooth);
        }

        private Rect ReadRect(int area)
        {
            Vector4 box = internal_m2n_ui_element_wrapper_get_rect(handle, area);
            return new Rect(box.x, box.y, box.z, box.w);
        }

        // ==== Animation ====

        /// <summary>
        /// Animates a property from its current value to @p target. Another animation of the same
        /// property is replaced. RCSS keyframes and transitions remain the way to author designed
        /// motion; this is for motion decided in code.
        /// </summary>
        /// <example><code>
        /// panel.Animate("opacity", "0", 0.3f, UIEasing.CubicOut).completed += () => panel.visible = false;
        /// coin.Animate("transform", "scale(1.4)", 0.15f, UIEasing.BackOut, loops: 2, alternate: true);
        /// </code></example>
        /// <param name="property">An animatable RCSS property, e.g. "opacity", "left", "transform", "color".</param>
        /// <param name="target">The end value as RCSS text, e.g. "0", "120dp", "rotate(90deg)", "#ff0000".</param>
        /// <param name="duration">Seconds per run.</param>
        /// <param name="easing">The curve.</param>
        /// <param name="loops">How many runs; -1 runs forever.</param>
        /// <param name="alternate">Every other run goes backwards.</param>
        /// <param name="delay">Seconds before the first run.</param>
        /// <returns>The animation; check <see cref="UIAnimation.isRunning"/> - false when the property or value is invalid.</returns>
        public UIAnimation Animate(string property, string target, float duration, UIEasing easing = UIEasing.Linear,
                                   int loops = 1, bool alternate = false, float delay = 0.0f)
        {
            string name = (property ?? "").Trim().ToLowerInvariant();
            var animation = new UIAnimation(this, name);
            if (internal_m2n_ui_element_wrapper_animate(handle, name, target ?? "", duration, (int)easing, loops, alternate, delay))
            {
                animation.Start();
                UIEventManager.TrackAnimation(this, animation);
            }
            return animation;
        }

        /// <summary>
        /// Restarts the element's RCSS keyframe animations (its 'animation' property) from the beginning.
        /// </summary>
        public void RestartAnimation()
        {
            internal_m2n_ui_element_wrapper_restart_animation(handle);
        }

        internal void StopAnimationOf(string property)
        {
            internal_m2n_ui_element_wrapper_stop_animation(handle, property);
        }

        // ==== Event Handling ====

        /// <summary>
        /// Adds an event listener to this element.
        /// </summary>
        /// <param name="eventType">The type of event to listen for.</param>
        /// <param name="callback">The callback to invoke when the event occurs.</param>
        public void RegisterCallback(string eventType, UIEventCallback callback)
        {
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
            return UIEventManager.Unsubscribe<T>(this, eventType, callback);
        }

        /// <summary>
        /// Unsubscribe all callbacks from this UIElement.
        /// This removes all event listeners (both legacy and typed) from this element.
        /// </summary>
        /// <returns>True if any subscriptions were removed</returns>
        public bool UnsubscribeAll()
        {
            return UIEventManager.UnsubscribeAll(this);
        }

        /// <summary>
        /// Subscribes to the event a class stands for: <see cref="UIClickEvent"/> "click",
        /// <see cref="UIChangeEvent"/> "change", <see cref="UIAnimationEvent"/> "animationend",
        /// <see cref="UISubmitEvent"/> "submit", <see cref="UIScrollEvent"/> "scroll",
        /// <see cref="UITextInputEvent"/> "textinput". Other classes need the event's name.
        /// </summary>
        /// <param name="callback">Runs for each event.</param>
        /// <param name="phase">Bubble (default): at the element and when the event bubbles up from a
        /// descendant. Capture: before descendants see it. Target: only for the element's own events.</param>
        /// <returns>Dispose it to unsubscribe.</returns>
        public UISubscription On<T>(Action<T> callback, EventPhase phase = EventPhase.Bubble) where T : UIEventBase
        {
            return UIEventManager.SubscribeTyped(this, UIEventManager.GetDefaultEventType<T>(), callback, phase, false);
        }

        /// <summary>
        /// Subscribes to an event by name, e.g. <c>On&lt;UIPointerEvent&gt;("mousemove", OnMove)</c>.
        /// </summary>
        /// <param name="eventType">The RmlUi event name.</param>
        /// <param name="callback">Runs for each event.</param>
        /// <param name="phase">Bubble (default), Capture or Target; see <see cref="On{T}(Action{T}, EventPhase)"/>.</param>
        /// <returns>Dispose it to unsubscribe.</returns>
        public UISubscription On<T>(string eventType, Action<T> callback, EventPhase phase = EventPhase.Bubble) where T : UIEventBase
        {
            return UIEventManager.SubscribeTyped(this, eventType, callback, phase, false);
        }

        /// <summary>
        /// Like <see cref="On{T}(Action{T}, EventPhase)"/>, ending after the first event.
        /// </summary>
        public UISubscription Once<T>(Action<T> callback) where T : UIEventBase
        {
            return UIEventManager.SubscribeTyped(this, UIEventManager.GetDefaultEventType<T>(), callback, EventPhase.Bubble, true);
        }

        /// <summary>
        /// Like <see cref="On{T}(string, Action{T}, EventPhase)"/>, ending after the first event.
        /// </summary>
        public UISubscription Once<T>(string eventType, Action<T> callback) where T : UIEventBase
        {
            return UIEventManager.SubscribeTyped(this, eventType, callback, EventPhase.Bubble, true);
        }

        /// <summary>
        /// Raised when the element is clicked (pointer press and release on it, or <see cref="Click"/>).
        /// Unlike a "click" callback it takes no event, and removing it works for the same method or delegate.
        /// </summary>
        public event Action clicked
        {
            add => UIEventManager.AddClicked(this, value);
            remove => UIEventManager.RemoveClicked(this, value);
        }

        /// <summary>
        /// Returns a string representation of this UI element wrapper.
        /// </summary>
        public override string ToString()
        {
            return $"UIElement('{ElementId}', Entity: {ownerEntity})";
        }

        // ==== Internal Calls ====

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_element_wrapper_is_valid(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_element_wrapper_get_inner_rml(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_set_inner_rml(IntPtr elementHandle, string rml);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_set_text(IntPtr elementHandle, string text);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_element_wrapper_is_visible(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_set_visible(IntPtr elementHandle, bool visible);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_element_wrapper_get_attribute(IntPtr elementHandle, string attributeName);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_set_attribute(IntPtr elementHandle, string attributeName, string value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_remove_attribute(IntPtr elementHandle, string attributeName);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_element_wrapper_has_attribute(IntPtr elementHandle, string attributeName);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_set_class(IntPtr elementHandle, string className, bool activate);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_element_wrapper_is_class_set(IntPtr elementHandle, string className);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_sync_transform_to_entity(IntPtr elementHandle, Entity transformEntity);
        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_focus(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_blur(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_click(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_scroll_into_view(IntPtr elementHandle, bool alignWithTop);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_element_wrapper_get_id(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_element_wrapper_get_tag_name(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_ui_element_wrapper_get_control_kind(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_element_wrapper_get_value(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_set_value(IntPtr elementHandle, string value, bool notify);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_element_wrapper_is_disabled(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_set_disabled(IntPtr elementHandle, bool disabled);

        // Used by the control classes; internal calls are registered for this class.

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern bool internal_m2n_ui_element_wrapper_is_checked(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void internal_m2n_ui_element_wrapper_set_checked(IntPtr elementHandle, bool isChecked, bool notify);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void internal_m2n_ui_element_wrapper_select_text(IntPtr elementHandle, int start, int end);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern int internal_m2n_ui_element_wrapper_get_selected_index(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void internal_m2n_ui_element_wrapper_set_selected_index(IntPtr elementHandle, int index, bool notify);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern long[] internal_m2n_ui_element_wrapper_get_options(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern int internal_m2n_ui_element_wrapper_add_option(IntPtr elementHandle, string rml, string value, int before);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void internal_m2n_ui_element_wrapper_remove_option(IntPtr elementHandle, int index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern Vector2 internal_m2n_ui_element_wrapper_get_progress(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void internal_m2n_ui_element_wrapper_set_progress(IntPtr elementHandle, float value, float max);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern Vector2 internal_m2n_ui_element_wrapper_get_tabs(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void internal_m2n_ui_element_wrapper_set_active_tab(IntPtr elementHandle, int index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void internal_m2n_ui_element_wrapper_set_tab(IntPtr elementHandle, int index, string tabRml, string panelRml);

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern void internal_m2n_ui_element_wrapper_remove_tab(IntPtr elementHandle, int index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_element_wrapper_get_text(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_set_pseudo_class(IntPtr elementHandle, string pseudoClass, bool activate);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_element_wrapper_is_pseudo_class_set(IntPtr elementHandle, string pseudoClass);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr internal_m2n_ui_element_wrapper_get_parent(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern long[] internal_m2n_ui_element_wrapper_get_children(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr internal_m2n_ui_element_wrapper_query_selector(IntPtr elementHandle, string selector);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern long[] internal_m2n_ui_element_wrapper_query_selector_all(IntPtr elementHandle, string selector);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr internal_m2n_ui_element_wrapper_closest(IntPtr elementHandle, string selector);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_element_wrapper_append_child(IntPtr parentHandle, IntPtr childHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_element_wrapper_insert_before(IntPtr parentHandle, IntPtr childHandle, IntPtr beforeHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_remove(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr internal_m2n_ui_element_wrapper_clone(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_element_wrapper_get_inline_style(IntPtr elementHandle, string property);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_element_wrapper_set_inline_style(IntPtr elementHandle, string property, string value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_remove_inline_style(IntPtr elementHandle, string property);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_ui_element_wrapper_get_computed_style(IntPtr elementHandle, string property);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_set_style_number(IntPtr elementHandle, string property, float value, int unit);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_ui_element_wrapper_get_style_number(IntPtr elementHandle, string property);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector4 internal_m2n_ui_element_wrapper_get_rect(IntPtr elementHandle, int area);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector4 internal_m2n_ui_element_wrapper_get_scroll(IntPtr elementHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_scroll_to(IntPtr elementHandle, Vector2 offset, bool smooth);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_ui_element_wrapper_animate(IntPtr elementHandle, string property, string target,
                                                                           float duration, int easing, int loops, bool alternate, float delay);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_stop_animation(IntPtr elementHandle, string property);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_ui_element_wrapper_restart_animation(IntPtr elementHandle);
    }
}
