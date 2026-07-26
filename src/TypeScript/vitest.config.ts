/** Vitest configuration for the TypeScript test suite. */
import { defineConfig } from "vitest/config";

const config: ReturnType<typeof defineConfig> = defineConfig({
    test: {
        include: ["test/**/*.test.ts"],
        pool: "forks",
        fileParallelism: false,
        maxWorkers: 1,
        sequence: { concurrent: false },
        testTimeout: 30_000,
    },
});

export default config;
