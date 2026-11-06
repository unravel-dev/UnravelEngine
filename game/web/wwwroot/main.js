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
//
// The game's files and the runtime's are kept in the browser's Cache Storage, so a later visit downloads only what a
// new deploy changed: a data file under its fingerprint from the manifest, a runtime file under its name, which
// carries its own. Each page keeps its own caches, and drops what its deploy no longer lists once the game started.
// A page without Cache Storage (plain HTTP from another computer) downloads everything.
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

async function openCache(kind) {
  try {
    return globalThis.caches ? await caches.open(`unravel-${kind}:${location.pathname}`) : null;
  } catch {
    return null;
  }
}

/// The response for `url` from `cache` under `key`, or from the network, which then goes into the cache.
async function fetchCached(cache, url, key, used) {
  used.add(key);
  const cached = cache ? await cache.match(key) : undefined;
  if (cached) {
    return cached;
  }
  const response = await fetch(url, { cache: 'no-cache' });
  if (!response.ok) {
    throw new Error(`${url} could not be downloaded (${response.status}).`);
  }
  if (cache) {
    await cache.put(key, response.clone()).catch(() => {});
  }
  return response;
}

/// Drops what `cache` holds that this start did not use: files an older deploy had.
async function dropUnused(cache, used) {
  if (!cache) {
    return;
  }
  for (const request of await cache.keys()) {
    if (!used.has(request.url)) {
      await cache.delete(request);
    }
  }
}

async function loadFiles(module, cache, used) {
  const files = await (await fetch('data_manifest.json', { cache: 'no-cache' })).json();
  let loaded = 0;
  await Promise.all(files.map(async ({ path, hash }) => {
    const url = new URL(path, location.href);
    const key = `${url.href}?fingerprint=${hash}`;
    const bytes = new Uint8Array(await (await fetchCached(cache, url.href, key, used)).arrayBuffer());
    const slash = path.lastIndexOf('/');
    const folder = '/' + path.substring(0, slash);
    module.FS_createPath('/', path.substring(0, slash), true, true);
    module.FS_createDataFile(folder, path.substring(slash + 1), bytes, true, true, true);
    loaded++;
    report(`Loading ${loaded} / ${files.length}`);
  }));
}

/// The runtime's binary files (assemblies, the wasm module, globalization data) from the cache; its JavaScript
/// modules load as the runtime loads them.
const CACHED_RUNTIME_TYPES = new Set(['assembly', 'pdb', 'dotnetwasm', 'globalization']);

function loadRuntimeResource(cache, used) {
  return (type, name, defaultUri) => {
    if (!cache || !CACHED_RUNTIME_TYPES.has(type)) {
      return null;
    }
    const url = new URL(defaultUri, location.href).href;
    return fetchCached(cache, url, url, used);
  };
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
  const runtimeCache = await openCache('runtime');
  const dataCache = await openCache('data');
  const usedRuntime = new Set();
  const usedData = new Set();
  const runtime = await dotnet
    .withModuleConfig({ preinitializedWebGPUDevice: device })
    .withResourceLoader(loadRuntimeResource(runtimeCache, usedRuntime))
    .withApplicationArgumentsFromQuery()
    .create();
  const module = runtime.Module ?? globalThis.getDotnetRuntime(0).Module;
  await loadFiles(module, dataCache, usedData);
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
  await Promise.all([dropUnused(runtimeCache, usedRuntime), dropUnused(dataCache, usedData)]);
} catch (e) {
  report('The game could not start: ' + e.message);
  console.error(e);
}
