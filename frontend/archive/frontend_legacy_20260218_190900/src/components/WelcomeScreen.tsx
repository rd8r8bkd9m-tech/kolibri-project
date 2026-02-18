import {
  BookOpenText,
  BrainCircuit,
  Compass,
  Lightbulb,
  Rocket,
  Wand2,
} from "lucide-react";
import SuggestionCard from "./SuggestionCard";

const suggestionItems = [
  { icon: Rocket, title: "Написать черновик", prompt: "Помоги мне написать текст..." },
  { icon: Compass, title: "Создать план", prompt: "Составь подробный план по теме..." },
  { icon: BookOpenText, title: "Узнать что-нибудь новое", prompt: "Что нового в мире Колибри?" },
  { icon: BrainCircuit, title: "Провести мозговой штурм", prompt: "Проведи мозговой штурм идей для..." },
  { icon: Lightbulb, title: "Получить совет", prompt: "Дай совет по улучшению проекта Колибри." },
  { icon: Wand2, title: "Практиковаться в языке", prompt: "Давай потренируем язык КолибриScript." },
] as const;

interface WelcomeScreenProps {
  onSuggestionSelect: (prompt: string) => void;
}

const WelcomeScreen = ({ onSuggestionSelect }: WelcomeScreenProps) => (
  <section className="flex h-full flex-col justify-center gap-10 rounded-3xl border border-border-strong bg-background-card/70 p-12 backdrop-blur">
    <div className="space-y-3">
      <p className="text-sm text-text-secondary">Привет, Владислав!</p>
      <h1 className="text-4xl font-semibold text-text-primary">
        Привет, Vladislav, чем Колибри может помочь сегодня?
      </h1>
      <p className="max-w-2xl text-sm text-text-secondary">
        Выбери подсказку или напиши свой запрос — я соберу формулы и знания, чтобы ответить тебе.
      </p>
    </div>
    <div className="flex flex-wrap gap-3">
      {suggestionItems.map((item) => (
        <SuggestionCard key={item.title} icon={item.icon} title={item.title} onSelect={() => onSuggestionSelect(item.prompt)} />
      ))}
    </div>
  </section>
);

export default WelcomeScreen;
