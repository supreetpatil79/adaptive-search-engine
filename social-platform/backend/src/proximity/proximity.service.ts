import { Injectable, Inject } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { RedisService } from '../redis/redis.service';
import { ProximitySession, ProximityStatus } from './entities/proximity-session.entity';
import { Profile, ProfileType } from '../profiles/entities/profile.entity';

export interface NearbyUser {
  profileId: string;
  distance: number;
  status: 'ACTIVE' | 'GHOSTING' | 'OFFLINE';
  ghostTime?: number;
  profile?: {
    id: string;
    username: string;
    displayName: string;
    avatarUrl: string;
    type: ProfileType;
    visibility: string;
  };
}

@Injectable()
export class ProximityService {
  private readonly PING_INTERVAL = 10000; // 10 seconds
  private readonly GHOST_TIMEOUT = 15000; // 15 seconds
  private readonly GHOST_TTL = 60; // 60 seconds

  constructor(
    @InjectRepository(ProximitySession)
    private proximitySessionRepository: Repository<ProximitySession>,
    @InjectRepository(Profile)
    private profileRepository: Repository<Profile>,
    private redisService: RedisService,
  ) {}

  /**
   * Update user location and handle proximity detection
   */
  async updateLocation(
    profileId: string,
    latitude: number,
    longitude: number,
  ): Promise<void> {
    // Update Redis GEO
    await this.redisService.addUserLocation(profileId, latitude, longitude);
    
    // Set user as ACTIVE
    await this.redisService.setUserStatus(profileId, 'ACTIVE', 15);
    await this.redisService.addOnlineUser(profileId);

    // Update or create proximity session in DB
    let session = await this.proximitySessionRepository.findOne({
      where: { profileId },
    });

    if (!session) {
      session = this.proximitySessionRepository.create({
        profileId,
        latitude,
        longitude,
        status: ProximityStatus.ACTIVE,
      });
    } else {
      session.latitude = latitude;
      session.longitude = longitude;
      session.status = ProximityStatus.ACTIVE;
      session.lastPing = new Date();
    }

    await this.proximitySessionRepository.save(session);
  }

  /**
   * Get nearby users with radar filtering
   */
  async getNearbyUsers(
    profileId: string,
    latitude: number,
    longitude: number,
    radius: number = 50,
  ): Promise<NearbyUser[]> {
    // Check if user has radar enabled
    const radarEnabled = await this.redisService.isRadarEnabled(profileId);
    if (!radarEnabled) {
      return [];
    }

    // Get nearby users from Redis GEO
    const nearbyLocations = await this.redisService.getNearbyUsers(
      latitude,
      longitude,
      radius,
    );

    const nearbyUsers: NearbyUser[] = [];

    for (const location of nearbyLocations) {
      const nearbyProfileId = location.profileId;

      // Skip self
      if (nearbyProfileId === profileId) {
        continue;
      }

      // Check if nearby user has radar enabled
      const nearbyRadarEnabled = await this.redisService.isRadarEnabled(
        nearbyProfileId,
      );
      if (!nearbyRadarEnabled) {
        continue;
      }

      // Get user status
      const status = (await this.redisService.getUserStatus(
        nearbyProfileId,
      )) as 'ACTIVE' | 'GHOSTING' | 'OFFLINE' | null;

      if (!status) {
        continue;
      }

      // Get ghost time if ghosting
      let ghostTime: number | undefined;
      if (status === 'GHOSTING') {
        const ghostTimeStr = await this.redisService.get(
          `ghost_time:${nearbyProfileId}`,
        );
        ghostTime = ghostTimeStr ? parseInt(ghostTimeStr) : undefined;
      }

      // Get profile info (only what's visible)
      const profile = await this.profileRepository.findOne({
        where: { id: nearbyProfileId },
        select: ['id', 'username', 'displayName', 'avatarUrl', 'type', 'visibility'],
      });

      if (!profile) {
        continue;
      }

      // Visibility rules:
      // - Professional: always visible
      // - Social: visible if public
      // - Private: never visible on radar
      if (profile.type === ProfileType.PRIVATE) {
        continue;
      }

      if (
        profile.type === ProfileType.SOCIAL &&
        profile.visibility !== 'PUBLIC'
      ) {
        continue;
      }

      nearbyUsers.push({
        profileId: nearbyProfileId,
        distance: location.distance,
        status: status as 'ACTIVE' | 'GHOSTING' | 'OFFLINE',
        ghostTime,
        profile: {
          id: profile.id,
          username: profile.username,
          displayName: profile.displayName || profile.username,
          avatarUrl: profile.avatarUrl || '',
          type: profile.type,
          visibility: profile.visibility,
        },
      });
    }

    return nearbyUsers;
  }

  /**
   * Handle user leaving proximity (ghost mode)
   */
  async handleUserLeft(profileId: string): Promise<void> {
    // Check if user was ACTIVE
    const currentStatus = await this.redisService.getUserStatus(profileId);
    
    if (currentStatus === 'ACTIVE') {
      // Move to GHOSTING state
      await this.redisService.setUserGhosting(profileId, this.GHOST_TTL);
      
      // Update DB session
      const session = await this.proximitySessionRepository.findOne({
        where: { profileId },
      });
      
      if (session) {
        session.status = ProximityStatus.GHOSTING;
        await this.proximitySessionRepository.save(session);
      }

      // Schedule removal after TTL
      setTimeout(async () => {
        await this.removeUserFromProximity(profileId);
      }, this.GHOST_TTL * 1000);
    }
  }

  /**
   * Remove user from proximity completely
   */
  async removeUserFromProximity(profileId: string): Promise<void> {
    await this.redisService.removeUserLocation(profileId);
    await this.redisService.setUserStatus(profileId, 'OFFLINE', 1);
    await this.redisService.removeOnlineUser(profileId);

    const session = await this.proximitySessionRepository.findOne({
      where: { profileId },
    });

    if (session) {
      session.status = ProximityStatus.OFFLINE;
      await this.proximitySessionRepository.save(session);
    }
  }

  /**
   * Check for users who haven't pinged (stale detection)
   */
  async checkStaleUsers(): Promise<void> {
    const activeSessions = await this.proximitySessionRepository.find({
      where: { status: ProximityStatus.ACTIVE },
    });

    const now = new Date();
    const staleThreshold = new Date(now.getTime() - this.GHOST_TIMEOUT);

    for (const session of activeSessions) {
      if (session.lastPing < staleThreshold) {
        await this.handleUserLeft(session.profileId);
      }
    }
  }

  /**
   * Enable/disable radar for a profile
   */
  async setRadarEnabled(profileId: string, enabled: boolean): Promise<void> {
    await this.redisService.setRadarEnabled(profileId, enabled);
  }

  /**
   * Get radar status
   */
  async isRadarEnabled(profileId: string): Promise<boolean> {
    return await this.redisService.isRadarEnabled(profileId);
  }
}
