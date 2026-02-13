import { useId } from 'react';

interface KolibriBrandMarkProps {
  size?: number;
  className?: string;
}

export const KolibriBrandMark = ({ size = 24, className }: KolibriBrandMarkProps) => {
  const gradientId = useId();

  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 48 48"
      fill="none"
      role="img"
      aria-label="Колибри"
      className={className}
    >
      <defs>
        <linearGradient id={gradientId} x1="9" y1="9" x2="39" y2="39" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="var(--brand-primary, #35ddd3)" />
          <stop offset="100%" stopColor="var(--brand-secondary, #5b8eff)" />
        </linearGradient>
      </defs>

      <path
        d="M24 8C15.2 8 8 15.2 8 24C8 32.8 15.2 40 24 40C27.8 40 31.4 38.7 34.3 36.3L30.4 32.4C28.6 33.7 26.4 34.4 24 34.4C18.3 34.4 13.6 29.7 13.6 24C13.6 18.3 18.3 13.6 24 13.6C26.2 13.6 28.3 14.3 30 15.4L34 11.4C31.1 9.2 27.7 8 24 8Z"
        fill={`url(#${gradientId})`}
        fillOpacity="0.9"
      />
      <path
        d="M8.8 38.6L21 26.4L18 33.2L39.2 10L27 22.2L30 15.3L8.8 38.6Z"
        fill={`url(#${gradientId})`}
      />
    </svg>
  );
};

