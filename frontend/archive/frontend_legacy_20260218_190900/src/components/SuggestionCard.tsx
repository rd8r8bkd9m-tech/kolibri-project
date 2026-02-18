import type { ElementType } from "react";

interface SuggestionCardProps {
  icon: ElementType;
  title: string;
  onSelect: () => void;
}

const SuggestionCard = ({ icon: Icon, title, onSelect }: SuggestionCardProps) => (
  <button
    type="button"
    onClick={onSelect}
    className="flex items-center gap-2 rounded-full border border-border-strong/60 bg-background-input/80 px-4 py-2 text-sm font-medium text-text-secondary transition hover:border-primary/60 hover:text-text-primary"
  >
    <span className="flex h-8 w-8 items-center justify-center rounded-full bg-primary/15 text-primary">
      <Icon className="h-4 w-4" />
    </span>
    <span>{title}</span>
  </button>
);

export default SuggestionCard;
