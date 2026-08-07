import { Injectable, Inject } from '@nestjs/common';
import { RedisService } from '../redis/redis.service';

@Injectable()
export class GatewayService {
  constructor(private redisService: RedisService) {}

  /**
   * Emit event to specific user
   */
  async emitToUser(profileId: string, event: string, data: any): Promise<void> {
    const socketId = await this.redisService.getSocketFromProfile(profileId);
    if (socketId) {
      // This will be handled by the gateway
      // Store event in Redis for gateway to pick up
      await this.redisService.set(
        `ws_event:${socketId}:${event}`,
        JSON.stringify(data),
        10,
      );
    }
  }

  /**
   * Broadcast to nearby users
   */
  async broadcastToNearby(
    profileId: string,
    event: string,
    data: any,
  ): Promise<void> {
    // Get nearby users from Redis GEO
    // This is a simplified version - in production, you'd query Redis GEO
    const nearby = await this.redisService.getNearbyUsers(0, 0, 100);
    for (const user of nearby) {
      if (user.profileId !== profileId) {
        await this.emitToUser(user.profileId, event, data);
      }
    }
  }
}

