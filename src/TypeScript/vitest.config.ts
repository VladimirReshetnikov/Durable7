import { defineConfig } from "vitest/config";

const config: ReturnType<typeof defineConfig> = defineConfig({
    test: {
        include: ["test/**/*.test.ts"],
        pool: "forks",
        sequence: { concurrent: false },
        testTimeout: 30_000,
    },
});

export default config;
