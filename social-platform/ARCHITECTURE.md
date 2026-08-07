# System Architecture Documentation

## Overview

This document provides a comprehensive overview of the Social Platform architecture, covering all three networks, proximity discovery, and deployment considerations.

## System Components

### 1. Backend (NestJS)

#### Core Modules

**AuthModule**
- JWT-based authentication
- User registration creates 3 profiles automatically
- Refresh token mechanism
- Password hashing with bcrypt

**UsersModule**
- User account management
- Last seen tracking

**ProfilesModule**
- Three profile types: PROFESSIONAL, SOCIAL, PRIVATE
- Profile visibility rules
- Connection management (FOLLOW, FRIEND, SUPER_PRIVATE_REQUEST)
- Peepin system (profile view tracking)

**PostsModule**
- Post creation with content types (TEXT, IMAGE, LINK, PROTOTYPE)
- Like and comment system
- Feed generation based on connections
- Redis caching for feeds

**MessagesModule**
- Conversation management per network type
- End-to-end encryption for PRIVATE network
- Message read receipts
- AES-256-GCM encryption

**ProximityModule**
- Redis GEO for location storage
- Nearby user discovery
- Ghost mode implementation (60-second TTL)
- Radar enable/disable
- Scheduled stale user detection

**RedisModule**
- Global Redis service
- GEO operations
- User presence tracking
- WebSocket session management
- Feed and profile caching
- Rate limiting

**GatewayModule**
- WebSocket server (Socket.io)
- Real-time proximity updates
- Message broadcasting
- Location ping handling

**StorageModule**
- S3-compatible storage (MinIO)
- File uploads
- Presigned URLs

### 2. Frontend (Next.js)

#### Key Components

**Radar Component**
- Polar coordinate mapping
- Ghost mode animation (fade + drift)
- Real-time user updates
- Distance visualization

**NetworkTabs**
- Switch between three networks
- Visual indicators

**RadarToggle**
- Enable/disable radar mode
- WebSocket integration

**Pages**
- Login/Register
- Home (with radar)
- Profile view
- Request inbox (to be implemented)

#### State Management

- Zustand for auth state
- Local storage persistence
- WebSocket real-time updates

### 3. Database (PostgreSQL)

#### Schema Highlights

**Users Table**
- Base user account
- Email, phone, password hash

**Profiles Table**
- Three profiles per user (unique constraint)
- Type: PROFESSIONAL, SOCIAL, PRIVATE
- Visibility: PUBLIC, PRIVATE
- Professional always PUBLIC
- Private always PRIVATE

**Connections Table**
- Connection types: FOLLOW, FRIEND, SUPER_PRIVATE_REQUEST
- Status: PENDING, ACCEPTED, REJECTED, BLOCKED
- Unique constraint on (from, to, type)

**Messages Table**
- Encrypted content for PRIVATE network
- Message types: TEXT, IMAGE, FILE
- Read receipts

**Profile Views Table**
- Peepin tracking
- Daily deduplication

**Proximity Sessions Table**
- Location history
- Status tracking (ACTIVE, GHOSTING, OFFLINE)

### 4. Redis

#### Data Structures

**GEO Sorted Set: `user_locations`**
- Stores user coordinates
- Key: profileId
- Value: (longitude, latitude)

**String Keys: `user_status:{profileId}`**
- Values: ACTIVE, GHOSTING, OFFLINE
- TTL: 15s (ACTIVE), 60s (GHOSTING)

**String Keys: `ghost_time:{profileId}`**
- Unix timestamp when ghosting started
- TTL: 60s

**Set: `online_users`**
- Active user profile IDs

**String Keys: `ws_session:{socketId}`**
- Maps socket ID to profile ID

**String Keys: `ws_profile:{profileId}`**
- Maps profile ID to socket ID

**String Keys: `feed:{profileId}`**
- Cached feed data
- TTL: 300s

**String Keys: `profile:{profileId}`**
- Cached profile data
- TTL: 600s

**String Keys: `radar_enabled:{profileId}`**
- Radar opt-in flag

## Data Flow

### Proximity Discovery Flow

1. User enables radar → `POST /proximity/radar { enabled: true }`
2. Frontend requests location permission
3. Every 10s: Frontend sends location ping → WebSocket `location:update`
4. Backend updates Redis GEO → `GEOADD user_locations`
5. Backend sets status → `SETEX user_status:{id} 15 ACTIVE`
6. Backend queries nearby users → `GEORADIUS user_locations`
7. Filters by radar enabled + visibility rules
8. Returns nearby users → WebSocket `proximity:nearby`
9. Frontend renders on radar with polar coordinates

### Ghost Mode Flow

1. User stops pinging (no location update for 15s)
2. Scheduled task detects stale user → `checkStaleUsers()`
3. Backend sets GHOSTING status → `SETEX user_status:{id} 60 GHOSTING`
4. Backend stores ghost time → `SETEX ghost_time:{id} 60 {timestamp}`
5. Frontend receives GHOSTING status in `proximity:nearby`
6. Frontend animates: opacity fade + outward drift
7. After 60s: Backend removes from GEO → `ZREM user_locations`
8. Backend sets OFFLINE → `SETEX user_status:{id} 1 OFFLINE`
9. Frontend removes user from radar

### Super Private Access Flow

1. User A views User B's Professional/Social profile
2. User A clicks "Request Super Private Access"
3. Backend creates connection → `connection_type: SUPER_PRIVATE_REQUEST, status: PENDING`
4. User B sees request in inbox
5. User B accepts → `status: ACCEPTED`
6. User A can now create PRIVATE conversation
7. Messages encrypted with AES-256-GCM
8. Backend decrypts on retrieval

### Peepin Flow

1. User A views User B's profile (PROFESSIONAL or SOCIAL)
2. Backend checks if already viewed today
3. If not, creates `ProfileView` record
4. User B can query views → `GET /profiles/:id/views?networkType=`
5. Returns list of viewers with timestamps

## Security Considerations

### Authentication
- JWT tokens with 7-day expiry
- Refresh tokens with 30-day expiry
- Token stored in HTTP-only cookies (recommended for production)

### Encryption
- Super private messages: AES-256-GCM
- Encryption key stored in environment variable
- IV and auth tag stored with ciphertext

### Privacy
- No exact coordinates exposed to users
- Only approximate distance shown
- Radar requires opt-in from both users
- Profile visibility enforced at API level

### Rate Limiting
- Redis-based rate limiting
- Per-endpoint limits
- Prevents abuse

## Scalability Considerations

### Current MVP Limitations
- Single server deployment
- No load balancing
- No database replication
- No Redis clustering

### Production Scaling Path

**Database**
- Read replicas for read-heavy operations
- Connection pooling
- Query optimization
- Indexing strategy

**Redis**
- Redis Cluster for horizontal scaling
- Persistence configuration
- Memory optimization

**Backend**
- Multiple instances behind load balancer
- WebSocket adapter for Socket.io clustering
- Stateless design (all state in Redis/DB)

**Frontend**
- CDN for static assets
- Server-side rendering optimization
- Image optimization

## Monitoring & Observability

### Health Checks
- `/health` endpoint
- Database connection check
- Redis connection check

### Logging
- Structured logging (to be implemented)
- Error tracking
- Performance metrics

### Metrics to Track
- Active users
- Proximity discoveries
- Message throughput
- API response times
- WebSocket connections

## Deployment Architecture

### Docker Compose Setup

```
┌─────────────┐
│  Frontend   │ (Next.js, Port 3000)
└──────┬──────┘
       │
       ▼
┌─────────────┐
│   Backend   │ (NestJS, Port 3001)
└──────┬──────┘
       │
   ┌───┴───┬──────────┬─────────┐
   │       │          │         │
   ▼       ▼          ▼         ▼
┌─────┐ ┌──────┐ ┌────────┐ ┌──────┐
│ DB  │ │Redis │ │ MinIO  │ │ WS   │
└─────┘ └──────┘ └────────┘ └──────┘
```

### Production Deployment

Recommended architecture:

```
                    ┌─────────────┐
                    │   CDN/      │
                    │   Frontend  │
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │ Load Balancer│
                    └──────┬───────┘
                           │
        ┌──────────────────┼──────────────────┐
        │                  │                  │
   ┌────▼────┐       ┌────▼────┐       ┌────▼────┐
   │ Backend │       │ Backend │       │ Backend │
   │  (WS)   │       │  (WS)   │       │  (WS)   │
   └────┬────┘       └────┬────┘       └────┬────┘
        │                 │                 │
        └─────────────────┼─────────────────┘
                          │
        ┌─────────────────┼─────────────────┐
        │                 │                 │
   ┌────▼────┐       ┌────▼────┐       ┌────▼────┐
   │PostgreSQL│      │  Redis  │       │   S3    │
   │(Primary) │      │ Cluster │       │         │
   └────┬────┘       └─────────┘       └─────────┘
        │
   ┌────▼────┐
   │PostgreSQL│
   │(Replica) │
   └─────────┘
```

## API Rate Limits

- Authentication: 5 requests/minute
- Proximity updates: 10 requests/minute
- Post creation: 20 requests/hour
- Message sending: 60 requests/minute
- Profile views: 100 requests/hour

## Future Enhancements

1. **True E2E Encryption**: Client-side key management
2. **Push Notifications**: Mobile app support
3. **Media Processing**: Image/video optimization
4. **Search**: Full-text search for profiles/posts
5. **Analytics**: User engagement metrics
6. **Moderation**: Content moderation system
7. **Groups**: Group conversations
8. **Stories**: Temporary content (24h)
