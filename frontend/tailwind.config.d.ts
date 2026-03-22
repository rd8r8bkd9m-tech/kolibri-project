declare const _default: {
    darkMode: ["class"];
    content: string[];
    theme: {
        container: {
            center: true;
            padding: string;
        };
        extend: {
            colors: {
                background: string;
                foreground: string;
                card: string;
                sidebar: string;
                muted: string;
                border: string;
                overlay: string;
                cyan: {
                    400: string;
                    500: string;
                    600: string;
                };
            };
            boxShadow: {
                "glow-cyan": string;
                "glow-cyan-sm": string;
                "soft-inner": string;
            };
            backdropBlur: {
                md: string;
            };
            keyframes: {
                ringPulse: {
                    "0%, 100%": {
                        boxShadow: string;
                    };
                    "50%": {
                        boxShadow: string;
                    };
                };
            };
            animation: {
                "ring-pulse": string;
            };
        };
    };
    plugins: any[];
};
export default _default;
