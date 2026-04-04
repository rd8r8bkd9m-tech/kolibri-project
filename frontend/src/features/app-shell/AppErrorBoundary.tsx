import { Component, type ErrorInfo, type ReactNode } from "react";
import { AlertTriangle, RefreshCw } from "lucide-react";

type Props = {
  children: ReactNode;
};

type State = {
  hasError: boolean;
};

export class AppErrorBoundary extends Component<Props, State> {
  state: State = { hasError: false };

  static getDerivedStateFromError() {
    return { hasError: true };
  }

  override componentDidCatch(error: Error, errorInfo: ErrorInfo) {
    console.error("Kolibri V3 crashed", error, errorInfo);
  }

  private handleRetry = () => {
    this.setState({ hasError: false });
    window.location.reload();
  };

  override render() {
    if (this.state.hasError) {
      return (
        <div className="flex h-dvh min-h-[100svh] w-full items-center justify-center bg-background px-6 text-foreground">
          <div className="w-full max-w-md rounded-[1.75rem] border border-foreground/8 bg-card p-6 shadow-[0_24px_60px_rgba(15,23,42,0.08)]">
            <div className="flex h-12 w-12 items-center justify-center rounded-2xl border border-foreground/8 bg-background text-foreground">
              <AlertTriangle className="h-5 w-5" />
            </div>
            <h1 className="mt-4 text-[1.4rem] font-semibold tracking-[-0.05em] text-foreground">
              Чат временно сбился
            </h1>
            <p className="mt-3 text-sm leading-7 text-muted">
              Мы поймали ошибку интерфейса и остановили сбой, чтобы страница не уходила в пустой экран.
              Обнови Kolibri и продолжай диалог.
            </p>
            <button
              type="button"
              onClick={this.handleRetry}
              className="mt-6 inline-flex w-full items-center justify-center gap-2 rounded-full bg-foreground px-4 py-3 text-sm font-semibold text-background transition hover:opacity-90"
            >
              <RefreshCw className="h-4 w-4" />
              Перезагрузить Kolibri
            </button>
          </div>
        </div>
      );
    }

    return this.props.children;
  }
}
