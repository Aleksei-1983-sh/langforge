import { defineConfig } from 'vite';
import tsconfigPaths from 'vite-tsconfig-paths';
import { resolve } from 'path';

export default defineConfig({
  root: '.',
  plugins: [tsconfigPaths()],
  server: { port: 3000 },

  build: {
    outDir: 'dist',
    rollupOptions: {
      input: {
        login: resolve(__dirname, 'pages/login/index.html'),
        register: resolve(__dirname, 'pages/register/index.html'),
        dashboard: resolve(__dirname, 'pages/dashboard/index.html')
      }
    }
  }
});
