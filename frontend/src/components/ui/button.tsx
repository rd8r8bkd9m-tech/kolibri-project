import * as React from "react";
import { Slot } from "@radix-ui/react-slot";
import { cva, type VariantProps } from "class-variance-authority";
import { cn } from "@/lib/utils";

const buttonVariants = cva(
  "inline-flex items-center justify-center rounded-xl text-sm font-medium transition-all focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-foreground/30 disabled:pointer-events-none disabled:opacity-40",
  {
    variants: {
      variant: {
        default: "bg-foreground text-background hover:opacity-90",
        ghost: "bg-transparent text-foreground hover:bg-foreground/5",
        outline: "border border-border/20 bg-card/60 text-foreground hover:bg-foreground/5",
      },
      size: {
        default: "h-10 px-4 py-2",
        icon: "h-10 w-10",
        lg: "h-12 px-5 text-base"
      }
    },
    defaultVariants: {
      variant: "default",
      size: "default"
    }
  },
);

export interface ButtonProps
  extends React.ButtonHTMLAttributes<HTMLButtonElement>,
    VariantProps<typeof buttonVariants> {
  asChild?: boolean;
}

export const Button = React.forwardRef<HTMLButtonElement, ButtonProps>(
  ({ className, variant, size, asChild = false, ...props }, ref) => {
    const Comp = asChild ? Slot : "button";
    return <Comp className={cn(buttonVariants({ variant, size }), className)} ref={ref} {...props} />;
  },
);
Button.displayName = "Button";
