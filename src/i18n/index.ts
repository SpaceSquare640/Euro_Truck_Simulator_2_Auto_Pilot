// Edited by: OpenAI-Codex_Responsible: Website
import registry from "./locales.json";
import en from "./en.json";
import zhHant from "./zh-hant.json";

const dictionaries = { en, "zh-hant": zhHant satisfies Record<MessageKey, string> } as const;

export type Locale = keyof typeof dictionaries;
export type MessageKey = keyof typeof en;
export const defaultLocale = registry.default as Locale;

export function t(key: MessageKey, locale: Locale): string {
  return dictionaries[locale][key];
}

export function localePath(locale: Locale, path = ""): string {
  return `${import.meta.env.BASE_URL}${locale}/${path}`;
}

export function htmlLang(locale: Locale): string {
  const entry = registry.locales.find((item) => item.code === locale && item.enabled);
  if (!entry) throw new Error(`Unknown locale: ${locale}`);
  return entry.htmlLang;
}

export interface AlternateLocale {
  locale: Locale;
  htmlLang: string;
  autonym: string;
  href: string;
  current: boolean;
}

export function alternateLocales(current: Locale, pathname: string): AlternateLocale[] {
  const enabled = registry.locales.filter((entry) => entry.enabled);
  if (enabled.length < 2 || !pathname.startsWith(localePath(current))) return [];
  const rest = pathname.slice(localePath(current).length);
  return enabled.map((entry) => ({
    locale: entry.code as Locale,
    htmlLang: entry.htmlLang,
    autonym: entry.autonym,
    href: localePath(entry.code as Locale, rest),
    current: entry.code === current,
  }));
}
