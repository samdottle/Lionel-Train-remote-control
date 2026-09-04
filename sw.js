// O27 Controller service worker — Version 1.5 — 2026-09-04
const CACHE='o27-v1.5';
const STATIC_ASSETS=['./manifest.json','./icon-192.png','./icon-512.png'];

self.addEventListener('install',event=>{
  event.waitUntil(caches.open(CACHE).then(cache=>cache.addAll(STATIC_ASSETS)));
  self.skipWaiting();
});

self.addEventListener('activate',event=>{
  event.waitUntil((async()=>{
    const keys=await caches.keys();
    await Promise.all(keys.filter(key=>key!==CACHE).map(key=>caches.delete(key)));
    await self.clients.claim();
  })());
});

self.addEventListener('fetch',event=>{
  if(event.request.method!=='GET') return;
  const url=new URL(event.request.url);
  if(url.origin!==self.location.origin) return;

  // Network-first for the app shell prevents an old installed PWA from
  // pinning index.html forever after a GitHub Pages deployment.
  if(event.request.mode==='navigate' || url.pathname.endsWith('/index.html') || url.pathname.endsWith('/')){
    event.respondWith((async()=>{
      try{
        const fresh=await fetch(event.request,{cache:'no-store'});
        const cache=await caches.open(CACHE);
        cache.put('./index.html',fresh.clone());
        return fresh;
      }catch(_){
        return (await caches.match('./index.html')) || Response.error();
      }
    })());
    return;
  }

  event.respondWith((async()=>{
    const cached=await caches.match(event.request);
    if(cached) return cached;
    const fresh=await fetch(event.request);
    if(fresh.ok){
      const cache=await caches.open(CACHE);
      cache.put(event.request,fresh.clone());
    }
    return fresh;
  })());
});
