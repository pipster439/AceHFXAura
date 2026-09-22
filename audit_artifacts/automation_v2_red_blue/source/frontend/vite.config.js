import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';
import { viteSingleFile } from 'vite-plugin-singlefile';
import path from 'path';

export default defineConfig({
  plugins: [
    react(),
    tailwindcss(),
    viteSingleFile()
  ],
  build: {
    outDir: path.resolve(__dirname, '../web'),
    emptyOutDir: false,
    cssCodeSplit: false,
    assetsInlineLimit: 100000000
  }
});
