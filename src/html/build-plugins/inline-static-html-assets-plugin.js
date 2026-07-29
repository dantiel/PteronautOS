import { promises as fs } from 'fs'
import path from 'path'

function inlineForHtml(code, tagName) {
  return code.replace(new RegExp(`</${tagName}`, 'gi'), `<\\/${tagName}`)
}

function toDataUrl(contentType, data) {
  return `data:${contentType};base64,${data.toString('base64')}`
}

export function inlineStaticHtmlAssetsPlugin() {
  let outDir = 'dist'
  let root = process.cwd()

  return {
    name: 'inline-static-html-assets',
    apply: 'build',
    configResolved(config) {
      outDir = config.build?.outDir || 'dist'
      root = config.root || process.cwd()
    },
    async writeBundle() {
      const distDir = path.resolve(root, outDir)
      const assetsDir = path.join(distDir, 'assets')
      const indexPath = path.join(distDir, 'index.html')

      let assetNames
      try {
        assetNames = await fs.readdir(assetsDir)
      } catch {
        return
      }

      // Entry chunk: app-[hash].js  but NOT app-core-[hash].js
      const appJsName = assetNames.find(
        (name) => /^app-[^.]+\.js$/.test(name) && !name.startsWith('app-core-')
      )
      const appCssName = assetNames.find((name) => /^app-[^.]+\.css$/.test(name))
      const faviconName = assetNames.find((name) => /^favicon-[^.]+\.svg$/.test(name))

      if (!appCssName && !appJsName && !faviconName) {
        return
      }

      let indexHtml = await fs.readFile(indexPath, 'utf8')

      if (appCssName) {
        const appCssPath = path.join(assetsDir, appCssName)
        const appCss = await fs.readFile(appCssPath, 'utf8')
        indexHtml = indexHtml.replace(
          /<link rel="stylesheet" crossorigin href="\/assets\/app-[^"]+\.css">/,
          `<style>${inlineForHtml(appCss, 'style')}</style>`
        )
        await fs.unlink(appCssPath)
      }

      if (appJsName) {
        const appJsPath = path.join(assetsDir, appJsName)
        let appJs = await fs.readFile(appJsPath, 'utf8')
        // Rewrite import so it resolves against the firmware's flat asset ns
        appJs = appJs.replace(/\.\/(app-core-[^"'`]+\.js)/g, '/assets/$1')
        indexHtml = indexHtml.replace(
          /<script type="module" crossorigin src="\/assets\/app-[^"]+\.js"><\/script>/,
          `<script type="module">${inlineForHtml(appJs, 'script')}</script>`
        )
        // Remove only the entry's own <link rel="modulepreload">
        const escapedName = appJsName.replace(/[.+\-]/g, '\\$&')
        indexHtml = indexHtml.replace(
          new RegExp(`\\s*<link rel="modulepreload" crossorigin href="\/assets\/${escapedName}">`, 'g'),
          ''
        )
        await fs.unlink(appJsPath)
      }

      if (faviconName) {
        const faviconPath = path.join(assetsDir, faviconName)
        const faviconDataUrl = toDataUrl('image/svg+xml', await fs.readFile(faviconPath))
        indexHtml = indexHtml.replace(
          /<link[^>]+href="\/assets\/favicon-[^"]+\.svg"[^>]*>/,
          `<link href="${faviconDataUrl}" rel="icon" type="image/svg+xml" />`
        )
        await fs.unlink(faviconPath)
      }

      await fs.writeFile(indexPath, indexHtml, 'utf8')
      this.info('Inlined static HTML assets into index.html.')
    },
  }
}
