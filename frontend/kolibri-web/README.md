# Kolibri Web App - SPA PWA

Modern, responsive web application for Kolibri License Management Platform.

**Author:** Vladislav Evgenievich Kochurov  
**Website:** https://kolibriai.ru  
**License:** Dual-licensed (Community AGPL-3.0 / Commercial)  
**Version:** 1.0.0

## Features

✨ **Progressive Web App (PWA)**
- Fully offline-functional
- Installable on any device
- Push notifications support
- App shell caching

🎨 **Modern UI**
- Dark theme optimized for extended use
- Responsive design (mobile/tablet/desktop)
- Tailwind CSS with custom components
- Smooth animations and transitions

🔐 **Security**
- JWT token-based authentication
- Secure token storage
- HTTPS required in production
- CORS protection

📱 **Responsive Design**
- Mobile-first approach
- Tablet optimization
- Desktop-friendly layouts
- Touch-friendly components

⚡ **Performance**
- Vite build optimization
- Code splitting
- Lazy loading
- Service worker caching

## Technology Stack

### Frontend
- **React 18.2.0** - UI library
- **React Router v6** - Client-side routing
- **TypeScript 5.2.2** - Type safety
- **Tailwind CSS 3.3.3** - Utility-first CSS

### State Management
- **Zustand 4.4.1** - Lightweight state management
- **React Query** - Server state management

### Build & Development
- **Vite 5.0.0** - Lightning-fast build tool
- **Vite PWA Plugin** - PWA support
- **Workbox** - Service worker toolkit

### API & Storage
- **Axios 1.5.0** - HTTP client
- **IndexedDB (idb)** - Offline storage
- **JWT Decode** - Token parsing

### UI Components & Utilities
- **React Icons** - Icon library
- **React Toastify** - Notifications
- **Date-fns** - Date utilities
- **Recharts** - Data visualization
- **QR Code** - QR code generation

## Project Structure

```
kolibri-web/
├── public/              # Static assets
│   └── sw.ts           # Service worker
├── src/
│   ├── components/     # Reusable UI components
│   │   └── Layout.tsx  # Main layout wrapper
│   ├── pages/          # Page components
│   │   ├── Login.tsx
│   │   ├── Dashboard.tsx
│   │   ├── Licenses.tsx
│   │   ├── LicenseDetail.tsx
│   │   ├── Payments.tsx
│   │   ├── Profile.tsx
│   │   ├── Settings.tsx
│   │   └── NotFound.tsx
│   ├── services/       # API and external services
│   │   └── api.ts
│   ├── store/          # Zustand stores
│   │   ├── auth.ts
│   │   └── license.ts
│   ├── types/          # TypeScript definitions
│   │   └── index.ts
│   ├── utils/          # Utility functions
│   ├── hooks/          # Custom React hooks
│   ├── constants/      # Constants
│   ├── App.tsx         # Root component
│   ├── main.tsx        # Entry point
│   └── index.css       # Global styles
├── index.html          # HTML template
├── vite.config.ts      # Vite configuration
├── tsconfig.json       # TypeScript configuration
├── package.json        # Dependencies
├── .env.example        # Environment template
└── README.md           # This file
```

## Getting Started

### Prerequisites
- Node.js 18+
- npm or pnpm

### Installation

1. **Clone the repository**
```bash
git clone https://github.com/kolibriai/kolibri-web.git
cd kolibri-web
```

2. **Install dependencies**
```bash
npm install
```

3. **Create environment file**
```bash
cp .env.example .env.local
# Edit .env.local with your configuration
```

### Development

Start the development server:
```bash
npm run dev
```

The app will be available at `http://localhost:5173`

### Build

Build for production:
```bash
npm run build
```

Preview the production build:
```bash
npm run preview
```

## Available Scripts

| Script | Description |
|--------|-------------|
| `npm run dev` | Start dev server |
| `npm run build` | Build for production |
| `npm run preview` | Preview production build |
| `npm run lint` | Run ESLint |
| `npm run type-check` | Run TypeScript check |
| `npm run test` | Run tests |
| `npm run test:coverage` | Generate coverage report |

## Configuration

### Environment Variables

Create `.env.local` file:

```env
VITE_API_URL=http://localhost:8000/api
VITE_APP_NAME=Kolibri
VITE_ENABLE_PWA=true
VITE_ENABLE_OFFLINE_MODE=true
```

### Tailwind CSS

Configuration in `tailwind.config.js`:
- Dark mode enabled by default
- Custom color scheme (Cyan/Blue primary)
- Extended font sizes and spacing

### TypeScript

Path aliases configured in `tsconfig.json`:
- `@/*` - Root src directory
- `@components/*` - Components directory
- `@pages/*` - Pages directory
- `@store/*` - Zustand stores
- `@services/*` - Services directory
- `@types/*` - Type definitions

## Pages & Routes

| Route | Component | Purpose |
|-------|-----------|---------|
| `/` | Dashboard | Main dashboard with stats |
| `/login` | Login | User authentication |
| `/licenses` | Licenses | License list and management |
| `/licenses/:id` | LicenseDetail | License details page |
| `/payments` | Payments | Payment history and methods |
| `/profile` | Profile | User profile management |
| `/settings` | Settings | Application settings |
| `*` | NotFound | 404 error page |

## Features in Detail

### Dashboard
- Real-time statistics
- License overview cards
- Recent activity
- Quick actions

### License Management
- View all licenses
- Filter and search
- License details with user/storage limits
- Renew licenses
- Download invoices

### Payment Management
- Payment history
- Multiple payment methods (6 Russian methods)
- Balance information
- Payment receipts

### User Profile
- Profile information
- Account settings
- Security options
- Activity history

### Settings
- Notification preferences
- Theme customization
- Language selection
- Security settings

## PWA Features

### Installation
1. Open the app in a compatible browser
2. Click "Install" or "Add to Home Screen"
3. App will run standalone

### Offline Support
- Service worker caches app shell
- Offline pages available
- Syncs when connection restored
- Local IndexedDB storage

### Push Notifications
- Configured in service worker
- Permission requested on first launch
- Real-time updates supported

## Security

### Authentication Flow
1. Login with email/password
2. Receive JWT token
3. Token stored securely in localStorage
4. Included in all API requests
5. Auto-refresh on expiry

### Protected Routes
- Login page not accessible when authenticated
- Other pages require valid token
- Auto-redirect to login on auth failure

### CORS
- Configured in backend
- Whitelist approved domains
- Credentials included in requests

## Performance Optimization

### Build Optimization
- Code splitting by route
- Lazy loading of components
- Tree shaking unused code
- Minification and compression

### Runtime Optimization
- Virtual scrolling for large lists
- Memoized components
- Debounced search/filter
- Progressive image loading

### Caching Strategy
- Service worker cache-first
- API responses cached
- Browser cache headers
- IndexedDB for offline data

## Deployment

### Build for Production
```bash
npm run build
npm run preview
```

### Deploy Options

**Vercel** (Recommended)
```bash
vercel deploy
```

**Netlify**
```bash
netlify deploy --prod --dir=dist
```

**Docker**
```dockerfile
FROM node:18-alpine
WORKDIR /app
COPY package*.json ./
RUN npm ci
COPY . .
RUN npm run build
EXPOSE 3000
CMD ["npm", "run", "preview"]
```

**Traditional Server**
```bash
# Build
npm run build

# Copy dist/ to web server root
# Configure server for SPA (all routes → index.html)
```

## Browser Support

| Browser | Version | Support |
|---------|---------|---------|
| Chrome | 90+ | ✅ Full |
| Firefox | 88+ | ✅ Full |
| Safari | 14+ | ✅ Full |
| Edge | 90+ | ✅ Full |
| Mobile Chrome | Latest | ✅ Full |
| Mobile Safari | Latest | ✅ Full |

## Development Guidelines

### Component Structure
```typescript
import { useState } from 'react';
import styles from './Component.module.css';

interface Props {
  title: string;
  onClose?: () => void;
}

export default function Component({ title, onClose }: Props) {
  const [state, setState] = useState(false);

  return <div className="p-4">{title}</div>;
}
```

### API Calls
```typescript
import api from '@services/api';

const data = await api.getLicenses();
```

### State Management
```typescript
import { useAuthStore } from '@store/auth';

const { user, logout } = useAuthStore();
```

### Styling
Use Tailwind CSS classes:
```tsx
<div className="flex items-center justify-between p-4 bg-gray-800 rounded-lg">
  <h2 className="text-lg font-bold">Title</h2>
  <button className="px-4 py-2 bg-cyan-500 rounded hover:bg-cyan-600">
    Action
  </button>
</div>
```

## Troubleshooting

### Installation Issues
**Problem:** npm install fails
```bash
# Clear cache
npm cache clean --force

# Try with legacy peer deps
npm install --legacy-peer-deps
```

### Port Already in Use
```bash
# Use different port
npm run dev -- --port 3000
```

### Service Worker Issues
- Open DevTools → Application → Service Workers
- Clear cache and hard refresh (Ctrl+Shift+R)
- Check Network tab for failed requests

### TypeScript Errors
```bash
npm run type-check
```

## Contributing

1. Fork the repository
2. Create feature branch (`git checkout -b feature/amazing-feature`)
3. Commit changes (`git commit -m 'Add amazing feature'`)
4. Push to branch (`git push origin feature/amazing-feature`)
5. Open Pull Request

## License

Dual-licensed:
- **Community:** AGPL-3.0 (Free, open-source)
- **Commercial:** Proprietary ($10K-$250K annually)

See LICENSE files for details.

## Support

- 📧 Email: support@kolibriai.ru
- 🌐 Website: https://kolibriai.ru
- 📱 Telegram: @kolibriai
- 💬 GitHub Issues: Report bugs

## Roadmap

### v1.1 (Q2 2025)
- [ ] Advanced analytics dashboard
- [ ] Custom branding options
- [ ] Team collaboration features
- [ ] Audit logs

### v1.2 (Q3 2025)
- [ ] Mobile app sync
- [ ] AI-powered recommendations
- [ ] Advanced security features
- [ ] Multi-language support (10+ languages)

### v2.0 (Q4 2025)
- [ ] Marketplace for plugins
- [ ] Advanced API
- [ ] Enterprise features
- [ ] White-label solution

## Credits

**Developer:** Vladislav Evgenievich Kochurov (всё везде)  
**Company:** Kolibri AI  
**Location:** Russia 🇷🇺  
**Website:** https://kolibriai.ru

© 2025 Kolibri. All rights reserved.

---

Made with ❤️ by Kolibri Team
