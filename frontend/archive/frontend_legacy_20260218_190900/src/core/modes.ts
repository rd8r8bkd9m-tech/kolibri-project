export interface ModeOption {
  value: string;
  label: string;
}

export const MODE_OPTIONS: ModeOption[] = [
  { value: "neutral", label: "Нейтральный" },
  { value: "journal", label: "Журнал" },
  { value: "emoji", label: "Эмодзи" },
  { value: "analytics", label: "Аналитика" },
];

export const findModeLabel = (value: string): string => {
  const option = MODE_OPTIONS.find((item) => item.value === value);
  return option ? option.label : value;
};
