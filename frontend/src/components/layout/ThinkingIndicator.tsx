import { motion } from "framer-motion";

export function ThinkingIndicator() {
  return (
    <motion.div initial={{ opacity: 0 }} animate={{ opacity: 1 }} className="flex flex-col items-center gap-3 py-8">
      <div className="h-24 w-24 rounded-full bg-foreground/10 blur-2xl" />
      <div className="-mt-16 flex items-center gap-2">
        {[0, 1, 2].map((i) => (
          <motion.div
            key={i}
            className="h-3 w-3 rounded-full bg-foreground"
            animate={{ scale: [1, 1.2, 1], opacity: [0.45, 1, 0.45] }}
            transition={{ duration: 0.6, repeat: Infinity, delay: i * 0.2 }}
          />
        ))}
      </div>
      <p className="text-sm font-medium text-muted">Думаю шаг за шагом...</p>
    </motion.div>
  );
}
