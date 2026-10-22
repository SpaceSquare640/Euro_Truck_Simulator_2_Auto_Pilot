// Edited by: OpenAI-Codex_Responsible: Website
import en from "./en.json";

const dictionaries = { en } as const;

export type Locale = keyof typeof dictionaries;
export type MessageKey = keyof typeof en;

export function t(key: MessageKey, locale: Locale = "en"): string {
  return dictionaries[locale][key];
}
