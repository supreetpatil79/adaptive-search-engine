import { Injectable, Inject } from '@nestjs/common';
import Redis from 'ioredis';

@Injectable()
export class RedisService {
  constructor(@Inject('REDIS_CLIENT') private readonly redis: Redis) {}

  /**
   * GEO Operations for Proximity Detection
   */
  async addUserLocation(
    profileId: string,
    latitude: number,
    longitude: number,
  ): Promise<void> {
    await this.redis.geoadd('user_locations', longitude, latitude, profileId);
    // Set TTL for location data (15 seconds default)
    await this.redis.expire(`user_locations`, 15);
  }

  async getNearbyUsers(
    latitude: number,
    longitude: number,
    radius: number = 50, // meters
  ): Promise<Array<{ profileId: string; distance: number }>> {
    const results = await this.redis.georadius(
      'user_locations',
      longitude,
      latitude,
      radius,
      'm',
      'WITHCOORD',
      'WITHDIST',
    );

    return results.map((result: any) => ({
      profileId: result[0],
      distance: parseFloat(result[1]),
      longitude: parseFloat(result[2][0]),
      latitude: parseFloat(result[2][1]),
    }));
  }

  async removeUserLocation(profileId: string): Promise<void> {
    await this.redis.zrem('user_locations', profileId);
  }

  /**
   * User Presence and Status
   */
  async setUserStatus(
    profileId: string,
    status: 'ACTIVE' | 'GHOSTING' | 'OFFLINE',
    ttl: number = 60,
  ): Promise<void> {
    const key = `user_status:${profileId}`;
    await this.redis.setex(key, ttl, status);
  }

  async getUserStatus(profileId: string): Promise<string | null> {
    return await this.redis.get(`user_status:${profileId}`);
  }

  async setUserGhosting(profileId: string, ttl: number = 60): Promise<void> {
    await this.setUserStatus(profileId, 'GHOSTING', ttl);
    // Store ghosting timestamp
    await this.redis.setex(`ghost_time:${profileId}`, ttl, Date.now().toString());
  }

  async isUserGhosting(profileId: string): Promise<boolean> {
    const status = await this.getUserStatus(profileId);
    return status === 'GHOSTING';
  }

  /**
   * Online Users Tracking
   */
  async addOnlineUser(profileId: string): Promise<void> {
    await this.redis.sadd('online_users', profileId);
  }

  async removeOnlineUser(profileId: string): Promise<void> {
    await this.redis.srem('online_users', profileId);
  }

  async isUserOnline(profileId: string): Promise<boolean> {
    return (await this.redis.sismember('online_users', profileId)) === 1;
  }

  async getOnlineUsers(): Promise<string[]> {
    return await this.redis.smembers('online_users');
  }

  /**
   * WebSocket Session Management
   */
  async setWebSocketSession(
    socketId: string,
    profileId: string,
  ): Promise<void> {
    await this.redis.setex(`ws_session:${socketId}`, 3600, profileId);
    await this.redis.set(`ws_profile:${profileId}`, socketId);
  }

  async getProfileFromSocket(socketId: string): Promise<string | null> {
    return await this.redis.get(`ws_session:${socketId}`);
  }

  async getSocketFromProfile(profileId: string): Promise<string | null> {
    return await this.redis.get(`ws_profile:${profileId}`);
  }

  async removeWebSocketSession(socketId: string): Promise<void> {
    const profileId = await this.getProfileFromSocket(socketId);
    if (profileId) {
      await this.redis.del(`ws_profile:${profileId}`);
    }
    await this.redis.del(`ws_session:${socketId}`);
  }

  /**
   * Feed Caching
   */
  async cacheFeed(profileId: string, feed: any[], ttl: number = 300): Promise<void> {
    await this.redis.setex(
      `feed:${profileId}`,
      ttl,
      JSON.stringify(feed),
    );
  }

  async getCachedFeed(profileId: string): Promise<any[] | null> {
    const cached = await this.redis.get(`feed:${profileId}`);
    return cached ? JSON.parse(cached) : null;
  }

  /**
   * Profile Caching
   */
  async cacheProfile(profileId: string, profile: any, ttl: number = 600): Promise<void> {
    await this.redis.setex(
      `profile:${profileId}`,
      ttl,
      JSON.stringify(profile),
    );
  }

  async getCachedProfile(profileId: string): Promise<any | null> {
    const cached = await this.redis.get(`profile:${profileId}`);
    return cached ? JSON.parse(cached) : null;
  }

  /**
   * Rate Limiting
   */
  async checkRateLimit(
    key: string,
    limit: number,
    window: number,
  ): Promise<{ allowed: boolean; remaining: number }> {
    const current = await this.redis.incr(`rate_limit:${key}`);
    if (current === 1) {
      await this.redis.expire(`rate_limit:${key}`, window);
    }
    const ttl = await this.redis.ttl(`rate_limit:${key}`);
    return {
      allowed: current <= limit,
      remaining: Math.max(0, limit - current),
    };
  }

  /**
   * Radar Settings
   */
  async setRadarEnabled(profileId: string, enabled: boolean): Promise<void> {
    await this.redis.set(`radar_enabled:${profileId}`, enabled ? '1' : '0');
  }

  async isRadarEnabled(profileId: string): Promise<boolean> {
    const result = await this.redis.get(`radar_enabled:${profileId}`);
    return result === '1';
  }

  /**
   * Generic Redis operations
   */
  async get(key: string): Promise<string | null> {
    return await this.redis.get(key);
  }

  async set(key: string, value: string, ttl?: number): Promise<void> {
    if (ttl) {
      await this.redis.setex(key, ttl, value);
    } else {
      await this.redis.set(key, value);
    }
  }

  async del(key: string): Promise<void> {
    await this.redis.del(key);
  }

  async exists(key: string): Promise<boolean> {
    return (await this.redis.exists(key)) === 1;
  }
}

