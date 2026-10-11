---
name: ui
title: In-game UI - documents, style sheets, events
description: HUDs and menus with RmlUi - create .rhtml/.rcss from the engine templates, write them, show a document with a UI Document component, drive it from C# (elements by id, click callbacks, text), the ways RCSS differs from CSS, and how to verify it in play mode.
order: 58
---
# In-game UI

## Files

- ui_create_batch {"items":[{"kind":"document","folder":"app:/data/UI","name":"Hud"},
  {"kind":"style_sheet","folder":"app:/data/UI","name":"Hud"}]} copies the engine
  templates, the same path as the Content Browser's Create menu.
- The template document links engine:/data/ui/rml.rcss (base element styles) and
  a starter style sheet. Point the second link at your own .rcss key:
  <link type="text/rcss" href="app:/data/UI/Hud.rcss"/>
- Write whole files with ui_set_sources_batch. The asset watcher reimports them and
  a document already on screen reloads. ui_get_sources_batch reads them back, and
  reads engine files such as rml.rcss.

## RCSS is not CSS

- Sizes in dp (density-independent pixels), never px. vw / vh are the document's
  width / height - 100vw x 100vh fills a screen-space document.
- rgba() alpha is 0-255, not 0-1: rgba(30,90,200,160).
- No "solid" keyword. Only properties RmlUi supports; the full rules are in the
  scripting-api skill.
- A button only takes a size you give it with display: block.

## Showing a document

Add component "UI Document" to an entity and set (it is typed):

- asset - the .rhtml key
- render_mode - screen_space_overlay for HUDs and menus, sized to the game view;
  world_space for a panel in the scene that follows the entity's transform
  (size in pixels, pixels_per_world_unit sets its scale)
- enabled - hide or show it without removing it

## Driving it from C#

    var document = owner.GetComponent<UIDocumentComponent>().GetDocument();
    var score = document.GetElementById("score");   // or QuerySelector("css")
    score.InnerRml = "Score: 10";                    // replace content
    button.RegisterCallback<UIPointerEvent>("click", OnClick);

- GetDocument() is null until the document has loaded; fetch it in OnStart and
  cache the elements.
- SetClass(name, on), SetAttribute(name, value) restyle elements; UIKeyEvent and
  UIChangeEvent cover "keydown" and "change".
- engine:/data/scripts/samples/SampleUIWrapperController.cs is a complete example.

## Verifying

- viewport_capture_game in play mode shows the overlay over the game.
- Prove behaviour with a Log.Warning in the callback and logs_get_recent, not only
  a screenshot.
- Clicking from MCP: input_set_mouse_position (game space) then input_send_mouse
  click. The game, its UI and the editor all follow the virtual pointer, so the
  press lands on the element under it. Clear it (or stop play) when done - while it
  is set the user's own mouse does not reach the editor.
