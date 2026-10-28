import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  base: './',
  plugins: [react({ include: /src\/.*\.tsx$/, exclude: /vendor/ }), {
    name: 'production-csp',
    apply: 'build',
    transformIndexHtml(html) { return html.replace("connect-src 'self' ws://127.0.0.1:5173", "connect-src 'none'"); },
  }],
  server: { host: '127.0.0.1', port: 5173, strictPort: true },
  build: { target: 'es2022' },
});
