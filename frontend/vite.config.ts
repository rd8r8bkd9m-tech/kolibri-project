import { defineConfig } from "vitest/config";
import react from "@vitejs/plugin-react";
import type { Plugin } from "vite";
import { copyFile, mkdir, access, readFile } from "node:fs/promises";
import { spawn } from "node:child_process";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

type WasiPluginContext = "serve" | "build";

function copyKolibriWasm(): Plugin {
  const frontendDir = fileURLToPath(new URL(".", import.meta.url));
  const wasmSource = resolve(frontendDir, "../build/wasm/kolibri.wasm");
  const wasmBuilder = resolve(frontendDir, "../scripts/build_wasm.sh");
  const wasmInfoSource = resolve(frontendDir, "../build/wasm/kolibri.wasm.txt");
  const publicTarget = resolve(frontendDir, "public/kolibri.wasm");
  const publicInfoTarget = resolve(frontendDir, "public/kolibri.wasm.txt");
  let copied = false;
  let buildPromise: Promise<void> | null = null;

  const shouldAttemptAutoBuild = (() => {
    const value = process.env.KOLIBRI_SKIP_WASM_AUTOBUILD?.toLowerCase();

    if (!value) {
      return process.platform !== "win32";
    }

    return !["1", "true", "yes", "on"].includes(value);
  })();

  const buildKolibriWasm = () =>
    new Promise<void>((fulfill, reject) => {
      const child = spawn(wasmBuilder, {
        cwd: resolve(frontendDir, ".."),
        env: process.env,
        stdio: "inherit",
      });

      child.once("error", (error) => {
        reject(error);
      });

      child.once("exit", (code, signal) => {
        if (code === 0) {
          fulfill();
          return;
        }

        const reason =
          signal !== null
            ? `был прерван сигналом ${signal}`
            : `завершился с кодом ${code ?? "неизвестно"}`;
        reject(new Error(`build_wasm.sh ${reason}`));
      });
    });

  const ensureWasmPresent = async () => {
    let wasmExists = false;

    try {
      await access(wasmSource);
      wasmExists = true;
    } catch (accessError) {
      if (!shouldAttemptAutoBuild) {
        const messageParts = [
          `[copy-kolibri-wasm] Не найден ${wasmSource}.`,
          "Запустите scripts/build_wasm.sh вручную.",
        ];

        if (accessError instanceof Error && accessError.message) {
          messageParts.push(`Причина: ${accessError.message}`);
        }

        throw new Error(messageParts.join(" "));
      }
    }

    if (!wasmExists) {
      buildPromise ||= buildKolibriWasm();

      try {
        await buildPromise;
      } catch (buildError) {
        buildPromise = null;

        const messageParts = [
          `[copy-kolibri-wasm] Не найден ${wasmSource}.`,
          "Автосборка kolibri.wasm завершилась с ошибкой.",
          "Попробуйте запустить scripts/build_wasm.sh вручную.",
          "Чтобы отключить автосборку, задайте KOLIBRI_SKIP_WASM_AUTOBUILD=1.",
        ];

        if (buildError instanceof Error && buildError.message) {
          messageParts.push(`Причина: ${buildError.message}`);
        }

        throw new Error(messageParts.join(" "));
      }

      buildPromise = null;
      wasmExists = true;
    }

    try {
      await access(wasmSource);
    } catch (postBuildError) {
      const messageParts = [
        `[copy-kolibri-wasm] kolibri.wasm не появился по пути ${wasmSource} после сборки.`,
        "Проверьте вывод scripts/build_wasm.sh.",
      ];

      if (postBuildError instanceof Error && postBuildError.message) {
        messageParts.push(`Причина: ${postBuildError.message}`);
      }

      throw new Error(messageParts.join(" "));
    }

    try {
      await access(wasmInfoSource);
      const info = await readFile(wasmInfoSource, "utf-8");
      if (/kolibri\.wasm:\s*заглушка/i.test(info)) {
        throw new Error(
          "kolibri.wasm собран как заглушка. Установите Emscripten или Docker и повторите scripts/build_wasm.sh."
        );
      }
    } catch (infoError) {
      const messageParts = [
        `[copy-kolibri-wasm] Не удалось проверить ${wasmInfoSource}.`,
        "kolibri.wasm должен быть полноценным модулем, а не заглушкой.",
      ];

      if (infoError instanceof Error && infoError.message) {
        messageParts.push(`Причина: ${infoError.message}`);
      }

      throw new Error(messageParts.join(" "));
    }
  };

  const performCopy = async (_context: WasiPluginContext) => {
    if (copied) {
      return;
    }
    await ensureWasmPresent();

    await mkdir(dirname(publicTarget), { recursive: true });
    await copyFile(wasmSource, publicTarget);
    await copyFile(wasmInfoSource, publicInfoTarget);
    copied = true;
  };

  return {
    name: "copy-kolibri-wasm",
    async buildStart() {
      await performCopy("build");
    },
    async configureServer() {
      await performCopy("serve");
    },
  };
}

const knowledgeProxyTarget = process.env.KNOWLEDGE_API || "http://127.0.0.1:8001";

export default defineConfig({
  define: {
    __KOLIBRI_BUILD__: JSON.stringify(new Date().toISOString()),
  },
  plugins: [react(), copyKolibriWasm()],
  server: {
    port: 3000,
    proxy: {
      "/api": {
        target: knowledgeProxyTarget,
        changeOrigin: true,
      },
    },
  },
  test: {
    environment: "jsdom",
    globals: true,
    setupFiles: ["./src/test/setup.ts"],
  },
});
