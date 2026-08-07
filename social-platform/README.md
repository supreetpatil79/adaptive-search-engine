# Social Platform - Three-Network Social Media Application

A deployable full-stack social media web application combining three interconnected social networks with real-time proximity discovery.

## 🎯 Core Concept

Each user has **three separate identities** under one account:

1. **Professional Network** - Always public, LinkedIn-like
2. **Social Network** - Public/Private, Instagram/Facebook-like  
3. **Super Private Network** - Hidden by default, WhatsApp/Telegram-like

## ✨ Key Features

- **Proximity Discovery**: Radar mode with GPS/Bluetooth BLE support
- **Ghost Mode**: 60-second fly-away animation when users leave proximity
- **Peepin System**: Track who viewed your profile
- **End-to-End Encryption**: Super private messages are encrypted
- **Real-time Updates**: WebSocket-powered live updates
- **Three-Network Architecture**: Isolated but linked profiles

## 🏗️ Architecture

### Backend (NestJS)
- RESTful API for all network operations
- WebSocket gateway for real-time updates
- Redis GEO for proximity detection
- PostgreSQL for persistent data
- End-to-end encryption for super private messages

### Frontend (Next.js)
- React components with Tailwind CSS
- Radar animation for proximity discovery
- Three-network interface (Professional, Social, Super Private)
- Real-time updates via WebSocket

## 🚀 Quick Start

### Prerequisites

- Docker and Docker Compose
- Node.js 20+ (for local development)
- PostgreSQL 15+ (or use Docker)
- Redis 7+ (or use Docker)

### Option 1: Docker Compose (Recommended)

```bash
# Clone repository
cd social-platform

# Create environment files
cp backend/.env.example backend/.env
cp frontend/.env.local.example frontend/.env.local

# Edit environment variables (IMPORTANT: Change secrets!)
# backend/.env
# frontend/.env.local

# Start all services
docker-compose up -d

# Initialize database schema
docker-compose exec db psql -U postgres -d social_platform -f /path/to/backend/src/config/database.schema.sql

# Or use TypeORM migrations
cd backend
npm install
npm run migration:run

# Access the application
# Frontend: http://localhost:3000
# Backend API: http://localhost:3001
# MinIO Console: http://localhost:9001
```

### Option 2: Local Development

#### Backend Setup

```bash
cd backend
npm install

# Create .env file
cp .env.example .env
# Edit .env with your configuration

# Start PostgreSQL and Redis (or use Docker)
docker-compose up -d db redis minio

# Run migrations
npm run migration:run

# Start dev server
npm run start:dev
```

#### Frontend Setup

```bash
cd frontend
npm install

# Create .env.local
cp .env.local.example .env.local
# Edit .env.local with API URLs

# Start dev server
npm run dev
```

## 📁 Project Structure

```
social-platform/
├── backend/                 # NestJS Backend
│   ├── src/
│   │   ├── auth/           # Authentication module
│   │   ├── users/          # User management
│   │   ├── profiles/       # Profile management (3 networks)
│   │   ├── posts/          # Posts, likes, comments
│   │   ├── messages/       # Messaging system
│   │   ├── proximity/     # Proximity detection
│   │   ├── redis/          # Redis service
│   │   ├── gateway/        # WebSocket gateway
│   │   └── storage/        # S3 storage service
│   └── Dockerfile
├── frontend/               # Next.js Frontend
│   ├── app/                # Next.js app router
│   ├── components/         # React components
│   │   └── Radar/          # Radar animation component
│   ├── lib/                # Utilities (API, WebSocket)
│   └── store/              # Zustand state management
├── deployment/             # Deployment documentation
└── docker-compose.yml      # Docker Compose configuration
```

## 🔑 Environment Variables

### Backend (.env)

```env
PORT=3001
DB_HOST=localhost
DB_PORT=5432
DB_USERNAME=postgres
DB_PASSWORD=postgres
DB_DATABASE=social_platform
REDIS_HOST=localhost
REDIS_PORT=6379
JWT_SECRET=your-secret-key
JWT_EXPIRES_IN=7d
S3_ENDPOINT=http://localhost:9000
S3_ACCESS_KEY=minioadmin
S3_SECRET_KEY=minioadmin
S3_BUCKET=social-platform-media
FRONTEND_URL=http://localhost:3000
ENCRYPTION_KEY=your-32-character-key
```

### Frontend (.env.local)

```env
NEXT_PUBLIC_API_URL=http://localhost:3001
NEXT_PUBLIC_WS_URL=http://localhost:3001
```

## 🎮 Usage

### Registration

1. Navigate to `/login`
2. Click "Register"
3. Enter email, password (and optional phone)
4. Three profiles are automatically created (Professional, Social, Private)

### Proximity Discovery

1. Enable Radar toggle in header
2. Grant location permissions
3. View nearby users on radar
4. Tap users to view profiles
5. Send connection requests

### Super Private Access

1. View a user's Professional or Social profile
2. Click "Request Super Private Access"
3. Wait for acceptance
4. Once accepted, you can message in Super Private network

### Ghost Mode

- When users leave proximity, they enter "GHOSTING" state
- Profile remains visible for 60 seconds
- Slowly fades and drifts outward
- Still tappable but cannot initiate new chats
- Automatically removed after 60 seconds

## 🔒 Security Features

- JWT-based authentication
- Password hashing with bcrypt
- End-to-end encryption for super private messages (AES-256-GCM)
- Rate limiting via Redis
- CORS protection
- Input validation

## 📊 Database Schema

See `backend/src/config/database.schema.sql` for complete schema.

Key tables:
- `users` - Base user accounts
- `profiles` - Three profiles per user
- `posts` - Posts across networks
- `connections` - Follows, friends, super private requests
- `conversations` - Chat conversations
- `messages` - Encrypted messages
- `profile_views` - Peepin tracking
- `proximity_sessions` - Location tracking

## 🧪 API Endpoints

### Authentication
- `POST /auth/register` - Register new user
- `POST /auth/login` - Login
- `POST /auth/refresh` - Refresh access token
- `POST /auth/logout` - Logout

### Profiles
- `GET /profiles/me` - Get my profiles
- `GET /profiles/:id` - View profile
- `PUT /profiles/:id` - Update profile
- `GET /profiles/:id/views` - Get profile views (peepin)
- `POST /profiles/:id/connect` - Send connection request
- `GET /profiles/connections/requests` - Get incoming requests

### Proximity
- `POST /proximity/location` - Update location
- `GET /proximity/nearby` - Get nearby users
- `POST /proximity/radar` - Toggle radar
- `GET /proximity/radar` - Get radar status

### Posts
- `POST /posts` - Create post
- `GET /posts/feed` - Get feed
- `GET /posts/profile/:profileId` - Get profile posts
- `POST /posts/:id/like` - Like post
- `POST /posts/:id/comment` - Add comment

### Messages
- `GET /messages/conversations` - Get conversations
- `POST /messages/conversations` - Create conversation
- `GET /messages/conversations/:id/messages` - Get messages
- `POST /messages/conversations/:id/messages` - Send message

## 🐳 Docker Services

- **db** - PostgreSQL database
- **redis** - Redis cache
- **minio** - S3-compatible storage
- **backend** - NestJS API server
- **frontend** - Next.js frontend

## 📝 License

MIT

## 🤝 Contributing

This is an MVP for low number of users, optimized for clarity and correctness.

## 📚 Documentation

- [Deployment Guide](./deployment/DEPLOYMENT.md)
- [Database Schema](./backend/src/config/database.schema.sql)
- [API Documentation](./backend/README.md)
