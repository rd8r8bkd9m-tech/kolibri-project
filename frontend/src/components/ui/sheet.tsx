import * as Dialog from "@radix-ui/react-dialog";
import { cn } from "@/lib/utils";

export const Sheet = Dialog.Root;
export const SheetTrigger = Dialog.Trigger;
export const SheetClose = Dialog.Close;

export function SheetContent({
  className,
  children,
  title = "Панель",
  description,
}: {
  className?: string;
  children: React.ReactNode;
  title?: string;
  description?: string;
}) {
  return (
    <Dialog.Portal>
      <Dialog.Overlay className="fixed inset-0 z-40 bg-overlay/60 backdrop-blur-sm" />
      <Dialog.Content
        aria-describedby={description ? undefined : undefined}
        className={cn("fixed inset-y-0 left-0 z-50 w-full max-w-md border-r border-border/10 bg-background/80 backdrop-blur-md", className)}
      >
        <Dialog.Title className="sr-only">
          {title}
        </Dialog.Title>
        {description ? (
          <Dialog.Description className="sr-only">
            {description}
          </Dialog.Description>
        ) : null}
        {children}
      </Dialog.Content>
    </Dialog.Portal>
  );
}
