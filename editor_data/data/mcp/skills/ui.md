---
name: ui
title: In-game UI - documents, data models, controls, events
description: HUDs and menus with RmlUi - create .rhtml/.rcss, show a document with a UI Document component, bind it to C# data models ({{ }}, data-for, data-event-*), use elements and typed controls (UISlider, UIToggle, UISelect...), events, styling, sprite skins, geometry and animation from C#, the ways RCSS differs from CSS, and how to verify it (ui_query_elements, ui_click_element, ui_get_model).
order: 58
---
# In-game UI

Two ways to drive a document from C#, freely mixed:
- a **data model** - the markup reads values from a C# object ({{ }}, data-if,
  data-for, data-event-click). Preferred for HUDs: no lookups, no setters.
- **elements** - fetch with Q("#id") and change them (text, classes, style, DOM,
  animation, events). Right for one-off effects and dynamic structure.

Complete example: engine:/data/ui/sample_hud.rhtml with
engine:/data/scripts/samples/SampleHudController.cs.

## Files

- ui_create_batch {"items":[{"kind":"document","folder":"app:/data/UI","name":"Hud"},
  {"kind":"style_sheet","folder":"app:/data/UI","name":"Hud"}]} copies the engine
  templates, the same path as the Content Browser's Create menu.
- The template document links engine:/data/ui/rml.rcss (base element styles) and
  a starter style sheet. Point the second link at your own .rcss key:
  <link type="text/rcss" href="app:/data/UI/Hud.rcss"/>
- Write whole files with ui_set_sources_batch. The asset watcher reimports them and
  a document already on screen reloads - in edit mode only: during play a document
  keeps the markup and style sheets it started with, so stop play before pushing UI
  changes (a changed texture still swaps in, against the old sprite rectangles).
  ui_get_sources_batch reads them back, and reads engine files such as rml.rcss.

## Skinning with sprites

- Pack the art into one atlas texture (import it with assets_import_files) and name its
  rectangles in a style sheet the documents link:
  @spritesheet ui { src: app:/data/UI/atlas.png; panel: 0px 0px 100px 100px;
  panel-inner: 10px 10px 80px 80px; }
- decorator: ninepatch(panel, panel-inner) stretches a frame (corners keep their size);
  tiled-horizontal(left, middle, right) for bars and slider tracks; image(sprite) for one
  sprite; <img sprite="name"/> or data-attr-sprite="expr" in markup.
- image-color multiplies an element's decorators: tint one white sprite many ways, dim a
  button and brighten it on :hover. In the UISample a two-decorator list on a checked
  checkbox (frame + tick) showed no tick; one sprite per state (box, box with tick) did.
- Textures compile block-compressed: put sprites on 4 px boundaries with 4 px gaps, and
  store pixel art at a whole multiple (4x) so it stays crisp - or turn compression off
  for the atlas: assets_set_import_settings {"items":[{"key":"app:/data/UI/atlas.png",
  "settings":{"quality":{"compression":"none"}}}]} (see the assets skill).
- Fonts: @font-face { font-family: "Name"; src: "app:/data/Fonts/File.ttf"; }. Prefer static
  TTFs; variable fonts can render squished on Windows.

## RCSS is not CSS

- Sizes in dp (density-independent pixels), never px. vw / vh are the document's
  width / height - 100vw x 100vh fills a screen-space document.
- rgba() alpha is 0-255, not 0-1: rgba(30,90,200,160).
- No "solid" keyword. Only properties RmlUi supports; the full rules are in the
  scripting-api skill.
- A button only takes a size you give it with display: block.
- One element cannot run a keyframe animation and a transition on the same property:
  a transform animation started while a hover/press transform transition is running
  is dropped with "Could not animate property". Put the animation on a wrapper.
- Never put focus: none on anything clickable, or on an ancestor of it (focus is
  inherited). A click goes to the nearest element that can take focus, so a click
  listener under focus: none never fires; mousedown and mouseup still do. The log
  warns when such a listener is added.

## Showing a document

Add component "UI Document" to an entity and set:

- asset - the .rhtml key
- render_mode - screen_space_overlay for HUDs and menus, sized to the game view;
  world_space for a panel in the scene that follows the entity's transform
  (size in pixels, pixels_per_world_unit sets its scale)
- enabled - hide or show it without removing it (visible in C#)

In C#: ui = owner.GetComponent<UIDocumentComponent>(); ui.document is null until
loaded; ui.documentLoaded += doc => ... runs for the current document and after every
reload. ui.visible hides it keeping its state; ui.Close() removes it until ui.Reload();
Reload() also resets a document to its initial markup.

## Data models

    <body data-model="hud">
      <span>{{ credit | money }}</span>
      <div class="overlay" data-if="showBonus">BONUS</div>
      <div class="row" data-for="row, i : rows" data-class-gold="row.rare">{{ i }}. {{ row.name }}</div>
      <button data-event-click="Spin()" data-class-busy="spinning">START</button>
      <input type="range" min="0" max="1" step="0.1" data-value="volume"/>
      <button onclick="Pause">II</button>
    </body>

    var model = ui.GetModel("hud");          // OnCreate; exists before the document loads
    model.Bind(hud);                         // public fields/properties -> variables,
                                             // public methods -> data-event-* events
    model.AddTransform<long>("money", v => v.ToString("N0"));
    hud.credit += win;
    model.Dirty("credit");                   // push: views using it update next frame

- Values: bool, numbers, string, enums (their name), Color (an RCSS colour); classes
  and structs nest ({{ row.name }}); lists and arrays are arrays (data-for, rows.size,
  rows[2]). Expressions: == != < > && || ! ?: + - * / and | transforms.
- Views: {{ }}, data-if, data-visible, data-class-x, data-style-prop, data-attr-x,
  data-value, data-checked, data-event-click (any event).
- After changing bound values call Dirty(name) or DirtyAll(). After a model event
  method runs the whole model updates. A control bound with data-value / data-checked
  writes back and updates only the variable it wrote: a property setter that changes
  other values too must Dirty them (keep the UIDataModel in a non-public field). model.autoDirty = true compares the top-level
  scalars every frame instead (prototypes, small models).
- data-event-click="Spin(3, 'gold')" calls the bound object's public Spin; a first
  parameter of an event class (UIClickEvent e) receives the event. BindEvent(name,
  (e, args) => ...) and Bind("name", () => value, v => value = v) bind single entries.
- onclick="Pause" (any on<event>) calls a public Pause() or Pause(UIClickEvent e) on a
  script of the document's entity.
- data-for keeps its template element hidden beside the clones: QueryAll(".row")
  includes it (filter with e.visible).
- Out of play mode nothing binds the model: {{ }} is empty and data-class-x is off,
  but data-style-prop writes an empty value and logs "Syntax error parsing inline
  property declaration". Prefer data-class-x with RCSS classes, or set the style from
  C# (el.style.opacity = v).
- Models and their bindings survive document reloads; bindings end with the script
  that made them. A model name the markup does not declare logs a warning.

## Elements and controls

    var document = ui.document;
    var score = document.Q("#score");              // RCSS selector; QueryAll for a list
    score.text = "Score: 10";                      // plain text, markup shows as written
    panel.InnerRml = "<b>Bonus</b>";               // parses markup, replaces all children
    document.Q("#start").clicked += Spin;          // pointer click or Click()
    var volume = document.Q<UISlider>("#volume");  // typed control, null if not a slider

- Every element is a UIElement; controls are subclasses chosen by tag: UIButton,
  UIInput (text input / textarea: Select, SetSelectionRange), UISlider (range: float
  value, min, max, step), UIToggle (checkbox / radio: isChecked), UISelect
  (selectedIndex, options, AddOption, RemoveOption, ClearOptions), UIProgress (value,
  max), UITabSet (activeTab, tabCount, SetTab, RemoveTab, tabChanged).
- The base class does the common part on any element: value (text; sends "change"),
  SetValueWithoutNotify, disabled, clicked. el.As<UISlider>() / el.Is<UIToggle>() /
  Q<T>(selector) give the typed view; typed setters have WithoutNotify variants too.
- An element reference can outlive its element (InnerRml on an ancestor, Remove(), a
  close or reload). That is safe: IsValid() turns false, calls do nothing, getters
  return defaults. Fetch the element again (or in documentLoaded).
- State: EnableClass(name, on) / ToggleClass / HasClass for states the stylesheet
  describes; SetPseudoClass("checked", on) for :checked rules. style["prop"] = "rcss
  value" sets an inline value, = null removes it; style.opacity / left / top / width /
  height (dp) are numeric shortcuts for per-frame writes. GetComputed("prop") reads the
  resolved value. visible = false hides; true restores the stylesheet's display.
- DOM: document.CreateElement("div"), AppendChild, InsertBefore, Remove(), Clone(),
  parent, children, Closest(selector), document.root. Prefer data-for (or one template
  element cloned per row) over InnerRml strings.
- Geometry: rect / paddingRect / contentRect in the document's pixels, y down - for a
  screen-space document the space of Input.mousePosition. Reads lay the document out
  first when it changed. document.ElementAt(point) hit-tests. scrollTop, scrollLeft,
  scrollHeight, ScrollTo(offset, smooth).
- Over world objects: var p = camera.WorldToScreenPoint(worldPos); when p.z > 0 set an
  absolutely positioned marker's style.left = p.x / document.pixelsPerDp (top likewise);
  RCSS lengths are dp, screen points pixels. Screen.resized fires when the view resizes.
- Motion: el.Animate("opacity", "0", 0.3f, UIEasing.CubicOut).completed += ...;
  loops: -1 repeats forever, alternate: true ping-pongs; anim.Stop() freezes it. Use
  RCSS @keyframes / transition for designed motion and RestartAnimation() to replay it
  (el.SetClass("hit", true); el.RestartAnimation();).

## Events

- On<T>(callback) for the event a class stands for (UIClickEvent, UIChangeEvent,
  UIAnimationEvent, UISubmitEvent, UIScrollEvent, UITextInputEvent); On<T>("name",
  callback) for the rest (UIPointerEvent "mousemove", UIKeyEvent "keydown", UIFocusEvent
  "focus", UIDragEvent "dragdrop", UIEventBase for anything). phase: EventPhase.Capture
  runs before descendants, EventPhase.Target only for the element's own events. Once<T>
  fires once. Each returns a UISubscription: Dispose() it to unsubscribe a lambda.
- A callback taking a base class receives the subclasses too: On<UIPointerEvent>("click")
  gets the UIClickEvent, On<UIEventBase>("x") gets everything.
- e.target / e.currentTarget are the elements. Event objects are reused (like Unity's
  pooled events): do not keep them past the callback.
- Subscriptions end with the element or with the script that made them, whichever
  goes first; no cleanup code is needed in OnDestroy.
- Keys reach a document only while a text field (input, textarea, select) has focus;
  otherwise they go to the game. Read game keys with Input, not UI key events.

## Verifying

- Find elements with ui_query_elements, never by reading pixels off a screenshot:
  {"selector":"#start"} (any RCSS selector) or {"point":[x, y]} (game pixels; the
  element a click there reaches). It reports path, text, visibility, pseudo-classes
  (hover, checked, disabled...), form value, rect_document, and rect_game /
  center_game in game pixels - world-space documents projected through the Game
  panel's camera. Add "properties":["decorator","display"] to read computed values.
  Works in edit mode too.
- Click with ui_click_element {"selector":"#buy", "index":0} (play mode): it moves the
  virtual pointer to the element, waits for hover, clicks, and reports covered_by when
  another element sits over it (that one takes the click - a z-order bug). "text"
  types after the click, for a text field.
- Read the data a document shows with ui_get_model {"entity_id":...}: every bound
  variable's current value (objects and lists expanded), the event and transform
  names. Use it to check what a click changed, and whether a value the view shows
  stale is stale in C# too (then the script forgot Dirty) or only in the view.
- viewport_capture_game in play mode shows the overlay over the game; logs_get_recent
  shows script errors and RmlUi warnings. Pass width + height to check a layout at a
  given resolution (1280 x 720, 3840 x 2160); viewport_set_render_size
  {"panel":"game"} holds one while you click and query (element rects follow it).
- Lower level: input_send_mouse {x, y, button} (game space), and
  input_send_keys {"text":"hello"} after a field has focus (key presses alone carry no
  characters). The game, its UI and the editor all follow the virtual pointer; clear
  it (or stop play) when done - while it is set the user's own mouse does not reach
  the editor.
