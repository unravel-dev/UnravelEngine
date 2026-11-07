// Starts the browser player: the WebGPU device, the .NET runtime, the game's files, then Main, which starts the
// engine (game/web/Program.cs).
//
// The device is created here because bgfx cannot wait for it inside the engine without JSPI or Asyncify, which the
// .NET runtime does not use (deps/3rdparty/bgfx/patches/0003). The game's files come from data_manifest.json, which a
// web deploy writes next to this page, into the module's file system at the paths the engine reads them from. The
// engine's arguments come from the page's address, one per arg= (index.html?arg=--log-level&arg=trace).
//
// The player's own files (user:/ and cache:/, in the game's folder the manifest names, where SDL puts them) are kept in
// the browser's IndexedDB, in a database of that folder, so games on one site keep their saves apart: read in before
// the engine starts, stored again whenever a written file is closed, and at once when the page is hidden or closed.
//
// The game's files and the runtime's are kept in the browser's Cache Storage, so a later visit downloads only what a
// new deploy changed: a data file under its fingerprint from the manifest, a runtime file under its name, which
// carries its own. Each page keeps its own caches, and drops what its deploy no longer lists once the game started.
// WebGPU and Cache Storage are only given to secure pages: the page is served over HTTPS, or from this computer.
import { dotnet } from './_framework/dotnet.js';

const status = document.getElementById('status');
/// What the page shows when the browser draws WebGPU on the processor (such as SwiftShader).
const SOFTWARE_ADAPTER_NOTICE = 'This browser draws without the graphics card, so the game runs very slowly. Turn on '
  + 'hardware acceleration in the browser settings, or play in a browser that gives WebGPU the graphics card.';
/// Data files downloaded at once: each is held twice while it goes into the cache and the file system, so fewer keep
/// the page's memory low on a large game.
const LOAD_CONCURRENCY = 4;

function report(text) {
  status.textContent = text;
  console.log('[player] ' + text);
}

async function createDevice() {
  if (!globalThis.isSecureContext) {
    throw new Error('The game needs a secure page: serve it over HTTPS, or open it from this computer.');
  }
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

async function download(url) {
  const response = await fetch(url, { cache: 'no-cache' });
  if (!response.ok) {
    throw new Error(`${url} could not be downloaded (${response.status}).`);
  }
  return response;
}

/// The runtime's response for `url` from `cache` under `key`, or from the network, which then goes into the cache.
async function fetchCached(cache, url, key, used) {
  used.add(key);
  const cached = cache ? await cache.match(key) : undefined;
  if (cached) {
    return cached;
  }
  const response = await download(url);
  if (cache) {
    await cache.put(key, response.clone()).catch(() => {});
  }
  return response;
}

/// The bytes of a data file from `cache` under `key`, or from the network, then stored in the cache from the same
/// bytes: the download is read once.
async function fetchDataFile(cache, url, key, used) {
  used.add(key);
  const cached = cache ? await cache.match(key) : undefined;
  if (cached) {
    return new Uint8Array(await cached.arrayBuffer());
  }
  const bytes = new Uint8Array(await (await download(url)).arrayBuffer());
  if (cache) {
    await cache.put(key, new Response(bytes)).catch(() => {});
  }
  return bytes;
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

/// Runs `action` on every item, at most `limit` at a time.
async function forEachLimited(items, limit, action) {
  let next = 0;
  const work = async () => {
    while (next < items.length) {
      await action(items[next++]);
    }
  };
  await Promise.all(Array.from({ length: Math.min(limit, items.length) }, work));
}

async function loadFiles(module, cache, used, files) {
  let loaded = 0;
  await forEachLimited(files, LOAD_CONCURRENCY, async ({ path, hash }) => {
    const url = new URL(path, location.href);
    const bytes = await fetchDataFile(cache, url.href, `${url.href}?fingerprint=${hash}`, used);
    const slash = path.lastIndexOf('/');
    module.FS_createPath('/', path.substring(0, slash), true, true);
    module.FS_createDataFile('/' + path.substring(0, slash), path.substring(slash + 1), bytes, true, true, true);
    loaded++;
    report(`Loading ${loaded} / ${files.length}`);
  });
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

/// Keeps the game's folder in IndexedDB and reads the stored files in. When they cannot be read, the folder stays in
/// memory only: storing it would replace the saves it could not read. True when the folder is stored.
async function mountUserFiles(module, folder) {
  module.FS.mkdirTree(folder);
  module.FS.mount(module.FS.filesystems.IDBFS, { autoPersist: true }, folder);
  try {
    await syncFiles(module, true);
    return true;
  } catch (e) {
    console.warn('[player] The saved files could not be read, so the game starts without them and keeps what it '
      + 'writes in memory only: ' + e.message);
    module.FS.unmount(folder);
    module.FS.mkdirTree(folder);
    return false;
  }
}

/// A written file is stored on a timer: going hidden (another tab, the browser minimized, a phone switching apps)
/// stores the files at once, in case the browser ends the page from there. Registered after the engine's own listener,
/// which lets the game save first (OnApplicationPause). A tab that closes or reloads goes hidden as it unloads, after
/// pagehide, and an unloading page cannot finish the store, which IndexedDB makes asynchronous: both try, no more.
function storeUserFilesWhenHidden(module) {
  const store = () => module.FS.syncfs(false, (error) => error && console.warn('[player] ' + error));
  document.addEventListener('visibilitychange', () => document.visibilityState === 'hidden' && store());
  window.addEventListener('pagehide', store);
}

/// miniaudio starts the sound on the page's first click or touch, as browsers require; a key starts it too, for a game
/// played with the keyboard. A key that is no user activation (Escape) cannot start it, so the page keeps listening
/// until one does.
function unlockAudioOnKey() {
  const unlock = () => {
    if (navigator.userActivation && !navigator.userActivation.isActive) {
      return;
    }
    globalThis.miniaudio?.unlock();
    document.removeEventListener('keydown', unlock, true);
  };
  document.addEventListener('keydown', unlock, true);
}

async function start() {
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
  const module = runtime.Module;
  const manifest = await (await download('data_manifest.json')).json();
  await loadFiles(module, dataCache, usedData, manifest.files);
  const isStored = await mountUserFiles(module, manifest.user_folder);
  report('Starting the engine');
  const result = await runtime.runMain();
  if (result !== 0) {
    throw new Error(`The engine did not start (${result}).`);
  }
  status.remove();
  if (isStored) {
    storeUserFilesWhenHidden(module);
  }
  unlockAudioOnKey();
  if (isSoftware) {
    showNotice(SOFTWARE_ADAPTER_NOTICE);
  }
  console.log('[player] started');
  return () => Promise.all([dropUnused(runtimeCache, usedRuntime), dropUnused(dataCache, usedData)]);
}

let dropUnusedFiles = null;
try {
  dropUnusedFiles = await start();
} catch (e) {
  report('The game could not start: ' + e.message);
  console.error(e);
}
if (dropUnusedFiles) {
  dropUnusedFiles().catch((e) => console.warn('[player] Older cached files could not be dropped: ' + e.message));
}
