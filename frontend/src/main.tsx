import React from "react";
import { createRoot } from "react-dom/client";
import App from "@/App";
import "@/globals.css";
import { AppProviders } from "@/providers/AppProviders";

createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <AppProviders>
      <App />
    </AppProviders>
  </React.StrictMode>,
);
