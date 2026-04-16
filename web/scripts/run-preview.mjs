import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const thisFile = fileURLToPath(import.meta.url);
const scriptsDir = dirname(thisFile);
const viteBin = resolve(scriptsDir, '../node_modules/vite/bin/vite.js');

const child = spawn(process.execPath, ['--max-http-header-size=65536', viteBin, 'preview', ...process.argv.slice(2)], {
  stdio: 'inherit',
});

child.on('close', (code, signal) => {
  if (signal) {
    process.kill(process.pid, signal);
    return;
  }

  process.exit(code ?? 0);
});

child.on('error', (error) => {
  console.error(error);
  process.exit(1);
});
