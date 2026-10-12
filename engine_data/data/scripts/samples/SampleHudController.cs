using System;
using System.Collections.Generic;
using System.Globalization;
using Unravel.Core;

namespace Unravel.Samples
{
    /// <summary>
    /// Drives engine:/data/ui/sample_hud.rhtml through a data model: put it on an entity with a UI
    /// Document of that asset. The markup reads the HudState fields ({{ score }}, data-for over the
    /// inventory, data-value on the controls) and calls its public methods (data-event-click); the
    /// script only changes values and marks them dirty.
    /// </summary>
    public class SampleHudController : ScriptComponent
    {
        public class InventoryItem
        {
            public string name;
            public int count;
            public bool rare;
        }

        /// <summary>The data model "sample_hud".</summary>
        public class HudState
        {
            public long score;
            public int health = 100;
            public bool paused;
            public float volume = 0.8f;
            public bool showFps;
            public string difficulty = "normal";
            public List<InventoryItem> inventory = new List<InventoryItem>
            {
                new InventoryItem { name = "Potion", count = 3 },
                new InventoryItem { name = "Dragon Scale", count = 1, rare = true }
            };

            /// <summary>data-event-click="TogglePause()"; the model updates after it runs.</summary>
            public void TogglePause()
            {
                paused = !paused;
            }
        }

        private readonly HudState state = new HudState();
        private UIDataModel model;
        private float damageTimer;

        public override void OnCreate()
        {
            var ui = owner.GetComponent<UIDocumentComponent>();
            if (ui == null)
            {
                Log.Error("SampleHudController needs a UI Document with engine:/data/ui/sample_hud.rhtml");
                return;
            }
            model = ui.GetModel("sample_hud");
            model.Bind(state);
            model.AddTransform<long>("thousands", v => v.ToString("N0", CultureInfo.InvariantCulture));
        }

        public override void OnUpdate()
        {
            if (model == null || state.paused)
            {
                return;
            }
            state.score += 10;
            model.Dirty(nameof(HudState.score));
            damageTimer += Time.deltaTime;
            if (damageTimer >= 1.0f && state.health > 10)
            {
                damageTimer = 0.0f;
                state.health -= 5;
                model.Dirty(nameof(HudState.health));
            }
        }

        /// <summary>onclick="Heal" on the button calls this method of a script on the document's entity.</summary>
        public void Heal(UIClickEvent e)
        {
            state.health = 100;
            model?.Dirty(nameof(HudState.health));
            Log.Info($"Healed by a click at ({e.x}, {e.y}); volume {state.volume:F2}, difficulty {state.difficulty}");
        }
    }
}
