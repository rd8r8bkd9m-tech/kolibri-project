import React from "react";
import { createRoot } from "react-dom/client";
import { MantineProvider, createTheme, rem } from '@mantine/core';
import '@mantine/core/styles.css';
import '@mantine/notifications/styles.css';
import App from "./App";
import "./index.css";

const theme = createTheme({
  primaryColor: 'indigo',
  fontFamily: 'Inter, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif',
  defaultRadius: 'md',
  headings: {
    fontFamily: 'Inter, sans-serif',
    fontWeight: '600',
  },
  components: {
    Paper: {
      defaultProps: {
        radius: 'md',
      },
    },
  },
});

createRoot(document.getElementById("root")!).render(
  <React.StrictMode>
    <MantineProvider theme={theme} defaultColorScheme="dark">
      <App />
    </MantineProvider>
  </React.StrictMode>
);
