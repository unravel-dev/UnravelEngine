using System;
using Ace.Core;

namespace Ace.Samples
{
    /// <summary>
    /// Sample script demonstrating the use of UI wrapper objects for caching and direct manipulation.
    /// This approach allows you to get UI elements once and keep them for later use without repeated searches.
    /// </summary>
    public class SampleUIWrapperController : ScriptComponent
    {
        // Cached UI wrapper objects - these hold C++ pointers and can be reused
        private UIDocument document;
        private UIElement titleElement;
        private UIElement buttonElement;
        private UIElement textInputElement;
        
        private int clickCount = 0;

        public override void OnStart()
        {
            // Get the UI document component
            var uiDoc = owner.GetComponent<UIDocumentComponent>();
            if (uiDoc == null)
            {
                Log.Error("No UIDocumentComponent found on entity");
                return;
            }

            // Get the document wrapper - this can be cached and reused
            document = uiDoc.GetDocument();
            if (document == null)
            {
                Log.Error("Failed to get document wrapper - document may not be loaded");
                return;
            }

            Log.Info($"Got document wrapper for: {document.Title}");

            // Cache element wrappers - these hold direct C++ pointers for fast access
            CacheUIElements();
            
            // Set up initial UI state
            SetupInitialUI();
            
            // Register event handlers using the wrapper objects
            RegisterEventHandlers();
        }

        private void CacheUIElements()
        {
            // Get elements by ID and cache them - no need to search repeatedly
            titleElement = document.GetElementById("title");
            buttonElement = document.GetElementById("click-button");
            textInputElement = document.GetElementById("text-input");

            // You can also use CSS selectors
            var headerElement = document.QuerySelector("h1");
            
            // Validate that we found the elements we need
            if (titleElement == null)
            {
                Log.Warning("Title element not found");
            }
            else
            {
                Log.Info($"Cached title element: {titleElement.ElementId}");
            }

            if (buttonElement == null)
            {
                Log.Warning("Button element not found");
            }
            else
            {
                Log.Info($"Cached button element: {buttonElement.ElementId}");
            }

            if (textInputElement == null)
            {
                Log.Warning("Text input element not found");
            }
            else
            {
                Log.Info($"Cached text input element: {textInputElement.ElementId}");
            }
        }

        private void SetupInitialUI()
        {
            // Direct manipulation using cached wrappers - very fast!
            if (titleElement != null)
            {
                titleElement.InnerRml = "UI Wrapper Demo";
                titleElement.SetAttribute("style", "color: #00ff00; font-size: 24px;");
            }

            if (buttonElement != null)
            {
                buttonElement.InnerRml = "Click Me!";
                buttonElement.SetClass("highlight", true);
            }

            if (textInputElement != null)
            {
                textInputElement.SetAttribute("placeholder", "Type something here...");
            }
        }

        private void RegisterEventHandlers()
        {
            // Register events directly on the wrapper objects
            if (buttonElement != null)
            {
                buttonElement.AddEventListener("click", OnButtonClick);
            }

            if (textInputElement != null)
            {
                textInputElement.AddEventListener("change", OnTextInputChange);
                textInputElement.AddEventListener("keydown", OnTextInputKeyDown);
            }
        }

        public override void OnUpdate()
        {
            // Example of periodic UI updates using cached wrappers
            if (titleElement != null)
            {
                // Update title with current time - very fast since we're using cached wrapper
                var currentTime = DateTime.Now.ToString("HH:mm:ss");
                titleElement.SetAttribute("data-time", currentTime);
            }
        }

        // Event handlers
        private void OnButtonClick(UIEventBase ev)
        {
            clickCount++;
            
            if (buttonElement != null)
            {
                // Direct manipulation - no search needed!
                buttonElement.InnerRml = $"Clicked {clickCount} times!";
                
                // Add some visual feedback
                buttonElement.SetClass("clicked", true);
                
                // You can also manipulate other cached elements
                if (titleElement != null)
                {
                    titleElement.SetAttribute("style", $"color: hsl({clickCount * 30 % 360}, 70%, 50%);");
                }
            }
        }

        private void OnTextInputChange(UIEventBase ev)
        {
            if (textInputElement != null)
            {
                var value = textInputElement.GetAttribute("value");
                Log.Info($"Text input changed to: {value}");
                
                // Update title with the input value
                if (titleElement != null)
                {
                    titleElement.InnerRml = string.IsNullOrEmpty(value) ? "UI Wrapper Demo" : value;
                }
            }
        }

        private void OnTextInputKeyDown(UIEventBase ev)
        {
            if (ev.keyCode == 13) // Enter key
            {
                Log.Info("Enter pressed in text input");
                
                // Simulate button click when Enter is pressed
                if (buttonElement != null)
                {
                    buttonElement.Click();
                }
            }
        }

        public override void OnDestroy()
        {
            // No need to manually clean up wrappers - they automatically become invalid
            // when the underlying C++ objects are destroyed
            Log.Info("UI wrapper controller destroyed");
        }

        // Example of dynamic element creation and caching
        public void CreateDynamicElement()
        {
            if (document != null)
            {
                // You could create new elements dynamically and cache their wrappers
                // This would require additional C++ bindings for element creation
                Log.Info("Dynamic element creation would go here");
            }
        }

        // Example of element validation and re-caching
        public void ValidateAndRecacheElements()
        {
            bool needsRecache = false;

            // Check if any cached elements became invalid
            if (titleElement != null)
            {
                Log.Warning("Title element became invalid");
                titleElement = null;
                needsRecache = true;
            }

            if (buttonElement != null)
            {
                Log.Warning("Button element became invalid");
                buttonElement = null;
                needsRecache = true;
            }

            if (textInputElement != null)
            {
                Log.Warning("Text input element became invalid");
                textInputElement = null;
                needsRecache = true;
            }

            // Re-cache if needed
            if (needsRecache && document != null)
            {
                Log.Info("Re-caching UI elements");
                CacheUIElements();
                SetupInitialUI();
                RegisterEventHandlers();
            }
        }
    }
}
