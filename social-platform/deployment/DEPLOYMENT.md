# Deployment Instructions

## Prerequisites

- Docker and Docker Compose installed
- Git (to clone repository)
- At least 4GB RAM available
- Ports 3000, 3001, 5432, 6379, 9000, 9001 available

## Quick Start with Docker Compose

### 1. Clone and Setup

```bash
cd social-platform
```

### 2. Environment Configuration

#### Backend Environment

Create `backend/.env`:

```env
PORT=3001
NODE_ENV=production
DB_HOST=db
DB_PORT=5432
DB_USERNAME=postgres
DB_PASSWORD=postgres
DB_DATABASE=social_platform
REDIS_HOST=redis
REDIS_PORT=6379
JWT_SECRET=your-super-secret-jwt-key-change-in-production
JWT_EXPIRES_IN=7d
S3_ENDPOINT=http://minio:9000
S3_ACCESS_KEY=minioadmin
S3_SECRET_KEY=minioadmin
S3_BUCKET=social-platform-media
S3_REGION=us-east-1
FRONTEND_URL=http://localhost:3000
ENCRYPTION_KEY=your-32-character-encryption-key-here
```

#### Frontend Environment

Create `frontend/.env.local`:

```env
NEXT_PUBLIC_API_URL=http://localhost:3001
NEXT_PUBLIC_WS_URL=http://localhost:3001
```

### 3. Initialize Database

Before starting services, initialize the database schema:

```bash
# Option 1: Using Docker exec (after starting db service)
docker-compose up -d db
docker-compose exec db psql -U postgres -d social_platform -f /path/to/database.schema.sql

# Option 2: Copy schema file into container and run
docker cp backend/src/config/database.schema.sql social-platform-db:/tmp/
docker-compose exec db psql -U postgres -d social_platform -f /tmp/database.schema.sql
```

Or use TypeORM migrations:

```bash
cd backend
npm install
npm run migration:run
```

### 4. Start All Services

```bash
docker-compose up -d
```

This will start:
- PostgreSQL (port 5432)
- Redis (port 6379)
- MinIO (ports 9000, 9001)
- Backend API (port 3001)
- Frontend (port 3000)

### 5. Initialize MinIO Bucket

Access MinIO Console at http://localhost:9001
- Login: minioadmin / minioadmin
- Create bucket: `social-platform-media`
- Set bucket policy to public read

### 6. Verify Deployment

- Backend Health: http://localhost:3001/health
- Frontend: http://localhost:3000
- MinIO Console: http://localhost:9001

## Development Setup (Without Docker)

### Backend

```bash
cd backend
npm install

# Create .env file (see above)
cp .env.example .env

# Start PostgreSQL and Redis locally or via Docker
docker-compose up -d db redis minio

# Run migrations
npm run migration:run

# Start dev server
npm run start:dev
```

### Frontend

```bash
cd frontend
npm install

# Create .env.local
cp .env.local.example .env.local

# Start dev server
npm run dev
```

## Production Deployment

### Environment Variables

**CRITICAL**: Change these in production:

1. `JWT_SECRET` - Use a strong random string (32+ characters)
2. `ENCRYPTION_KEY` - Exactly 32 characters for AES-256
3. `DB_PASSWORD` - Strong database password
4. `MINIO_ROOT_PASSWORD` - Strong MinIO password
5. `FRONTEND_URL` - Your production frontend URL
6. `S3_*` - Use real S3 credentials if not using MinIO

### Security Checklist

- [ ] Change all default passwords
- [ ] Use HTTPS in production
- [ ] Set up proper CORS origins
- [ ] Enable database backups
- [ ] Set up Redis persistence
- [ ] Configure S3 bucket policies
- [ ] Set up monitoring and logging
- [ ] Enable rate limiting
- [ ] Set up SSL certificates

### Scaling Considerations

For production scaling:

1. **Database**: Use managed PostgreSQL (AWS RDS, etc.)
2. **Redis**: Use managed Redis (AWS ElastiCache, etc.)
3. **Storage**: Use AWS S3 or compatible service
4. **Backend**: Run multiple instances behind load balancer
5. **Frontend**: Use CDN (Vercel, Netlify, etc.)
6. **WebSocket**: Use Redis adapter for Socket.io clustering

## Troubleshooting

### Database Connection Issues

```bash
# Check if database is running
docker-compose ps db

# View database logs
docker-compose logs db

# Connect to database
docker-compose exec db psql -U postgres -d social_platform
```

### Redis Connection Issues

```bash
# Check Redis
docker-compose exec redis redis-cli ping

# View Redis logs
docker-compose logs redis
```

### Backend Issues

```bash
# View backend logs
docker-compose logs backend

# Restart backend
docker-compose restart backend
```

### Frontend Issues

```bash
# View frontend logs
docker-compose logs frontend

# Rebuild frontend
docker-compose up -d --build frontend
```

## Monitoring

### Health Checks

- Backend: `GET http://localhost:3001/health`
- Database: `docker-compose exec db pg_isready`
- Redis: `docker-compose exec redis redis-cli ping`

### Logs

```bash
# All services
docker-compose logs -f

# Specific service
docker-compose logs -f backend
```

## Backup and Restore

### Database Backup

```bash
docker-compose exec db pg_dump -U postgres social_platform > backup.sql
```

### Database Restore

```bash
docker-compose exec -T db psql -U postgres social_platform < backup.sql
```

### Redis Backup

```bash
docker-compose exec redis redis-cli SAVE
docker cp social-platform-redis:/data/dump.rdb ./redis-backup.rdb
```

## Cleanup

```bash
# Stop all services
docker-compose down

# Remove volumes (WARNING: deletes data)
docker-compose down -v
```
