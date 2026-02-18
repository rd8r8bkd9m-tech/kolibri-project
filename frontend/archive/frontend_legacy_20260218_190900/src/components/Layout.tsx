import type { PropsWithChildren, ReactNode } from "react";

interface LayoutProps {
  navigation?: ReactNode;
  sidebar?: ReactNode;
  children: ReactNode;
}

const Layout = ({ navigation, sidebar, children }: PropsWithChildren<LayoutProps>) => (
  <div className="min-h-screen bg-gradient-to-br from-background-main via-[#101a33] to-background-main text-text-primary">
    <div className="mx-auto flex min-h-screen max-w-7xl gap-8 px-4 pb-10 pt-8 lg:px-8">
      {navigation && <aside className="hidden shrink-0 xl:flex">{navigation}</aside>}
      {sidebar && <aside className="hidden w-72 shrink-0 lg:flex">{sidebar}</aside>}
      <main className="flex flex-1 flex-col gap-6">{children}</main>
    </div>
  </div>
);

export default Layout;
