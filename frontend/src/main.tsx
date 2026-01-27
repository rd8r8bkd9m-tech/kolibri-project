import React from "react";
import ReactDOM from "react-dom/client";
import DesktopOS from "./DesktopOS";
import "./styles/tailwind.css";

ReactDOM.createRoot(document.getElementById("root") as HTMLElement).render(
  <React.StrictMode>
    <DesktopOS />
  </React.StrictMode>,
);
