import type { PropsWithChildren, ReactNode } from "react";

interface AppShellProps {
  navigation?: ReactNode;
  header?: ReactNode;
  inspector?: ReactNode;
  footer?: ReactNode;
}

const AppShell = ({ navigation, header, inspector, footer, children }: PropsWithChildren<AppShellProps>) => (
  <div className="min-h-screen bg-gradient-to-br from-background-main via-[#0f1b33] to-background-main text-text-primary">
    <div className="mx-auto flex min-h-screen max-w-7xl gap-6 px-4 pb-10 pt-8 lg:px-8">
      {navigation && <aside className="hidden shrink-0 xl:flex">{navigation}</aside>}
      <div className="flex min-h-[80vh] flex-1 flex-col gap-6">
        {header}
        <div className="flex flex-1 flex-col gap-6 lg:flex-row">
          <section className="flex min-h-[32rem] flex-1 flex-col gap-6">{children}</section>
          {inspector && <aside className="hidden w-full max-w-xs shrink-0 lg:flex xl:max-w-sm">{inspector}</aside>}
        </div>
        {footer}
      </div>
    </div>
  </div>
);

export default AppShell;
