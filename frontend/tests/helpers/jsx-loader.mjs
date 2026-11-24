import { transformSync } from 'esbuild';
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';

export async function resolve(specifier, context, nextResolve) {
  try {
    return await nextResolve(specifier, context);
  } catch (err) {
    if (err.code === 'ERR_MODULE_NOT_FOUND' && !specifier.startsWith('node:')) {
      for (const ext of ['.js', '.jsx', '/index.js', '/index.jsx']) {
        try {
          return await nextResolve(specifier + ext, context);
        } catch {}
      }
    }
    throw err;
  }
}

export async function load(url, context, nextLoad) {
  if (url.endsWith('.jsx')) {
    const filePath = fileURLToPath(url);
    const raw = fs.readFileSync(filePath, 'utf8');
    const { code } = transformSync(raw, {
      loader: 'jsx',
      format: 'esm',
      sourcefile: filePath
    });
    return {
      format: 'module',
      shortCircuit: true,
      source: code
    };
  }
  return nextLoad(url, context);
}