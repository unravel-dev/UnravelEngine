// Starts the browser player: the WebGPU device, the .NET runtime, the game's files, then Main, which starts the
// engine (game/web/Program.cs).
//
// The device is created here because bgfx cannot wait for it inside the engine without JSPI or Asyncify, which the
// .NET runtime does not use (deps/3rdparty/bgfx/patches/0003). The game's files come from data_manifest.json, which a
// web deploy writes next to this page, into the module's file system at the paths the engine reads them from. The
// engine's arguments come from the page's address, one per arg= (index.html?arg=--log-level&arg=trace).
//
// The player's own files (user:/ and cache:/, which SDL puts under /libsdl) are kept in the browser's IndexedDB:
// read in before the engine starts, and stored again whenever a written file is closed.
import { dotnet } from './_framework/dotnet.js';

const status = document.getElementById('status');
/// Where SDL_GetPrefPath puts the player's folders in the browser.
const USER_ROOT = '/libsdl';
/// What the page shows when the browser draws WebGPU on the processor (such as SwiftShader).
const SOFTWARE_ADAPTER_NOTICE = 'This browser draws without the graphics card, so the game runs very slowly. Turn on '
  + 'hardware acceleration in the browser settings, or play in a browser that gives WebGPU the graphics card.';

function report(text) {
  status.textContent = text;
  console.log('[player] ' + text);
}

async function createDevice() {
  if (!navigator.gpu) {
    throw new Error('This browser has no WebGPU.');
  }
  const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
  if (!adapter) {
    throw new Error('This browser found no WebGPU adapter.');
  }
  const info = adapter.info ?? {};
  const isSoftware = Boolean(info.isFallbackAdapter ?? adapter.isFallbackAdapter);
  const name = [info.vendor, info.architecture, info.device, info.description].filter(Boolean).join(' ');
  console.log(`[player] WebGPU on ${name || 'an unnamed adapter'}${isSoftware ? ', in software' : ''}`);
  if (isSoftware) {
    console.warn('[player] ' + SOFTWARE_ADAPTER_NOTICE);
  }
  const requiredLimits = {};
  for (const key in adapter.limits) {
    requiredLimits[key] = adapter.limits[key];
  }
  // Timestamps are left off in the browser (deps/3rdparty/bgfx/patches/0002).
  const requiredFeatures = [...adapter.features].filter((feature) => feature !== 'timestamp-query');
  const device = await adapter.requestDevice({ requiredFeatures, requiredLimits });
  return { device, isSoftware };
}

// Above the canvas, which takes the page once the engine starts.
function showNotice(text) {
  const notice = document.createElement('div');
  notice.textContent = text;
  notice.style.cssText = 'position: fixed; left: 0; right: 0; top: 0; z-index: 1; padding: 8px 16px; '
    + 'background: rgba(0, 0, 0, 0.75); color: #ddd; font: 14px sans-serif;';
  document.body.appendChild(notice);
}

async function loadFiles(module) {
  const files = await (await fetch('data_manifest.json')).json();
  let loaded = 0;
  await Promise.all(files.map(async (path) => {
    const bytes = new Uint8Array(await (await fetch(path)).arrayBuffer());
    const slash = path.lastIndexOf('/');
    const folder = '/' + path.substring(0, slash);
    module.FS_createPath('/', path.substring(0, slash), true, true);
    module.FS_createDataFile(folder, path.substring(slash + 1), bytes, true, true, true);
    loaded++;
    report(`Loading ${loaded} / ${files.length}`);
  }));
}

function syncFiles(module, populate) {
  return new Promise((resolve, reject) => module.FS.syncfs(populate, (error) => (error ? reject(error) : resolve())));
}

async function mountUserFiles(module) {
  module.FS.mkdirTree(USER_ROOT);
  module.FS.mount(module.FS.filesystems.IDBFS, { autoPersist: true }, USER_ROOT);
  try {
    await syncFiles(module, true);
  } catch (e) {
    console.warn('[player] The saved files could not be read, and the game starts without them: ' + e.message);
  }
}

// miniaudio starts the sound on the page's first click or touch, as browsers require; the first key starts it too,
// for a game played with the keyboard.
function unlockAudioOnKey() {
  document.addEventListener('keydown', () => globalThis.miniaudio?.unlock(), { capture: true, once: true });
}

try {
  report('Starting WebGPU');
  const { device, isSoftware } = await createDevice();
  report('Starting .NET');
  const runtime = await dotnet
    .withModuleConfig({ preinitializedWebGPUDevice: device })
    .withApplicationArgumentsFromQuery()
    .create();
  const module = runtime.Module ?? globalThis.getDotnetRuntime(0).Module;
  await loadFiles(module);
  await mountUserFiles(module);
  report('Starting the engine');
  const result = await runtime.runMain();
  if (result !== 0) {
    throw new Error(`The engine did not start (${result}).`);
  }
  status.remove();
  unlockAudioOnKey();
  if (isSoftware) {
    showNotice(SOFTWARE_ADAPTER_NOTICE);
  }
  console.log('[player] started');
} catch (e) {
  report('The game could not start: ' + e.message);
  console.error(e);
}
