// Edited by: OpenAI-Codex_Responsible: Website
import { defineConfig } from "astro/config";

export default defineConfig({
  site: "https://spacesquare640.github.io",
  base: "/Euro_Truck_Simulator_2_Auto_Pilot/",
  output: "static",
  trailingSlash: "always",
  i18n: {
    defaultLocale: "en",
    locales: ["en"],
    routing: {
      prefixDefaultLocale: true,
    },
  },
});
