import { readFile, writeFile, mkdir, rm } from 'node:fs/promises';
import { build } from 'esbuild';

await rm('dist', { recursive: true, force: true });
await mkdir('dist', { recursive: true });
await build({
  entryPoints: ['app/native.js'], outfile: 'dist/native.js', bundle: true,
  format: 'iife', target: 'safari15', minify: true
});

for (const filename of ['mantis.html', 'viewer.html', 'index.html']) {
  let html = await readFile(filename, 'utf8');
  // The app is self-contained; use the existing system font fallbacks offline.
  html = html.replace(/<link[^>]+fonts\.googleapis\.com[^>]*>\n?/, '');
  html = html.replace('initial-scale=1.0', 'initial-scale=1.0, viewport-fit=cover');
  html = html.replace('</head>', `<script src="native.js"></script>
<style>
  header { flex-wrap: wrap; gap: 16px; }
  .header-right { flex-wrap: wrap; }
  .config-input { min-width: 0; width: 100%; }
  button { min-height: 44px; }
  a, button { -webkit-tap-highlight-color: transparent; }
  @media (max-width: 700px) { .logo-text { font-size: 1.3rem; } .logo-svg { height: 36px; } }
</style></head>`);
  html = html.replaceAll('href="index.html"', 'href="menu.html"');
  if (filename === 'index.html') {
    // Only Mantis and its saved-session viewer are packaged in this app.
    html = html.replace(/\s*<a href="https:\/\/zmalkani\.github\.io\/openRow\/"[\s\S]*?<\/a>/, '');
  }
  await writeFile(`dist/${filename === 'index.html' ? 'menu.html' : filename}`, html);
}
await writeFile('dist/index.html', '<!doctype html><html><head><meta charset="utf-8"><meta http-equiv="refresh" content="0;url=mantis.html"><title>openRow</title></head><body><a href="mantis.html">Open Mantis</a></body></html>');
console.log('Built offline app assets in dist/.');
