/**
 * Vite plugin: HAML → HTML
 * Intercepts index.html loading and serves compiled index.haml instead.
 * Also handles .haml imports from JS (exports compiled HTML string).
 */
import haml from 'hamljs';
import fs from 'fs';

export function hamlPlugin() {
  return {
    name: 'vite-plugin-haml',
    load(id) {
      // Intercept index.html — serve compiled index.haml
      if (id.endsWith('index.html')) {
        const hamlPath = id.replace(/\.html$/, '.haml');
        if (fs.existsSync(hamlPath)) {
          const hamlSrc = fs.readFileSync(hamlPath, 'utf-8');
          return haml.render(hamlSrc);
        }
      }
      return null;
    },
    transform(src, id) {
      // For .haml files imported from JS: export as HTML string
      if (!id.endsWith('.haml')) return;
      const html = haml.render(src);
      return {
        code: `export default ${JSON.stringify(html)};`,
        map: null,
      };
    },
  };
}
