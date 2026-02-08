/**
 * App.tsx
 * 
 * Единая точка входа в Kolibri OS.
 * Использует унифицированный интерфейс в стиле Manus.
 * Все вкладки доступны через боковую навигацию.
 */

import { ManusAppUnified } from "./manus/ManusAppUnified";

const App = () => {
  return <ManusAppUnified />;
};

export default App;
