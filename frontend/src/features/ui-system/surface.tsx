import type { PropsWithChildren } from "react";
import { cn } from "@/lib/utils";

export function AppPanel({
  className,
  children,
}: PropsWithChildren<{ className?: string }>) {
  return <section className={cn("v3-panel", className)}>{children}</section>;
}

export function SectionTitle({
  eyebrow,
  title,
  description,
  className,
}: {
  eyebrow?: string;
  title: string;
  description?: string;
  className?: string;
}) {
  return (
    <div className={cn("space-y-1.5", className)}>
      {eyebrow ? <p className="text-[11px] font-semibold uppercase tracking-[0.18em] text-muted">{eyebrow}</p> : null}
      <div>
        <h2 className="text-[1.02rem] font-semibold tracking-[-0.03em] text-foreground">{title}</h2>
        {description ? <p className="mt-1 text-sm leading-6 text-muted">{description}</p> : null}
      </div>
    </div>
  );
}
